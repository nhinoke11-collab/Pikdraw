#pragma once

#include "interop_types.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pikdraw::interop {

inline constexpr uint32_t IPC_MAGIC = 0x50494B43; // 'PIKC'
inline constexpr uint16_t IPC_WIRE_VERSION = 1;
inline constexpr uint16_t IPC_WIRE_VERSION_V2 = 2;

inline constexpr size_t IPC_MAX_PAYLOAD_BYTES = 16 * 1024 * 1024; // 16 MiB
inline constexpr size_t IPC_V2_MAX_FRAME_BYTES = 1024 * 1024; // 1 MiB
inline constexpr size_t IPC_V2_IDENTITY_PREFIX_BYTES = 24;
inline constexpr size_t IPC_V2_MAX_QUEUED_FRAMES = 2;
inline constexpr size_t IPC_V2_MAX_DOCUMENT_ID_BYTES = 128;
inline constexpr size_t IPC_V2_MAX_FORMAT_ID_BYTES = 32;
inline constexpr size_t IPC_V2_MAX_PROFILE_ID_BYTES = 64;
inline constexpr size_t IPC_V2_MAX_RECORDS_PER_CHUNK = 4096;
inline constexpr size_t IPC_V2_MAX_RECORD_BYTES = 512 * 1024;
inline constexpr size_t IPC_MAX_OBJECTS = 100000;
inline constexpr uint32_t IPC_MAX_DEPTH = 64;

inline constexpr uint32_t IPC_V2_FEATURE_SOURCE_HANDLE = 1u << 0;
inline constexpr uint32_t IPC_V2_FEATURE_CHECKED_PROPOSAL_STREAM = 1u << 1;
inline constexpr uint32_t IPC_V2_FEATURE_SOURCE_PAGE_SELECTION = 1u << 2;
inline constexpr uint32_t IPC_V2_FEATURE_RASTER_SOURCE_ASSETS = 1u << 3;
inline constexpr uint32_t IPC_V2_FEATURE_RASTER_RGBA16LE = 1u << 4;
inline constexpr uint32_t IPC_V2_SUPPORTED_FEATURES =
    IPC_V2_FEATURE_SOURCE_HANDLE | IPC_V2_FEATURE_CHECKED_PROPOSAL_STREAM |
    IPC_V2_FEATURE_SOURCE_PAGE_SELECTION | IPC_V2_FEATURE_RASTER_SOURCE_ASSETS |
    IPC_V2_FEATURE_RASTER_RGBA16LE;

enum class MessageType : uint16_t {
    Ping = 1,
    Pong = 2,
    ImportRequest = 10,
    ImportResponse = 11,
    CanaryRequest = 20,
    CanaryResponse = 21,
    V2NegotiateRequest = 30,
    V2NegotiateResponse = 31,
    V2ImportBegin = 32,
    V2ProposalChunk = 33,
    V2ImportEnd = 34,
    V2ErrorResponse = 35,
    V2MemoryWait = 36,
    ErrorResponse = 99
};

#pragma pack(push, 1)
struct IpcHeader {
    uint32_t magic{IPC_MAGIC};
    uint16_t wire_version{IPC_WIRE_VERSION};
    uint16_t message_type{0};
    uint32_t sequence_id{0};
    uint32_t payload_bytes{0};
};
#pragma pack(pop)

struct IpcV2Frame {
    IpcHeader header{};
    std::vector<uint8_t> payload;
};

using IpcV2Token = std::array<uint8_t, 16>;
using IpcV2Digest = std::array<uint8_t, 32>;

struct V2FrameIdentity {
    IpcV2Token source_token{};
    uint64_t generation{0};

    [[nodiscard]] bool IsZero() const noexcept {
        return generation == 0 &&
               std::all_of(source_token.begin(), source_token.end(), [](uint8_t b) { return b == 0; });
    }
};

struct V2JobIdentity {
    std::string document_id;
    uint64_t epoch{0};
    uint64_t base_revision{0};
    uint64_t generation{0};
    IpcV2Token source_token{};

    [[nodiscard]] bool IsMatching(const V2JobIdentity& other) const noexcept {
        return document_id == other.document_id && epoch == other.epoch &&
               base_revision == other.base_revision && generation == other.generation &&
               source_token == other.source_token;
    }
};

struct V2NegotiationRequest {
    uint16_t major{IPC_WIRE_VERSION_V2};
    uint16_t minor{0};
    uint32_t required_features{IPC_V2_SUPPORTED_FEATURES};
};

struct V2NegotiationResponse {
    uint16_t major{0};
    uint16_t minor{0};
    uint32_t accepted_features{0};
    uint32_t error_code{0};
    bool accepted{false};
};

struct V2ImportBegin {
    V2JobIdentity job;
    uint64_t source_bytes{0};
    IpcV2Digest source_sha256{};
    std::string format_id;
    std::string profile_id;
    // Optional zero-based PDF page index. Absent means the adapter default.
    std::optional<uint32_t> source_page_index;
};

struct V2ProposalChunk {
    V2JobIdentity job;
    uint32_t chunk_ordinal{0};
    uint64_t first_record_ordinal{0};
    uint32_t record_count{0};
    uint32_t checksum_crc32{0};
    std::vector<uint16_t> record_types;
    std::vector<uint8_t> canonical_record_bytes;
};

// Zero-copy view into an already received V2ProposalChunk frame payload. The
// canonical bytes remain valid only while the caller keeps that payload alive.
struct V2ProposalChunkView {
    V2JobIdentity job;
    uint32_t chunk_ordinal{0};
    uint64_t first_record_ordinal{0};
    uint32_t record_count{0};
    uint32_t checksum_crc32{0};
    std::span<const uint8_t> canonical_record_bytes;
};

struct V2ImportEnd {
    V2JobIdentity job;
    uint32_t chunk_count{0};
    uint64_t record_count{0};
    uint64_t canonical_bytes{0};
    IpcV2Digest canonical_sha256{};
    bool fidelity_report_complete{false};
};

struct V2ErrorResponse {
    uint32_t error_code{0};
    std::string diagnostic;
};

struct V2ProposalStreamLimits {
    uint64_t max_total_response_bytes{0};
    uint64_t max_canonical_bytes{0};
    uint64_t max_records{0};
    uint32_t max_chunks{0};
    uint32_t max_response_frames{0};
};

struct JobEnvelope {
    std::string document_id;
    uint64_t epoch{0};
    uint64_t base_revision{0};
    uint64_t generation{0};

    [[nodiscard]] bool IsMatching(const JobEnvelope& current) const noexcept {
        return document_id == current.document_id &&
               epoch == current.epoch &&
               base_revision == current.base_revision &&
               generation == current.generation;
    }
};

struct ImportPayloadRequest {
    JobEnvelope job;
    std::string source_format; // e.g. "SVG", "PDF_SUBSET"
    std::vector<uint8_t> raw_input_bytes;
};

struct ImportPayloadResponse {
    JobEnvelope job;
    bool success{false};
    std::string error_message;
    std::vector<VectorGraphicObject> objects;
    std::vector<std::string> loss_reports;
};

struct CanaryPayloadRequest {
    uint32_t canary_type{0};
    std::string target_path;
    std::string network_address;
    uint16_t network_port{0};
    uint64_t test_handle_value{0};
};

struct CanaryPayloadResponse {
    uint32_t canary_type{0};
    bool action_attempted{false};
    bool denied_by_os{false};
    uint32_t os_error_code{0};
    std::string diagnostic_message;
};

// V2 frames use the same fixed header layout but a separate negotiated major version.
[[nodiscard]] bool IsV2MessageType(uint16_t message_type) noexcept;
[[nodiscard]] bool ValidateV2FrameHeader(const IpcHeader& header) noexcept;
[[nodiscard]] std::vector<uint8_t> SerializeV2Frame(
    MessageType message_type,
    uint32_t sequence_id,
    std::span<const uint8_t> payload);
[[nodiscard]] std::optional<IpcV2Frame> DeserializeV2Frame(
    std::span<const uint8_t> packet);
[[nodiscard]] std::vector<uint8_t> SerializeV2NegotiationRequest(
    const V2NegotiationRequest& request, uint32_t sequence_id);
[[nodiscard]] std::optional<V2NegotiationRequest> DeserializeV2NegotiationRequest(
    std::span<const uint8_t> packet);
[[nodiscard]] std::vector<uint8_t> SerializeV2NegotiationResponse(
    const V2NegotiationResponse& response, uint32_t sequence_id);
[[nodiscard]] std::optional<V2NegotiationResponse> DeserializeV2NegotiationResponse(
    std::span<const uint8_t> packet);
[[nodiscard]] std::vector<uint8_t> SerializeV2ImportBegin(
    const V2ImportBegin& request, uint32_t sequence_id);
[[nodiscard]] std::optional<V2ImportBegin> DeserializeV2ImportBegin(
    std::span<const uint8_t> packet);
[[nodiscard]] std::vector<uint8_t> SerializeV2ProposalChunk(
    const V2ProposalChunk& chunk, uint32_t sequence_id);
[[nodiscard]] std::optional<V2ProposalChunk> DeserializeV2ProposalChunk(
    std::span<const uint8_t> packet);
[[nodiscard]] std::optional<V2ProposalChunkView> DeserializeV2ProposalChunkPayload(
    std::span<const uint8_t> payload);
[[nodiscard]] std::vector<uint8_t> SerializeV2ImportEnd(
    const V2ImportEnd& end, uint32_t sequence_id);
[[nodiscard]] std::optional<V2ImportEnd> DeserializeV2ImportEnd(
    std::span<const uint8_t> packet);
[[nodiscard]] std::vector<uint8_t> SerializeV2ErrorResponse(
    const V2ErrorResponse& error, uint32_t sequence_id);
[[nodiscard]] std::optional<V2ErrorResponse> DeserializeV2ErrorResponse(
    std::span<const uint8_t> packet);
[[nodiscard]] uint32_t V2ProposalRecordsCrc32(std::span<const uint8_t> bytes) noexcept;
[[nodiscard]] bool AppendV2ProposalRecord(
    std::vector<uint8_t>& canonical_records,
    uint16_t record_type,
    std::span<const uint8_t> payload);
[[nodiscard]] std::array<uint8_t, IPC_V2_IDENTITY_PREFIX_BYTES> SerializeV2FrameIdentityPrefix(
    const V2FrameIdentity& identity) noexcept;
[[nodiscard]] std::optional<V2FrameIdentity> DeserializeV2FrameIdentityPrefix(
    std::span<const uint8_t> bytes) noexcept;

// Binary serialization helpers for wire v1.
std::vector<uint8_t> SerializeImportRequest(const ImportPayloadRequest& req, uint32_t sequence_id);
std::optional<ImportPayloadRequest> DeserializeImportRequest(std::span<const uint8_t> data);

std::vector<uint8_t> SerializeImportResponse(const ImportPayloadResponse& resp, uint32_t sequence_id);
std::optional<ImportPayloadResponse> DeserializeImportResponse(std::span<const uint8_t> data);

std::vector<uint8_t> SerializeCanaryRequest(const CanaryPayloadRequest& req, uint32_t sequence_id);
std::optional<CanaryPayloadRequest> DeserializeCanaryRequest(std::span<const uint8_t> data);

std::vector<uint8_t> SerializeCanaryResponse(const CanaryPayloadResponse& resp, uint32_t sequence_id);
std::optional<CanaryPayloadResponse> DeserializeCanaryResponse(std::span<const uint8_t> data);

} // namespace pikdraw::interop
