#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace pikdraw::interop {

enum class RasterImageOutputFormat : uint8_t { Tiff, Jpeg };

struct RasterImageWriteOptions {
    RasterImageOutputFormat format{RasterImageOutputFormat::Tiff};
    double dpi_x{300.0};
    double dpi_y{300.0};
    // JPEG is always lossy. 1.0 requests the WIC encoder's highest quality.
    float jpeg_quality{1.0f};
    uint8_t jpeg_background_r{255};
    uint8_t jpeg_background_g{255};
    uint8_t jpeg_background_b{255};
    // Optional RGB profile describing the input pixels. Empty means sRGB.
    // Used only when convert_pixels_to_destination_profile is enabled.
    std::span<const uint8_t> source_icc_profile{};
    // RGB profile used as the pixel conversion destination. Required only when
    // convert_pixels_to_destination_profile is enabled.
    std::span<const uint8_t> destination_icc_profile{};
    // Optional RGB profile embedded as metadata. Without explicit conversion,
    // it must describe the supplied pixels. With conversion, it should match
    // destination_icc_profile when present.
    std::span<const uint8_t> embedded_icc_profile{};
    // Convert RGBA8 pixels from source_icc_profile (or sRGB) to
    // destination_icc_profile. Conversion is streamed in bounded strips.
    // JPEG flattening backgrounds are in destination encoded RGB when converting.
    bool convert_pixels_to_destination_profile{false};
    bool allow_replace_existing{false};
    // Optional operation-level RAM waiter shared with the host render stage.
    // Return true when these temporary bytes fit below the 95% system-RAM limit.
    std::function<bool(uint64_t)> wait_for_memory_headroom;
};

struct RasterCmykImageWriteOptions {
    double dpi_x{300.0};
    double dpi_y{300.0};
    // The profile must be a validated CMYK ICC profile describing the samples.
    std::span<const uint8_t> embedded_icc_profile{};
    bool allow_replace_existing{false};
    std::function<bool(uint64_t)> wait_for_memory_headroom;
};

struct RasterImageWriteResult {
    bool success{false};
    std::string diagnostic;
};

/// Writes straight-alpha, top-down RGBA8 pixels to TIFF (lossless LZW) or
/// JPEG (explicitly lossy, RGB 4:4:4). Optional RGB ICC conversion is streamed
/// by strips; without it, embedded metadata must describe the supplied pixels.
[[nodiscard]] RasterImageWriteResult WriteRasterImageFile(
    std::wstring_view output_path, uint32_t width_px, uint32_t height_px,
    std::span<const uint8_t> rgba8, const RasterImageWriteOptions& options);

/// Writes straight-alpha, top-down RGBA16LE pixels to lossless TIFF without
/// reducing channel precision. ICC conversion is currently not supported on
/// this overload; any embedded profile must describe the supplied samples.
[[nodiscard]] RasterImageWriteResult WriteRasterImageFile16(
    std::wstring_view output_path, uint32_t width_px, uint32_t height_px,
    std::span<const uint8_t> rgba16le, const RasterImageWriteOptions& options);

/// Writes top-down interleaved CMYK16LE samples to TIFF.
/// Samples are already converted into the embedded CMYK profile; this writer
/// does not apply a fallback or perform color conversion.
[[nodiscard]] RasterImageWriteResult WriteRasterImageCmykFile16(
    std::wstring_view output_path, uint32_t width_px, uint32_t height_px,
    std::span<const uint8_t> cmyk16le,
    const RasterCmykImageWriteOptions& options);

} // namespace pikdraw::interop
