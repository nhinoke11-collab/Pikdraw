#pragma once

#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include <vector>
#include <cstdint>
#include <string>
#include <memory>
#include <optional>

namespace pikdraw::document {

/// Fixed native container header: PKDR, version, flags, epoch, revision,
/// payload-byte count and CRC32. All integers are little-endian; CRC offset 32.
/// v3 adds authoring paths, v4 appearance records, v5 gradients, v6 stable
/// multi-page roots, v7 paragraph text layout, v8 embedded raster assets,
/// and v9 document process mode/profile/raster DPI. Readers retain v2-v8 sources.
#pragma pack(push, 1)
struct PikDrawHeader {
    uint8_t magic[4];       // {'P', 'K', 'D', 'R'}
    uint32_t version;       // v2-v8 legacy input or current v9 source
    uint32_t flags;         // feature flags / compression
    uint64_t epoch;         // document epoch at save
    uint64_t revision;      // document revision at save
    uint32_t payload_bytes; // byte length of serialized snapshot payload
    uint32_t crc32;         // CRC32 of container with this field zeroed (explicit LE encoding)
};
#pragma pack(pop)

inline constexpr uint8_t kPikDrawMagic[4] = {'P', 'K', 'D', 'R'};
inline constexpr uint32_t kLegacyFormatVersion = 2;
inline constexpr uint32_t kAuthoringSourceFormatVersion = 3;
inline constexpr uint32_t kAppearanceFormatVersion = 4;
inline constexpr uint32_t kGradientFormatVersion = 5;
inline constexpr uint32_t kMultiPageFormatVersion = 6;
inline constexpr uint32_t kTextLayoutFormatVersion = 7;
inline constexpr uint32_t kRasterAssetFormatVersion = 8;
inline constexpr uint32_t kDocumentColorFormatVersion = 9;
inline constexpr uint32_t kCurrentFormatVersion = 9;
inline constexpr uint32_t kMinimumCompatibleVersion = 9;
inline constexpr uint32_t kAuthoringPathSchemaMajor = 1;
inline constexpr uint32_t kAuthoringPathSchemaMinor = 0;

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
