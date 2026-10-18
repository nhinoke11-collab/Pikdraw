#pragma once

#include "pikdraw/desktop/shell/native_window.hpp"
#include "../tools/gradient_tool_controller.hpp"

namespace pikdraw::desktop {

namespace GradientPropertiesLayout {
inline constexpr int content_left = 14;
inline constexpr int content_width = 252;
inline constexpr int title_top = 2;
inline constexpr int kind_top = 34;
inline constexpr int spread_top = 61;
inline constexpr int stops_header_top = 90;
inline constexpr int stops_top = 108;
inline constexpr int stop_row_height = 24;
inline constexpr int stop_row_gap = 2;
inline constexpr int visible_stop_rows = 3;
inline constexpr int stop_actions_top = 188;
inline constexpr int color_top = 223;
inline constexpr int offset_top = 257;
inline constexpr int opacity_top = 286;
inline constexpr int midpoint_top = 315;
inline constexpr int geometry_heading_top = 344;
inline constexpr int angle_top = 363;
inline constexpr int length_top = 392;
inline constexpr int center_x_top = 421;
inline constexpr int center_y_top = 450;
inline constexpr int hint_top = 479;
inline constexpr int actions_top = 517;
inline constexpr int field_height = 26;
} // namespace GradientPropertiesLayout

void paint_gradient_properties_panel(
    HDC dc, int inspector_x, int width, int top, int bottom, ThemeMode theme, UILanguage language,
    tools::GradientToolController& controller, NativeWindow::NumericFieldId active_field,
    const std::string& numeric_buffer);

} // namespace pikdraw::desktop
