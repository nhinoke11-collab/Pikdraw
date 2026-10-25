#pragma once

#include "native_window_length_input.hpp"
#include <string>

namespace pikdraw::desktop {

inline std::wstring format_snap_grid_status(
    bool enabled, double spacing_mm, geometry::units::LengthUnit display_unit, bool vietnamese) {
    const auto interval = format_mm_display(spacing_mm, display_unit, 4);
    if (vietnamese) {
        return std::wstring(enabled ? L"☑ Hít vào lưới: BẬT · Bước "
                                    : L"☐ Hít vào lưới: TẮT · Bước ") + interval;
    }
    return std::wstring(enabled ? L"☑ Snap to Grid: ON · Interval "
                                : L"☐ Snap to Grid: OFF · Interval ") + interval;
}

} // namespace pikdraw::desktop
