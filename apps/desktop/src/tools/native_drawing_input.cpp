#include "pikdraw/desktop/shell/native_window.hpp"
#include "drawing_tool_controller.hpp"

#ifdef _WIN32
#include <windowsx.h>
#endif

namespace pikdraw::desktop {

#ifdef _WIN32

bool NativeWindow::handle_drawing_panel_click(int x, int y) {
    if (!drawing_tool_ || !drawing_tool_->snap_panel_open_value()) return false;
    const RECT area = canvas_rect(width_, height_);
    const int left = area.left + 12, top = area.top + 12;
    const RECT panel{left, top, left + 250, top + 264};
    if (x < panel.left || x >= panel.right || y < panel.top || y >= panel.bottom)
        return false;

    using vector::precision::snap::DirectionMode;
    using vector::precision::snap::SnapOption;
    const int row = y - top;
    if (row >= 32 && row < 56) (void)drawing_tool_->toggle_snap_master();
    else if (row >= 58 && row < 82)
        (void)drawing_tool_->toggle_snap_option(SnapOption::Endpoint);
    else if (row >= 82 && row < 106)
        (void)drawing_tool_->toggle_snap_option(SnapOption::SourceNode);
    else if (row >= 106 && row < 130)
        (void)drawing_tool_->toggle_snap_option(SnapOption::LineMidpoint);
    else if (row >= 130 && row < 154)
        (void)drawing_tool_->toggle_snap_option(SnapOption::Grid);
    else if (row >= 156 && row < 180) {
        const int column = (x - left - 12) / 76;
        if (column == 0) (void)drawing_tool_->toggle_direction(DirectionMode::Free);
        else if (column == 1) (void)drawing_tool_->toggle_direction(DirectionMode::Ortho);
        else if (column == 2) (void)drawing_tool_->toggle_direction(DirectionMode::Polar);
    } else if (row >= 182 && row < 206) {
        (void)drawing_tool_->cycle_polar_step();
    } else if (row >= 207 && row < 231) {
        (void)drawing_tool_->cycle_profile();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool NativeWindow::handle_drawing_click(int x, int y, unsigned modifiers) {
    if ((active_tool_ != 4 && active_tool_ != 5) || !drawing_tool_ ||
        !drawing_tool_->active() || !is_canvas_point(x, y))
        return false;
    drawing_tool_->pointer_move(canvas_to_world(x, y), root_.renderer().camera(),
        window_dpi_scale(), (modifiers & MK_SHIFT) != 0,
        (modifiers & MK_CONTROL) != 0);
    const auto result = drawing_tool_->confirm_candidate();
    if (result.committed() && result.object_id)
        root_.vector_controller().set_selection({*result.object_id},
            *root_.document().current_snapshot());
    status_msg_ = drawing_tool_->visual_state().message;
    workflow_.sync_status_bar();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool NativeWindow::handle_drawing_character(char value) {
    if (!drawing_tool_ || !drawing_tool_->active() ||
        (GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
        (GetKeyState(VK_MENU) & 0x8000) != 0)
        return false;
    // L switches drawing tools and has no supported numeric-input unit meaning.
    if (value == 'l' || value == 'L') return false;
    const bool handled = drawing_tool_->append_numeric_character(value);
    if (handled) InvalidateRect(hwnd_, nullptr, FALSE);
    return handled;
}

#endif

} // namespace pikdraw::desktop
