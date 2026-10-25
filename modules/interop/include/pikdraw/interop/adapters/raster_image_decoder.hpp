#pragma once

#include "pikdraw/raster/memory_budget.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pikdraw::interop {

enum class RasterImageFormat : uint8_t { Unknown, Jpeg, Png, Tiff, Bmp };
enum class RasterIccProfileStatus : uint8_t {
    NotPresent,
    Preserved,
    ExceedsBudget,
    MultipleContextsUnsupported,
    Unavailable
};

enum class RasterPixelColorBasis : uint8_t {
    AssumedSRgb,
    ConvertedToSRgb,
    EmbeddedProfileUnapplied
};

// Raster image/source chunk records store total byte counts in U32 fields.
// This is a wire representation ceiling, not a product file-size policy.
inline constexpr uint64_t kRasterRecordByteLimit = UINT32_MAX;

struct RasterImageDecodeOptions {
    uint64_t max_source_bytes{UINT64_MAX};
    uint64_t max_decoded_pixels{UINT32_MAX / 4};
    uint64_t max_decoded_bytes{UINT32_MAX};
    uint64_t max_icc_profile_bytes{UINT32_MAX};
    // Frame count follows the U32 format metadata representation; no fixed
    // product cap is applied to multi-page TIFF sources.
    uint32_t max_frames{UINT32_MAX};
    uint32_t frame_index{0};
    bool apply_orientation{true};
    // Keep 16-bit-per-channel output for source formats that support it.
    // Row callbacks then receive RGBA16LE bytes and RasterImageData reports
    // decoded_bits_per_channel == 16; unsupported conversions stay RGBA8.
    bool preserve_16bit{false};
    /// Optional shared reservation ledger for explicitly owned decode buffers.
    /// Its admission callback can pause until external RAM headroom is available.
    std::shared_ptr<raster::RasterMemoryBudget> memory_budget;
};

struct RasterImageData {
    RasterImageFormat format{RasterImageFormat::Unknown};
    uint32_t width_px{0};
    uint32_t height_px{0};
    uint32_t frame_count{0};
    uint32_t frame_index{0};
    uint32_t source_bits_per_pixel{0};
    uint8_t decoded_bits_per_channel{8};
    uint16_t exif_orientation{1};
    bool orientation_applied{false};
    double dpi_x{96.0};
    double dpi_y{96.0};
    bool has_embedded_icc_profile{false};
    RasterIccProfileStatus icc_profile_status{RasterIccProfileStatus::Unavailable};
    std::vector<uint8_t> embedded_icc_profile;
    RasterPixelColorBasis pixel_color_basis{RasterPixelColorBasis::AssumedSRgb};
    /// Keeps decoded pixel bytes charged while the returned image is retained.
    /// Declared before pixels_rgba so the vector is destroyed before release.
    std::shared_ptr<raster::RasterMemoryBudget::Reservation> pixel_memory_reservation;
    // Converted, straight-alpha RGBA8 pixels. This is an explicit display
    // conversion and does not preserve source bit depth or source bytes.
    std::vector<uint8_t> pixels_rgba;
    // Optional straight-alpha RGBA16LE master buffer. RGBA8 remains the
    // renderer-facing preview and is empty during row-stream decoding.
    std::vector<uint8_t> pixels_rgba16le;
};

/// Pixel region in the oriented source frame. Row callbacks for a region carry
/// absolute source Y coordinates and only the requested pixel width.
struct RasterImageRegion {
    uint32_t x_px{0};
    uint32_t y_px{0};
    uint32_t width_px{0};
    uint32_t height_px{0};
};

struct RasterImageDecodeResult {
    bool success{false};
    RasterImageData image;
    std::string diagnostic_code;
    std::string message;
};

/// Random-access, read-only source. Offsets and sizes are 64 bit so callers
/// can address assets larger than the process's addressable memory range.
/// Implementations must report the actual number of bytes read; short reads
/// are treated as errors by the decoder.
class RasterByteSource {
public:
    virtual ~RasterByteSource() = default;
    [[nodiscard]] virtual uint64_t size_bytes() const noexcept = 0;
    [[nodiscard]] virtual bool read_at(uint64_t offset, std::span<uint8_t> destination,
                                       size_t& bytes_read) const noexcept = 0;
};

using RasterRowCallback = std::function<bool(uint32_t row_y, uint32_t width_px,
                                             std::span<const uint8_t> rgba8)>;
using RasterImageRowCallback = std::function<bool(
    const RasterImageData& image, uint32_t row_y, uint32_t width_px,
    std::span<const uint8_t> rgba)>;

/// Bounded Windows Imaging Component reader for PNG, JPEG and TIFF.
/// Callers must still run untrusted-file decoding in the isolated worker.
class RasterImageDecoder final {
public:
    /// Reads an exact byte range without allocating a buffer proportional to
    /// source size. This primitive is also useful to codecs for metadata reads.
    [[nodiscard]] static bool read_exact(const RasterByteSource& source,
        uint64_t offset, std::span<uint8_t> destination,
        const volatile bool* cancelled = nullptr) noexcept;

    /// Decodes and delivers rows to a consumer. A false callback result stops
    /// decoding. The span overload remains the compatibility full-frame API.
    [[nodiscard]] static RasterImageDecodeResult decode_rows(
        const RasterByteSource& source, const RasterImageDecodeOptions& options,
        const RasterRowCallback& row_callback,
        const volatile bool* cancelled = nullptr);

    /// Metadata is available before the first row is delivered. The image's
    /// pixels_rgba remains empty so consumers can stream records or tiles.
    [[nodiscard]] static RasterImageDecodeResult decode_rows(
        const RasterByteSource& source, const RasterImageDecodeOptions& options,
        const RasterImageRowCallback& row_callback,
        const volatile bool* cancelled = nullptr);

    /// Decodes a rectangular region without materializing full-frame pixels.
    /// Metadata still reports the full oriented source dimensions.
    [[nodiscard]] static RasterImageDecodeResult decode_region_rows(
        const RasterByteSource& source, const RasterImageDecodeOptions& options,
        RasterImageRegion region, const RasterImageRowCallback& row_callback,
        const volatile bool* cancelled = nullptr);

    [[nodiscard]] static RasterImageDecodeResult decode(
        std::span<const uint8_t> source,
        const RasterImageDecodeOptions& options = {});

    [[nodiscard]] static RasterImageFormat sniff_format(
        std::span<const uint8_t> source) noexcept;
};

} // namespace pikdraw::interop
