#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_file_menu.hpp"

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#include <windowsx.h>
#endif

namespace pikdraw::desktop {

#ifdef _WIN32

void NativeWindow::handle_mouse_down(int x, int y, unsigned modifiers) {
    int w = width_;

    // Handle clicks inside or dismissing the Settings Modal (Corel Options Dialog)
    if (show_settings_modal_) {
        int modal_w = 720;
        int modal_h = 500;
        int modal_x = (w - modal_w) / 2;
        int modal_y = (height_ - modal_h) / 2;

        if (x < modal_x || x > modal_x + modal_w || y < modal_y || y > modal_y + modal_h) {
            close_settings_modal();
            return;
        }

        // Close button [X]
        if (x >= modal_x + modal_w - 38 && x <= modal_x + modal_w - 12 &&
            y >= modal_y + 9 && y <= modal_y + 35) {
            close_settings_modal();
            return;
        }

        // Left sidebar tabs (6 categories)
        if (x >= modal_x + 10 && x <= modal_x + 185) {
            for (int i = 0; i < 6; ++i) {
                int ty = modal_y + 54 + i * 36;
                if (y >= ty && y <= ty + 32) {
                    set_settings_tab(i);
                    return;
                }
            }
        }

        // Tab 0: Chung & Ngôn ngữ (General & Language)
        if (settings_active_tab_ == 0) {
            // UI Language cards (y: modal_y + 85 to modal_y + 115)
            if (y >= modal_y + 85 && y <= modal_y + 115) {
                if (x >= modal_x + 205 && x <= modal_x + 345) { set_language(UILanguage::Vietnamese); return; }
                if (x >= modal_x + 355 && x <= modal_x + 495) { set_language(UILanguage::English); return; }
            }
            // Quick Theme Mode cards inside General tab (y: modal_y + 155 to modal_y + 185)
            if (y >= modal_y + 155 && y <= modal_y + 185) {
                if (x >= modal_x + 205 && x <= modal_x + 345) { set_settings_theme_choice(0); return; }
                if (x >= modal_x + 355 && x <= modal_x + 495) { set_settings_theme_choice(1); return; }
            }
        }

        // Tab 1: Giao diện & Hiển thị (Display & Theme)
        if (settings_active_tab_ == 1) {
            // Theme Mode cards
            if (y >= modal_y + 95 && y <= modal_y + 175) {
                int cx0 = modal_x + 205;
                if (x >= cx0 && x <= cx0 + 155) { set_settings_theme_choice(0); return; }
                if (x >= cx0 + 165 && x <= cx0 + 320) { set_settings_theme_choice(1); return; }
                if (x >= cx0 + 330 && x <= cx0 + 485) { set_settings_theme_choice(2); return; }
            }
            // Accent Color options
            if (y >= modal_y + 225 && y <= modal_y + 255) {
                for (int i = 0; i < 4; ++i) {
                    int ax = modal_x + 205 + i * 90;
                    if (x >= ax && x <= ax + 85) { set_settings_accent_choice(i); return; }
                }
            }
        }

        // Tab 2: Đơn vị & Bắt điểm (Units & Snapping)
        if (settings_active_tab_ == 2 && y >= modal_y + 95 && y <= modal_y + 130) {
            for (int i = 0; i < 4; ++i) {
                int ux = modal_x + 205 + i * 115;
                if (x >= ux && x <= ux + 105) { set_settings_unit_choice(i); return; }
            }
        }
        if (settings_active_tab_ == 3 && x >= modal_x + 205 && x <= modal_x + 695 &&
            y >= modal_y + 338 && y <= modal_y + 370) {
            show_color_profiles();
            return;
        }

        // Footer buttons
        if (y >= modal_y + modal_h - 44 && y <= modal_y + modal_h - 10) {
            // Reset to defaults
            if (x >= modal_x + 16 && x <= modal_x + 160) {
                set_theme(ThemeMode::CyberDark);
                settings_accent_choice_ = 0;
                settings_unit_choice_ = 0;
                status_msg_ = (language_ == UILanguage::Vietnamese) ?
                    "Đã khôi phục cấu hình mặc định (Dark Mode Obsidian)" :
                    "Reset preferences to defaults (Dark Mode Obsidian)";
                InvalidateRect(hwnd_, NULL, FALSE);
                return;
            }
            // Cancel
            if (x >= modal_x + modal_w - 170 && x <= modal_x + modal_w - 95) {
                close_settings_modal();
                return;
            }
            // OK / Apply
            if (x >= modal_x + modal_w - 85 && x <= modal_x + modal_w - 15) {
                close_settings_modal();
                return;
            }
        }
        return;
    }

    // File menu exposes only actions wired to the current document workflow.
    if (show_file_menu_) {
        const int mx = 402, my = 38, mw = 270, mh = file_menu_height();
        if (x >= mx && x <= mx + mw && y >= my && y <= my + mh) {
            const auto action = file_menu_action_at(y - my);
            close_file_menu();
            if (!action)
                return;
            switch (*action) {
            case FileMenuAction::NewDocument:
                create_new_document();
                break;
            case FileMenuAction::Open:
                open_document_dialog();
                break;
            case FileMenuAction::Save:
                save_document_dialog();
                break;
            case FileMenuAction::SaveAs:
                save_document_dialog(true);
                break;
            case FileMenuAction::ExportSvg:
                export_svg_dialog();
                break;
            case FileMenuAction::Undo:
                workflow_.undo();
                root_.vector_controller().selection().clear();
                status_msg_ = language_ == UILanguage::Vietnamese ? "Đã hoàn tác" : "Undo complete";
                InvalidateRect(hwnd_, nullptr, FALSE);
                break;
            case FileMenuAction::Redo:
                workflow_.redo();
                root_.vector_controller().selection().clear();
                status_msg_ = language_ == UILanguage::Vietnamese ? "Đã làm lại" : "Redo complete";
                InvalidateRect(hwnd_, nullptr, FALSE);
                break;
            case FileMenuAction::Exit:
                DestroyWindow(hwnd_);
                break;
            case FileMenuAction::Separator:
                break;
            }
            return;
        }
        close_file_menu();
    }

    // 2. Handle clicks on Home Screen (Trang Home)
    if (show_home_screen_) {
        // Back to Canvas button at top-right
        if (x >= w - 240 && x <= w - 40 && y >= 55 && y <= 88) {
            close_home_screen();
            return;
        }

        // Home Navigation Tabs (y: 110 to 142)
        if (y >= 110 && y <= 142) {
            int tx = 180;
            if (x >= tx && x < tx + 140) { set_active_home_tab(0); return; }
            if (x >= tx + 145 && x < tx + 295) { set_active_home_tab(1); return; }
            if (x >= tx + 300 && x < tx + 480) { set_active_home_tab(2); return; }
            if (x >= tx + 485 && x < tx + 665) { set_active_home_tab(3); return; }
        }

        // Tab 0: Bắt đầu nhanh cards (y: 160 to 280)
        if (active_home_tab_ == 0 && y >= 160 && y <= 280) {
            if (x >= 180 && x <= 420) { close_home_screen(); return; } // Tạo mới
            if (x >= 440 && x <= 680) { set_active_home_tab(2); return; } // Từ mẫu
            if (x >= 700 && x <= 940) { close_home_screen(); return; } // Mở tệp
            if (x >= 960 && x <= 1200) {
                status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đang đồng bộ Cloud..." : "Connecting to Cloud...";
                InvalidateRect(hwnd_, NULL, FALSE);
                return;
            }
        }

        // Tab 1: Tệp gần đây rows (y: 160 + i * 55)
        if (active_home_tab_ == 1 && y >= 160) {
            for (int i = 0; i < 5; ++i) {
                int ry = 160 + i * 55;
                if (y >= ry && y <= ry + 46 && x >= 180 && x <= w - 180) {
                    open_recent_document(i);
                    return;
                }
            }
        }

        // Tab 2: Mẫu thi công cards
        if (active_home_tab_ == 2) {
            if (y >= 160 && y <= 260) {
                if (x >= 180 && x <= 520) { create_from_template(0); return; }
                if (x >= 540 && x <= 880) { create_from_template(1); return; }
                if (x >= 900 && x <= 1240) { create_from_template(2); return; }
            }
            if (y >= 280 && y <= 380) {
                if (x >= 180 && x <= 520) { create_from_template(3); return; }
                if (x >= 540 && x <= 880) { create_from_template(4); return; }
                if (x >= 900 && x <= 1240) { create_from_template(5); return; }
            }
        }
    }

    // Direct document actions: New, Open, Save, Undo, Redo.
    if (y >= 6 && y <= 32 && x >= 148 && x < 402) {
        if (x < 198) create_new_document();
        else if (x < 248) open_document_dialog();
        else if (x < 298) save_document_dialog();
        else if (x < 350) {
            workflow_.undo();
            root_.vector_controller().selection().clear();
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã hoàn tác" : "Undo complete";
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else {
            workflow_.redo();
            root_.vector_controller().selection().clear();
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã làm lại" : "Redo complete";
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }

    // Compact File menu with only implemented document commands.
    if (x >= 402 && x <= 456 && y >= 6 && y <= 32) {
        toggle_file_menu();
        return;
    }

    // Topbar Settings Button [ ⚙ Cài đặt (Ctrl+J) ] (x: w - 215 to w - 105, y: 6 to 32)
    int sett_x = w - 215;
    if (x >= sett_x && x <= sett_x + 110 && y >= 6 && y <= 32) {
        open_settings_modal();
        return;
    }

    // Topbar Export Button [ Xuất file ] (x: w - 95 to w - 12, y: 6 to 32)
    if (x >= w - 95 && x <= w - 12 && y >= 6 && y <= 32) {
        trigger_export();
        return;
    }

    // The Vector 2D workspace is the only implemented workspace.
    if (x >= 520 && x < 620 && y >= 6 && y <= 32) {
        set_active_workspace(0);
        return;
    }

    // Space + left drag temporarily pans without changing the active tool.
    if (space_down_ && is_canvas_point(x, y)) {
        is_panning_ = true;
        last_pointer_x_ = x;
        last_pointer_y_ = y;
        SetCapture(hwnd_);
        return;
    }

    // 4. Tool Strip (x: 6 to 46)
    if (x >= 6 && x <= 46 && y >= 80) {
        int tool_idx = (y - 82) / 42;
        if (tool_idx >= 0 && tool_idx < 4) {
            set_active_tool(tool_idx);
            return;
        }
    }

    // 5. Right Dock Tabs (x: w - 280 to w, y: 72 to 102)
    if (x >= w - 280 && x <= w && y >= 72 && y <= 102) {
        int tab_w = 280 / 3;
        int tab_idx = (x - (w - 280)) / tab_w;
        if (tab_idx >= 0 && tab_idx < 3) {
            set_active_dock_tab(tab_idx);
            return;
        }
    }

    // 6. Page and zoom controls in the status bar.
    if (y >= height_ - 26) {
        if (x >= w - 210 && x <= w - 145) { fit_page_to_canvas(); return; }
        if (x >= w - 120 && x <= w - 95) { zoom_out(); return; }
        if (x >= w - 90 && x <= w - 50) {
            auto& camera = root_.renderer().camera();
            const auto area = canvas_rect(width_, height_);
            camera.set_viewport_size(area.right - area.left, area.bottom - area.top);
            camera.set_zoom(1.0);
            zoom_level_ = 100;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (x >= w - 45 && x <= w - 20) { zoom_in(); return; }
    }

    if (handle_style_click(x, y))
        return;
    handle_canvas_mouse_down(x, y, modifiers);
}

void NativeWindow::handle_mouse_move(int x, int y, unsigned modifiers) {
    handle_canvas_mouse_move(x, y, modifiers);
}

void NativeWindow::handle_mouse_up(int x, int y, unsigned modifiers) {
    if (is_panning_) {
        is_panning_ = false;
        if (GetCapture() == hwnd_)
            ReleaseCapture();
        return;
    }
    handle_canvas_mouse_up(x, y, modifiers);
}

LRESULT CALLBACK NativeWindow::window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<NativeWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (uMsg) {
    case WM_KEYDOWN:
        if (self && wParam == VK_SPACE) {
            self->space_down_ = true;
            return 0;
        }
        if (self && self->handle_key_down(wParam))
            return 0;
        break;
    case WM_KEYUP:
        if (self && wParam == VK_SPACE) {
            self->space_down_ = false;
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        if (self) {
            self->space_down_ = false;
            self->cancel_canvas_interaction();
        }
        break;
    case WM_LBUTTONDOWN:
        if (self) {
            SetFocus(hwnd);
            self->handle_mouse_down(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), static_cast<unsigned>(wParam));
        }
        return 0;
    case WM_MOUSEMOVE:
        if (self) self->handle_mouse_move(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), static_cast<unsigned>(wParam));
        return 0;
    case WM_LBUTTONUP:
        if (self) self->handle_mouse_up(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), static_cast<unsigned>(wParam));
        return 0;
    case WM_RBUTTONDOWN:
        if (self && !self->show_settings_modal_ && !self->show_file_menu_ && !self->show_home_screen_)
            self->handle_style_click(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), true);
        return 0;
    case WM_MBUTTONDOWN:
        if (self && self->is_canvas_point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam))) {
            self->cancel_canvas_interaction();
            self->is_panning_ = true;
            self->last_pointer_x_ = GET_X_LPARAM(lParam);
            self->last_pointer_y_ = GET_Y_LPARAM(lParam);
            SetCapture(hwnd);
        }
        return 0;
    case WM_MBUTTONUP:
        if (self && self->is_panning_) {
            self->is_panning_ = false;
            if (GetCapture() == hwnd) ReleaseCapture();
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (self) {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &point);
            if (self->is_canvas_point(point.x, point.y)) {
                const auto area = self->canvas_rect(self->width_, self->height_);
                auto& camera = self->root_.renderer().camera();
                const double factor = std::pow(1.1,
                    static_cast<double>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA);
                const double target_zoom = std::clamp(camera.zoom() * factor, 0.01, 64.0);
                camera.zoom_at(target_zoom / camera.zoom(),
                    static_cast<float>(point.x - area.left),
                    static_cast<float>(point.y - area.top));
                self->zoom_level_ = std::clamp(
                    static_cast<int>(std::lround(camera.zoom() * 100.0)), 1, 6400);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_SETCURSOR:
        if (self && LOWORD(lParam) == HTCLIENT) {
            POINT point;
            GetCursorPos(&point);
            ScreenToClient(hwnd, &point);
            const WORD cursor_id = self->canvas_cursor_at(point.x, point.y);
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(cursor_id)));
            return TRUE;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (self) self->paint(hdc, rc.right - rc.left, rc.bottom - rc.top);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE: {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        if (self && w > 0 && h > 0) {
            self->width_ = w;
            self->height_ = h;
            self->shell_.set_window_size(static_cast<double>(w), static_cast<double>(h));
            const auto area = self->canvas_rect(w, h);
            self->root_.renderer().camera().set_viewport_size(
                area.right - area.left, area.bottom - area.top);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (self) {
            self->cancel_canvas_interaction();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

#endif

} // namespace pikdraw::desktop
