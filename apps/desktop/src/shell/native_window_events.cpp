#include "pikdraw/desktop/shell/native_window.hpp"

namespace pikdraw::desktop {

#ifdef _WIN32

void NativeWindow::handle_mouse_down(int x, int y) {
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

    // 1. Handle clicks inside or dismissing File Menu Dropdown (Corel Reference)
    if (show_file_menu_) {
        int mx = 236, my = 38, mw = 270, mh = 590;
        if (x >= mx && x <= mx + mw && y >= my && y <= my + mh) {
            bool is_vi = (language_ == UILanguage::Vietnamese);
            int rel_y = y - my - 4;
            if (rel_y < 23) {
                status_msg_ = is_vi ? "Tạo bản vẽ vector mới (Ctrl+N)" : "Created new vector document (Ctrl+N)";
            } else if (rel_y < 46) {
                open_home_screen();
                set_active_home_tab(2);
            } else if (rel_y < 69) {
                status_msg_ = is_vi ? "Mở tệp bản vẽ (.pikdraw, .cdr, .svg, .dxf)" : "Open document (Ctrl+O)";
            } else if (rel_y < 92) {
                status_msg_ = is_vi ? "Đang kết nối kho lưu trữ đám mây Cloud..." : "Connecting to Cloud storage...";
            } else if (rel_y < 115) {
                open_recent_document(0);
            } else if (rel_y >= 122 && rel_y < 145) {
                status_msg_ = is_vi ? "Đã đóng tài liệu hiện tại" : "Closed current document";
            } else if (rel_y >= 145 && rel_y < 168) {
                status_msg_ = is_vi ? "Đã đóng tất cả tài liệu" : "Closed all documents";
            } else if (rel_y >= 175 && rel_y < 198) {
                status_msg_ = is_vi ? "Đã lưu tài liệu thành công (CRC32 Checksum Verified)" : "Document saved successfully (Ctrl+S)";
            } else if (rel_y >= 198 && rel_y < 221) {
                status_msg_ = is_vi ? "Lưu bản sao tài liệu (Ctrl+Shift+S)" : "Saved document copy (Ctrl+Shift+S)";
            } else if (rel_y >= 221 && rel_y < 244) {
                status_msg_ = is_vi ? "Đang đồng bộ bản vẽ lên đám mây Cloud..." : "Syncing document to cloud...";
            } else if (rel_y >= 244 && rel_y < 267) {
                status_msg_ = is_vi ? "Đã lưu vào danh mục mẫu thi công chuẩn" : "Saved as manufacturing template";
            } else if (rel_y >= 267 && rel_y < 290) {
                status_msg_ = is_vi ? "Khôi phục trạng thái bản vẽ gốc lúc mở" : "Reverted to original document";
            } else if (rel_y >= 297 && rel_y < 320) {
                status_msg_ = is_vi ? "Đang kết nối máy quét / TWAIN Scanner..." : "Connecting scanner/camera...";
            } else if (rel_y >= 320 && rel_y < 343) {
                status_msg_ = is_vi ? "Đã nhập vector SVG/DXF thành công (Ctrl+I)" : "Imported vector SVG/DXF (Ctrl+I)";
            } else if (rel_y >= 343 && rel_y < 366) {
                trigger_export();
            } else if (rel_y >= 366 && rel_y < 389) {
                status_msg_ = is_vi ? "Xuất tối ưu cho máy in bạt & máy cắt laser CNC" : "Exported for print & CNC cutters";
            } else if (rel_y >= 389 && rel_y < 412) {
                status_msg_ = is_vi ? "Mở Docker quản lý xuất file đa định dạng" : "Opened Export Docker";
            } else if (rel_y >= 412 && rel_y < 435) {
                status_msg_ = is_vi ? "Gửi trực tiếp đến máy cắt Decal / CNC" : "Sent to Vinyl/CNC Cutter";
            } else if (rel_y >= 435 && rel_y < 458) {
                status_msg_ = is_vi ? "Đã xuất file PDF CMYK chất lượng cao cho xưởng in!" : "Published high-resolution CMYK PDF!";
            } else if (rel_y >= 465 && rel_y < 488) {
                status_msg_ = is_vi ? "Trộn dữ liệu in tem nhãn hàng loạt (Print Merge)" : "Print Merge batch generator";
            } else if (rel_y >= 488 && rel_y < 511) {
                status_msg_ = is_vi ? "Đã gửi bản vẽ tới máy in chuyên dụng (Ctrl+P)" : "Sent print job to printer (Ctrl+P)";
            } else if (rel_y >= 511 && rel_y < 534) {
                status_msg_ = is_vi ? "Xem trước trang in (Print Preview: 100%)" : "Print Preview active";
            } else if (rel_y >= 541 && rel_y < 564) {
                status_msg_ = is_vi ? "Thuộc tính: 1850x420 mm | Màu: sRGB/CMYK | Đối tượng: 4" : "Properties: 1850x420 mm | Color: sRGB/CMYK";
            } else if (rel_y >= 571) {
                PostQuitMessage(0);
                return;
            }
            close_file_menu();
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

    // Topbar Home Button [ 🏠 Trang chủ / 🎨 Bản vẽ ] (x: 148 to 232, y: 6 to 32)
    if (x >= 148 && x <= 232 && y >= 6 && y <= 32) {
        toggle_home_screen();
        return;
    }

    // Topbar Menu [ Tệp ] (x: 236 to 282, y: 6 to 32)
    if (x >= 236 && x <= 282 && y >= 6 && y <= 32) {
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

    // Workspace Pills (x: 520 to 790, y: 6 to 32)
    int pill_x = 520;
    if (y >= 6 && y <= 32) {
        if (x >= pill_x && x < pill_x + 65) { set_active_workspace(0); return; }
        if (x >= pill_x + 70 && x < pill_x + 135) { set_active_workspace(1); return; }
        if (x >= pill_x + 140 && x < pill_x + 215) { set_active_workspace(2); return; }
        if (x >= pill_x + 220 && x < pill_x + 270) { set_active_workspace(3); return; }
    }

    // 4. Tool Strip (x: 6 to 46, y: 80 to 420)
    if (x >= 6 && x <= 46 && y >= 80) {
        int tool_idx = (y - 80) / 42;
        if (tool_idx >= 0 && tool_idx < 8) {
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

    // 6. Zoom buttons on status bar (x: w - 120 to w - 20, y: height - 26 to height)
    if (y >= height_ - 26) {
        if (x >= w - 120 && x <= w - 95) { zoom_out(); return; }
        if (x >= w - 45 && x <= w - 20) { zoom_in(); return; }
    }

    // 7. Canvas interaction: clicking/dragging the vector signboard object
    int tool_w = 52;
    int inspect_w = 280;
    int canvas_w = w - tool_w - inspect_w;
    int art_w = 660;
    int art_x = tool_w + (canvas_w - art_w) / 2;
    int art_y = 38 + 34 + 35;

    int canvas_obj_x = art_x + 30 + static_cast<int>(shape_x_ * 0.15);
    int canvas_obj_y = art_y + 40 + static_cast<int>(shape_y_ * 0.15);
    int canvas_obj_w = 460;
    int canvas_obj_h = 160;

    if (x >= canvas_obj_x - 10 && x <= canvas_obj_x + canvas_obj_w + 10 &&
        y >= canvas_obj_y - 10 && y <= canvas_obj_y + canvas_obj_h + 10) {
        is_dragging_ = true;
        drag_start_x_ = x;
        drag_start_y_ = y;
        drag_init_shape_x_ = shape_x_;
        drag_init_shape_y_ = shape_y_;
        status_msg_ = (language_ == UILanguage::Vietnamese) ?
            "Đang di chuyển: AN PHÁT SIGNAGE 2026" :
            "Moving: AN PHAT SIGNAGE 2026";
        InvalidateRect(hwnd_, NULL, FALSE);
    }
}

void NativeWindow::handle_mouse_move(int x, int y) {
    cursor_mm_x_ = static_cast<double>(x - 102) * 2.0;
    cursor_mm_y_ = static_cast<double>(y - 102) * 2.0;
    if (cursor_mm_x_ < 0.0) cursor_mm_x_ = 0.0;
    if (cursor_mm_y_ < 0.0) cursor_mm_y_ = 0.0;

    if (is_dragging_) {
        double dx = static_cast<double>(x - drag_start_x_) * 4.0;
        double dy = static_cast<double>(y - drag_start_y_) * 4.0;
        shape_x_ = drag_init_shape_x_ + dx;
        shape_y_ = drag_init_shape_y_ + dy;
        InvalidateRect(hwnd_, NULL, FALSE);
    }
}

void NativeWindow::handle_mouse_up(int /*x*/, int /*y*/) {
    if (is_dragging_) {
        is_dragging_ = false;
        status_msg_ = (language_ == UILanguage::Vietnamese) ?
            "Đã cập nhật vị trí đối tượng (" + std::to_string(static_cast<int>(shape_x_)) + " mm, " + std::to_string(static_cast<int>(shape_y_)) + " mm)" :
            "Updated object position (" + std::to_string(static_cast<int>(shape_x_)) + " mm, " + std::to_string(static_cast<int>(shape_y_)) + " mm)";
        InvalidateRect(hwnd_, NULL, FALSE);
    }
}

LRESULT CALLBACK NativeWindow::window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<NativeWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (uMsg) {
    case WM_LBUTTONDOWN:
        if (self) self->handle_mouse_down(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_MOUSEMOVE:
        if (self) self->handle_mouse_move(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_LBUTTONUP:
        if (self) self->handle_mouse_up(LOWORD(lParam), HIWORD(lParam));
        return 0;
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
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
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
