#pragma once
#include "pikdraw/desktop/shell/native_window.hpp"

namespace pikdraw::desktop {
#ifdef _WIN32
enum class ColorEditContext { Generic, Fill, Stroke };
struct StyleEditorResult {
    color::ColorValue color;
    double width_mm;
    bool color_changed{false};
    bool width_changed{false};
    std::optional<bool> overprint;
};
std::optional<StyleEditorResult> edit_source_color(HWND owner, const color::ColorValue &initial,
                                                   double width_mm, bool stroke, bool vietnamese,
                                                   ColorEditContext context = ColorEditContext::Generic,
                                                   bool mixed_color = false,
                                                   bool mixed_overprint = false,
                                                   std::optional<color::DocumentColorMode> document_mode = {},
                                                   bool dark_theme = false,
                                                   const color::IccPreview* icc = nullptr,
                                                   geometry::units::LengthUnit display_unit =
                                                       geometry::units::LengthUnit::Millimeter);
#endif
} // namespace pikdraw::desktop
