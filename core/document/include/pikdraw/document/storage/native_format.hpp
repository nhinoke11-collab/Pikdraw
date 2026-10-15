#pragma once

#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include <vector>
#include <cstdint>
#include <string>
#include <memory>
#include <optional>

namespace pikdraw::document {

/// Header for the native .pikdraw file container format
#pragma pack(push, 1)
struct PikDrawHeader {
    uint8_t magic[4];       // {'P', 'K', 'D', 'R'}
    uint32_t version;       // e.g. 1
    uint32_t flags;         // feature flags / compression
    uint64_t epoch;         // document epoch at save
    uint64_t revision;      // document revision at save
    uint32_t payload_bytes; // byte length of serialized snapshot payload
    uint32_t crc32;         // CRC32 of payload bytes
};
#pragma pack(pop)

inline constexpr uint8_t kPikDrawMagic[4] = {'P', 'K', 'D', 'R'};
inline constexpr uint32_t kCurrentFormatVersion = 1;

/// Native format serialization and deserialization
class NativeFormat {
public:
    /// Calculate standard IEEE 802.3 CRC32
    [[nodiscard]] static uint32_t calculate_crc32(const uint8_t* data, size_t length) noexcept;

    /// Serializes a DocumentSnapshot into binary data with header and CRC32
    [[nodiscard]] static std::vector<uint8_t> serialize(const DocumentSnapshot& snapshot);

    /// Deserializes a DocumentSnapshot from binary data, verifying magic, version, and CRC32
    [[nodiscard]] static std::shared_ptr<DocumentSnapshot> deserialize(
        const uint8_t* data, size_t length, std::string* out_error = nullptr);
};

} // namespace pikdraw::document
