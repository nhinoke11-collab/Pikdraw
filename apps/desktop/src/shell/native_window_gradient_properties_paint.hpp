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
inline constexpr int ramp_header_top = 90;
inline constexpr int ramp_top = 110;
inline constexpr int ramp_height = 48;
inline constexpr int stop_actions_top = 164;
inline constexpr int color_top = 196;
inline constexpr int offset_top = 228;
inline constexpr int opacity_top = 256;
inline constexpr int midpoint_top = 284;
inline constexpr int preset_header_top = 314;
inline constexpr int presets_top = 334;
inline constexpr int align_top = 366;
inline constexpr int geometry_heading_top = 398;
inline constexpr int angle_top = 418;
inline constexpr int length_top = 446;
inline constexpr int center_x_top = 474;
inline constexpr int center_y_top = 502;
inline constexpr int hint_top = 530;
inline constexpr int actions_top = 562;
inline constexpr int field_height = 26;
} // namespace GradientPropertiesLayout

void paint_gradient_properties_panel(
    HDC dc, int inspector_x, int width, int top, int bottom, ThemeMode theme, UILanguage language,
    tools::GradientToolController& controller, NativeWindow::NumericFieldId active_field,
    const std::string& numeric_buffer, const color::IccPreview* color_preview,
    geometry::units::LengthUnit display_length_unit);

} // namespace pikdraw::desktop
