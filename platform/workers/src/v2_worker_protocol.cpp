#include "v2_worker_protocol.hpp"

#include "pikdraw/interop/adapters/cdr_extractor.hpp"
#include "pikdraw/interop/adapters/dxf_importer.hpp"
#include "pikdraw/interop/adapters/dxf_worker_adapter.hpp"
#include "pikdraw/interop/adapters/raster_worker_adapter.hpp"
#include "pikdraw/interop/adapters/svg_worker_adapter.hpp"
#include "pikdraw/workers/source_snapshot.hpp"

#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
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

bool HashBytes(std::span<const uint8_t> bytes, IpcV2Digest& digest) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<uint8_t> object;
    const auto close = [&] {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    };
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) return false;
    DWORD object_bytes = 0;
    DWORD written = 0;
    if (!BCRYPT_SUCCESS(BCryptGetProperty(
            algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_bytes), sizeof(object_bytes),
            &written, 0)) || object_bytes == 0 || written != sizeof(object_bytes)) {
        close();
        return false;
    }
    try {
        object.resize(object_bytes);
    } catch (...) {
        close();
        return false;
    }
    if (!BCRYPT_SUCCESS(BCryptCreateHash(
            algorithm, &hash, object.data(), static_cast<ULONG>(object.size()), nullptr, 0, 0))) {
        close();
        return false;
    }
    size_t offset = 0;
    while (offset < bytes.size()) {
        const ULONG count = static_cast<ULONG>(std::min<size_t>(
            bytes.size() - offset, std::numeric_limits<ULONG>::max()));
        if (!BCRYPT_SUCCESS(BCryptHashData(
                hash, const_cast<PUCHAR>(bytes.data() + offset), count, 0))) {
            close();
            return false;
        }
        offset += count;
    }
    const bool ok = BCRYPT_SUCCESS(BCryptFinishHash(
        hash, digest.data(), static_cast<ULONG>(digest.size()), 0));
    close();
    return ok;
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
        expected_bytes > 64ULL * 1024ULL * 1024ULL ||
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

bool SendError(HANDLE output, uint32_t sequence, uint32_t code, const char* diagnostic);

template <typename Builder>
bool SendImportProposal(HANDLE output, const V2ImportBegin& request,
                        std::span<const uint8_t> source,
                        uint32_t request_sequence, std::string_view format,
                        size_t max_records, Builder&& build_records) {
    const uint32_t first_response_sequence = request_sequence;
    std::vector<CanonicalImportRecord> records;
    std::string diagnostic;
    if (!build_records(source, records, diagnostic)) {
        const char* fallback = format == "DXF" ? "DXF subset rejected source"
            : format == "RASTER" ? "Raster source rejected by the bounded RGBA8 profile"
                                  : "SVG subset rejected source";
        return SendError(output, request_sequence, 3,
                         diagnostic.empty() ? fallback : diagnostic.c_str());
    }
    if (records.empty() || records.size() > max_records) {
        return SendError(output, request_sequence, 4, "import proposal record count exceeds profile");
    }

    std::vector<uint8_t> canonical_stream;
    std::vector<V2ProposalChunk> chunks;
    V2ProposalChunk chunk;
    chunk.job = request.job;
    uint32_t next_record_ordinal = 0;
    const size_t max_stream_bytes = format == "RASTER" ? 20 * 1024 * 1024
                                                        : 16 * 1024 * 1024;
    constexpr size_t chunk_target_bytes = IPC_V2_MAX_FRAME_BYTES - 256;
    try {
        canonical_stream.reserve(1024 * 1024);
        chunk.canonical_record_bytes.reserve(256 * 1024);
        for (const auto& record : records) {
            const size_t framed_bytes = 8 + record.payload.size();
            if (record.payload.size() > IPC_V2_MAX_RECORD_BYTES || framed_bytes > chunk_target_bytes ||
                framed_bytes > max_stream_bytes - canonical_stream.size()) {
                return SendError(output, request_sequence, 4, "import proposal exceeds bounded record or byte profile");
            }
            if (chunk.record_count > 0 && chunk.canonical_record_bytes.size() + framed_bytes > chunk_target_bytes) {
                chunk.checksum_crc32 = V2ProposalRecordsCrc32(chunk.canonical_record_bytes);
                chunks.push_back(std::move(chunk));
                if (chunks.size() >= 64) return SendError(output, request_sequence, 4, "import proposal has too many chunks");
                chunk = {};
                chunk.job = request.job;
                chunk.chunk_ordinal = static_cast<uint32_t>(chunks.size());
                chunk.first_record_ordinal = next_record_ordinal;
                chunk.canonical_record_bytes.reserve(256 * 1024);
            }
            if (!AppendV2ProposalRecord(chunk.canonical_record_bytes, static_cast<uint16_t>(record.type), record.payload) ||
                !AppendV2ProposalRecord(canonical_stream, static_cast<uint16_t>(record.type), record.payload)) {
                return SendError(output, request_sequence, 4, "import proposal record could not be framed");
            }
            ++chunk.record_count;
            ++next_record_ordinal;
        }
        chunk.checksum_crc32 = V2ProposalRecordsCrc32(chunk.canonical_record_bytes);
        chunks.push_back(std::move(chunk));
    } catch (...) {
        return SendError(output, request_sequence, 4, "import proposal allocation failed");
    }
    IpcV2Digest digest{};
    if (chunks.empty() || !HashBytes(canonical_stream, digest)) return SendError(output, request_sequence, 4, "import proposal digest failed");
    for (size_t index = 0; index < chunks.size(); ++index) {
        chunks[index].chunk_ordinal = static_cast<uint32_t>(index);
        chunks[index].checksum_crc32 = V2ProposalRecordsCrc32(chunks[index].canonical_record_bytes);
        const auto packet = SerializeV2ProposalChunk(chunks[index], first_response_sequence + static_cast<uint32_t>(index));
        if (packet.empty() || !WritePacket(output, packet)) return false;
    }
    V2ImportEnd end;
    end.job = request.job;
    end.chunk_count = static_cast<uint32_t>(chunks.size());
    end.record_count = records.size();
    end.canonical_bytes = canonical_stream.size();
    end.canonical_sha256 = digest;
    end.fidelity_report_complete = true;
    const auto packet = SerializeV2ImportEnd(end, first_response_sequence + static_cast<uint32_t>(chunks.size()));
    return !packet.empty() && WritePacket(output, packet);
}

bool SendError(HANDLE output, uint32_t sequence, uint32_t code, const char* diagnostic) {
    return WritePacket(output, SerializeV2ErrorResponse({code, diagnostic}, sequence));
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
    if (!VerifySource(source, *request)) return SendError(output, header.sequence_id, 2, "source capability hash mismatch");
    const bool is_svg = request->format_id == "SVG" &&
        FormatCapabilities::is_worker_import_profile_supported(
            FormatType::Svg, request->profile_id);
    const bool is_dxf = request->format_id == "DXF" &&
        FormatCapabilities::is_worker_import_profile_supported(
            FormatType::Dxf, request->profile_id);
    const bool is_png = request->format_id == "PNG";
    const bool is_jpeg = request->format_id == "JPEG";
    const bool is_tiff = request->format_id == "TIFF";
    const bool is_raster = (is_png || is_jpeg || is_tiff) &&
        FormatCapabilities::is_worker_import_profile_supported(
            is_png ? FormatType::Png : is_jpeg ? FormatType::Jpeg : FormatType::Tiff,
            request->profile_id);
    const bool is_cdr = request->format_id == "CDR" &&
        FormatCapabilities::is_worker_import_profile_supported(
            FormatType::Cdr, request->profile_id);
    if (!is_svg && !is_dxf && !is_raster && !is_cdr) {
        return SendError(output, header.sequence_id, 1, "unsupported format or profile");
    }
    const uint64_t source_limit = (is_raster || is_cdr) ? 64ULL * 1024ULL * 1024ULL
        : is_dxf ? DxfImporter::kMaxSourceBytes : SvgImporter::kMaxSourceBytes;
    if (request->source_bytes > source_limit) {
        return SendError(output, header.sequence_id, 4, "source exceeds import profile byte limit");
    }
    std::vector<uint8_t> source_bytes;
    if (!ReadVerifiedSource(source, request->source_bytes, source_bytes)) {
        return SendError(output, header.sequence_id, 2, "source capability read failed");
    }
    if (is_cdr) {
        const auto build_cdr = [](std::span<const uint8_t> source,
                                  std::vector<CanonicalImportRecord>& records,
                                  std::string& diagnostic) -> bool {
            auto extracted = CdrExtractor::extract_preview(source);
            if (!extracted.success || extracted.thumbnail_png.empty()) {
                diagnostic = extracted.error_message.empty()
                    ? "Failed to extract preview from CorelDRAW container"
                    : extracted.error_message;
                return false;
            }
            if (!BuildRasterImportRecords(extracted.thumbnail_png, records, diagnostic)) {
                return false;
            }
            if (extracted.page_width_mm > 0.0 && extracted.page_height_mm > 0.0 && !records.empty()) {
                ImportPageRecord page_record{extracted.page_width_mm, extracted.page_height_mm};
                auto encoded = SerializeImportRecord(page_record);
                if (encoded) {
                    records.front() = std::move(*encoded);
                }
            }
            return true;
        };
        return SendImportProposal(output, *request, source_bytes, header.sequence_id,
            "CorelDRAW", 80, build_cdr);
    }
    if (is_dxf) {
        return SendImportProposal(output, *request, source_bytes, header.sequence_id,
            "DXF", DxfImporter::kMaxObjects * 4 + 2, BuildDxfImportRecords);
    }
    if (is_raster) {
        return SendImportProposal(output, *request, source_bytes, header.sequence_id,
            "RASTER", 80, BuildRasterImportRecords);
    }
    return SendImportProposal(output, *request, source_bytes, header.sequence_id,
        "SVG", SvgImporter::kMaxObjects * 2 + 2, BuildSvgImportRecords);
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
