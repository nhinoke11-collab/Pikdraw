#pragma once

#ifdef _WIN32
#include <windows.h>
#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_menus.hpp"

namespace pikdraw::desktop {

void draw_toolbox_flyout_marker(HDC dc, int right_x, int bottom_y, COLORREF color);
void draw_tool_icon(HDC dc, int tool, int center_x, int center_y, COLORREF color);
void draw_object_context_icon(HDC dc, ObjectContextAction action, int cx, int cy, COLORREF color);
void draw_lock_icon(HDC dc, int cx, int cy, bool locked, COLORREF color);
void draw_angle_icon(HDC dc, int cx, int cy, COLORREF color);

} // namespace pikdraw::desktop
#endif
