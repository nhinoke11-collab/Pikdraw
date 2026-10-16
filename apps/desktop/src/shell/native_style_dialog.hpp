#pragma once
#include "pikdraw/desktop/shell/native_window.hpp"

namespace pikdraw::desktop {
#ifdef _WIN32
struct StyleEditorResult {
    color::ColorValue color;
    double width_mm;
};
std::optional<StyleEditorResult> edit_source_color(HWND owner, const color::ColorValue &initial,
                                                   double width_mm, bool stroke, bool vietnamese);
#endif
} // namespace pikdraw::desktop
