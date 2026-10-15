#pragma once

#include <cstdint>

namespace pikdraw::desktop {

/// Shell dimensions conforming to 2026 Cyber Dark Design System and Contract C7
struct ShellGeometry {
    static constexpr double kTopbarHeight = 38.0;
    static constexpr double kContextRibbonHeight = 34.0;
    static constexpr double kMinToolStripWidth = 48.0;
    static constexpr double kDefaultToolStripWidth = 52.0;
    static constexpr double kMaxToolStripWidth = 56.0;
    static constexpr double kMinInspectorWidth = 280.0;
    static constexpr double kDefaultInspectorWidth = 300.0;
    static constexpr double kMaxInspectorWidth = 320.0;
    static constexpr double kStatusBarHeight = 26.0;

    struct CanvasArea {
        double x{0.0};
        double y{0.0};
        double width{0.0};
        double height{0.0};
    };

    /// Compute available canvas viewport rectangle inside the main window
    [[nodiscard]] static CanvasArea compute_canvas_area(
        double window_width,
        double window_height,
        double dpi_scale = 1.0,
        bool inspector_visible = true,
        double custom_inspector_width = kDefaultInspectorWidth,
        double custom_tool_strip_width = kDefaultToolStripWidth
    ) noexcept;

    /// Validates and clamps DPI scaling factor (typically 1.0, 1.5, 2.0)
    [[nodiscard]] static double clamp_dpi_scale(double dpi_scale) noexcept;
};

} // namespace pikdraw::desktop
