#pragma once

#include "pikdraw/document/model/object.hpp"

#include <utility>

namespace pikdraw::raster {

/// Returns whether an enabled node needs color-aware sRGB pixel math.
[[nodiscard]] bool raster_edit_requires_srgb_processing(
    const document::RasterEditGraph& graph) noexcept;

/// Maps an evaluated raster's output dimensions back to the image placement.
[[nodiscard]] geometry::Rect2D raster_edited_bounds(
    const document::ImageObject& image, uint32_t output_width,
    uint32_t output_height) noexcept;

/// Predicts the full-resolution pixel dimensions after enabled crop/rotate
/// nodes. The arithmetic mirrors the raster evaluator's coordinate rounding.
[[nodiscard]] std::pair<uint32_t, uint32_t> raster_edited_pixel_dimensions(
    const document::ImageObject& image) noexcept;

}  // namespace pikdraw::raster
