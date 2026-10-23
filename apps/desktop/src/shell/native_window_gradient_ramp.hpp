#pragma once

#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/document/model/gradient_paint.hpp"
#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/color/values/color_value.hpp"

#include <cstdint>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace pikdraw::desktop {

enum class RampHitKind {
    None,
    StopPin,
    MidpointDiamond,
    RampTrack
};

struct RampHitResult {
    RampHitKind kind{RampHitKind::None};
    size_t index{0};     // stop index or segment index
    double offset{0.0};  // clicked offset (0.0 .. 1.0) on the track
};

/// Paints the horizontal Gradient Ramp slider bar with stop pins, midpoints, and active highlights.
void paint_gradient_ramp(
    HDC dc,
    const RECT& bounds,
    const document::GradientPaint& paint,
    uint64_t selected_stop_id,
    ThemeMode theme,
    const color::IccPreview* preview);

/// Hit tests mouse coordinate against the ramp component.
[[nodiscard]] RampHitResult hit_test_gradient_ramp(
    POINT pt,
    const RECT& bounds,
    const document::GradientPaint& paint);

/// Preset gradient definition.
struct GradientPreset {
    const wchar_t* name_vi;
    const wchar_t* name_en;
    document::GradientKind kind;
    std::vector<std::pair<double, color::RGBColor>> stops;
};

/// Retrieves curated presets (Vàng/Gold, Kim loại/Chrome, Hoàng hôn/Sunset, Cầu vồng/Rainbow, Đen-Trắng/B&W).
[[nodiscard]] const std::vector<GradientPreset>& get_gradient_presets();

} // namespace pikdraw::desktop
