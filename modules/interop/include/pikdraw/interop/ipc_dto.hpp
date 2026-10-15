#pragma once

#include "interop_types.hpp"
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
inline constexpr size_t IPC_V2_MAX_QUEUED_FRAMES = 2;
inline constexpr size_t IPC_MAX_OBJECTS = 100000;
inline constexpr uint32_t IPC_MAX_DEPTH = 64;

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
