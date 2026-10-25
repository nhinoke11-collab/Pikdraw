#pragma once

#include "pikdraw/document/model/object.hpp"
#include "pikdraw/color/preview/profile_color_transform.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace pikdraw::desktop {

struct RasterBitmapExportOptions {
    std::span<const uint8_t> srgb_profile;
    // Empty preserves the resolved RGB master. A non-empty validated CMYK
    // profile requests an explicit RGB-to-CMYK16 TIFF output conversion.
    std::span<const uint8_t> cmyk_output_profile;
    color::IccRenderingIntent cmyk_rendering_intent{
        color::IccRenderingIntent::RelativeColorimetric};
    bool allow_replace_existing{false};
    std::function<bool(uint64_t)> wait_for_memory_headroom;
    std::function<bool()> cancelled;
};

struct RasterBitmapExportResult {
    bool success{false};
    uint32_t width_px{0};
    uint32_t height_px{0};
    uint8_t bits_per_channel{0};
    bool cmyk_output{false};
    std::string diagnostic;
};

/// Exports one immutable image master as TIFF, evaluating its non-destructive graph first.
/// The sample ICC profile is retained for unapplied source samples; otherwise sRGB is embedded.
[[nodiscard]] RasterBitmapExportResult export_raster_bitmap_master_tiff(
    const document::ImageObject& image, std::wstring_view output_path,
    const RasterBitmapExportOptions& options = {});

} // namespace pikdraw::desktop
