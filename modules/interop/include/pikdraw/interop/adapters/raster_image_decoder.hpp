#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pikdraw::interop {

enum class RasterImageFormat : uint8_t { Unknown, Jpeg, Png, Tiff };
enum class RasterIccProfileStatus : uint8_t {
    NotPresent,
    Preserved,
    ExceedsBudget,
    MultipleContextsUnsupported,
    Unavailable
};

struct RasterImageDecodeOptions {
    uint64_t max_source_bytes{64ULL * 1024ULL * 1024ULL};
    uint64_t max_decoded_pixels{16ULL * 1024ULL * 1024ULL};
    uint64_t max_decoded_bytes{64ULL * 1024ULL * 1024ULL};
    uint64_t max_icc_profile_bytes{1024ULL * 1024ULL};
    uint32_t max_frames{256};
    uint32_t frame_index{0};
    bool apply_orientation{true};
};

struct RasterImageData {
    RasterImageFormat format{RasterImageFormat::Unknown};
    uint32_t width_px{0};
    uint32_t height_px{0};
    uint32_t frame_count{0};
    uint32_t frame_index{0};
    uint32_t source_bits_per_pixel{0};
    uint16_t exif_orientation{1};
    bool orientation_applied{false};
    double dpi_x{96.0};
    double dpi_y{96.0};
    bool has_embedded_icc_profile{false};
    RasterIccProfileStatus icc_profile_status{RasterIccProfileStatus::Unavailable};
    std::vector<uint8_t> embedded_icc_profile;
    // Converted, straight-alpha RGBA8 pixels. This is an explicit display
    // conversion and does not preserve source bit depth or source bytes.
    std::vector<uint8_t> pixels_rgba;
};

struct RasterImageDecodeResult {
    bool success{false};
    RasterImageData image;
    std::string diagnostic_code;
    std::string message;
};

/// Bounded Windows Imaging Component reader for PNG, JPEG and TIFF.
/// Callers must still run untrusted-file decoding in the isolated worker.
class RasterImageDecoder final {
public:
    [[nodiscard]] static RasterImageDecodeResult decode(
        std::span<const uint8_t> source,
        const RasterImageDecodeOptions& options = {});

    [[nodiscard]] static RasterImageFormat sniff_format(
        std::span<const uint8_t> source) noexcept;
};

} // namespace pikdraw::interop
