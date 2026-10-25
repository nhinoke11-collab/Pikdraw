#pragma once

#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/document/effects/effect_stack.hpp"
#include "pikdraw/document/model/object.hpp"
#include "pikdraw/rendering/camera.hpp"
#include "pikdraw/rendering/frame_target.hpp"

#include <cstddef>
#include <string>

namespace pikdraw::rendering::effects {

/// Renders one Path/Text source in isolation, evaluates enabled surface-effect
/// nodes in object-local order, then applies object opacity once before
/// compositing over the existing backdrop. Returns false for unsupported or
/// invalid appearance data, resource admission failures or raster/filter
/// failures; the backdrop remains untouched on false.
/// Optional telemetry reports peak live vector capacity separately for
/// Gaussian Blur and Drop Shadow nodes.
[[nodiscard]] bool render_path_with_appearance(
    const document::PathObject &path,
    const document::Appearance &appearance, const Camera2D &camera,
    PixelBufferTarget &target, const color::IccPreview *preview = nullptr,
    std::size_t *out_peak_gaussian_blur_scratch_capacity_bytes = nullptr,
    std::size_t *out_peak_drop_shadow_scratch_capacity_bytes = nullptr,
    int32_t raster_dpi = 0, std::string *out_failure = nullptr);

} // namespace pikdraw::rendering::effects
