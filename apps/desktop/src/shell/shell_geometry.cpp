#include "pikdraw/desktop/shell/shell_geometry.hpp"
#include <algorithm>

namespace pikdraw::desktop {

double ShellGeometry::clamp_dpi_scale(double dpi_scale) noexcept {
    if (dpi_scale <= 1.0) {
        return 1.0;
    }
    if (dpi_scale <= 1.25) {
        return 1.25;
    }
    if (dpi_scale <= 1.5) {
        return 1.5;
    }
    if (dpi_scale <= 1.75) {
        return 1.75;
    }
    return 2.0;
}

ShellGeometry::CanvasArea ShellGeometry::compute_canvas_area(
    double window_width,
    double window_height,
    double dpi_scale,
    bool inspector_visible,
    double custom_inspector_width,
    double custom_tool_strip_width
) noexcept {
    const double scale = clamp_dpi_scale(dpi_scale);
    const double scaled_topbar = kTopbarHeight * scale;
    const double scaled_ribbon = kContextRibbonHeight * scale;
    const double scaled_statusbar = kStatusBarHeight * scale;

    const double tool_width = std::clamp(
        custom_tool_strip_width, kMinToolStripWidth, kMaxToolStripWidth) * scale;
    const double insp_width = inspector_visible ? std::clamp(
        custom_inspector_width, kMinInspectorWidth, kMaxInspectorWidth) * scale : 0.0;

    CanvasArea area;
    area.x = tool_width;
    area.y = scaled_topbar + scaled_ribbon;
    area.width = std::max(0.0, window_width - tool_width - insp_width);
    area.height = std::max(0.0, window_height - (scaled_topbar + scaled_ribbon + scaled_statusbar));

    return area;
}

} // namespace pikdraw::desktop
