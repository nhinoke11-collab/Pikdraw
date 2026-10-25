#include "v2_worker_protocol.hpp"

#include "pikdraw/interop/adapters/dxf_importer.hpp"
#include "pikdraw/interop/adapters/dxf_worker_adapter.hpp"
#include "pikdraw/interop/adapters/cdr_worker_adapter.hpp"
#include "pikdraw/interop/adapters/eps_worker_adapter.hpp"
#include "pikdraw/interop/adapters/pdf_worker_adapter.hpp"
#include "pikdraw/interop/adapters/raster_worker_adapter.hpp"
#include "pikdraw/interop/adapters/raster_image_decoder.hpp"
#include "pikdraw/interop/adapters/svg_worker_adapter.hpp"
#include "pikdraw/workers/source_snapshot.hpp"
#include "pikdraw/workers/job_object.hpp"

#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <vector>

namespace pikdraw::workers {
namespace {

using namespace pikdraw::interop;

bool ReadExact(HANDLE handle, void* data, DWORD count) {
    auto* bytes = static_cast<uint8_t*>(data);
    DWORD total = 0;
    while (total < count) {
        DWORD received = 0;
        if (!ReadFile(handle, bytes + total, count - total, &received, nullptr) || received == 0) return false;
        total += received;
    }
    return true;
}

bool WriteExact(HANDLE handle, const void* data, DWORD count) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    DWORD total = 0;
    while (total < count) {
        DWORD sent = 0;
        if (!WriteFile(handle, bytes + total, count - total, &sent, nullptr) || sent == 0) return false;
        total += sent;
    }
    return true;
}

bool ReadPacketAfterHeader(
    HANDLE input,
    const IpcHeader& header,
    std::vector<uint8_t>& packet,
    bool require_zero_identity,
    const V2FrameIdentity* expected_identity = nullptr) {
    if (!ValidateV2FrameHeader(header) || header.payload_bytes < IPC_V2_IDENTITY_PREFIX_BYTES) return false;
    std::array<uint8_t, IPC_V2_IDENTITY_PREFIX_BYTES> prefix_bytes{};
    if (!ReadExact(input, prefix_bytes.data(), static_cast<DWORD>(prefix_bytes.size()))) return false;
    const auto prefix = DeserializeV2FrameIdentityPrefix(prefix_bytes);
    if (!prefix || (require_zero_identity && !prefix->IsZero()) ||
        (expected_identity && (prefix->source_token != expected_identity->source_token ||
                               prefix->generation != expected_identity->generation)) ||
        (!require_zero_identity && !expected_identity &&
         (prefix->IsZero() || prefix->generation == 0))) return false;
    try {
        packet.resize(sizeof(IpcHeader) + header.payload_bytes);
    } catch (...) {
        return false;
    }
    std::memcpy(packet.data(), &header, sizeof(header));
    std::copy(prefix_bytes.begin(), prefix_bytes.end(), packet.begin() + sizeof(IpcHeader));
    const DWORD remaining = header.payload_bytes - static_cast<DWORD>(prefix_bytes.size());
    return remaining == 0 || ReadExact(
        input, packet.data() + sizeof(IpcHeader) + prefix_bytes.size(), remaining);
}

bool ReadPacket(HANDLE input, IpcHeader& header, std::vector<uint8_t>& packet, bool zero_identity) {
    return ReadExact(input, &header, sizeof(header)) &&
           ReadPacketAfterHeader(input, header, packet, zero_identity);
}

bool WritePacket(HANDLE output, const std::vector<uint8_t>& packet) {
    return packet.size() <= MAXDWORD && WriteExact(output, packet.data(), static_cast<DWORD>(packet.size()));
}

class Sha256Accumulator final {
public:
    Sha256Accumulator() = default;
    Sha256Accumulator(const Sha256Accumulator&) = delete;
    Sha256Accumulator& operator=(const Sha256Accumulator&) = delete;
    ~Sha256Accumulator() { Close(); }

    bool Initialize() {
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
            return false;
        DWORD object_bytes = 0;
        DWORD written = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(
                algorithm_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_bytes), sizeof(object_bytes),
                &written, 0)) || object_bytes == 0 || written != sizeof(object_bytes)) {
            return false;
        }
        try {
            object_.resize(object_bytes);
        } catch (...) {
            return false;
        }
        return BCRYPT_SUCCESS(BCryptCreateHash(
            algorithm_, &hash_, object_.data(), static_cast<ULONG>(object_.size()), nullptr, 0, 0));
    }

    bool Update(std::span<const uint8_t> bytes) {
        if (!hash_) return false;
        size_t offset = 0;
        while (offset < bytes.size()) {
            const ULONG count = static_cast<ULONG>(std::min<size_t>(
                bytes.size() - offset, std::numeric_limits<ULONG>::max()));
            if (!BCRYPT_SUCCESS(BCryptHashData(
                    hash_, const_cast<PUCHAR>(bytes.data() + offset), count, 0))) return false;
            offset += count;
        }
        return true;
    }

    bool Finish(IpcV2Digest& digest) {
        if (!hash_) return false;
        const bool ok = BCRYPT_SUCCESS(BCryptFinishHash(
            hash_, digest.data(), static_cast<ULONG>(digest.size()), 0));
        Close();
        return ok;
    }

private:
    void Close() noexcept {
        if (hash_) { BCryptDestroyHash(hash_); hash_ = nullptr; }
        if (algorithm_) { BCryptCloseAlgorithmProvider(algorithm_, 0); algorithm_ = nullptr; }
        object_.clear();
    }

    BCRYPT_ALG_HANDLE algorithm_{nullptr};
    BCRYPT_HASH_HANDLE hash_{nullptr};
    std::vector<uint8_t> object_;
};

bool HashBytes(std::span<const uint8_t> bytes, IpcV2Digest& digest) {
    Sha256Accumulator accumulator;
    return accumulator.Initialize() && accumulator.Update(bytes) && accumulator.Finish(digest);
}

bool VerifySource(HANDLE source, const V2ImportBegin& request) {
    if (source == INVALID_HANDLE_VALUE) return false;
    uint64_t bytes = 0;
    SourceDigest digest{};
    if (!HashReadOnlyHandle(source, request.source_bytes, bytes, digest) ||
        bytes != request.source_bytes || digest != request.source_sha256) return false;
    LARGE_INTEGER beginning{};
    return SetFilePointerEx(source, beginning, nullptr, FILE_BEGIN) != FALSE;
}

bool ReadVerifiedSource(HANDLE source, uint64_t expected_bytes, std::vector<uint8_t>& bytes) {
    if (source == INVALID_HANDLE_VALUE || expected_bytes == 0 ||
        expected_bytes > SIZE_MAX) return false;
    LARGE_INTEGER beginning{};
    if (!SetFilePointerEx(source, beginning, nullptr, FILE_BEGIN)) return false;
    try {
        bytes.resize(static_cast<size_t>(expected_bytes));
    } catch (...) {
        return false;
    }
    size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD requested = static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 1024 * 1024));
        DWORD received = 0;
        if (!ReadFile(source, bytes.data() + offset, requested, &received, nullptr) || received == 0) return false;
        offset += received;
    }
    uint8_t extra = 0;
    DWORD received = 0;
    if (!ReadFile(source, &extra, 1, &received, nullptr) || received != 0) return false;
    return SetFilePointerEx(source, beginning, nullptr, FILE_BEGIN) != FALSE;
}

class HandleRasterByteSource final : public RasterByteSource {
public:
    HandleRasterByteSource(HANDLE handle, uint64_t size) noexcept
        : handle_(handle), size_(size) {}

    uint64_t size_bytes() const noexcept override { return size_; }

    bool read_at(uint64_t offset, std::span<uint8_t> destination,
                 size_t& bytes_read) const noexcept override {
        bytes_read = 0;
        if (handle_ == INVALID_HANDLE_VALUE || offset > size_) return false;
        const uint64_t available = size_ - offset;
        const size_t requested = static_cast<size_t>((std::min)(
            available, static_cast<uint64_t>(destination.size())));
        std::lock_guard lock(mutex_);
        if (offset > static_cast<uint64_t>(std::numeric_limits<LONGLONG>::max()))
            return false;
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(handle_, position, nullptr, FILE_BEGIN)) return false;
        while (bytes_read < requested) {
            const size_t remaining = requested - bytes_read;
            const DWORD count = static_cast<DWORD>((std::min)(
                remaining, static_cast<size_t>(MAXDWORD)));
            DWORD received = 0;
            if (!ReadFile(handle_, destination.data() + bytes_read, count, &received, nullptr) ||
                received == 0)
                return false;
            bytes_read += received;
        }
        return true;
    }

private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
    uint64_t size_{0};
    mutable std::mutex mutex_;
};

bool SendError(HANDLE output, uint32_t sequence, uint32_t code, const char* diagnostic);

bool SendRasterImportProposal(HANDLE output, const V2ImportBegin& request,
                              const RasterByteSource& source,
                              uint32_t request_sequence, uint32_t frame_index,
                              bool preserve_16bit,
                              uint32_t& next_response_sequence,
                              SystemMemoryPressureWaiter& memory_waiter) {
    constexpr size_t chunk_target_bytes = IPC_V2_MAX_FRAME_BYTES - 256;
    V2ProposalChunk chunk;
    chunk.job = request.job;
    Sha256Accumulator digest_accumulator;
    if (!digest_accumulator.Initialize()) return false;
    uint64_t stream_bytes = 0;
    uint32_t next_record_ordinal = 0;
    uint32_t chunks_sent = 0;
    size_t record_count = 0;
    std::string diagnostic;

    const auto ram_wait_diagnostic = [](SystemMemoryWaitResult result) {
        return result == SystemMemoryWaitResult::TimedOut
            ? "system RAM pressure wait timed out after 5 minutes"
            : "raster import could not continue while waiting for system RAM";
    };
    const auto flush_chunk = [&]() {
        if (chunk.record_count == 0 || chunks_sent >= IPC_MAX_OBJECTS) return false;
        chunk.checksum_crc32 = V2ProposalRecordsCrc32(chunk.canonical_record_bytes);
        const auto packet = SerializeV2ProposalChunk(
            chunk, request_sequence + chunks_sent);
        if (packet.empty() || !WritePacket(output, packet)) return false;
        ++chunks_sent;
        next_response_sequence = request_sequence + chunks_sent;
        chunk = {};
        chunk.job = request.job;
        chunk.chunk_ordinal = chunks_sent;
        chunk.first_record_ordinal = next_record_ordinal;
        return true;
    };
    const RasterImportRecordSink sink = [&](CanonicalImportRecord&& record) {
        if (record_count >= IPC_MAX_OBJECTS ||
            record.payload.size() > IPC_V2_MAX_RECORD_BYTES ||
            record.payload.size() > chunk_target_bytes - 8) {
            diagnostic = "raster proposal exceeds the structural record size limit";
            return false;
        }
        const size_t framed_bytes = 8 + record.payload.size();
        if (framed_bytes > std::numeric_limits<uint64_t>::max() - stream_bytes) {
            diagnostic = "raster proposal byte count overflow";
            return false;
        }
        if (chunk.record_count > 0 &&
            chunk.canonical_record_bytes.size() + framed_bytes > chunk_target_bytes &&
            !flush_chunk()) {
            if (diagnostic.empty()) diagnostic = "raster proposal chunk could not be sent";
            return false;
        }
        const auto memory_result = memory_waiter.WaitForHeadroom(
            framed_bytes);
        if (memory_result != SystemMemoryWaitResult::Ready) {
            diagnostic = ram_wait_diagnostic(memory_result);
            return false;
        }
        const size_t record_start = chunk.canonical_record_bytes.size();
        if (!AppendV2ProposalRecord(chunk.canonical_record_bytes,
                static_cast<uint16_t>(record.type), record.payload)) {
            diagnostic = "raster proposal record could not be framed";
            return false;
        }
        if (!digest_accumulator.Update(std::span<const uint8_t>(
                chunk.canonical_record_bytes.data() + record_start, framed_bytes))) {
            diagnostic = "raster proposal digest could not be updated";
            return false;
        }
        stream_bytes += framed_bytes;
        ++chunk.record_count;
        ++next_record_ordinal;
        ++record_count;
        return true;
    };

    bool built = false;
    try {
        const RasterMemoryAdmissionCallback memory_admission =
            [&](uint64_t additional_bytes, std::string& memory_diagnostic) {
                if (additional_bytes > SIZE_MAX) {
                    memory_diagnostic = "raster staging allocation exceeds process address space";
                    return false;
                }
                const auto waited = memory_waiter.WaitForHeadroom(
                    static_cast<size_t>(additional_bytes));
                if (waited == SystemMemoryWaitResult::Ready) return true;
                memory_diagnostic = ram_wait_diagnostic(waited);
                return false;
            };
        built = StreamRasterImportRecords(
            source, frame_index, sink, diagnostic, memory_admission, preserve_16bit);
    } catch (...) {
        diagnostic = "Raster proposal stream allocation failed.";
    }
    if (!built) {
        const char* fallback = "Raster source rejected by its decoder, record representation, or system RAM admission";
        return SendError(output, request_sequence + chunks_sent, 3,
                         diagnostic.empty() ? fallback : diagnostic.c_str());
    }
    if (record_count == 0 || !flush_chunk()) return false;

    IpcV2Digest digest{};
    if (!digest_accumulator.Finish(digest)) return false;
    V2ImportEnd end;
    end.job = request.job;
    end.chunk_count = chunks_sent;
    end.record_count = static_cast<uint32_t>(record_count);
    end.canonical_bytes = stream_bytes;
    end.canonical_sha256 = digest;
    end.fidelity_report_complete = true;
    const auto packet = SerializeV2ImportEnd(
        end, request_sequence + chunks_sent);
    return !packet.empty() && WritePacket(output, packet);
}

template <typename Builder>
bool SendImportProposal(HANDLE output, const V2ImportBegin& request,
                        std::vector<uint8_t>& source,
                        uint32_t request_sequence, std::string_view format,
                        size_t max_records, uint32_t& next_response_sequence,
                        SystemMemoryPressureWaiter& memory_waiter,
                        Builder&& build_records) {
    const uint32_t first_response_sequence = request_sequence;
    std::vector<CanonicalImportRecord> records;
    std::string diagnostic;
    const auto wait_for_memory = [&](uint64_t bytes) {
        if (bytes > SIZE_MAX) {
            diagnostic = "import staging allocation exceeds process address space";
            return false;
        }
        const auto waited = memory_waiter.WaitForHeadroom(
            static_cast<size_t>(bytes));
        if (waited == SystemMemoryWaitResult::Ready) return true;
        diagnostic = waited == SystemMemoryWaitResult::TimedOut
            ? "system RAM pressure wait timed out after 5 minutes"
            : "import could not continue while waiting for system RAM";
        return false;
    };
    if (!wait_for_memory(1))
        return SendError(output, next_response_sequence, 3, diagnostic.c_str());
    if (!build_records(source, records, diagnostic)) {
        const char* fallback = format == "DXF" ? "DXF subset rejected source"
            : format == "RASTER" ? "Raster source rejected by the RGBA8 representation or decoder"
                                  : "SVG subset rejected source";
        return SendError(output, request_sequence, 3,
                         diagnostic.empty() ? fallback : diagnostic.c_str());
    }
    if (records.empty() || records.size() > max_records) {
        return SendError(output, request_sequence, 4, "import proposal record count exceeds profile");
    }
    // Adapter records own all decoded values now; release the source snapshot before
    // framing and transmitting the proposal to avoid holding both full representations.
    std::vector<uint8_t>().swap(source);
    if (!wait_for_memory(1))
        return SendError(output, next_response_sequence, 3, diagnostic.c_str());

    constexpr size_t chunk_target_bytes = IPC_V2_MAX_FRAME_BYTES - 256;
    uint64_t stream_bytes = 0;
    uint64_t planned_chunks = 1;
    size_t planned_chunk_bytes = 0;
    for (const auto& record : records) {
        if (record.payload.size() > IPC_V2_MAX_RECORD_BYTES ||
            record.payload.size() > chunk_target_bytes - 8)
            return SendError(output, request_sequence, 4,
                             "import proposal exceeds the structural record size limit");
        const uint64_t framed_bytes = 8ULL + record.payload.size();
        if (framed_bytes > std::numeric_limits<uint64_t>::max() - stream_bytes)
            return SendError(output, request_sequence, 4,
                             "import proposal byte count overflow");
        stream_bytes += framed_bytes;
        if (planned_chunk_bytes > 0 && planned_chunk_bytes + static_cast<size_t>(framed_bytes) > chunk_target_bytes) {
            ++planned_chunks;
            planned_chunk_bytes = 0;
        }
        planned_chunk_bytes += static_cast<size_t>(framed_bytes);
    }
    if (planned_chunks > IPC_MAX_OBJECTS || planned_chunks > UINT32_MAX)
        return SendError(output, request_sequence, 4, "import proposal exceeds the structural chunk count limit");

    V2ProposalChunk chunk;
    chunk.job = request.job;
    Sha256Accumulator digest_accumulator;
    if (!digest_accumulator.Initialize()) return false;
    uint32_t next_record_ordinal = 0;
    uint32_t chunks_sent = 0;
    try {
        for (auto& record : records) {
            const size_t framed_bytes = 8 + record.payload.size();
            if (!wait_for_memory(framed_bytes))
                return SendError(output, next_response_sequence, 3, diagnostic.c_str());
            if (chunk.record_count > 0 && chunk.canonical_record_bytes.size() + framed_bytes > chunk_target_bytes) {
                chunk.checksum_crc32 = V2ProposalRecordsCrc32(chunk.canonical_record_bytes);
                const auto packet = SerializeV2ProposalChunk(
                    chunk, first_response_sequence + chunks_sent);
                if (packet.empty() || !WritePacket(output, packet)) return false;
                ++chunks_sent;
                next_response_sequence = first_response_sequence + chunks_sent;
                chunk = {};
                chunk.job = request.job;
                chunk.chunk_ordinal = chunks_sent;
                chunk.first_record_ordinal = next_record_ordinal;
            }
            const size_t record_start = chunk.canonical_record_bytes.size();
            if (!AppendV2ProposalRecord(chunk.canonical_record_bytes, static_cast<uint16_t>(record.type), record.payload)) {
                return SendError(output, request_sequence, 4, "import proposal record could not be framed");
            }
            if (!digest_accumulator.Update(std::span<const uint8_t>(
                    chunk.canonical_record_bytes.data() + record_start, framed_bytes))) return false;
            // The chunk now owns the serialized bytes, so discard this adapter payload
            // before the receiver grows its canonical proposal buffer.
            std::vector<uint8_t>().swap(record.payload);
            ++chunk.record_count;
            ++next_record_ordinal;
        }
        chunk.checksum_crc32 = V2ProposalRecordsCrc32(chunk.canonical_record_bytes);
        const auto packet = SerializeV2ProposalChunk(
            chunk, first_response_sequence + chunks_sent);
        if (packet.empty() || !WritePacket(output, packet)) return false;
        ++chunks_sent;
        next_response_sequence = first_response_sequence + chunks_sent;
    } catch (...) {
        return false;
    }
    IpcV2Digest digest{};
    if (chunks_sent != planned_chunks || !digest_accumulator.Finish(digest)) return false;
    V2ImportEnd end;
    end.job = request.job;
    end.chunk_count = chunks_sent;
    end.record_count = records.size();
    end.canonical_bytes = stream_bytes;
    end.canonical_sha256 = digest;
    end.fidelity_report_complete = true;
    const auto packet = SerializeV2ImportEnd(end, first_response_sequence + chunks_sent);
    return !packet.empty() && WritePacket(output, packet);
}

bool SendError(HANDLE output, uint32_t sequence, uint32_t code, const char* diagnostic) {
    return WritePacket(output, SerializeV2ErrorResponse({code, diagnostic}, sequence));
}

bool SendMemoryWaitStatus(HANDLE output, uint32_t sequence,
                          const V2JobIdentity& job, size_t required_bytes) {
    using namespace pikdraw::interop;
    const auto identity = SerializeV2FrameIdentityPrefix(
        V2FrameIdentity{job.source_token, job.generation});
    std::array<uint8_t, IPC_V2_IDENTITY_PREFIX_BYTES + sizeof(uint64_t)> payload{};
    std::copy(identity.begin(), identity.end(), payload.begin());
    const auto required = static_cast<uint64_t>(required_bytes);
    for (size_t index = 0; index < sizeof(required); ++index)
        payload[IPC_V2_IDENTITY_PREFIX_BYTES + index] =
            static_cast<uint8_t>((required >> (index * 8)) & 0xff);
    try {
        const auto packet = SerializeV2Frame(MessageType::V2MemoryWait, sequence, payload);
        return !packet.empty() && WritePacket(output, packet);
    } catch (...) {
        return false;
    }
}

bool Negotiate(HANDLE output, const IpcHeader& header, std::span<const uint8_t> packet,
               bool& negotiated, uint32_t& accepted_features) {
    if (negotiated || header.sequence_id != 0) return false;
    const auto request = DeserializeV2NegotiationRequest(packet);
    if (!request || (request->required_features & ~IPC_V2_SUPPORTED_FEATURES) != 0) return false;
    V2NegotiationResponse response;
    response.major = IPC_WIRE_VERSION_V2;
    response.minor = 0;
    response.accepted_features = request->required_features;
    response.accepted = true;
    const auto encoded = SerializeV2NegotiationResponse(response, header.sequence_id);
    if (encoded.empty() || !WritePacket(output, encoded)) return false;
    accepted_features = response.accepted_features;
    negotiated = true;
    return true;
}

bool HandleImportBegin(
    HANDLE output,
    HANDLE source,
    const IpcHeader& header,
    std::span<const uint8_t> packet,
    uint32_t accepted_features) {
    constexpr uint32_t required = IPC_V2_FEATURE_SOURCE_HANDLE | IPC_V2_FEATURE_CHECKED_PROPOSAL_STREAM;
    const auto request = DeserializeV2ImportBegin(packet);
    if (!request || header.sequence_id != 1 || (accepted_features & required) != required) return false;
    uint32_t next_response_sequence = header.sequence_id;
    SystemMemoryPressureWaiter memory_waiter(
        kSystemMemoryPressureWaitLimit, {}, {}, {},
        [&](size_t required_bytes) {
            (void)SendMemoryWaitStatus(output, next_response_sequence,
                                       request->job, required_bytes);
        });
    if (!VerifySource(source, *request)) return SendError(output, header.sequence_id, 2, "source capability hash mismatch");
    const bool is_svg = request->format_id == "SVG" &&
        FormatCapabilities::is_worker_import_profile_supported(
            FormatType::Svg, request->profile_id);
    const bool is_dxf = request->format_id == "DXF" &&
        FormatCapabilities::is_worker_import_profile_supported(
            FormatType::Dxf, request->profile_id);
    const bool is_pdf = request->format_id == "PDF" &&
        FormatCapabilities::is_worker_import_profile_supported(FormatType::Pdf, request->profile_id);
    const bool is_ai = request->format_id == "AI" &&
        FormatCapabilities::is_worker_import_profile_supported(FormatType::Ai, request->profile_id);
    const bool is_eps = request->format_id == "EPS" &&
        FormatCapabilities::is_worker_import_profile_supported(FormatType::Eps, request->profile_id);
    const bool is_cdr = request->format_id == "CorelDRAW" &&
        FormatCapabilities::is_worker_import_profile_supported(FormatType::Cdr, request->profile_id);
    if (is_cdr && (accepted_features & IPC_V2_FEATURE_RASTER_SOURCE_ASSETS) == 0) {
        return SendError(output, header.sequence_id, 1,
            "CDR image preservation requires raster source asset support");
    }
    if (request->source_page_index &&
        ((accepted_features & IPC_V2_FEATURE_SOURCE_PAGE_SELECTION) == 0 ||
         (!is_pdf && !is_ai && request->format_id != "TIFF"))) {
        return SendError(output, header.sequence_id, 1,
            "source page selection is supported only by PDF/AI and TIFF import profiles");
    }
    const bool is_png = request->format_id == "PNG";
    const bool is_jpeg = request->format_id == "JPEG";
    const bool is_tiff = request->format_id == "TIFF";
    const bool is_bmp = request->format_id == "BMP";
    const bool is_raster = (is_png || is_jpeg || is_tiff || is_bmp) &&
        FormatCapabilities::is_worker_import_profile_supported(
            is_png ? FormatType::Png : is_jpeg ? FormatType::Jpeg : is_tiff ? FormatType::Tiff : FormatType::Bmp,
            request->profile_id);
    if (!is_svg && !is_dxf && !is_pdf && !is_ai && !is_eps && !is_cdr && !is_raster) {
        return SendError(output, header.sequence_id, 1,
            "no native editable import adapter is registered for this format/profile");
    }
    const uint64_t source_limit = is_dxf ? DxfImporter::kMaxSourceBytes :
        is_raster ? kRasterRecordByteLimit : static_cast<uint64_t>(SIZE_MAX);
    if (request->source_bytes > source_limit) {
        return SendError(output, header.sequence_id, 4, is_raster
            ? "raster source exceeds the 32-bit raster-source record representation"
            : "source exceeds the addressable size for this worker build");
    }
    if (is_raster) {
        HandleRasterByteSource raster_source(source, request->source_bytes);
        return SendRasterImportProposal(output, *request, raster_source,
            header.sequence_id, request->source_page_index.value_or(0),
            (accepted_features & IPC_V2_FEATURE_RASTER_RGBA16LE) != 0,
            next_response_sequence, memory_waiter);
    }
    const auto source_wait = memory_waiter.WaitForHeadroom(
        static_cast<size_t>(request->source_bytes));
    if (source_wait != SystemMemoryWaitResult::Ready)
        return SendError(output, header.sequence_id, 3,
            source_wait == SystemMemoryWaitResult::TimedOut
                ? "system RAM pressure wait timed out after 5 minutes"
                : "import could not continue while waiting for system RAM");
    std::vector<uint8_t> source_bytes;
    if (!ReadVerifiedSource(source, request->source_bytes, source_bytes))
        return SendError(output, header.sequence_id, 2, "source capability read failed");
    if (is_dxf) {
        return SendImportProposal(output, *request, source_bytes, header.sequence_id,
            "DXF", DxfImporter::kMaxObjects * 4 + 2, next_response_sequence,
            memory_waiter, BuildDxfImportRecords);
    }
    if (is_pdf || is_ai) {
        const bool ai_representation = is_ai;
        const std::string_view profile = request->profile_id;
        const bool page_set_profile = profile == "pdf-page-set-v3" ||
            profile == "ai-pdf-compatible-page-set-v3";
        const bool source_manifest_profile = profile == "pdf-print-source-v1";
        const std::optional<uint32_t> source_page_index = request->source_page_index;
        return SendImportProposal(output, *request, source_bytes, header.sequence_id,
            is_ai ? "AI" : "PDF", IPC_MAX_OBJECTS, next_response_sequence,
            memory_waiter,
            [ai_representation, page_set_profile, source_manifest_profile,
             source_page_index](std::span<const uint8_t> bytes,
                                std::vector<CanonicalImportRecord>& records,
                                std::string& diagnostic) {
                if (page_set_profile)
                    return BuildPdfPageSetImportRecords(bytes, ai_representation, records,
                                                        diagnostic, source_page_index);
                if (source_manifest_profile)
                    return BuildPdfSourceManifestImportRecords(bytes, records,
                                                                diagnostic, source_page_index);
                return BuildPdfImportRecords(bytes, ai_representation, records, diagnostic,
                    source_page_index.value_or(0));
            });
    }
    if (is_eps) {
        return SendImportProposal(output, *request, source_bytes, header.sequence_id,
            "EPS", IPC_MAX_OBJECTS, next_response_sequence, memory_waiter,
            BuildEpsImportRecords);
    }
    if (is_cdr) {
        return SendImportProposal(output, *request, source_bytes, header.sequence_id,
            "CorelDRAW", IPC_MAX_OBJECTS, next_response_sequence, memory_waiter,
            [&memory_waiter](std::vector<uint8_t>& bytes,
               std::vector<CanonicalImportRecord>& records,
               std::string& diagnostic) {
                return BuildCdrImportRecordsAndReleaseSource(bytes, records,
                    diagnostic,
                    [&memory_waiter](uint64_t additional_bytes,
                                     std::string& memory_diagnostic) {
                        if (additional_bytes > SIZE_MAX) {
                            memory_diagnostic =
                                "CDR raster staging exceeds process address space";
                            return false;
                        }
                        const auto waited = memory_waiter.WaitForHeadroom(
                            static_cast<size_t>(additional_bytes));
                        if (waited == SystemMemoryWaitResult::Ready) return true;
                        memory_diagnostic = waited == SystemMemoryWaitResult::TimedOut
                            ? "system RAM pressure wait timed out after 5 minutes"
                            : "CDR import could not continue while waiting for system RAM";
                        return false;
                    });
            });
    }
    return SendImportProposal(output, *request, source_bytes, header.sequence_id,
        "SVG", SvgImporter::kMaxObjects * 2 + 2, next_response_sequence,
        memory_waiter,
        BuildSvgImportRecords);
}

bool SendFixtureStream(HANDLE output, const V2ImportBegin& request, uint32_t sequence, std::string_view mode) {
    if (mode == "--test-v2-stream-oversize") {
        IpcHeader oversized{};
        oversized.wire_version = IPC_WIRE_VERSION_V2;
        oversized.message_type = static_cast<uint16_t>(MessageType::V2ProposalChunk);
        oversized.sequence_id = sequence;
        oversized.payload_bytes = static_cast<uint32_t>(IPC_V2_MAX_FRAME_BYTES + 1);
        return WriteExact(output, &oversized, sizeof(oversized));
    }

    if (mode == "--test-v2-stream-memory-wait" &&
        !SendMemoryWaitStatus(output, sequence, request.job, 1))
        return false;

    const std::array<uint8_t, 3> sample_payload{0x11, 0x22, 0x33};
    V2ProposalChunk chunk;
    chunk.job = request.job;
    chunk.chunk_ordinal = mode == "--test-v2-stream-bad-ordinal" ? 1 : 0;
    chunk.first_record_ordinal = 0;
    chunk.record_count = 1;
    if (!AppendV2ProposalRecord(chunk.canonical_record_bytes, 1, sample_payload)) return false;
    chunk.checksum_crc32 = V2ProposalRecordsCrc32(chunk.canonical_record_bytes);
    if (mode == "--test-v2-stream-wrong-job") chunk.job.document_id = "wrong-document";
    auto chunk_packet = SerializeV2ProposalChunk(chunk, sequence);
    if (chunk_packet.empty()) return false;
    if (mode == "--test-v2-stream-wrong-token") chunk_packet[sizeof(IpcHeader)] ^= 0x80;
    if (mode == "--test-v2-stream-bad-crc") chunk_packet.back() ^= 0x01;
    if (mode == "--test-v2-stream-gap") {
        IpcHeader header{};
        std::memcpy(&header, chunk_packet.data(), sizeof(header));
        ++header.sequence_id;
        std::memcpy(chunk_packet.data(), &header, sizeof(header));
    }
    if (!WritePacket(output, chunk_packet)) return false;
    if (mode == "--test-v2-stream-no-end") return true;

    V2ImportEnd end;
    end.job = request.job;
    end.chunk_count = 1;
    end.record_count = mode == "--test-v2-stream-bad-count" ? 2 : 1;
    end.canonical_bytes = chunk.canonical_record_bytes.size();
    end.fidelity_report_complete = mode != "--test-v2-stream-bad-report";
    if (!HashBytes(chunk.canonical_record_bytes, end.canonical_sha256)) return false;
    if (mode == "--test-v2-stream-bad-hash") end.canonical_sha256[0] ^= 0x80;
    const uint32_t end_sequence = mode == "--test-v2-stream-gap" ? sequence + 3 : sequence + 1;
    return WritePacket(output, SerializeV2ImportEnd(end, end_sequence));
}

} // namespace

bool HandleV2WorkerFrame(
    HANDLE input,
    HANDLE output,
    HANDLE source_handle,
    const IpcHeader& header,
    bool& negotiated,
    uint32_t& accepted_features) {
    std::vector<uint8_t> packet;
    if (!ValidateV2FrameHeader(header)) return false;
    const bool negotiation = header.message_type == static_cast<uint16_t>(MessageType::V2NegotiateRequest);
    if (!ReadPacketAfterHeader(input, header, packet, negotiation)) return false;
    if (negotiation) return Negotiate(output, header, packet, negotiated, accepted_features);
    if (!negotiated || header.message_type != static_cast<uint16_t>(MessageType::V2ImportBegin)) return false;
    return HandleImportBegin(output, source_handle, header, packet, accepted_features);
}

int RunV2StreamFixture(std::string_view mode, HANDLE source_handle) {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    IpcHeader header{};
    std::vector<uint8_t> packet;
    bool negotiated = false;
    uint32_t accepted_features = 0;
    if (!ReadPacket(input, header, packet, true) ||
        header.message_type != static_cast<uint16_t>(MessageType::V2NegotiateRequest)) return 50;
    if (mode == "--test-v2-stream-negotiation-reject") {
        V2NegotiationResponse rejected;
        rejected.major = IPC_WIRE_VERSION_V2;
        rejected.error_code = 1001;
        const auto response = SerializeV2NegotiationResponse(rejected, header.sequence_id);
        return WritePacket(output, response) ? 0 : 51;
    }
    if (mode == "--test-v2-stream-v1-downgrade") {
        IpcHeader legacy{};
        legacy.message_type = static_cast<uint16_t>(MessageType::Pong);
        legacy.sequence_id = header.sequence_id;
        return WriteExact(output, &legacy, sizeof(legacy)) ? 0 : 52;
    }
    if (!Negotiate(output, header, packet, negotiated, accepted_features)) return 53;

    if (!ReadPacket(input, header, packet, false) ||
        header.message_type != static_cast<uint16_t>(MessageType::V2ImportBegin)) return 54;
    const auto request = DeserializeV2ImportBegin(packet);
    if (!request || header.sequence_id != 1 || !VerifySource(source_handle, *request)) return 55;
    return SendFixtureStream(output, *request, header.sequence_id, mode) ? 0 : 56;
}

} // namespace pikdraw::workers
