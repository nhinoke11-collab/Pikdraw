#pragma once

#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include <vector>
#include <cstdint>
#include <string>
#include <memory>
#include <optional>
#include <atomic>
#include <functional>
#include <span>

namespace pikdraw::document {

/// Legacy native container header (PKDR v2-v23): magic, version, flags, epoch,
/// revision, payload-byte count and CRC32. All integers are little-endian; CRC
/// offset 32. PKDR v24/v25 keep this prefix and add a 64-bit chunk table header.
/// v3 adds authoring paths, v4 appearance, v5 gradients, v6 multi-page roots,
/// v7 text layout, v8 embedded raster assets, v9 page color/DPI, v10 PowerClip,
/// v11 locked PDF sources, v12 screen-only PDF page preview caches, v13
/// explicit per-page display length units, v14 per-page color/DPI settings,
/// v15 byte-backed ICC resources referenced from page color settings, v16
/// raster preview pixel color-basis metadata, v17 paint-level ICC bindings,
/// v18 path feature specifications used by editable Dimensions and Contours,
/// v19 disk-backed raster source descriptors with nondestructive edits, v20
/// host-reviewed import fidelity reports, v21 text character properties,
/// v22 persistent layer printable/color properties, v23 shared-scene artboards,
/// and v24 deduplicated embedded raster source and decoded sample assets.
#pragma pack(push, 1)
struct PikDrawHeader {
    uint8_t magic[4];       // {'P', 'K', 'D', 'R'}
    uint32_t version;       // v2-v23 uncompressed containers
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
inline constexpr uint32_t kPowerClipFormatVersion = 10;
inline constexpr uint32_t kPdfSourceFormatVersion = 11;
inline constexpr uint32_t kPdfSourcePreviewFormatVersion = 12;
inline constexpr uint32_t kPageDisplayUnitFormatVersion = 13;
inline constexpr uint32_t kPageColorSettingsFormatVersion = 14;
inline constexpr uint32_t kPageIccResourceFormatVersion = 15;
inline constexpr uint32_t kRasterPreviewColorBasisFormatVersion = 16;
inline constexpr uint32_t kPaintProfileBindingFormatVersion = 17;
inline constexpr uint32_t kPathFeatureSpecFormatVersion = 18;
inline constexpr uint32_t kRasterEditGraphFormatVersion = 19;
inline constexpr uint32_t kImportFidelityReportFormatVersion = 20;
inline constexpr uint32_t kTextCharacterPropertiesFormatVersion = 21;
inline constexpr uint32_t kLayerPropertiesFormatVersion = 22;
inline constexpr uint32_t kArtboardsFormatVersion = 23;
inline constexpr uint32_t kRasterAssetTableFormatVersion = 24;
/// Document payload schema version. The v25 container wraps this schema in
/// independently checksummed, optionally DEFLATE-compressed chunks.
inline constexpr uint32_t kCurrentFormatVersion = kRasterAssetTableFormatVersion;
inline constexpr uint32_t kLegacyChunkedContainerFormatVersion = 24;
inline constexpr uint32_t kChunkedContainerFormatVersion = 25;
inline constexpr uint32_t kChunkedContainerHeaderBytes = 52;
inline constexpr uint32_t kChunkedContainerChunkBytes = 16;
inline constexpr uint32_t kNativeChunkBytes = 1024u * 1024u;
inline constexpr uint32_t kNativeCompressionFlagChunked = 1u;
/// Lowest payload schema still emitted for documents without encoded raster sources.
inline constexpr uint32_t kMinimumCompatibleVersion = kArtboardsFormatVersion;
inline constexpr uint32_t kAuthoringPathSchemaMajor = 1;
inline constexpr uint32_t kAuthoringPathSchemaMinor = 0;

/// Native format serialization and deserialization
class NativeFormat {
public:
    using StreamWrite = std::function<void(std::span<const uint8_t>)>;
    using StreamPatch = std::function<void(uint64_t, std::span<const uint8_t>)>;

    /// Calculate standard IEEE 802.3 CRC32
    [[nodiscard]] static uint32_t calculate_crc32(const uint8_t* data, size_t length) noexcept;

    /// Serializes a DocumentSnapshot into binary data with header and CRC32
    [[nodiscard]] static std::vector<uint8_t> serialize(const DocumentSnapshot& snapshot);

    /// Streams the native container and patches its final CRC after all bytes are written.
    /// Payload staging is bounded to one raw chunk; serialized bytes stream to write().
    static void serialize_to(const DocumentSnapshot& snapshot,
                             const StreamWrite& write,
                             const StreamPatch& patch);

    /// Deserializes a DocumentSnapshot from binary data, verifying magic, version, and CRC32
    [[nodiscard]] static std::shared_ptr<DocumentSnapshot> deserialize(
        const uint8_t* data, size_t length, std::string* out_error = nullptr,
        const std::atomic_bool* cancel_requested = nullptr);
};

} // namespace pikdraw::document
