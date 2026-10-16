#include "v2_proposal_validator.hpp"
#include "pikdraw/interop/adapters/svg_worker_adapter.hpp"

#include <bcrypt.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pikdraw::workers {
using namespace pikdraw::interop;
namespace {

class Sha256State final {
public:
    ~Sha256State() noexcept {
        if (hash_) BCryptDestroyHash(hash_);
        if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
    }

    bool Start() noexcept {
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
            return false;
        }
        DWORD size = 0;
        DWORD written = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(
                algorithm_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof(size), &written, 0)) ||
            size == 0 || written != sizeof(size)) return false;
        try {
            object_.resize(size);
        } catch (...) {
            return false;
        }
        return BCRYPT_SUCCESS(BCryptCreateHash(
            algorithm_, &hash_, object_.data(), static_cast<ULONG>(object_.size()), nullptr, 0, 0));
    }

    bool Update(std::span<const uint8_t> bytes) noexcept {
        while (!bytes.empty()) {
            const auto count = static_cast<ULONG>(std::min<size_t>(
                bytes.size(), std::numeric_limits<ULONG>::max()));
            if (!BCRYPT_SUCCESS(BCryptHashData(hash_, const_cast<PUCHAR>(bytes.data()), count, 0))) return false;
            bytes = bytes.subspan(count);
        }
        return hash_ != nullptr;
    }

    bool Finish(pikdraw::interop::IpcV2Digest& digest) noexcept {
        return hash_ && BCRYPT_SUCCESS(BCryptFinishHash(
            hash_, digest.data(), static_cast<ULONG>(digest.size()), 0));
    }

private:
    BCRYPT_ALG_HANDLE algorithm_{nullptr};
    BCRYPT_HASH_HANDLE hash_{nullptr};
    std::vector<uint8_t> object_;
};

bool TransferV2(
    HANDLE pipe,
    HANDLE process,
    uint8_t* bytes,
    size_t count,
    bool write,
    std::chrono::steady_clock::time_point deadline) noexcept {
    ScopedHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event.IsValid()) return false;
    while (count > 0) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) return false;
        ResetEvent(event.Get());
        OVERLAPPED operation{};
        operation.hEvent = event.Get();
        DWORD transferred = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(count, 65536));
        const BOOL immediate = write
            ? WriteFile(pipe, bytes, chunk, &transferred, &operation)
            : ReadFile(pipe, bytes, chunk, &transferred, &operation);
        if (!immediate) {
            if (GetLastError() != ERROR_IO_PENDING) return false;
            const HANDLE waits[] = {event.Get(), process};
            const DWORD wait_ms = static_cast<DWORD>(std::min<int64_t>(remaining.count(), MAXDWORD - 1));
            if (WaitForMultipleObjects(2, waits, FALSE, wait_ms) != WAIT_OBJECT_0) {
                CancelIoEx(pipe, &operation);
                GetOverlappedResult(pipe, &operation, &transferred, TRUE);
                return false;
            }
            if (!GetOverlappedResult(pipe, &operation, &transferred, FALSE)) return false;
        }
        if (transferred == 0) return false;
        bytes += transferred;
        count -= transferred;
    }
    return true;
}

bool ReadV2Payload(
    HANDLE pipe,
    HANDLE process,
    const pikdraw::interop::IpcHeader& header,
    std::chrono::steady_clock::time_point deadline,
    const pikdraw::interop::V2FrameIdentity* expected_identity,
    std::vector<uint8_t>& payload,
    std::string_view& failure) noexcept {
    using namespace pikdraw::interop;
    if (header.payload_bytes < IPC_V2_IDENTITY_PREFIX_BYTES) {
        failure = "v2 frame omitted identity prefix";
        return false;
    }
    std::array<uint8_t, IPC_V2_IDENTITY_PREFIX_BYTES> prefix_bytes{};
    if (!TransferV2(pipe, process, prefix_bytes.data(), prefix_bytes.size(), false, deadline)) {
        failure = "v2 identity prefix read failed/timeout";
        return false;
    }
    const auto prefix = DeserializeV2FrameIdentityPrefix(prefix_bytes);
    if (!prefix || (expected_identity &&
                    (prefix->source_token != expected_identity->source_token ||
                     prefix->generation != expected_identity->generation)) ||
        (!expected_identity && !prefix->IsZero())) {
        failure = "v2 identity prefix mismatch";
        return false;
    }
    try {
        payload.resize(header.payload_bytes);
    } catch (...) {
        failure = "v2 payload allocation failed";
        return false;
    }
    std::copy(prefix_bytes.begin(), prefix_bytes.end(), payload.begin());
    const size_t remainder = payload.size() - prefix_bytes.size();
    if (remainder > 0 && !TransferV2(
            pipe, process, payload.data() + prefix_bytes.size(), remainder, false, deadline)) {
        failure = "v2 payload read failed/timeout";
        return false;
    }
    return true;
}

} // namespace

struct V2ProposalStreamValidator::DigestState {
    Sha256State sha;
    bool finished{false};
};

struct V2ProposalStreamValidator::SemanticState {
    enum class Phase { NeedPage, Layers, Objects, Issues, Report, Complete };
    enum class ObjectKind { Path, Text, Group };
    struct ObjectInfo {
        ObjectKind kind{ObjectKind::Path};
        std::vector<uint32_t> children;
    };

    Phase phase{Phase::NeedPage};
    uint32_t next_layer_id{1};
    uint32_t next_object_id{1};
    uint32_t issue_count{0};
    uint64_t records_seen{0};
    std::unordered_set<std::string> source_ids;
    std::vector<std::vector<uint32_t>> layer_roots;
    std::unordered_map<uint32_t, ObjectInfo> objects;

    bool AddObject(uint32_t id, std::string_view source_id, ObjectInfo object) {
        if (phase == Phase::Issues || phase == Phase::Report || phase == Phase::Complete ||
            id != next_object_id || source_id.empty() || !source_ids.emplace(source_id).second) return false;
        if (phase == Phase::Layers) phase = Phase::Objects;
        objects.emplace(id, std::move(object));
        ++next_object_id;
        return true;
    }

    bool Consume(ImportRecordType type, const ImportRecordValue& value) {
        if (phase == Phase::Complete) return false;
        if (type == ImportRecordType::Page) {
            if (phase != Phase::NeedPage || records_seen != 0 || !std::holds_alternative<ImportPageRecord>(value)) return false;
            phase = Phase::Layers;
        } else if (type == ImportRecordType::Layer) {
            const auto* layer = std::get_if<ImportLayerRecord>(&value);
            if (!layer || (phase != Phase::Layers && phase != Phase::Objects) ||
                phase == Phase::Objects || layer->local_id != next_layer_id ||
                layer->source_id.empty() || !source_ids.emplace(layer->source_id).second) return false;
            layer_roots.push_back(layer->object_local_ids);
            ++next_layer_id;
        } else if (type == ImportRecordType::Group) {
            const auto* group = std::get_if<ImportGroupRecord>(&value);
            if (!group || !AddObject(group->local_id, group->source_id,
                                     ObjectInfo{ObjectKind::Group, group->child_local_ids})) return false;
        } else if (type == ImportRecordType::Path) {
            const auto* path = std::get_if<ImportPathRecord>(&value);
            if (!path || !AddObject(path->local_id, path->source_id, ObjectInfo{ObjectKind::Path, {}})) return false;
        } else if (type == ImportRecordType::Text) {
            const auto* text = std::get_if<ImportTextRecord>(&value);
            if (!text || !AddObject(text->local_id, text->source_id, ObjectInfo{ObjectKind::Text, {}})) return false;
        } else if (type == ImportRecordType::FidelityIssue) {
            const auto* issue = std::get_if<ImportFidelityIssueRecord>(&value);
            if (!issue || issue->severity == ImportFidelitySeverity::Unsupported ||
                (!issue->source_locator.empty() && !source_ids.contains(issue->source_locator)) ||
                phase == Phase::NeedPage || phase == Phase::Report || phase == Phase::Complete) return false;
            phase = Phase::Issues;
            ++issue_count;
        } else if (type == ImportRecordType::FidelityReport) {
            const auto* report = std::get_if<ImportFidelityReportRecord>(&value);
            if (!report || (phase != Phase::Layers && phase != Phase::Objects && phase != Phase::Issues) ||
                layer_roots.empty() || report->coverage_profile != "svg-hierarchy-v2" ||
                report->issue_count != issue_count || report->coverage != ImportCoverage::Partial ||
                report->parse_outcome != (issue_count == 0 ? ImportParseOutcome::Complete : ImportParseOutcome::CompleteWithDiagnostics) ||
                report->appearance != (issue_count == 0 ? ImportAppearance::Unknown : ImportAppearance::Partial) ||
                report->editability != ImportEditability::NativeEditable ||
                report->production != ImportProduction::NotAssessed || !ValidateGraph()) return false;
            phase = Phase::Complete;
        } else {
            return false;
        }
        ++records_seen;
        return true;
    }

    bool ConsumeChunk(std::span<const uint8_t> bytes, uint32_t expected_count) {
        size_t offset = 0;
        uint32_t count = 0;
        while (offset < bytes.size()) {
            if (bytes.size() - offset < 8) return false;
            const uint16_t raw_type = static_cast<uint16_t>(bytes[offset]) |
                static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
            const uint16_t flags = static_cast<uint16_t>(bytes[offset + 2]) |
                static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 3]) << 8);
            const uint32_t payload_size = static_cast<uint32_t>(bytes[offset + 4]) |
                (static_cast<uint32_t>(bytes[offset + 5]) << 8) |
                (static_cast<uint32_t>(bytes[offset + 6]) << 16) |
                (static_cast<uint32_t>(bytes[offset + 7]) << 24);
            offset += 8;
            if (flags != 0 || payload_size > bytes.size() - offset || raw_type < 1 || raw_type > 7) return false;
            const auto type = static_cast<ImportRecordType>(raw_type);
            const auto value = DeserializeImportRecord(type, bytes.subspan(offset, payload_size));
            if (!value || !Consume(type, *value)) return false;
            offset += payload_size;
            ++count;
        }
        return count == expected_count;
    }

    bool ValidateGraph() const {
        if (layer_roots.empty()) return false;
        std::unordered_map<uint32_t, uint32_t> owners;
        owners.reserve(objects.size());
        for (const auto& [id, object] : objects) {
            if (object.kind != ObjectKind::Group) {
                if (!object.children.empty()) return false;
                continue;
            }
            uint32_t previous_child = id;
            for (const uint32_t child_id : object.children) {
                const auto found = objects.find(child_id);
                if (found == objects.end() || child_id <= previous_child || ++owners[child_id] != 1) return false;
                previous_child = child_id;
            }
        }
        for (const auto& roots : layer_roots) {
            uint32_t previous_root = 0;
            for (const uint32_t root_id : roots) {
                if (!objects.contains(root_id) || root_id <= previous_root || ++owners[root_id] != 1) return false;
                previous_root = root_id;
            }
        }
        for (const auto& [id, object] : objects) {
            (void)object;
            const auto owner = owners.find(id);
            if (owner == owners.end() || owner->second != 1) return false;
        }
        return true;
    }

    [[nodiscard]] bool IsComplete() const noexcept { return phase == Phase::Complete; }
};

V2ProposalStreamValidator::V2ProposalStreamValidator(
    pikdraw::interop::V2ImportBegin expected,
    uint64_t max_canonical_bytes,
    uint64_t max_records,
    uint32_t max_chunks) noexcept
    : expected_(std::move(expected)),
      max_canonical_bytes_(max_canonical_bytes),
      max_records_(max_records),
      max_chunks_(max_chunks) {
    digest_.reset(new (std::nothrow) DigestState());
    const bool is_svg = expected_.format_id == "SVG";
    const bool supported_svg_profile = !is_svg || expected_.profile_id == "svg-hierarchy-v2";
    if (is_svg && supported_svg_profile) semantic_.reset(new (std::nothrow) SemanticState());
    ready_ = digest_ && max_canonical_bytes_ > 0 && max_records_ > 0 && max_chunks_ > 0 &&
             !expected_.job.document_id.empty() && expected_.source_bytes > 0 &&
             supported_svg_profile && (!is_svg || semantic_) && digest_->sha.Start();
    if (!supported_svg_profile) failure_ = "unsupported SVG import profile";
    else if (!ready_) failure_ = "v2 validator initialization failed";
}

V2ProposalStreamValidator::~V2ProposalStreamValidator() noexcept = default;

bool V2ProposalStreamValidator::IsReady() const noexcept {
    return ready_;
}

bool V2ProposalStreamValidator::Accept(const pikdraw::interop::IpcV2Frame& frame) noexcept {
    using namespace pikdraw::interop;
    const auto reject = [&](std::string_view reason) {
        failure_ = reason;
        return false;
    };
    if (!ready_) return false;
    if (complete_) return reject("v2 frame followed terminal import end");
    try {
        const auto packet = SerializeV2Frame(
            static_cast<MessageType>(frame.header.message_type), frame.header.sequence_id, frame.payload);
        const auto type = static_cast<MessageType>(frame.header.message_type);
        if (type == MessageType::V2ProposalChunk) {
            const auto chunk = DeserializeV2ProposalChunk(packet);
            if (!chunk) return reject("invalid v2 proposal record framing or CRC32");
            if (!chunk->job.IsMatching(expected_.job)) return reject("v2 proposal job/source identity mismatch");
            if (chunk->chunk_ordinal != chunk_count_ || chunk->first_record_ordinal != record_count_) {
                return reject("v2 proposal chunk or record ordinal gap");
            }
            if (chunk_count_ >= max_chunks_ || chunk->record_count > max_records_ - record_count_ ||
                chunk->canonical_record_bytes.size() > max_canonical_bytes_ - canonical_bytes_) {
                return reject("v2 proposal record or canonical byte quota exceeded");
            }
            if (semantic_ && !semantic_->ConsumeChunk(chunk->canonical_record_bytes, chunk->record_count)) {
                return reject("invalid SVG import record order, hierarchy, or reference");
            }
            if (!digest_->sha.Update(chunk->canonical_record_bytes)) return reject("v2 canonical SHA-256 update failed");
            ++chunk_count_;
            record_count_ += chunk->record_count;
            canonical_bytes_ += chunk->canonical_record_bytes.size();
            return true;
        }
        if (type != MessageType::V2ImportEnd) return reject("unexpected v2 proposal message type");

        const auto end = DeserializeV2ImportEnd(packet);
        if (!end) return reject("malformed v2 import end");
        if (!end->job.IsMatching(expected_.job)) return reject("v2 import end job/source identity mismatch");
        if (!end->fidelity_report_complete) return reject("v2 fidelity report is incomplete");
        if (semantic_ && !semantic_->IsComplete()) return reject("v2 SVG fidelity report or hierarchy is incomplete");
        if (end->chunk_count != chunk_count_ || end->record_count != record_count_ ||
            end->canonical_bytes != canonical_bytes_) return reject("v2 final chunk/record/byte count mismatch");

        IpcV2Digest actual{};
        if (!digest_->sha.Finish(actual)) return reject("v2 canonical SHA-256 finalization failed");
        digest_->finished = true;
        if (actual != end->canonical_sha256) return reject("v2 canonical SHA-256 mismatch");
        complete_ = true;
        return true;
    } catch (...) {
        return reject("v2 proposal validation allocation or codec failure");
    }
}

bool NegotiateV2WorkerProcess(
    HANDLE stdin_pipe,
    HANDLE stdout_pipe,
    HANDLE process,
    uint32_t required_features,
    bool has_source_capability,
    std::chrono::milliseconds timeout,
    uint32_t& accepted_features,
    std::string_view& failure) noexcept {
    using namespace pikdraw::interop;
    failure = "none";
    accepted_features = 0;
    if (stdin_pipe == INVALID_HANDLE_VALUE || stdout_pipe == INVALID_HANDLE_VALUE ||
        process == INVALID_HANDLE_VALUE || timeout.count() <= 0 || timeout > std::chrono::minutes(1) ||
        required_features == 0 || (required_features & ~IPC_V2_SUPPORTED_FEATURES) != 0 ||
        ((required_features & IPC_V2_FEATURE_SOURCE_HANDLE) != 0 && !has_source_capability)) {
        failure = "invalid v2 negotiation request/lifecycle";
        return false;
    }
    try {
        const auto request = SerializeV2NegotiationRequest({IPC_WIRE_VERSION_V2, 0, required_features}, 0);
        if (request.empty()) {
            failure = "invalid v2 negotiation features";
            return false;
        }
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        if (!TransferV2(stdin_pipe, process, const_cast<uint8_t*>(request.data()), request.size(), true, deadline)) {
            failure = "v2 negotiation write failed/timeout";
            return false;
        }
        IpcHeader header{};
        if (!TransferV2(stdout_pipe, process, reinterpret_cast<uint8_t*>(&header), sizeof(header), false, deadline) ||
            !ValidateV2FrameHeader(header) ||
            header.message_type != static_cast<uint16_t>(MessageType::V2NegotiateResponse) ||
            header.sequence_id != 0 || header.payload_bytes > IPC_V2_IDENTITY_PREFIX_BYTES + 16) {
            failure = "v2 negotiation response header/version mismatch";
            return false;
        }
        std::vector<uint8_t> payload;
        if (!ReadV2Payload(stdout_pipe, process, header, deadline, nullptr, payload, failure)) return false;
        std::vector<uint8_t> packet(sizeof(header));
        std::memcpy(packet.data(), &header, sizeof(header));
        packet.insert(packet.end(), payload.begin(), payload.end());
        const auto response = DeserializeV2NegotiationResponse(packet);
        if (!response || !response->accepted || response->major != IPC_WIRE_VERSION_V2 ||
            (response->accepted_features & required_features) != required_features) {
            failure = "v2 negotiation rejected or required features unavailable";
            return false;
        }
        accepted_features = response->accepted_features;
        return true;
    } catch (...) {
        failure = "v2 negotiation codec or allocation failure";
        return false;
    }
}

bool TransactV2ProposalStreamProcess(
    HANDLE stdin_pipe,
    HANDLE stdout_pipe,
    HANDLE process,
    const pikdraw::interop::V2ImportBegin& expected,
    std::span<const uint8_t> request_bytes,
    std::chrono::milliseconds timeout,
    const pikdraw::interop::V2ProposalStreamLimits& limits,
    const std::function<bool(const pikdraw::interop::IpcV2Frame&)>& consumer,
    std::string_view& failure) noexcept {
    using namespace pikdraw::interop;
    failure = "none";
    if (stdin_pipe == INVALID_HANDLE_VALUE || stdout_pipe == INVALID_HANDLE_VALUE ||
        process == INVALID_HANDLE_VALUE || timeout.count() <= 0 || timeout > std::chrono::minutes(1) ||
        limits.max_total_response_bytes == 0 || limits.max_canonical_bytes == 0 ||
        limits.max_canonical_bytes > limits.max_total_response_bytes || limits.max_records == 0 ||
        limits.max_records > IPC_MAX_OBJECTS || limits.max_chunks == 0 ||
        limits.max_chunks > IPC_MAX_OBJECTS || limits.max_response_frames == 0 ||
        limits.max_response_frames > IPC_MAX_OBJECTS + 1 || limits.max_chunks >= limits.max_response_frames) {
        failure = "invalid v2 stream limits/lifecycle";
        return false;
    }
    try {
        const auto request = DeserializeV2ImportBegin(request_bytes);
        if (!request || !request->job.IsMatching(expected.job) ||
            request->source_bytes != expected.source_bytes || request->source_sha256 != expected.source_sha256) {
            failure = "v2 import begin identity or snapshot mismatch";
            return false;
        }
        if (request->source_bytes == 0) {
            failure = "v2 import begin has an empty source";
            return false;
        }
        IpcHeader request_header{};
        std::memcpy(&request_header, request_bytes.data(), sizeof(request_header));
        if (request_header.sequence_id == UINT32_MAX) {
            failure = "v2 sequence exhausted";
            return false;
        }

        const auto deadline = std::chrono::steady_clock::now() + timeout;
        if (!TransferV2(stdin_pipe, process, const_cast<uint8_t*>(request_bytes.data()), request_bytes.size(), true, deadline)) {
            failure = "v2 import begin write failed/timeout";
            return false;
        }
        V2ProposalStreamValidator validator(
            *request, limits.max_canonical_bytes, limits.max_records, limits.max_chunks);
        if (!validator.IsReady()) {
            failure = validator.Failure();
            return false;
        }

        uint64_t received_bytes = 0;
        uint32_t expected_sequence = request_header.sequence_id;
        for (uint32_t index = 0; index < limits.max_response_frames; ++index) {
            IpcHeader header{};
            if (!TransferV2(stdout_pipe, process, reinterpret_cast<uint8_t*>(&header), sizeof(header), false, deadline)) {
                failure = "v2 response header failed/timeout";
                return false;
            }
            const auto type = static_cast<MessageType>(header.message_type);
            if (!ValidateV2FrameHeader(header) || header.sequence_id != expected_sequence ||
                (type != MessageType::V2ProposalChunk && type != MessageType::V2ImportEnd &&
                 type != MessageType::V2ErrorResponse)) {
                failure = "invalid v2 response header/order";
                return false;
            }
            const uint64_t frame_bytes = sizeof(IpcHeader) + header.payload_bytes;
            if (frame_bytes > limits.max_total_response_bytes - received_bytes) {
                failure = "v2 response stream exceeds host byte quota";
                return false;
            }
            const V2FrameIdentity expected_identity{request->job.source_token, request->job.generation};
            std::vector<uint8_t> payload;
            if (!ReadV2Payload(
                    stdout_pipe, process, header, deadline,
                    type == MessageType::V2ErrorResponse ? nullptr : &expected_identity,
                    payload, failure)) return false;
            received_bytes += frame_bytes;

            if (type == MessageType::V2ErrorResponse) {
                failure = "worker returned v2 error response";
                return false;
            }
            IpcV2Frame frame{header, std::move(payload)};
            if (!validator.Accept(frame)) {
                failure = validator.Failure();
                return false;
            }
            if (consumer && !consumer(frame)) {
                failure = "v2 frame consumer rejected proposal";
                return false;
            }
            if (type == MessageType::V2ImportEnd) return validator.IsComplete();
            if (expected_sequence == UINT32_MAX) {
                failure = "v2 sequence exhausted";
                return false;
            }
            ++expected_sequence;
        }
        failure = "v2 response frame count exceeded";
        return false;
    } catch (...) {
        failure = "v2 stream codec or allocation failure";
        return false;
    }
}

} // namespace pikdraw::workers
