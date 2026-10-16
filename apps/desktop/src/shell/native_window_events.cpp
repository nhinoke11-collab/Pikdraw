#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_file_menu.hpp"
#include "native_window_page_presets.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <windowsx.h>
#endif

namespace pikdraw::desktop {

#ifdef _WIN32

void NativeWindow::handle_mouse_down(int x, int y) {
    int w = width_;
    int h = height_;
    if (hwnd_) {
        RECT client_rc;
        if (GetClientRect(hwnd_, &client_rc)) {
            w = client_rc.right - client_rc.left;
            h = client_rc.bottom - client_rc.top;
            width_ = w;
            height_ = h;
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
            return;
        }

        // Close button [X]
        if (x >= modal_x + modal_w - 38 && x <= modal_x + modal_w - 12 &&
            y >= modal_y + 9 && y <= modal_y + 35) {
            close_new_doc_modal();
            return;
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
                return;
            }
        }

        int fy_form = cy + 142;
        // Width input box
        RECT rc_w_box = { cx, fy_form + 22, cx + 290, fy_form + 54 };
        if (x >= rc_w_box.left && x <= rc_w_box.right && y >= rc_w_box.top && y <= rc_w_box.bottom) {
            start_numeric_input(NumericFieldId::ModalWidth);
            return;
        }

        // Height input box
        RECT rc_h_box = { cx + 320, fy_form + 22, cx + 610, fy_form + 54 };
        if (x >= rc_h_box.left && x <= rc_h_box.right && y >= rc_h_box.top && y <= rc_h_box.bottom) {
            start_numeric_input(NumericFieldId::ModalHeight);
            return;
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
                return;
            }
            // Landscape [ ▭ Ngang ]
            if (x >= cx + 150 && x <= cx + 300) {
                new_doc_landscape_ = true;
                if (new_doc_h_ > new_doc_w_) std::swap(new_doc_w_, new_doc_h_);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            // Color Mode: CMYK
            if (x >= cx + 320 && x <= cx + 460) {
                new_doc_color_mode_ = 0;
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            // Color Mode: RGB
            if (x >= cx + 470 && x <= cx + 610) {
                new_doc_color_mode_ = 1;
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
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
                    return;
                }
            }
        }

        // Footer buttons (fy_btn: modal_y + modal_h - 48)
        int fy_btn = modal_y + modal_h - 48;
        if (y >= fy_btn + 8 && y <= fy_btn + 40) {
            // Cancel [ Hủy bỏ ]
            if (x >= modal_x + modal_w - 280 && x <= modal_x + modal_w - 150) {
                close_new_doc_modal();
                return;
            }
            // Create [ Tạo tài liệu ]
            if (x >= modal_x + modal_w - 140 && x <= modal_x + modal_w - 15) {
                confirm_new_document();
                return;
            }
        }
        return;
    }

    // 1. Handle clicks inside or dismissing the Settings Modal (Corel Options Dialog)
    if (show_settings_modal_) {
        int modal_w = 720;
        int modal_h = 500;
        int modal_x = (w - modal_w) / 2;
        int modal_y = (h - modal_h) / 2;

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

        // Tab 4: Phím tắt & Thao tác (Shortcuts profile chooser)
        if (settings_active_tab_ == 4 && y >= modal_y + 80 && y <= modal_y + 115) {
            for (int i = 0; i < 3; ++i) {
                int px = modal_x + 205 + i * 160;
                if (x >= px && x <= px + 150) {
                    shortcut_profile_ = i;
                    status_msg_ = (language_ == UILanguage::Vietnamese) ?
                        "Đã chuyển hồ sơ phím tắt" : "Switched shortcut profile";
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return;
                }
            }
        }

        // Footer buttons
        if (y >= modal_y + modal_h - 44 && y <= modal_y + modal_h - 10) {
            // Reset to defaults
            if (x >= modal_x + 16 && x <= modal_x + 160) {
                set_language(UILanguage::English);
                set_theme(ThemeMode::Light);
                settings_accent_choice_ = 0;
                settings_unit_choice_ = 0;
                status_msg_ = (language_ == UILanguage::Vietnamese) ?
                    "Đã khôi phục cấu hình mặc định (Light Mode Silver Tech)" :
                    "Reset preferences to defaults (Light Mode Silver Tech)";
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

    // File menu exposes actions wired to active document workflow.
    if (show_file_menu_) {
        const int mx = 402, my = 38, mw = 270, mh = file_menu_height();
        if (x >= mx && x <= mx + mw && y >= my && y <= my + mh) {
            const auto action = file_menu_action_at(y - my);
            close_file_menu();
            if (!action)
                return;
            switch (*action) {
            case FileMenuAction::NewDocument:
                open_new_doc_modal();
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
            case FileMenuAction::ToggleGuidelines:
                toggle_guidelines();
                break;
            case FileMenuAction::ClearGuidelines:
                clear_guidelines();
                break;
            case FileMenuAction::Undo:
                workflow().undo();
                root().vector_controller().selection().clear();
                status_msg_ = language_ == UILanguage::Vietnamese ? "Đã hoàn tác" : "Undo complete";
                InvalidateRect(hwnd_, nullptr, FALSE);
                break;
            case FileMenuAction::Redo:
                workflow().redo();
                root().vector_controller().selection().clear();
                status_msg_ = language_ == UILanguage::Vietnamese ? "Đã làm lại" : "Redo complete";
                InvalidateRect(hwnd_, nullptr, FALSE);
                break;
            case FileMenuAction::Exit:
                DestroyWindow(hwnd_);
                break;
            case FileMenuAction::Separator:
            case FileMenuAction::Separator2:
            case FileMenuAction::Separator3:
                break;
            }
            return;
        }
        close_file_menu();
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
                    return;
                }
                row_y += kPagePresetRowHeight;
            }
            return;
        }
        close_page_preset_dropdown();
        if (x >= 52 && x <= 165 && y >= 38 + 28 && y <= 38 + 28 + 36) {
            return;
        }
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
        if (x < 198) open_new_doc_modal();
        else if (x < 248) open_document_dialog();
        else if (x < 298) save_document_dialog();
        else if (x < 350) {
            workflow().undo();
            root().vector_controller().selection().clear();
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã hoàn tác" : "Undo complete";
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else {
            workflow().redo();
            root().vector_controller().selection().clear();
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

    // 3a. Document Tab Bar clicks (y: 38 to 66)
    if (y >= 38 && y <= 66) {
        int cur_x = 10;
        int tab_w = 210;
        for (size_t i = 0; i < tabs_.size(); ++i) {
            int tab_left = cur_x;
            int tab_right = cur_x + tab_w;
            if (x >= tab_left && x <= tab_right) {
                if (x >= tab_right - 24 && x <= tab_right - 4) {
                    close_tab(i);
                    return;
                }
                switch_to_tab(i);
                return;
            }
            cur_x += tab_w + 4;
        }
        if (x >= cur_x && x <= cur_x + 28) {
            open_new_doc_modal();
            return;
        }
        return;
    }

    // 3b. Context Tool Ribbon clicks (y: 66 to 102)
    if (y >= 66 && y <= 102) {
        const auto& selection = root().vector_controller().selection().selected_ids();
        bool is_text_mode = (active_tool_ == 4 || is_inline_text_editing_ || is_text_object_selected());

        if (is_text_mode) {
            // TYPOGRAPHY RIBBON CLICKS (Corel / Word Reference)
            if (x >= 52 && x <= 192) { cycle_font_family(); return; }
            if (x >= 220 && x <= 240) { adjust_font_size(-2.0); return; }
            if (x >= 294 && x <= 314) { adjust_font_size(2.0); return; }
            if (x >= 322 && x <= 350) { toggle_font_bold(); return; }
            if (x >= 354 && x <= 382) { toggle_font_italic(); return; }
            if (x >= 386 && x <= 414) { toggle_font_underline(); return; }
            if (x >= 422 && x <= 462) { set_font_alignment(typography::TextAlignment::Left); return; }
            if (x >= 466 && x <= 506) { set_font_alignment(typography::TextAlignment::Center); return; }
            if (x >= 510 && x <= 550) { set_font_alignment(typography::TextAlignment::Right); return; }
            if (x >= 556 && x <= 616) { convert_selected_text_to_curves(); return; }
            if (x >= 622 && x <= 692) { insert_table(table_rows_, table_cols_); return; }
            if (!selection.empty() && x >= 698 && x <= 748) {
                workflow().delete_selected();
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            int color_start_x = (!selection.empty()) ? 754 : 698;
            if (x >= color_start_x && x <= color_start_x + 36) { apply_fill_to_selection("#00f2c3"); return; }
            if (x >= color_start_x + 40 && x <= color_start_x + 76) { apply_fill_to_selection("#0ea5e9"); return; }
            if (x >= color_start_x + 80 && x <= color_start_x + 116) { apply_fill_to_selection("#eab308"); return; }
            if (x >= color_start_x + 120 && x <= color_start_x + 156) { apply_fill_to_selection("#ef4444"); return; }
            return;
        } else if (selection.empty()) {
            // PAGE MODE (Direct Numeric Input + Compact Preset Combobox)
            const auto snapshot = root().document().current_snapshot();
            const double pw = snapshot->page().width_mm;
            const double ph = snapshot->page().height_mm;
            if (x >= 52 && x <= 165) {
                commit_numeric_field();
                toggle_page_preset_dropdown();
                return;
            }
            if (x >= 175 && x <= 275) { close_page_preset_dropdown(); start_numeric_input(NumericFieldId::PageWidth); return; } // Page W
            if (x >= 283 && x <= 383) { close_page_preset_dropdown(); start_numeric_input(NumericFieldId::PageHeight); return; } // Page H
            if (x >= 391 && x <= 455) { close_page_preset_dropdown(); commit_numeric_field(); if (pw > ph) toggle_page_orientation(); return; } // Dọc
            if (x >= 461 && x <= 525) { close_page_preset_dropdown(); commit_numeric_field(); if (ph > pw) toggle_page_orientation(); return; } // Ngang
            close_page_preset_dropdown();
            commit_numeric_field();
        } else {
            // OBJECT MODE (Direct Numeric Input)
            if (x >= 10 && x <= 95) { start_numeric_input(NumericFieldId::ObjectX); return; } // X box
            if (x >= 100 && x <= 185) { start_numeric_input(NumericFieldId::ObjectY); return; } // Y box
            if (x >= 190 && x <= 285) { start_numeric_input(NumericFieldId::ObjectWidth); return; } // W box
            if (x >= 290 && x <= 360) { commit_numeric_field(); toggle_aspect_ratio_lock(); InvalidateRect(hwnd_, nullptr, FALSE); return; } // Lock toggle
            if (x >= 365 && x <= 460) { start_numeric_input(NumericFieldId::ObjectHeight); return; } // H box
            if (x >= 468 && x <= 518) {
                commit_numeric_field();
                if (workflow().delete_selected()) {
                    status_msg_ = (language_ == UILanguage::Vietnamese) ?
                        "Đã xóa các đối tượng được chọn · Nhấn Ctrl+Z để hoàn tác" :
                        "Deleted selected object(s) · Press Ctrl+Z to undo";
                }
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            } // Delete
            if (x >= 524 && x <= 574) { commit_numeric_field(); workflow().duplicate_selected(15.0); InvalidateRect(hwnd_, nullptr, FALSE); return; } // Duplicate
            commit_numeric_field();

            // Group / Ungroup / Enter Group / Exit Group / Align Ribbon Buttons
            if (is_in_isolation_mode()) {
                if (x >= 580 && x <= 670) { exit_group_isolation(); return; }
            } else {
                if (selection.size() >= 2) {
                    if (x >= 580 && x <= 620) { align_selected_objects(geometry::AlignMode::Left); return; }
                    if (x >= 624 && x <= 664) { align_selected_objects(geometry::AlignMode::CenterHorizontal); return; }
                    if (x >= 668 && x <= 708) { align_selected_objects(geometry::AlignMode::Right); return; }
                    if (x >= 712 && x <= 752) { align_selected_objects(geometry::AlignMode::Top); return; }
                    if (x >= 756 && x <= 796) { align_selected_objects(geometry::AlignMode::CenterVertical); return; }
                    if (x >= 800 && x <= 840) { align_selected_objects(geometry::AlignMode::Bottom); return; }
                    if (x >= 844 && x <= 904) { align_selected_objects(geometry::AlignMode::CenterPage); return; }
                    if (x >= 908 && x <= 968) {
                        auto gid = workflow().group_selected();
                        if (gid != document::kInvalidObjectId) {
                            status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đã nhóm các đối tượng (Group)" : "Grouped selected objects";
                        }
                        InvalidateRect(hwnd_, nullptr, FALSE);
                        return;
                    }
                    if (x >= 972 && x <= 1026) { trigger_boolean_operation(geometry::BooleanOp::Union); return; }
                    if (x >= 1030 && x <= 1084) { trigger_boolean_operation(geometry::BooleanOp::Difference); return; }
                } else if (selection.size() == 1) {
                    if (x >= 580 && x <= 640) { align_selected_objects(geometry::AlignMode::CenterPage); return; }
                    if (workflow().is_group(selection.front())) {
                        if (x >= 646 && x <= 716) {
                            if (workflow().ungroup_selected()) {
                                status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đã rã nhóm (Ungroup)" : "Ungrouped";
                                InvalidateRect(hwnd_, nullptr, FALSE);
                            }
                            return;
                        }
                        if (x >= 722 && x <= 792) {
                            enter_group_isolation(selection.front());
                            return;
                        }
                    }
                }
            }

            int color_start_x = (selection.size() == 1 && workflow().is_group(selection.front())) ? 798
                              : (selection.size() == 1) ? 646
                              : (selection.size() >= 2) ? 1090
                              : is_in_isolation_mode() ? 676 : 580;
            if (x >= color_start_x && x <= color_start_x + 36) { apply_fill_to_selection("#00f2c3"); return; }
            if (x >= color_start_x + 40 && x <= color_start_x + 76) { apply_fill_to_selection("#0ea5e9"); return; }
            if (x >= color_start_x + 80 && x <= color_start_x + 116) { apply_fill_to_selection("#eab308"); return; }
        }
        return;
    }

    if (active_num_field_ != NumericFieldId::None) {
        commit_numeric_field();
    }

    // Space + left drag temporarily pans without changing the active tool.
    if (space_down_ && is_canvas_point(x, y)) {
        is_panning_ = true;
        last_pointer_x_ = x;
        last_pointer_y_ = y;
        SetCapture(hwnd_);
        return;
    }

    // 4. Tool Strip (x: 6 to 46, y >= 102) - 6 tools
    if (x >= 6 && x <= 46 && y >= 102) {
        int tool_idx = (y - 112) / 42;
        if (tool_idx >= 0 && tool_idx < 6) {
            set_active_tool(tool_idx);
            return;
        }
    }

    // 5. Right Dock Tabs & Shaping Docker (x: w - 280 to w)
    if (x >= w - 280 && x <= w) {
        if (y >= 102 && y <= 132) {
            int tab_w = 280 / 3;
            int tab_idx = (x - (w - 280)) / tab_w;
            if (tab_idx >= 0 && tab_idx < 3) {
                set_active_dock_tab(tab_idx);
                return;
            }
        }
        if (active_dock_tab_ == 0) {
            int insp_x = w - 280;
            int content_y = 38 + 28 + 36 + 36;
            // Checkboxes: Keep Source / Keep Target
            if (y >= content_y + 222 && y <= content_y + 244) {
                if (x >= insp_x + 14 && x <= insp_x + 135) {
                    toggle_keep_source();
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return;
                }
                if (x >= insp_x + 140 && x <= w - 14) {
                    toggle_keep_target();
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return;
                }
            }
            int btn_w = (280 - 38) / 3;
            int by1 = content_y + 248;
            int by2 = by1 + 30;
            if (y >= by1 && y <= by1 + 26) {
                if (x >= insp_x + 14 && x <= insp_x + 14 + btn_w) {
                    trigger_boolean_operation(geometry::BooleanOp::Union);
                    return;
                }
                if (x >= insp_x + 14 + btn_w + 5 && x <= insp_x + 14 + 2 * btn_w + 5) {
                    trigger_boolean_operation(geometry::BooleanOp::Difference);
                    return;
                }
                if (x >= insp_x + 14 + (btn_w + 5) * 2 && x <= insp_x + 14 + 3 * btn_w + 10) {
                    trigger_boolean_operation(geometry::BooleanOp::Intersection);
                    return;
                }
            }
            if (y >= by2 && y <= by2 + 26) {
                if (x >= insp_x + 14 && x <= insp_x + 14 + btn_w) {
                    trigger_boolean_operation(geometry::BooleanOp::Xor);
                    return;
                }
                if (x >= insp_x + 14 + btn_w + 5 && x <= insp_x + 14 + 2 * btn_w + 5) {
                    trigger_boolean_operation(geometry::BooleanOp::BackMinusFront);
                    return;
                }
                if (x >= insp_x + 14 + (btn_w + 5) * 2 && x <= insp_x + 14 + 3 * btn_w + 10) {
                    trigger_boolean_operation(geometry::BooleanOp::Boundary);
                    return;
                }
            }
            int by3 = by2 + 32;
            if (y >= by3 && y <= by3 + 28 && x >= insp_x + 14 && x <= w - 14) {
                trigger_auto_weld();
                return;
            }

            // Align & Distribute Docker clicks
            int by_align_hdr = by3 + 36;
            int by_a1 = by_align_hdr + 24;
            int by_a2 = by_a1 + 30;
            int by_a3 = by_a2 + 30;
            int by_a4 = by_a3 + 32;
            int by_a5 = by_a4 + 30;

            // Row 1: Left, Center X, Right
            if (y >= by_a1 && y <= by_a1 + 26) {
                if (x >= insp_x + 14 && x <= insp_x + 14 + btn_w) {
                    align_selected_objects(geometry::AlignMode::Left);
                    return;
                }
                if (x >= insp_x + 14 + btn_w + 5 && x <= insp_x + 14 + 2 * btn_w + 5) {
                    align_selected_objects(geometry::AlignMode::CenterHorizontal);
                    return;
                }
                if (x >= insp_x + 14 + 2 * (btn_w + 5) && x <= insp_x + 14 + 3 * btn_w + 10) {
                    align_selected_objects(geometry::AlignMode::Right);
                    return;
                }
            }

            // Row 2: Top, Middle Y, Bottom
            if (y >= by_a2 && y <= by_a2 + 26) {
                if (x >= insp_x + 14 && x <= insp_x + 14 + btn_w) {
                    align_selected_objects(geometry::AlignMode::Top);
                    return;
                }
                if (x >= insp_x + 14 + btn_w + 5 && x <= insp_x + 14 + 2 * btn_w + 5) {
                    align_selected_objects(geometry::AlignMode::CenterVertical);
                    return;
                }
                if (x >= insp_x + 14 + 2 * (btn_w + 5) && x <= insp_x + 14 + 3 * btn_w + 10) {
                    align_selected_objects(geometry::AlignMode::Bottom);
                    return;
                }
            }

            // Row 3: Center to Page (P)
            if (y >= by_a3 && y <= by_a3 + 26 && x >= insp_x + 14 && x <= w - 14) {
                align_selected_objects(geometry::AlignMode::CenterPage);
                return;
            }

            // Row 4: Align Reference Selector
            int ref_w = (280 - 85) / 3;
            if (y >= by_a4 - 2 && y <= by_a4 + 22) {
                if (x >= insp_x + 72 && x <= insp_x + 72 + ref_w) {
                    set_align_reference(geometry::AlignReference::Selection);
                    status_msg_ = (language_ == UILanguage::Vietnamese)
                        ? "Quy chiếu căn chỉnh: Vùng chọn chung (Selection Bounds)"
                        : "Align reference: Selection Bounds";
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return;
                }
                if (x >= insp_x + 72 + ref_w + 3 && x <= insp_x + 72 + 2 * ref_w + 3) {
                    set_align_reference(geometry::AlignReference::KeyObject);
                    status_msg_ = (language_ == UILanguage::Vietnamese)
                        ? "Quy chiếu căn chỉnh: Đối tượng neo (Key Object)"
                        : "Align reference: Key Object";
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return;
                }
                if (x >= insp_x + 72 + 2 * (ref_w + 3) && x <= insp_x + 72 + 3 * ref_w + 6) {
                    set_align_reference(geometry::AlignReference::Page);
                    status_msg_ = (language_ == UILanguage::Vietnamese)
                        ? "Quy chiếu căn chỉnh: Khổ giấy in (Page Bounds)"
                        : "Align reference: Page Bounds";
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return;
                }
            }

            // Row 5: Distribute Spacing with Specified Gap mm
            int gap_half = (280 - 90) / 2;
            if (y >= by_a5 - 2 && y <= by_a5 + 24) {
                if (x >= insp_x + 72 && x <= insp_x + 72 + gap_half) {
                    apply_specified_spacing(true); // Horizontal
                    return;
                }
                if (x >= insp_x + 72 + gap_half + 4 && x <= insp_x + 72 + 2 * gap_half + 4) {
                    apply_specified_spacing(false); // Vertical
                    return;
                }
            }
        }
    }

    // 6. Page and zoom controls in the status bar.
    if (y >= height_ - 26) {
        if (x >= w - 210 && x <= w - 145) { fit_page_to_canvas(); return; }
        if (x >= w - 120 && x <= w - 95) { zoom_out(); return; }
        if (x >= w - 90 && x <= w - 50) {
            auto& camera = root().renderer().camera();
            const auto area = canvas_rect(width_, height_);
            camera.set_viewport_size(area.right - area.left, area.bottom - area.top);
            camera.set_zoom(1.0);
            zoom_level_ = 100;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (x >= w - 45 && x <= w - 20) { zoom_in(); return; }
    }

    const auto area = canvas_rect(width_, height_);
    if (is_in_isolation_mode() && y >= area.top && y <= area.top + 34 && x >= area.left && x <= area.right) {
        if (x >= area.right - 150) {
            exit_group_isolation();
            return;
        }
        if (x >= area.left + 8 && x <= area.left + 140) {
            exit_all_isolation();
            return;
        }
        return;
    }

    // Ruler guideline dragging: Top ruler creates horizontal guideline, Left ruler creates vertical guideline
    if (x >= area.left && x <= area.right && y >= 102 && y <= area.top) {
        is_dragging_guideline_ = true;
        drag_guideline_orientation_ = GuidelineOrientation::Horizontal;
        show_guidelines_ = true;
        const auto world_pt = canvas_to_world(x, y);
        drag_guideline_pos_mm_ = world_pt.y;
        SetCapture(hwnd_);
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đang kéo đường gióng ngang từ thước trên..."
            : "Dragging horizontal guideline from top ruler...";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (x >= 52 && x <= area.left && y >= area.top && y <= area.bottom) {
        is_dragging_guideline_ = true;
        drag_guideline_orientation_ = GuidelineOrientation::Vertical;
        show_guidelines_ = true;
        const auto world_pt = canvas_to_world(x, y);
        drag_guideline_pos_mm_ = world_pt.x;
        SetCapture(hwnd_);
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đang kéo đường gióng dọc từ thước trái..."
            : "Dragging vertical guideline from left ruler...";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    handle_canvas_mouse_down(x, y);
}

void NativeWindow::handle_mouse_move(int x, int y) {
    bool prev_visible = tooltip_.visible;
    std::wstring prev_text = tooltip_.text;

    update_hover_tooltip(x, y);

    if (prev_visible != tooltip_.visible || prev_text != tooltip_.text) {
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    }

    if (is_dragging_guideline_) {
        const auto world_pt = canvas_to_world(x, y);
        if (drag_guideline_orientation_ == GuidelineOrientation::Horizontal) {
            drag_guideline_pos_mm_ = world_pt.y;
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1) << drag_guideline_pos_mm_;
            status_msg_ = (language_ == UILanguage::Vietnamese)
                ? "Đường gióng ngang Y: " + ss.str() + " mm"
                : "Horizontal guideline Y: " + ss.str() + " mm";
        } else {
            drag_guideline_pos_mm_ = world_pt.x;
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1) << drag_guideline_pos_mm_;
            status_msg_ = (language_ == UILanguage::Vietnamese)
                ? "Đường gióng dọc X: " + ss.str() + " mm"
                : "Vertical guideline X: " + ss.str() + " mm";
        }
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    handle_canvas_mouse_move(x, y);
}

void NativeWindow::handle_mouse_up(int x, int y) {
    if (is_dragging_guideline_) {
        is_dragging_guideline_ = false;
        if (GetCapture() == hwnd_)
            ReleaseCapture();

        const RECT area = canvas_rect(width_, height_);
        const auto world_pt = canvas_to_world(x, y);
        if (drag_guideline_orientation_ == GuidelineOrientation::Horizontal) {
            if (y >= area.top && y <= area.bottom) {
                add_guideline(GuidelineOrientation::Horizontal, world_pt.y);
            } else {
                status_msg_ = (language_ == UILanguage::Vietnamese)
                    ? "Đã hủy kéo đường gióng"
                    : "Cancelled guideline";
            }
        } else {
            if (x >= area.left && x <= area.right) {
                add_guideline(GuidelineOrientation::Vertical, world_pt.x);
            } else {
                status_msg_ = (language_ == UILanguage::Vietnamese)
                    ? "Đã hủy kéo đường gióng"
                    : "Cancelled guideline";
            }
        }
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (is_panning_) {
        is_panning_ = false;
        if (GetCapture() == hwnd_)
            ReleaseCapture();
        return;
    }
    handle_canvas_mouse_up(x, y);
}

bool NativeWindow::handle_key_down(WPARAM key) {
    if (active_num_field_ != NumericFieldId::None) {
        if (key == VK_ESCAPE) {
            cancel_numeric_field();
            return true;
        }
        if (key == VK_RETURN) {
            commit_numeric_field();
            return true;
        }
        if (key == VK_TAB) {
            handle_numeric_char_input(9);
            return true;
        }
        if (key == VK_BACK) {
            return false;
        }
        return true;
    }

    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    if (control) {
        if (key == VK_OEM_1 || key == ';') {
            if (alt) {
                clear_guidelines();
            } else {
                toggle_guidelines();
            }
            return true;
        }
        if (key == 'N') { open_new_doc_modal(); return true; }
        if (key == 'W') { close_tab(active_tab_index_); return true; }
        if (key == VK_TAB) {
            if (tabs_.size() > 1) switch_to_tab((active_tab_index_ + 1) % tabs_.size());
            return true;
        }
        if (key == 'D') { workflow().duplicate_selected(15.0); InvalidateRect(hwnd_, nullptr, FALSE); return true; }
        if (key == 'O') { open_document_dialog(); return true; }
        if (key == 'S') { save_document_dialog(shift); return true; }
        if (key == 'E') { export_svg_dialog(); return true; }
        if (key == 'J') { open_settings_modal(); return true; }
        if (key == 'A' && shift) { open_inspector_properties_panel(); return true; }
        if (key == 'Z' || (key == 'Y' && shift)) {
            workflow().undo();
            root().vector_controller().selection().clear();
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã hoàn tác" : "Undo complete";
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == 'Y') {
            workflow().redo();
            root().vector_controller().selection().clear();
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã làm lại" : "Redo complete";
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == 'G') {
            if (shift) {
                if (workflow().ungroup_selected()) {
                    status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đã rã nhóm (Ungroup)" : "Ungrouped";
                }
            } else {
                auto gid = workflow().group_selected();
                if (gid != document::kInvalidObjectId) {
                    status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đã nhóm các đối tượng (Group)" : "Grouped selected objects";
                }
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == 'Q') {
            convert_selected_text_to_curves();
            return true;
        }
        if (key == 'B') {
            toggle_font_bold();
            return true;
        }
        if (key == 'I') {
            toggle_font_italic();
            return true;
        }
        if (key == 'U') {
            if (workflow().ungroup_selected()) {
                status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đã rã nhóm (Ungroup)" : "Ungrouped";
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return true;
        }
        if (shift) {
            if (key == 'U' || key == 'W') {
                trigger_boolean_operation(geometry::BooleanOp::Union);
                return true;
            }
            if (key == 'T') {
                trigger_boolean_operation(geometry::BooleanOp::Difference);
                return true;
            }
            if (key == 'I') {
                trigger_boolean_operation(geometry::BooleanOp::Intersection);
                return true;
            }
            if (key == 'X') {
                trigger_boolean_operation(geometry::BooleanOp::Xor);
                return true;
            }
            if (key == 'B') {
                trigger_boolean_operation(geometry::BooleanOp::Boundary);
                return true;
            }
        }
    }
    if (key == VK_RETURN) {
        if (is_inline_text_editing_) {
            commit_inline_text_editing();
            return true;
        }
        const auto& sel = root().vector_controller().selection().selected_ids();
        if (sel.size() == 1 && workflow().is_group(sel.front())) {
            enter_group_isolation(sel.front());
            return true;
        }
    }
    if (key == VK_ESCAPE) {
        if (is_dragging_guideline_) {
            is_dragging_guideline_ = false;
            if (GetCapture() == hwnd_) ReleaseCapture();
            status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đã hủy đường gióng" : "Cancelled guideline";
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (is_inline_text_editing_) {
            cancel_inline_text_editing();
            return true;
        }
        if (is_in_isolation_mode()) {
            exit_group_isolation();
            return true;
        }
        if (show_new_doc_modal_) close_new_doc_modal();
        else if (show_settings_modal_) close_settings_modal();
        else if (show_file_menu_) close_file_menu();
        else if (show_page_preset_dropdown_) close_page_preset_dropdown();
        else if (is_panning_) {
            is_panning_ = false;
            if (GetCapture() == hwnd_) ReleaseCapture();
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (pointer_active_) {
            root().vector_controller().on_escape();
            pointer_active_ = false;
            is_dragging_ = false;
            is_creating_shape_ = false;
            is_knife_cutting_ = false;
            drag_preview_bounds_.reset();
            if (GetCapture() == hwnd_) ReleaseCapture();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return true;
    }
    if (key == VK_DELETE || key == VK_BACK) {
        if (is_inline_text_editing_) {
            return false; // let WM_CHAR handle backspace
        }
        if (workflow().delete_selected()) {
            status_msg_ = (language_ == UILanguage::Vietnamese) ?
                "Đã xóa các đối tượng được chọn · Nhấn Ctrl+Z để hoàn tác" :
                "Deleted selected object(s) · Press Ctrl+Z to undo";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    const auto sel_count = root().vector_controller().selection().count();
    if (!is_inline_text_editing_ && sel_count >= 1) {
        if (key == 'P') {
            if (shift) align_selected_objects(geometry::AlignMode::CenterPageHorizontal);
            else if (alt) align_selected_objects(geometry::AlignMode::CenterPageVertical);
            else align_selected_objects(geometry::AlignMode::CenterPage);
            return true;
        }
        if (sel_count >= 2 || align_reference_ == geometry::AlignReference::Page) {
            if (key == 'L') { align_selected_objects(geometry::AlignMode::Left); return true; }
            if (key == 'R') { align_selected_objects(geometry::AlignMode::Right); return true; }
            if (key == 'T') { align_selected_objects(geometry::AlignMode::Top); return true; }
            if (key == 'B') { align_selected_objects(geometry::AlignMode::Bottom); return true; }
            if (key == 'C') { align_selected_objects(geometry::AlignMode::CenterHorizontal); return true; }
            if (key == 'E') { align_selected_objects(geometry::AlignMode::CenterVertical); return true; }
        }
    }

    if (key == 'F') { fit_page_to_canvas(); return true; }
    if (key == 'H') { set_active_tool(3); return true; }
    if (key == '1') {
        auto& camera = root().renderer().camera();
        camera.set_viewport_size(std::max(1, width_ - 352), std::max(1, height_ - 148));
        camera.set_zoom(1.0);
        zoom_level_ = 100;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (key == 'V') { set_active_tool(0); return true; }
    if (key == 'R') { set_active_tool(1); return true; }
    if (key == 'E') { set_active_tool(2); return true; }
    if (key == 'K' || key == 'C') { set_active_tool(5); return true; }
    if (key == 'A' || key == 'T') { set_active_tool(4); return true; }
    return false;
}

LRESULT CALLBACK NativeWindow::window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<NativeWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (uMsg) {
    case WM_CHAR:
        if (self && self->active_numeric_field() != NumericFieldId::None) {
            self->handle_numeric_char_input(static_cast<wchar_t>(wParam));
            return 0;
        }
        if (self && self->is_inline_text_editing_) {
            if (wParam == VK_BACK) {
                if (!self->inline_text_buffer_.empty()) {
                    while (!self->inline_text_buffer_.empty()) {
                        char c = self->inline_text_buffer_.back();
                        self->inline_text_buffer_.pop_back();
                        if ((c & 0xC0) != 0x80) break;
                    }
                }
            } else if (wParam == VK_RETURN) {
                if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
                    self->inline_text_buffer_ += '\n';
                } else {
                    self->commit_inline_text_editing();
                }
            } else if (wParam == VK_ESCAPE) {
                self->cancel_inline_text_editing();
            } else if (wParam >= 32) {
                wchar_t wc = static_cast<wchar_t>(wParam);
                char utf8[8] = {0};
                WideCharToMultiByte(CP_UTF8, 0, &wc, 1, utf8, sizeof(utf8), nullptr, nullptr);
                self->inline_text_buffer_ += utf8;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
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
            self->commit_numeric_field();
            self->space_down_ = false;
            if (self->is_panning_) {
                self->is_panning_ = false;
                if (GetCapture() == hwnd) ReleaseCapture();
            }
        }
        break;
    case WM_LBUTTONDOWN:
        if (self) {
            SetFocus(hwnd);
            self->handle_mouse_down(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        }
        return 0;
    case WM_LBUTTONDBLCLK:
        if (self) {
            SetFocus(hwnd);
            self->handle_double_click(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        }
        return 0;
    case WM_MOUSEMOVE:
        if (self) self->handle_mouse_move(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
        if (self) self->handle_mouse_up(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MBUTTONDOWN:
        if (self && self->is_canvas_point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam))) {
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
                auto& camera = self->root().renderer().camera();
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
            const WORD cursor_id = self->is_panning_ ? 32646
                : self->active_tool_ == 3 ? 32649 : 32512;
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
            self->root().renderer().camera().set_viewport_size(
                area.right - area.left, area.bottom - area.top);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (self) {
            self->is_panning_ = false;
            self->is_dragging_guideline_ = false;
            if (self->pointer_active_) {
                self->root().vector_controller().on_escape();
                self->pointer_active_ = false;
                self->is_dragging_ = false;
                self->is_creating_shape_ = false;
                self->drag_preview_bounds_.reset();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
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
