#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_file_menu.hpp"
#include "native_window_menus.hpp"
#include "native_window_page_presets.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "../tools/gradient_tool_controller.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windowsx.h>
#endif

namespace pikdraw::desktop {

#ifdef _WIN32

bool NativeWindow::handle_modal_and_home_mouse_down(int x, int y) {
    if (show_recovery_modal_)
        return handle_recovery_mouse_down(x, y);
    int w = width_;
    int h = height_;
    if (hwnd_) {
        RECT client_rc;
        if (GetClientRect(hwnd_, &client_rc)) {
            w = logical_pixel(client_rc.right - client_rc.left);
            h = logical_pixel(client_rc.bottom - client_rc.top);
        }
    }

    // 0. Handle clicks inside or dismissing the New Document Modal
    if (show_new_doc_modal_) {
        int modal_w = 680;
        int modal_h = 510;
        int modal_x = (w - modal_w) / 2;
        int modal_y = (h - modal_h) / 2;

        if (x < modal_x || x > modal_x + modal_w || y < modal_y || y > modal_y + modal_h) {
            close_new_doc_modal();
            return true;
        }

        // Close button [X]
        if (x >= modal_x + modal_w - 38 && x <= modal_x + modal_w - 12 &&
            y >= modal_y + 9 && y <= modal_y + 35) {
            close_new_doc_modal();
            return true;
        }

        int cx = modal_x + 24;
        int cy = modal_y + 58;

        // Presets: 6 cards (y: cy + 24 to cy + 120)
        struct PresetInfo { double w; double h; };
        PresetInfo presets[] = {
            { 210.0, 297.0 }, { 297.0, 420.0 }, { 420.0, 594.0 },
            { 2000.0, 1000.0 }, { 3000.0, 1000.0 }, { 1080.0, 1080.0 }
        };
        for (int i = 0; i < 6; ++i) {
            int px = cx + (i % 3) * 212;
            int py = cy + 24 + (i / 3) * 48;
            if (x >= px && x <= px + 204 && y >= py && y <= py + 42) {
                new_doc_w_ = presets[i].w;
                new_doc_h_ = presets[i].h;
                new_doc_landscape_ = (new_doc_w_ > new_doc_h_);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
        }

        int fy_form = cy + 132; // Match the painted File > New form rows.
        // Width input box
        RECT rc_w_box = { cx, fy_form + 22, cx + 290, fy_form + 54 };
        if (x >= rc_w_box.left && x <= rc_w_box.right && y >= rc_w_box.top && y <= rc_w_box.bottom) {
            start_numeric_input(NumericFieldId::ModalWidth);
            return true;
        }

        // Height input box
        RECT rc_h_box = { cx + 320, fy_form + 22, cx + 610, fy_form + 54 };
        if (x >= rc_h_box.left && x <= rc_h_box.right && y >= rc_h_box.top && y <= rc_h_box.bottom) {
            start_numeric_input(NumericFieldId::ModalHeight);
            return true;
        }

        if (active_num_field_ == NumericFieldId::ModalWidth || active_num_field_ == NumericFieldId::ModalHeight) {
            commit_numeric_field();
        }

        // Orientation & Color Mode (oy: fy_form + 64, y: oy + 22 to oy + 54)
        int oy = fy_form + 64;
        if (y >= oy + 22 && y <= oy + 54) {
            // Portrait [ ▯ Dọc ]
            if (x >= cx && x <= cx + 140) {
                new_doc_landscape_ = false;
                if (new_doc_w_ > new_doc_h_) std::swap(new_doc_w_, new_doc_h_);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
            // Landscape [ ▭ Ngang ]
            if (x >= cx + 150 && x <= cx + 300) {
                new_doc_landscape_ = true;
                if (new_doc_h_ > new_doc_w_) std::swap(new_doc_w_, new_doc_h_);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
            // Color Mode: CMYK
            if (x >= cx + 320 && x <= cx + 460) {
                new_doc_color_mode_ = 0;
                new_doc_dpi_ = 300;
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
            // Color Mode: RGB
            if (x >= cx + 470 && x <= cx + 610) {
                new_doc_color_mode_ = 1;
                new_doc_dpi_ = 72;
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
        }

        // DPI (ry: oy + 64, y: ry + 22 to ry + 52)
        int ry = oy + 64;
        if (y >= ry + 22 && y <= ry + 52) {
            int dpis[] = { 300, 150, 72 };
            for (int i = 0; i < 3; ++i) {
                int dx = cx + i * 205;
                if (x >= dx && x <= dx + 195) {
                    new_doc_dpi_ = dpis[i];
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return true;
                }
            }
        }

        // Footer buttons (fy_btn: modal_y + modal_h - 48)
        int fy_btn = modal_y + modal_h - 48;
        if (y >= fy_btn + 8 && y <= fy_btn + 40) {
            // Cancel [ Hủy bỏ ]
            if (x >= modal_x + modal_w - 280 && x <= modal_x + modal_w - 150) {
                close_new_doc_modal();
                return true;
            }
            // Create [ Tạo tài liệu ]
            if (x >= modal_x + modal_w - 140 && x <= modal_x + modal_w - 15) {
                confirm_new_document();
                return true;
            }
        }
        return true;
    }

    // 1. Handle clicks inside or dismissing the Settings Modal (Corel Options Dialog)
    if (show_settings_modal_) {
        int modal_w = 720;
        int modal_h = 500;
        int modal_x = (w - modal_w) / 2;
        int modal_y = (h - modal_h) / 2;

        if (x < modal_x || x > modal_x + modal_w || y < modal_y || y > modal_y + modal_h) {
            close_settings_modal();
            return true;
        }

        // Close button [X]
        if (x >= modal_x + modal_w - 38 && x <= modal_x + modal_w - 12 &&
            y >= modal_y + 9 && y <= modal_y + 35) {
            close_settings_modal();
            return true;
        }

        // Left sidebar tabs (7 categories)
        if (x >= modal_x + 10 && x <= modal_x + 185) {
            for (int i = 0; i < 8; ++i) {
                int ty = modal_y + 54 + i * 36;
                if (y >= ty && y <= ty + 32) {
                    set_settings_tab(i);
                    return true;
                }
            }
        }

        // Tab 0: Chung & Ngôn ngữ (General & Language)
        if (settings_active_tab_ == 0) {
            // UI Language cards (y: modal_y + 85 to modal_y + 115)
            if (y >= modal_y + 85 && y <= modal_y + 115) {
                if (x >= modal_x + 205 && x <= modal_x + 345) { set_language(UILanguage::Vietnamese); return true; }
                if (x >= modal_x + 355 && x <= modal_x + 495) { set_language(UILanguage::English); return true; }
            }
            // Quick Theme Mode cards inside General tab (y: modal_y + 155 to modal_y + 185)
            if (y >= modal_y + 155 && y <= modal_y + 185) {
                if (x >= modal_x + 205 && x <= modal_x + 345) { set_settings_theme_choice(0); return true; }
                if (x >= modal_x + 355 && x <= modal_x + 495) { set_settings_theme_choice(1); return true; }
            }
        }

        // Tab 1: Giao diện & Hiển thị (Display & Theme)
        if (settings_active_tab_ == 1) {
            // Theme Mode cards
            if (y >= modal_y + 95 && y <= modal_y + 175) {
                int cx0 = modal_x + 205;
                if (x >= cx0 && x <= cx0 + 155) { set_settings_theme_choice(0); return true; }
                if (x >= cx0 + 165 && x <= cx0 + 320) { set_settings_theme_choice(1); return true; }
                if (x >= cx0 + 330 && x <= cx0 + 485) { set_settings_theme_choice(2); return true; }
            }
            // Accent Color options
            if (y >= modal_y + 225 && y <= modal_y + 255) {
                for (int i = 0; i < 4; ++i) {
                    int ax = modal_x + 205 + i * 90;
                    if (x >= ax && x <= ax + 85) { set_settings_accent_choice(i); return true; }
                }
            }
            // Page-focused versus Artboard overview, below the theme settings.
            if (y >= modal_y + 388 && y <= modal_y + 424) {
                if (x >= modal_x + 205 && x <= modal_x + 443) {
                    set_artboard_overview_enabled(false);
                    return true;
                }
                if (x >= modal_x + 455 && x <= modal_x + 695) {
                    set_artboard_overview_enabled(true);
                    return true;
                }
            }
        }

        // Tab 2: Đơn vị & Bắt điểm (Units & Snapping)
        if (settings_active_tab_ == 2 && y >= modal_y + 86 && y <= modal_y + 154) {
            for (int i = 0; i < 8; ++i) {
                const int ux = modal_x + 205 + (i % 4) * 120;
                const int uy = modal_y + 86 + (i / 4) * 34;
                if (x >= ux && x <= ux + 105 && y >= uy && y <= uy + 28) {
                    set_settings_unit_choice(i);
                    return true;
                }
            }
        }
        if (settings_active_tab_ == 2 && x >= modal_x + 205 && x <= modal_x + 695 &&
            y >= modal_y + 297 && y <= modal_y + 324) {
            if (active_num_field_ != NumericFieldId::None) {
                commit_numeric_field();
                if (active_num_field_ != NumericFieldId::None) return true;
            }
            start_numeric_input(NumericFieldId::GridSpacing);
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Khoảng lưới theo đơn vị trang; hậu tố đổi đơn vị · Enter lưu · Esc hủy"
                : "Grid interval uses the page unit; a suffix overrides it · Enter saves · Esc cancels";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (settings_active_tab_ == 2 && x >= modal_x + 205 && x <= modal_x + 695 &&
            y >= modal_y + 331 && y < modal_y + 457 && drawing_tool_) {
            const int row = (y - (modal_y + 331)) / 21;
            if (row >= 0 && row < 6) {
                if (active_num_field_ != NumericFieldId::None) {
                    commit_numeric_field();
                    if (active_num_field_ != NumericFieldId::None) return true;
                }
                bool changed = row == 0 ? drawing_tool_->toggle_snap_master()
                    : row == 1 ? drawing_tool_->toggle_snap_objects()
                    : row == 2 ? drawing_tool_->toggle_smart_guides()
                    : row == 3 ? drawing_tool_->toggle_smart_spacing()
                    : row == 4 ? drawing_tool_->toggle_snap_page()
                               : drawing_tool_->toggle_snap_guides();
                if (changed) {
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Đã cập nhật bắt điểm · áp dụng ngay khi kéo/vẽ"
                        : "Snapping updated · applies immediately while drawing/moving";
                    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                }
                return true;
            }
        }
        if (settings_active_tab_ == 3) {
            // Profile manager remains reachable from the Color & ICC page header.
            if (x >= modal_x + 533 && x <= modal_x + 695 &&
                y >= modal_y + 56 && y <= modal_y + 82) {
                show_color_profiles();
                return true;
            }
            const int tac_row_top = modal_y + 274;
            if (y >= tac_row_top && y <= tac_row_top + 28) {
                if (x >= modal_x + 205 && x <= modal_x + 287) {
                    settings_tac_focus_index_ = 0;
                    if (active_num_field_ != NumericFieldId::None) commit_numeric_field();
                    if (active_num_field_ == NumericFieldId::None) set_settings_tac_scope_choice(0);
                    return true;
                }
                if (x >= modal_x + 291 && x <= modal_x + 373) {
                    settings_tac_focus_index_ = 1;
                    if (active_num_field_ != NumericFieldId::None) commit_numeric_field();
                    if (active_num_field_ == NumericFieldId::None) set_settings_tac_scope_choice(1);
                    return true;
                }
                if (x >= modal_x + 377 && x <= modal_x + 459) {
                    settings_tac_focus_index_ = 2;
                    if (active_num_field_ != NumericFieldId::None) commit_numeric_field();
                    if (active_num_field_ == NumericFieldId::None) set_settings_tac_scope_choice(2);
                    return true;
                }
                if (x >= modal_x + 467 && x <= modal_x + 543) {
                    settings_tac_focus_index_ = 3;
                    start_numeric_input(NumericFieldId::TacLimitPercent);
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Ngưỡng TAC thủ công 100–400% · Enter áp dụng · Esc hủy"
                        : "Manual TAC limit 100–400% · Enter applies · Esc cancels";
                    return true;
                }
                if (x >= modal_x + 563 && x <= modal_x + 629) {
                    settings_tac_focus_index_ = 4;
                    if (active_num_field_ != NumericFieldId::None) commit_numeric_field();
                    if (active_num_field_ == NumericFieldId::None) validate_settings_tac();
                    return true;
                }
                if (x >= modal_x + 635 && x <= modal_x + 695) {
                    settings_tac_focus_index_ = 5;
                    if (active_num_field_ != NumericFieldId::None) commit_numeric_field();
                    if (active_num_field_ == NumericFieldId::None) fix_settings_tac();
                    return true;
                }
            }
        }

        // Tab 4: Phím tắt & Thao tác (Shortcuts profile chooser)
        if (settings_active_tab_ == 4 && y >= modal_y + 80 && y <= modal_y + 115) {
            for (int i = 0; i < 3; ++i) {
                int px = modal_x + 205 + i * 160;
                if (x >= px && x <= px + 150) {
                    shortcut_profile_ = i;
                    if (pen_tool_ && pen_tool_->active()) {
                        auto preferences = tools::PenGesturePreferences{};
                        preferences.profile = i == 0
                            ? tools::PenShortcutProfile::Corel
                            : i == 1 ? tools::PenShortcutProfile::Illustrator
                                     : tools::PenShortcutProfile::PikDraw;
                        (void)pen_tool_->set_gesture_preferences(preferences);
                    }
                    status_msg_ = (language_ == UILanguage::Vietnamese) ?
                        "Đã chuyển hồ sơ phím tắt" : "Switched shortcut profile";
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return true;
                }
            }
        }

        if (settings_active_tab_ == 6 && x >= modal_x + 205 && x <= modal_x + 695) {
            if (y >= modal_y + 92 && y <= modal_y + 150) {
                settings_ai_focus_index_ = 0;
                toggle_ai_connections_read_choice();
                return true;
            }
            if (y >= modal_y + 160 && y <= modal_y + 226) {
                settings_ai_focus_index_ = 1;
                toggle_ai_connections_edit_choice();
                return true;
            }
            if (x >= modal_x + 205 + 410 && y >= modal_y + 304 && y <= modal_y + 324) {
                settings_ai_focus_index_ = 2;
                activate_ai_connections_focus();
                return true;
            }
            if (x >= modal_x + 205 + 410 && y >= modal_y + 325 && y <= modal_y + 345) {
                settings_ai_focus_index_ = 3;
                activate_ai_connections_focus();
                return true;
            }
        }

        // Footer buttons
        if (y >= modal_y + modal_h - 44 && y <= modal_y + modal_h - 10) {
            // Reset to defaults
            if (x >= modal_x + 16 && x <= modal_x + 160) {
                if (settings_active_tab_ == 6) {
                    settings_ai_focus_index_ = 4;
                    reset_ai_connections_choices();
                    return true;
                }
                set_language(UILanguage::English);
                set_theme(ThemeMode::Light);
                settings_accent_choice_ = 0;
                set_settings_unit_choice(0);
                status_msg_ = (language_ == UILanguage::Vietnamese) ?
                    "Đã khôi phục cấu hình mặc định (Light Mode Silver Tech)" :
                    "Reset preferences to defaults (Light Mode Silver Tech)";
                InvalidateRect(hwnd_, NULL, FALSE);
                return true;
            }
            // Cancel
            if (x >= modal_x + modal_w - 170 && x <= modal_x + modal_w - 95) {
                settings_ai_focus_index_ = 5;
                close_settings_modal();
                return true;
            }
            // OK / Apply
            if (x >= modal_x + modal_w - 85 && x <= modal_x + modal_w - 15) {
                if (settings_active_tab_ == 6) {
                    settings_ai_focus_index_ = 6;
                    (void)apply_ai_connections_choices();
                    return true;
                }
                close_settings_modal();
                return true;
            }
        }
        return true;
    }

    if (open_menu_id_ >= 0) {
        if (handle_text_case_submenu_mouse_down(x, y)) return true;
        const auto menu = static_cast<MenuId>(open_menu_id_);
        const int menu_height = get_menu_height(menu);
        constexpr int menu_width = 300;
        const RECT anchor = get_menu_bar_item_rect(open_menu_id_, 38);
        const int menu_x = std::clamp(static_cast<int>(anchor.left), 4, std::max(4, w - menu_width - 4));
        const int menu_y = std::clamp(38, 4, std::max(4, h - menu_height - 4));
        if (x >= menu_x && x < menu_x + menu_width &&
            y >= menu_y && y < menu_y + menu_height) {
            const auto action = get_menu_action_at(menu, y - menu_y);
            close_menu();
            if (action && is_menu_action_enabled(*action)) execute_menu_action(static_cast<int>(*action));
            return true;
        }
        const int top_item = get_menu_bar_item_at(x, y, 38);
        if (top_item >= 0) {
            if (top_item == open_menu_id_) close_menu();
            else open_menu(top_item);
            return true;
        }
        close_menu();
    } else {
        const int top_item = get_menu_bar_item_at(x, y, 38);
        if (top_item >= 0) {
            toggle_menu(top_item);
            return true;
        }
    }

    // Page preset dropdown list click handling (CorelDRAW / Illustrator Style)
    if (show_page_preset_dropdown_) {
        int drop_x = 52;
        int drop_y = 38 + 28 + 36; // 102
        int drop_w = kPagePresetDropdownWidth; // 240
        int drop_h = page_preset_dropdown_height();
        if (x >= drop_x && x <= drop_x + drop_w && y >= drop_y && y <= drop_y + drop_h) {
            int row_y = drop_y + kPagePresetPadding;
            for (size_t i = 0; i < kPagePresetEntries.size(); ++i) {
                if (y >= row_y && y < row_y + kPagePresetRowHeight) {
                    select_page_preset(static_cast<int>(i));
                    return true;
                }
                row_y += kPagePresetRowHeight;
            }
            return true;
        }
        close_page_preset_dropdown();
        if (x >= 52 && x <= 165 && y >= 38 + 28 && y <= 38 + 28 + 36) {
            return true;
        }
    }

    // 2. Handle clicks on Home Screen (Trang Home)
    if (show_home_screen_) {
        // Back to Canvas button at top-right
        if (x >= w - 240 && x <= w - 40 && y >= 55 && y <= 88) {
            close_home_screen();
            return true;
        }

        // Home Navigation Tabs (y: 110 to 142)
        if (y >= 110 && y <= 142) {
            int tx = 180;
            if (x >= tx && x < tx + 140) { set_active_home_tab(0); return true; }
            if (x >= tx + 145 && x < tx + 295) { set_active_home_tab(1); return true; }
            if (x >= tx + 300 && x < tx + 480) { set_active_home_tab(2); return true; }
            if (x >= tx + 485 && x < tx + 665) { set_active_home_tab(3); return true; }
        }

        // Tab 0: Bắt đầu nhanh cards (y: 160 to 280)
        if (active_home_tab_ == 0 && y >= 160 && y <= 280) {
            if (x >= 180 && x <= 420) { close_home_screen(); return true; } // Tạo mới
            if (x >= 440 && x <= 680) { set_active_home_tab(2); return true; } // Từ mẫu
            if (x >= 700 && x <= 940) { close_home_screen(); return true; } // Mở tệp
            if (x >= 960 && x <= 1200) {
                status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đang đồng bộ Cloud..." : "Connecting to Cloud...";
                InvalidateRect(hwnd_, NULL, FALSE);
                return true;
            }
        }

        // Tab 1: Tệp gần đây rows (y: 160 + i * 55)
        if (active_home_tab_ == 1 && y >= 160) {
            for (int i = 0; i < 5; ++i) {
                int ry = 160 + i * 55;
                if (y >= ry && y <= ry + 46 && x >= 180 && x <= w - 180) {
                    open_recent_document(i);
                    return true;
                }
            }
        }

        // Tab 2: Mẫu thi công cards
        if (active_home_tab_ == 2) {
            if (y >= 160 && y <= 260) {
                if (x >= 180 && x <= 520) { create_from_template(0); return true; }
                if (x >= 540 && x <= 880) { create_from_template(1); return true; }
                if (x >= 900 && x <= 1240) { create_from_template(2); return true; }
            }
            if (y >= 280 && y <= 380) {
                if (x >= 180 && x <= 520) { create_from_template(3); return true; }
                if (x >= 540 && x <= 880) { create_from_template(4); return true; }
                if (x >= 900 && x <= 1240) { create_from_template(5); return true; }
            }
        }
    }
    return false;
}
#endif

} // namespace pikdraw::desktop
