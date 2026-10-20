#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_paint_icons.hpp"
#include "native_window_toolbox.hpp"
#include "native_window_gradient_properties_paint.hpp"
#include "native_window_text_properties.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "../tools/gradient_tool_controller.hpp"
#include "../tools/dimension_tool_controller.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <sstream>
#include <cmath>

namespace pikdraw::desktop {

#ifdef _WIN32

void NativeWindow::paint_left_toolbox(HDC mem_dc, int w, int h) {
    (void)w;
    ensure_ui_fonts();
    HFONT font_be_vietnam = font_be_vietnam_;
    HFONT font_be_vietnam_bold = font_be_vietnam_bold_;
    HFONT font_secondary = font_secondary_;
    HFONT font_mono = font_mono_;
    (void)font_be_vietnam; (void)font_be_vietnam_bold; (void)font_secondary; (void)font_mono;
    bool is_dark = (theme_ == ThemeMode::CyberDark);
    bool is_vi = (language_ == UILanguage::Vietnamese);
    (void)is_vi;
    COLORREF col_header = is_dark ? RGB(12, 19, 29) : RGB(255, 255, 255);
    COLORREF col_border = is_dark ? RGB(29, 38, 54) : RGB(203, 213, 225);
    COLORREF col_teal = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    COLORREF col_text_white = is_dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    COLORREF col_text_muted = is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    COLORREF col_active_tab = is_dark ? RGB(22, 37, 54) : RGB(236, 242, 248);
    int topbar_h = 38, tabbar_h = 28, ribbon_h = 36, status_h = 26, tool_w = 52;
    int content_top = topbar_h + tabbar_h + ribbon_h;

        // 5. Left Tool Strip: direct tools plus Quick Shapes, Path Drawing and Appearance groups.
        HBRUSH br_tools = CreateSolidBrush(col_header);
        RECT rc_tools = { 0, content_top, tool_w, h - status_h };
        FillRect(mem_dc, &rc_tools, br_tools);
        DeleteObject(br_tools);

        const auto slots = visible_toolbox_slots();
        const int num_slots = static_cast<int>(slots.size());

        for (int row = 0; row < num_slots; ++row) {
            const auto slot = slots[row];
            const auto info = get_toolbox_slot_info(slot);
            const int group = info.group_id;
            const int ty = content_top + 10 + row * 42;
            RECT rc_tb = { 6, ty, tool_w - 6, ty + 36 };

            const bool active = (slot == ToolboxSlotId::Pick) ? (active_tool_ == 0)
                              : (slot == ToolboxSlotId::Node) ? (active_tool_ == 11 || (node_tool_ && node_tool_->active()))
                              : (slot == ToolboxSlotId::QuickShapes) ? (active_tool_ == 1 || active_tool_ == 2 || active_tool_ == 9)
                              : (slot == ToolboxSlotId::PathDrawing) ? ((active_tool_ >= 6 && active_tool_ <= 8))
                              : (slot == ToolboxSlotId::Pan) ? (active_tool_ == 3)
                              : (slot == ToolboxSlotId::Text) ? (active_tool_ == 4)
                              : (slot == ToolboxSlotId::Knife) ? (active_tool_ == 5)
                              : (slot == ToolboxSlotId::Blur) ? workflow().selected_effect_index(document::GaussianBlurEffect{1.0, 1.0}).has_value()
                              : (slot == ToolboxSlotId::DropShadow) ? workflow().selected_effect_index(document::DropShadowEffect{}).has_value()
                              : (slot == ToolboxSlotId::Contour) ? (active_tool_ == 12 || (contour_tool_ && contour_tool_->active()))
                              : (slot == ToolboxSlotId::Gradient) ? (active_tool_ == 13 || (gradient_tool_ && gradient_tool_->active()))
                              : (slot == ToolboxSlotId::Dimension) ? (active_tool_ == 10 || (dimension_tool_ && dimension_tool_->active()))
                              : false;

            const int btn_cx = (rc_tb.left + rc_tb.right) / 2;
            const int btn_cy = (rc_tb.top + rc_tb.bottom) / 2;
            const int icon = (slot == ToolboxSlotId::Node) ? 15
                           : (slot == ToolboxSlotId::QuickShapes) ? quick_shapes_last_tool_
                           : (slot == ToolboxSlotId::PathDrawing) ? path_drawing_last_tool_
                           : info.icon_id;

            if (active) {
                HBRUSH br_t_act = CreateSolidBrush(col_active_tab);
                HPEN pen_t_act = CreatePen(PS_SOLID, 1, col_teal);
                const HGDIOBJ old_pen = SelectObject(mem_dc, pen_t_act);
                const HGDIOBJ old_brush = SelectObject(mem_dc, br_t_act);
                RoundRect(mem_dc, rc_tb.left, rc_tb.top, rc_tb.right, rc_tb.bottom, 6, 6);
                SelectObject(mem_dc, old_brush);
                SelectObject(mem_dc, old_pen);
                DeleteObject(pen_t_act);
                DeleteObject(br_t_act);
                draw_tool_icon(mem_dc, icon, btn_cx, btn_cy, col_teal);
            } else {
                HBRUSH br_t_inact = CreateSolidBrush(col_header);
                HPEN pen_t_inact = CreatePen(PS_SOLID, 1, col_border);
                const HGDIOBJ old_pen = SelectObject(mem_dc, pen_t_inact);
                const HGDIOBJ old_brush = SelectObject(mem_dc, br_t_inact);
                RoundRect(mem_dc, rc_tb.left, rc_tb.top, rc_tb.right, rc_tb.bottom, 6, 6);
                SelectObject(mem_dc, old_brush);
                SelectObject(mem_dc, old_pen);
                DeleteObject(pen_t_inact);
                DeleteObject(br_t_inact);
                draw_tool_icon(mem_dc, icon, btn_cx, btn_cy, col_text_muted);
            }

            if (group != 0) {
                draw_toolbox_flyout_marker(mem_dc, rc_tb.right - 5, rc_tb.bottom - 5,
                                           active ? col_teal : RGB(100, 116, 139));
            }

            // Quick Remove badge in edit mode (except Pick)
            if (toolbox_edit_mode_ && slot != ToolboxSlotId::Pick) {
                const int badge_x = rc_tb.right - 6;
                const int badge_y = rc_tb.top + 6;
                HBRUSH br_red = CreateSolidBrush(RGB(239, 68, 68));
                HPEN pen_red = CreatePen(PS_SOLID, 1, RGB(220, 38, 38));
                const HGDIOBJ o_pen = SelectObject(mem_dc, pen_red);
                const HGDIOBJ o_brush = SelectObject(mem_dc, br_red);
                Ellipse(mem_dc, badge_x - 5, badge_y - 5, badge_x + 5, badge_y + 5);
                SelectObject(mem_dc, o_brush);
                SelectObject(mem_dc, o_pen);
                DeleteObject(pen_red);
                DeleteObject(br_red);

                HPEN pen_white = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
                const HGDIOBJ o_white = SelectObject(mem_dc, pen_white);
                MoveToEx(mem_dc, badge_x - 3, badge_y, nullptr);
                LineTo(mem_dc, badge_x + 4, badge_y);
                SelectObject(mem_dc, o_white);
                DeleteObject(pen_white);
            }

            if (toolbox_keyboard_focus_ && group == toolbox_focus_group_) {
                RECT focus = rc_tb;
                InflateRect(&focus, -3, -3);
                DrawFocusRect(mem_dc, &focus);
            }
        }

        // Draw Quick Customize [+] button at bottom of toolbox
        {
            const int plus_y = content_top + 10 + num_slots * 42;
            RECT rc_plus = { 10, plus_y + 3, tool_w - 10, plus_y + 33 };
            const bool plus_open = show_toolbox_customize_flyout_;
            HBRUSH br_plus = CreateSolidBrush(plus_open ? col_active_tab : col_header);
            HPEN pen_plus = CreatePen(PS_SOLID, 1, plus_open ? col_teal : col_border);
            const HGDIOBJ o_pen = SelectObject(mem_dc, pen_plus);
            const HGDIOBJ o_brush = SelectObject(mem_dc, br_plus);
            RoundRect(mem_dc, rc_plus.left, rc_plus.top, rc_plus.right, rc_plus.bottom, 6, 6);
            SelectObject(mem_dc, o_brush);
            SelectObject(mem_dc, o_pen);
            DeleteObject(pen_plus);
            DeleteObject(br_plus);

            // Draw '+' glyph
            const int pcx = (rc_plus.left + rc_plus.right) / 2;
            const int pcy = (rc_plus.top + rc_plus.bottom) / 2;
            HPEN pen_glyph = CreatePen(PS_SOLID, 1, plus_open ? col_teal : col_text_muted);
            const HGDIOBJ o_glyph = SelectObject(mem_dc, pen_glyph);
            MoveToEx(mem_dc, pcx - 5, pcy, nullptr);
            LineTo(mem_dc, pcx + 6, pcy);
            MoveToEx(mem_dc, pcx, pcy - 5, nullptr);
            LineTo(mem_dc, pcx, pcy + 6);
            SelectObject(mem_dc, o_glyph);
            DeleteObject(pen_glyph);
        }

        if (toolbox_open_group_ >= 1 && toolbox_open_group_ <= 3) {
            const int group_row = toolbox_open_group_ == 1 ? get_visible_row_for_slot(ToolboxSlotId::QuickShapes)
                                : toolbox_open_group_ == 2 ? get_visible_row_for_slot(ToolboxSlotId::PathDrawing) : 6;
            const int flyout_top = content_top + 10 + (group_row >= 0 ? group_row : 1) * 42;
            const int flyout_left = 54;
            const int flyout_width = toolbox_open_group_ == 3 ? 208 : 188;
            const int flyout_height = toolbox_open_group_ == 3 ? 174
                : toolbox_open_group_ == 2 ? 145 : 116;
            RECT flyout{flyout_left, flyout_top, flyout_left + flyout_width, flyout_top + flyout_height};
            HBRUSH flyout_brush = CreateSolidBrush(is_dark ? RGB(20, 31, 46) : RGB(250, 252, 255));
            HPEN flyout_pen = CreatePen(PS_SOLID, 1, col_border);
            const HGDIOBJ old_flyout_pen = SelectObject(mem_dc, flyout_pen);
            const HGDIOBJ old_flyout_brush = SelectObject(mem_dc, flyout_brush);
            RoundRect(mem_dc, flyout.left, flyout.top, flyout.right, flyout.bottom, 7, 7);
            SelectObject(mem_dc, old_flyout_brush);
            SelectObject(mem_dc, old_flyout_pen);
            DeleteObject(flyout_brush);
            DeleteObject(flyout_pen);

            const bool quick_shapes = toolbox_open_group_ == 1;
            const bool path_drawing = toolbox_open_group_ == 2;
            const bool appearance = toolbox_open_group_ == 3;
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT flyout_title{flyout_left + 10, flyout_top + 3, flyout.right - 8, flyout_top + 25};
            const wchar_t *title = quick_shapes
                ? (is_vi ? L"Hình cơ bản · Quick Shapes" : L"Quick Shapes")
                : path_drawing
                    ? (is_vi ? L"Đường vẽ · Path Drawing" : L"Path Drawing")
                    : (is_vi ? L"Hiệu ứng · Appearance" : L"Effects · Appearance");
            DrawTextW(mem_dc, title,
                -1, &flyout_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            const int flyout_tools[] = {
                quick_shapes ? 1 : path_drawing ? 6 : 10,
                quick_shapes ? 2 : path_drawing ? 7 : 11,
                quick_shapes ? 9 : path_drawing ? 8 : 12,
                appearance ? 14 : path_drawing ? 11 : 13,
                13};
            const wchar_t* labels_en[] = {L"Rectangle", L"Ellipse", L"Polygon", L"", L""};
            const wchar_t* labels_vi[] = {L"Hình chữ nhật", L"Hình elip", L"Đa giác", L"", L""};
            const wchar_t* path_en[] = {L"Line", L"Polyline", L"Pen", L"Node", L""};
            const wchar_t* path_vi[] = {L"Đoạn thẳng", L"Đường gấp khúc", L"Pen", L"Node", L""};
            const wchar_t* appearance_en[] = {L"Appearance Properties", L"Gaussian Blur", L"Drop Shadow", L"Contour", L"Gradient"};
            const wchar_t* appearance_vi[] = {L"Thuộc tính diện mạo", L"Gaussian Blur", L"Drop Shadow", L"Viền Contour", L"Chuyển sắc"};
            const bool has_blur = appearance && workflow().selected_effect_index(
                document::GaussianBlurEffect{1.0, 1.0}).has_value();
            const bool has_shadow = appearance && workflow().selected_effect_index(
                document::DropShadowEffect{}).has_value();
            const bool is_contour = appearance && (active_tool_ == 12 || (contour_tool_ && contour_tool_->active()));
            const int max_items = toolbox_open_group_ == 3 ? 5
                : toolbox_open_group_ == 2 ? 4 : 3;
            for (int item = 0; item < max_items; ++item) {
                const int item_top = flyout_top + 26 + item * 29;
                const bool selected = appearance
                    ? (item == 1 && has_blur) || (item == 2 && has_shadow) || (item == 3 && is_contour) || (item == 4 && active_tool_ == 13)
                    : active_tool_ == flyout_tools[item];
                const bool available = !appearance || item == 0 || item == 4 ||
                    (item == 3 && !root().vector_controller().selection().selected_ids().empty()) ||
                    (item == 1 && workflow().can_activate_selected_effect(
                        document::GaussianBlurEffect{1.0, 1.0})) ||
                    (item == 2 && workflow().can_activate_selected_effect(
                        document::DropShadowEffect{}));
                RECT item_rect{flyout_left + 5, item_top, flyout.right - 5, item_top + 28};
                if (selected || (toolbox_flyout_index_ == item && available)) {
                    HBRUSH item_brush = CreateSolidBrush(selected ? col_active_tab : col_header);
                    FillRect(mem_dc, &item_rect, item_brush);
                    DeleteObject(item_brush);
                }
                const int icon = path_drawing && flyout_tools[item] == 11
                    ? 15 : flyout_tools[item];
                draw_tool_icon(mem_dc, icon, flyout_left + 20,
                               item_top + 14, !available ? col_text_muted : selected ? col_teal : col_text_white);
                SetTextColor(mem_dc, !available ? col_text_muted : selected ? col_teal : col_text_white);
                SelectObject(mem_dc, selected ? font_be_vietnam_bold : font_be_vietnam);
                RECT item_label{flyout_left + 35, item_top, flyout.right - 8, item_top + 28};
                const wchar_t* label = quick_shapes
                    ? (is_vi ? labels_vi[item] : labels_en[item])
                    : path_drawing
                        ? (is_vi ? path_vi[item] : path_en[item])
                        : (is_vi ? appearance_vi[item] : appearance_en[item]);
                DrawTextW(mem_dc, label, -1, &item_label,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
        }

        if (show_toolbox_customize_flyout_) {
            const int plus_y = content_top + 10 + num_slots * 42;
            const int flyout_left = 54;
            const int flyout_top = std::max(content_top + 10, std::min(plus_y - 200, h - status_h - 430));
            const int flyout_w = 264;
            const int flyout_h = 405;
            RECT flyout{flyout_left, flyout_top, flyout_left + flyout_w, flyout_top + flyout_h};

            HBRUSH flyout_brush = CreateSolidBrush(is_dark ? RGB(20, 31, 46) : RGB(250, 252, 255));
            HPEN flyout_pen = CreatePen(PS_SOLID, 1, col_border);
            const HGDIOBJ old_flyout_pen = SelectObject(mem_dc, flyout_pen);
            const HGDIOBJ old_flyout_brush = SelectObject(mem_dc, flyout_brush);
            RoundRect(mem_dc, flyout.left, flyout.top, flyout.right, flyout.bottom, 7, 7);
            SelectObject(mem_dc, old_flyout_brush);
            SelectObject(mem_dc, old_flyout_pen);
            DeleteObject(flyout_brush);
            DeleteObject(flyout_pen);

            // Title
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT flyout_title{flyout_left + 10, flyout_top + 6, flyout.right - 26, flyout_top + 26};
            const wchar_t *title = is_vi ? L"Tùy biến Hộp công cụ" : L"Customize Toolbox";
            DrawTextW(mem_dc, title, -1, &flyout_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Close 'X'
            RECT rc_close{flyout.right - 24, flyout_top + 4, flyout.right - 4, flyout_top + 24};
            SetTextColor(mem_dc, col_text_muted);
            DrawTextW(mem_dc, L"×", -1, &rc_close, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Subtitle
            SelectObject(mem_dc, font_secondary);
            SetTextColor(mem_dc, col_text_muted);
            RECT flyout_sub{flyout_left + 10, flyout_top + 25, flyout.right - 10, flyout_top + 40};
            const wchar_t *sub = is_vi ? L"Đánh dấu để hiện/ẩn icon trên thanh:" : L"Check to show/hide tool icons:";
            DrawTextW(mem_dc, sub, -1, &flyout_sub, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Checklist of all 11 slots
            for (int i = 0; i < static_cast<int>(ToolboxSlotId::Count); ++i) {
                const auto slot = static_cast<ToolboxSlotId>(i);
                const auto info = get_toolbox_slot_info(slot);
                const bool visible = is_toolbox_slot_visible(slot);
                const int item_top = flyout_top + 43 + i * 26;

                // Checkbox rect:
                RECT rc_chk{flyout_left + 12, item_top + 4, flyout_left + 26, item_top + 18};
                HBRUSH br_chk = CreateSolidBrush(visible ? col_teal : (is_dark ? RGB(26, 40, 58) : RGB(241, 245, 249)));
                HPEN pen_chk = CreatePen(PS_SOLID, 1, visible ? col_teal : col_border);
                const HGDIOBJ o_pen_chk = SelectObject(mem_dc, pen_chk);
                const HGDIOBJ o_br_chk = SelectObject(mem_dc, br_chk);
                RoundRect(mem_dc, rc_chk.left, rc_chk.top, rc_chk.right, rc_chk.bottom, 3, 3);
                SelectObject(mem_dc, o_br_chk);
                SelectObject(mem_dc, o_pen_chk);
                DeleteObject(pen_chk);
                DeleteObject(br_chk);

                if (visible) {
                    HPEN pen_chk_mark = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
                    const HGDIOBJ o_pen_m = SelectObject(mem_dc, pen_chk_mark);
                    MoveToEx(mem_dc, rc_chk.left + 3, rc_chk.top + 7, nullptr);
                    LineTo(mem_dc, rc_chk.left + 6, rc_chk.top + 11);
                    LineTo(mem_dc, rc_chk.left + 11, rc_chk.top + 4);
                    SelectObject(mem_dc, o_pen_m);
                    DeleteObject(pen_chk_mark);
                }

                // Name and shortcut
                SelectObject(mem_dc, font_secondary);
                SetTextColor(mem_dc, visible ? col_text_white : col_text_muted);
                RECT name_rc{flyout_left + 32, item_top + 1, flyout.right - 58, item_top + 21};
                const wchar_t* name = is_vi ? info.name_vi : info.name_en;
                DrawTextW(mem_dc, name, -1, &name_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                if (slot == ToolboxSlotId::Pick) {
                    SelectObject(mem_dc, font_mono);
                    SetTextColor(mem_dc, col_text_muted);
                    RECT lock_rc{flyout.right - 62, item_top + 1, flyout.right - 8, item_top + 21};
                    DrawTextW(mem_dc, L"[Khóa]", -1, &lock_rc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                } else if (info.shortcut[0] != L'\0') {
                    SelectObject(mem_dc, font_mono);
                    SetTextColor(mem_dc, col_text_muted);
                    RECT sc_rc{flyout.right - 62, item_top + 1, flyout.right - 8, item_top + 21};
                    DrawTextW(mem_dc, info.shortcut, -1, &sc_rc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
            }

            // Bottom Buttons: Reset & Quick Remove Mode
            const int bottom_y = flyout_top + 338;
            // Reset button
            RECT rc_reset{flyout_left + 10, bottom_y, flyout_left + 122, bottom_y + 26};
            HBRUSH br_btn = CreateSolidBrush(is_dark ? RGB(26, 40, 58) : RGB(236, 242, 248));
            HPEN pen_btn = CreatePen(PS_SOLID, 1, col_border);
            SelectObject(mem_dc, pen_btn);
            SelectObject(mem_dc, br_btn);
            RoundRect(mem_dc, rc_reset.left, rc_reset.top, rc_reset.right, rc_reset.bottom, 4, 4);
            SetTextColor(mem_dc, col_text_white);
            SelectObject(mem_dc, font_secondary);
            const wchar_t* rst_text = is_vi ? L"Mặc định" : L"Reset Defaults";
            DrawTextW(mem_dc, rst_text, -1, &rc_reset, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Edit Mode button
            RECT rc_edit{flyout_left + 128, bottom_y, flyout.right - 10, bottom_y + 26};
            HBRUSH br_edit = CreateSolidBrush(toolbox_edit_mode_ ? col_teal : (is_dark ? RGB(26, 40, 58) : RGB(236, 242, 248)));
            SelectObject(mem_dc, br_edit);
            RoundRect(mem_dc, rc_edit.left, rc_edit.top, rc_edit.right, rc_edit.bottom, 4, 4);
            SetTextColor(mem_dc, toolbox_edit_mode_ ? RGB(255, 255, 255) : col_text_white);
            const wchar_t* edit_text = is_vi ? (toolbox_edit_mode_ ? L"Xóa nhanh: BẬT" : L"Xóa nhanh: TẮT")
                                             : (toolbox_edit_mode_ ? L"Quick Del: ON" : L"Quick Del: OFF");
            DrawTextW(mem_dc, edit_text, -1, &rc_edit, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            DeleteObject(pen_btn);
            DeleteObject(br_btn);
            DeleteObject(br_edit);

            // Tip at bottom
            RECT rc_tip{flyout_left + 10, bottom_y + 30, flyout.right - 10, bottom_y + 50};
            SetTextColor(mem_dc, col_text_muted);
            const wchar_t* tip_str = is_vi ? L"Mẹo: Bấm nút [-] trên icon để xóa nhanh"
                                            : L"Tip: Click [-] on icon to remove quickly";
            DrawTextW(mem_dc, tip_str, -1, &rc_tip, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
}

void NativeWindow::paint_right_inspector_dock(HDC mem_dc, int w, int h) {
    ensure_ui_fonts();
    HFONT font_be_vietnam = font_be_vietnam_;
    HFONT font_be_vietnam_bold = font_be_vietnam_bold_;
    HFONT font_secondary = font_secondary_;
    HFONT font_mono = font_mono_;
    bool is_dark = (theme_ == ThemeMode::CyberDark);
    bool is_vi = (language_ == UILanguage::Vietnamese);
    COLORREF col_header = is_dark ? RGB(12, 19, 29) : RGB(255, 255, 255);
    COLORREF col_border = is_dark ? RGB(29, 38, 54) : RGB(203, 213, 225);
    COLORREF col_teal = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    COLORREF col_text_white = is_dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    COLORREF col_text_muted = is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    COLORREF col_active_tab = is_dark ? RGB(22, 37, 54) : RGB(236, 242, 248);
    COLORREF col_btn_bg = is_dark ? RGB(18, 28, 42) : RGB(241, 245, 249);
    COLORREF col_btn_bdr = is_dark ? RGB(29, 38, 54) : RGB(203, 213, 225);
    int topbar_h = 38, tabbar_h = 28, ribbon_h = 36, status_h = 26, inspect_w = 280;
    int content_top = topbar_h + tabbar_h + ribbon_h;

    const auto snapshot = root().document().current_snapshot();
    const auto& selection = root().vector_controller().selection().selected_ids();

        // 6. Right Inspector & Layers Dock
        int insp_x = w - inspect_w;
        HBRUSH br_dock = CreateSolidBrush(col_header);
        RECT rc_dock = { insp_x, content_top, w, h - status_h };
        FillRect(mem_dc, &rc_dock, br_dock);
        DeleteObject(br_dock);

        int tab_w = inspect_w / 3;
        const wchar_t* tab_names_vi[] = { L"Thuộc tính", L"Lớp (Layers)", L"Lịch sử" };
        const wchar_t* tab_names_en[] = { L"Inspector", L"Layers", L"History" };
        for (int i = 0; i < 3; ++i) {
            RECT rc_tab = { insp_x + i * tab_w, content_top, insp_x + (i + 1) * tab_w, content_top + 28 };
            if (active_dock_tab_ == i) {
                HBRUSH br_tab = CreateSolidBrush(col_active_tab);
                HPEN p_tab = CreatePen(PS_SOLID, 1, col_teal);
                SelectObject(mem_dc, p_tab);
                SelectObject(mem_dc, br_tab);
                RoundRect(mem_dc, rc_tab.left + 2, rc_tab.top + 2, rc_tab.right - 2, rc_tab.bottom, 6, 6);
                DeleteObject(p_tab);
                DeleteObject(br_tab);
                SetTextColor(mem_dc, col_teal);
                SelectObject(mem_dc, font_be_vietnam_bold);
            } else {
                SetTextColor(mem_dc, col_text_muted);
                SelectObject(mem_dc, font_secondary);
            }
            DrawTextW(mem_dc, is_vi ? tab_names_vi[i] : tab_names_en[i], -1, &rc_tab, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        const int content_y = content_top + 36;
        SelectObject(mem_dc, font_be_vietnam_bold);
        SetTextColor(mem_dc, col_teal);
        RECT rc_head = { insp_x + 14, content_y,
                         active_dock_tab_ == 0 ? w - 100 : w - 14, content_y + 22 };
        SelectObject(mem_dc, font_secondary);
        SetTextColor(mem_dc, col_text_white);

        if (active_dock_tab_ == 0 && active_tool_ == 9) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT properties_title{insp_x + 14, content_y + 2, w - 14, content_y + 25};
            DrawTextW(mem_dc, is_vi ? L"THUỘC TÍNH · ĐA GIÁC" : L"PROPERTIES · POLYGON", -1,
                      &properties_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            const auto draw_polygon_property = [&](int y, const wchar_t* name,
                                                   const std::wstring& value,
                                                   NumericFieldId field) {
                RECT name_rect{insp_x + 14, y, insp_x + 126, y + 32};
                SetTextColor(mem_dc, col_text_white);
                SelectObject(mem_dc, font_be_vietnam);
                DrawTextW(mem_dc, name, -1, &name_rect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                RECT input{insp_x + 132, y + 2, w - 14, y + 30};
                const bool focused = active_num_field_ == field;
                HBRUSH input_brush = CreateSolidBrush(focused ? col_active_tab : col_btn_bg);
                HPEN input_pen = CreatePen(PS_SOLID, focused ? 2 : 1,
                                           focused ? col_teal : col_btn_bdr);
                const HGDIOBJ old_pen = SelectObject(mem_dc, input_pen);
                const HGDIOBJ old_brush = SelectObject(mem_dc, input_brush);
                RoundRect(mem_dc, input.left, input.top, input.right, input.bottom, 5, 5);
                SelectObject(mem_dc, old_brush);
                SelectObject(mem_dc, old_pen);
                DeleteObject(input_brush);
                DeleteObject(input_pen);
                RECT value_rect{input.left + 8, input.top, input.right - 8, input.bottom};
                SetTextColor(mem_dc, focused ? col_teal : col_text_white);
                SelectObject(mem_dc, font_mono);
                const std::wstring shown = focused ? utf8_to_wide(num_input_buffer_) + L"|" : value;
                DrawTextW(mem_dc, shown.c_str(), -1, &value_rect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            };
            draw_polygon_property(content_y + 34, is_vi ? L"Số cạnh" : L"Sides",
                std::to_wstring(polygon_sides_), NumericFieldId::PolygonSides);
            std::wostringstream rotation_value;
            rotation_value << std::fixed << std::setprecision(1) << polygon_rotation_degrees_;
            std::wstring rotation_text = rotation_value.str();
            if (rotation_text.size() > 2 && rotation_text.substr(rotation_text.size() - 2) == L".0")
                rotation_text.erase(rotation_text.size() - 2);
            rotation_text += L"°";
            draw_polygon_property(content_y + 78, is_vi ? L"Góc xoay" : L"Rotation",
                rotation_text, NumericFieldId::PolygonRotation);

            SelectObject(mem_dc, font_secondary);
            SetTextColor(mem_dc, col_text_muted);
            RECT property_hint{insp_x + 14, content_y + 122, w - 14, content_y + 208};
            DrawTextW(mem_dc,
                is_vi ? L"Số cạnh: 3–256. Góc xoay là góc đỉnh đầu tiên. Shift khóa hướng kéo theo bước 15°; Ctrl lấy điểm đầu làm tâm. Các giá trị này áp dụng cho đa giác tạo tiếp theo và không sửa path đã có."
                      : L"Sides: 3–256. Rotation sets the first vertex angle. Shift snaps the drag direction to 15°; Ctrl uses the start point as center. Settings apply to the next polygon and do not alter existing paths.",
                -1, &property_hint, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        } else if (active_dock_tab_ == 0 && active_tool_ == 11 && node_tool_ && node_tool_->active()) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT title{insp_x + 14, content_y + 2, w - 14, content_y + 25};
            DrawTextW(mem_dc, is_vi ? L"THUỘC TÍNH · NODE" : L"PROPERTIES · NODE", -1,
                      &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            const size_t node_count = node_tool_->selected_node_count();
            const auto position = node_tool_->single_selected_node_position();
            const auto mode = node_tool_->selected_nodes_mode();
            SelectObject(mem_dc, font_secondary);
            SetTextColor(mem_dc, col_text_muted);
            RECT selection_summary{insp_x + 14, content_y + 24, w - 14, content_y + 44};
            const std::wstring count_text = node_count == 1
                ? (is_vi ? L"1 node được chọn" : L"1 node selected")
                : (is_vi ? std::to_wstring(node_count) + L" node được chọn"
                         : std::to_wstring(node_count) + L" nodes selected");
            const wchar_t* mode_name = nullptr;
            if (mode) {
                switch (*mode) {
                    case document::PathNodeMode::Corner: mode_name = is_vi ? L"Góc" : L"Corner"; break;
                    case document::PathNodeMode::Cusp: mode_name = L"Cusp"; break;
                    case document::PathNodeMode::Smooth: mode_name = is_vi ? L"Mượt" : L"Smooth"; break;
                    case document::PathNodeMode::Symmetric: mode_name = is_vi ? L"Đối xứng" : L"Symmetric"; break;
                    case document::PathNodeMode::AutoSmooth: mode_name = is_vi ? L"Tự mượt" : L"Auto Smooth"; break;
                }
            }
            const std::wstring summary_text = count_text + L" · " +
                (mode_name ? mode_name : node_count > 1
                    ? (is_vi ? L"mode hỗn hợp" : L"Mixed modes")
                    : (is_vi ? L"chưa chọn node" : L"no node selected"));
            DrawTextW(mem_dc, summary_text.c_str(), -1, &selection_summary,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            const auto draw_coordinate = [&](int y, const wchar_t* label,
                                             NumericFieldId field, double value) {
                RECT name{insp_x + 14, y, insp_x + 126, y + 32};
                SetTextColor(mem_dc, col_text_white);
                SelectObject(mem_dc, font_be_vietnam);
                DrawTextW(mem_dc, label, -1, &name,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                RECT input{insp_x + 132, y + 2, w - 14, y + 30};
                const bool focused = active_num_field_ == field;
                const bool enabled = position.has_value();
                HBRUSH brush = CreateSolidBrush(!enabled ? col_header : focused ? col_active_tab : col_btn_bg);
                HPEN pen = CreatePen(PS_SOLID, focused ? 2 : 1,
                                     focused ? col_teal : enabled ? col_btn_bdr : col_border);
                const auto old_pen = SelectObject(mem_dc, pen);
                const auto old_brush = SelectObject(mem_dc, brush);
                RoundRect(mem_dc, input.left, input.top, input.right, input.bottom, 5, 5);
                SelectObject(mem_dc, old_brush);
                SelectObject(mem_dc, old_pen);
                DeleteObject(brush);
                DeleteObject(pen);
                std::wostringstream formatted;
                formatted << std::fixed << std::setprecision(3) << value << L" mm";
                const std::wstring shown = focused ? utf8_to_wide(num_input_buffer_) + L"|"
                    : enabled ? formatted.str()
                    : node_count > 1 ? (is_vi ? L"Hỗn hợp" : L"Mixed") : L"—";
                RECT value_rect{input.left + 8, input.top, input.right - 8, input.bottom};
                SetTextColor(mem_dc, !enabled ? col_text_muted : focused ? col_teal : col_text_white);
                SelectObject(mem_dc, font_mono);
                DrawTextW(mem_dc, shown.c_str(), -1, &value_rect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            };
            draw_coordinate(content_y + 48, L"X (mm)", NumericFieldId::ObjectX,
                            position ? position->x : 0.0);
            draw_coordinate(content_y + 88, L"Y (mm)", NumericFieldId::ObjectY,
                            position ? position->y : 0.0);

            const wchar_t* mode_names_en[] = {L"⋀ Cusp", L"∿ Smooth", L"⎚ Symm"};
            const wchar_t* mode_names_vi[] = {L"⋀ Cusp", L"∿ Smooth", L"⎚ Đối xứng"};
            constexpr document::PathNodeMode modes[] = {
                document::PathNodeMode::Cusp,
                document::PathNodeMode::Smooth,
                document::PathNodeMode::Symmetric};
            const int button_width = (AlignPanelMetrics::content_width - 12) / 3;
            for (int i = 0; i < 3; ++i) {
                const int left = insp_x + AlignPanelMetrics::content_left + i * (button_width + 6);
                const bool enabled = node_count > 0;
                const bool selected = mode && *mode == modes[i];
                HBRUSH brush = CreateSolidBrush(!enabled ? col_header : selected ? col_active_tab : col_btn_bg);
                HPEN pen = CreatePen(PS_SOLID, selected ? 2 : 1,
                                     !enabled ? col_border : selected ? col_teal : col_btn_bdr);
                const auto old_pen = SelectObject(mem_dc, pen);
                const auto old_brush = SelectObject(mem_dc, brush);
                RoundRect(mem_dc, left, content_y + 136, left + button_width,
                          content_y + 164, 5, 5);
                SelectObject(mem_dc, old_brush);
                SelectObject(mem_dc, old_pen);
                DeleteObject(brush);
                DeleteObject(pen);
                RECT text{left + 2, content_y + 136, left + button_width - 2, content_y + 164};
                SetTextColor(mem_dc, !enabled ? col_text_muted : selected ? col_teal : col_text_white);
                SelectObject(mem_dc, selected ? font_be_vietnam_bold : font_be_vietnam);
                DrawTextW(mem_dc, is_vi ? mode_names_vi[i] : mode_names_en[i], -1,
                          &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
            SelectObject(mem_dc, font_secondary);
            SetTextColor(mem_dc, col_text_muted);
            RECT hint{insp_x + 14, content_y + 172, w - 14, content_y + 256};
            DrawTextW(mem_dc,
                is_vi ? L"Chọn một node để nhập tọa độ mm. Enter áp dụng; Tab đổi trục; Esc hủy. Có thể kéo anchor/tay nắm trên canvas. Nhiều node: tọa độ bị tắt, mode áp dụng cho toàn bộ vùng chọn."
                      : L"Select one node to enter coordinates in mm. Enter applies; Tab switches axes; Esc cancels. Drag anchors/handles on canvas. Multiple nodes disable coordinates; mode applies to the full selection.",
                -1, &hint, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        } else if (active_dock_tab_ == 0 &&
                   ((active_tool_ == 13 && gradient_tool_ && gradient_tool_->active()) ||
                    (active_tool_ == 12 && contour_tool_ && contour_tool_->active()))) {
        } else if (active_dock_tab_ == 0 && has_text_properties_target(*this)) {
            paint_text_properties_panel(mem_dc, insp_x, inspect_w, content_y, h - status_h,
                theme_, language_, text_properties_view(*this), active_num_field_, num_input_buffer_);
            return;
        } else if (active_dock_tab_ == 0) {
            const int panel_left = insp_x + AlignPanelMetrics::content_left;
            const int section_width = (AlignPanelMetrics::content_width - 12) / 3;
            const wchar_t* section_labels_vi[] = {L"Đối tượng", L"Kiểu", L"Căn chỉnh"};
            const wchar_t* section_labels_en[] = {L"Objects", L"Style", L"Align"};
            for (int i = 0; i < 3; ++i) {
                const bool active = i == 0 ? (!show_style_inspector_ && !show_align_panel_)
                    : i == 1 ? show_style_inspector_ : show_align_panel_;
                RECT section_button{
                    panel_left + i * (section_width + 6),
                    AlignPanelMetrics::section_tabs_top,
                    panel_left + i * (section_width + 6) + section_width,
                    AlignPanelMetrics::section_tabs_top + AlignPanelMetrics::section_tabs_height - 2};
                HBRUSH section_brush = CreateSolidBrush(active ? col_active_tab : col_btn_bg);
                HPEN section_pen = CreatePen(PS_SOLID, 1, active ? col_teal : col_btn_bdr);
                const auto old_pen = SelectObject(mem_dc, section_pen);
                const auto old_brush = SelectObject(mem_dc, section_brush);
                RoundRect(mem_dc, section_button.left, section_button.top, section_button.right,
                          section_button.bottom, 5, 5);
                SelectObject(mem_dc, old_pen);
                SelectObject(mem_dc, old_brush);
                DeleteObject(section_pen);
                DeleteObject(section_brush);
                SetTextColor(mem_dc, active ? col_teal : col_text_white);
                DrawTextW(mem_dc, is_vi ? section_labels_vi[i] : section_labels_en[i], -1,
                          &section_button, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
        }

        std::wostringstream content;
        if (active_dock_tab_ == 0) {
            if (active_tool_ == 13 && gradient_tool_ && gradient_tool_->active()) {
                paint_gradient_properties_panel(mem_dc, insp_x, inspect_w, content_y,
                    h - status_h, theme_, language_, *gradient_tool_, active_num_field_,
                    num_input_buffer_);
            } else if (active_tool_ == 12 && contour_tool_ && contour_tool_->active()) {
                const auto target = contour_tool_->target_object();
                const auto* object = snapshot ? snapshot->objects().lookup_ref(target) : nullptr;
                const auto* path = object ? std::get_if<document::PathObject>(object) : nullptr;
                const bool can_edit = path && !path->locked;
                const bool has_contour = can_edit &&
                    document::get_object_contour(root().document(), target).has_value();
                const auto& preview = contour_tool_->preview_result();
                const int panel_left = insp_x + 14;
                const int panel_right = w - 14;
                const int panel_width = panel_right - panel_left;
                SelectObject(mem_dc, font_be_vietnam_bold); SetTextColor(mem_dc, col_teal);
                RECT title{panel_left, content_y + 2, panel_right, content_y + 24};
                DrawTextW(mem_dc, is_vi ? L"THUỘC TÍNH · CONTOUR" : L"PROPERTIES · CONTOUR", -1,
                          &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(mem_dc, font_secondary); SetTextColor(mem_dc, !can_edit ? col_text_muted : col_text_white);
                const std::wstring target_name = path && !path->name.empty()
                    ? utf8_to_wide(path->name) : std::to_wstring(target);
                const std::wstring scope = !can_edit ? (is_vi ? L"Chọn một Path không khóa" : L"Select one unlocked Path")
                    : target_name + (is_vi ? L" · xem trước " : L" · preview ") + std::to_wstring(preview.steps.size()) +
                      (is_vi ? L" bước" : preview.steps.size() == 1 ? L" step" : L" steps") +
                      (has_contour ? (is_vi ? L" · đã lưu" : L" · saved")
                                                                  : (is_vi ? L" · chưa lưu" : L" · not saved"));
                RECT summary{panel_left, content_y + 25, panel_right, content_y + 45};
                DrawTextW(mem_dc, scope.c_str(), -1, &summary,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                const auto draw_button = [&](int x, int y, int width, const wchar_t* label, bool enabled, bool selected = false) {
                    HBRUSH brush = CreateSolidBrush(!enabled ? col_header : selected ? col_active_tab : col_btn_bg);
                    HPEN pen = CreatePen(PS_SOLID, selected ? 2 : 1,
                                         !enabled ? col_border : selected ? col_teal : col_btn_bdr);
                    const auto old_pen = SelectObject(mem_dc, pen); const auto old_brush = SelectObject(mem_dc, brush);
                    RoundRect(mem_dc, x, y, x + width, y + 28, 5, 5);
                    SelectObject(mem_dc, old_brush); SelectObject(mem_dc, old_pen);
                    DeleteObject(brush); DeleteObject(pen);
                    RECT label_rect{x + 2, y, x + width - 2, y + 28};
                    SetTextColor(mem_dc, !enabled ? col_text_muted : selected ? col_teal : col_text_white);
                    SelectObject(mem_dc, selected ? font_be_vietnam_bold : font_be_vietnam);
                    DrawTextW(mem_dc, label, -1, &label_rect,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                };
                const auto draw_field = [&](int row, const wchar_t* label, const std::wstring& value, NumericFieldId field, bool enabled) {
                    RECT name{panel_left, content_y + row, panel_left + 112, content_y + row + 32};
                    SetTextColor(mem_dc, enabled ? col_text_white : col_text_muted); SelectObject(mem_dc, font_be_vietnam);
                    DrawTextW(mem_dc, label, -1, &name,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    RECT input{panel_left + 118, content_y + row + 2, panel_right, content_y + row + 30};
                    const bool focused = active_num_field_ == field;
                    HBRUSH brush = CreateSolidBrush(!enabled ? col_header : focused ? col_active_tab : col_btn_bg);
                    HPEN pen = CreatePen(PS_SOLID, focused ? 2 : 1,
                                         !enabled ? col_border : focused ? col_teal : col_btn_bdr);
                    const auto old_pen = SelectObject(mem_dc, pen); const auto old_brush = SelectObject(mem_dc, brush);
                    RoundRect(mem_dc, input.left, input.top, input.right, input.bottom, 5, 5);
                    SelectObject(mem_dc, old_brush); SelectObject(mem_dc, old_pen);
                    DeleteObject(brush); DeleteObject(pen);
                    RECT shown_rect{input.left + 7, input.top, input.right - 6, input.bottom};
                    const std::wstring shown = focused ? utf8_to_wide(num_input_buffer_) + L"|" : value;
                    SetTextColor(mem_dc, !enabled ? col_text_muted : focused ? col_teal : col_text_white);
                    SelectObject(mem_dc, font_mono);
                    DrawTextW(mem_dc, shown.c_str(), -1, &shown_rect,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                };
                SetTextColor(mem_dc, col_teal);
                RECT direction_label{panel_left, content_y + 48, panel_right, content_y + 66};
                DrawTextW(mem_dc, is_vi ? L"HƯỚNG VIỀN" : L"CONTOUR DIRECTION", -1, &direction_label,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                const int triple_width = (panel_width - 12) / 3;
                const wchar_t* direction_names_en[] = {L"Outside", L"Inside", L"To center"};
                const wchar_t* direction_names_vi[] = {L"Ngoài", L"Trong", L"Vào tâm"};
                constexpr geometry::ContourDirection directions[] = {geometry::ContourDirection::Outside,
                    geometry::ContourDirection::Inside, geometry::ContourDirection::ToCenter};
                for (int i = 0; i < 3; ++i)
                    draw_button(panel_left + i * (triple_width + 6), content_y + 68, triple_width,
                        is_vi ? direction_names_vi[i] : direction_names_en[i], can_edit,
                        contour_tool_->direction() == directions[i]);
                std::wostringstream spacing; spacing << std::fixed << std::setprecision(3)
                    << contour_tool_->spacing_mm() << L" mm";
                draw_field(104, is_vi ? L"Khoảng cách" : L"Spacing", spacing.str(), NumericFieldId::DistributeSpacing, can_edit);
                draw_field(140, is_vi ? L"Số bước" : L"Steps", std::to_wstring(contour_tool_->steps()),
                           NumericFieldId::PolygonSides, can_edit);
                SetTextColor(mem_dc, col_teal);
                RECT join_label{panel_left, content_y + 176, panel_right, content_y + 194};
                DrawTextW(mem_dc, is_vi ? L"GÓC NỐI" : L"CORNER JOIN", -1, &join_label,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                const wchar_t* join_names_en[] = {L"Miter", L"Round", L"Bevel"};
                const wchar_t* join_names_vi[] = {L"Nhọn", L"Tròn", L"Vát"};
                constexpr geometry::JoinType joins[] = {geometry::JoinType::Miter,
                    geometry::JoinType::Round, geometry::JoinType::Bevel};
                for (int i = 0; i < 3; ++i)
                    draw_button(panel_left + i * (triple_width + 6), content_y + 196, triple_width,
                        is_vi ? join_names_vi[i] : join_names_en[i], can_edit,
                        contour_tool_->join_type() == joins[i]);
                std::wostringstream miter; miter << std::fixed << std::setprecision(2) << contour_tool_->miter_limit();
                draw_field(228, L"Miter limit", miter.str(), NumericFieldId::PolygonRotation,
                           can_edit && contour_tool_->join_type() == geometry::JoinType::Miter);
                SelectObject(mem_dc, font_secondary); SetTextColor(mem_dc, col_text_muted);
                RECT hint{panel_left, content_y + 264, panel_right, content_y + 310};
                DrawTextW(mem_dc, is_vi
                    ? L"Enter cập nhật xem trước; Tab chuyển trường. Apply lưu một bước Undo. Remove xóa viền đã lưu; Break tách thành Path độc lập. Cancel khôi phục tham số đã lưu (hoặc mặc định nếu chưa lưu)."
                    : L"Enter updates preview; Tab moves between fields. Apply saves one undo step. Remove deletes the saved contour; Break creates independent Paths. Cancel restores saved settings or defaults.",
                    -1, &hint, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                const int action_width = (panel_width - 18) / 4;
                const wchar_t* action_names_en[] = {L"Apply", L"Remove", L"Break", L"Cancel"};
                const wchar_t* action_names_vi[] = {L"Áp dụng", L"Xóa", L"Tách", L"Hủy"};
                for (int i = 0; i < 4; ++i)
                    draw_button(panel_left + i * (action_width + 6), content_y + 316, action_width,
                        is_vi ? action_names_vi[i] : action_names_en[i],
                        i == 0 ? can_edit && contour_tool_->has_preview() : i < 3 ? has_contour : can_edit);
            } else if (active_tool_ == 11 && node_tool_ && node_tool_->active()) {
                // Node-specific selection and properties were painted above.
            } else if (show_align_panel_) {
                using Metrics = AlignPanelMetrics;
                const int panel_left = insp_x + Metrics::content_left;
                const int three_button_width = (Metrics::content_width - 12) / 3;
                const int two_button_width = (Metrics::content_width - 6) / 2;
                const auto draw_panel_button = [&](int x, int y, int width, int height,
                                                   const wchar_t* label, bool enabled, bool active = false) {
                    const COLORREF fill = !enabled ? col_header : active ? col_active_tab : col_btn_bg;
                    const COLORREF border = active ? col_teal : enabled ? col_btn_bdr : col_border;
                    const COLORREF text = !enabled ? col_text_muted : active ? col_teal : col_text_white;
                    HBRUSH brush = CreateSolidBrush(fill);
                    HPEN pen = CreatePen(PS_SOLID, 1, border);
                    const auto old_pen = SelectObject(mem_dc, pen);
                    const auto old_brush = SelectObject(mem_dc, brush);
                    RoundRect(mem_dc, x, y, x + width, y + height, 4, 4);
                    SelectObject(mem_dc, old_pen);
                    SelectObject(mem_dc, old_brush);
                    DeleteObject(pen);
                    DeleteObject(brush);
                    SetTextColor(mem_dc, text);
                    RECT rect{x + 2, y, x + width - 2, y + height};
                    DrawTextW(mem_dc, label, -1, &rect,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                };
                const auto draw_heading = [&](int y, const wchar_t* label) {
                    SetTextColor(mem_dc, col_teal);
                    RECT rect{panel_left, y, panel_left + Metrics::content_width, y + 18};
                    DrawTextW(mem_dc, label, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                };

                draw_heading(164, is_vi ? L"CĂN THEO" : L"ALIGN REFERENCE");
                draw_panel_button(panel_left + Metrics::content_width - 98, 164, 98, 20,
                    is_vi ? L"Giữa trang" : L"Center Page",
                    workflow().can_align_selected(geometry::AlignMode::CenterPage));

                constexpr geometry::AlignReference references[] = {
                    geometry::AlignReference::Selection,
                    geometry::AlignReference::KeyObject,
                    geometry::AlignReference::Page
                };
                const wchar_t* reference_labels_en[] = {L"Selection", L"Key Object", L"Page"};
                const wchar_t* reference_labels_vi[] = {L"Vùng chọn", L"Đối tượng", L"Trang"};
                for (int i = 0; i < 3; ++i) {
                    const bool enabled = i != 1 || selection.size() >= 2;
                    draw_panel_button(panel_left + i * (three_button_width + 6), Metrics::reference_top,
                        three_button_width, Metrics::reference_height,
                        is_vi ? reference_labels_vi[i] : reference_labels_en[i], enabled,
                        align_reference_ == references[i]);
                }

                draw_heading(216, is_vi ? L"CĂN CHỈNH ĐỐI TƯỢNG" : L"ALIGN OBJECTS");
                constexpr geometry::AlignMode align_modes[] = {
                    geometry::AlignMode::Left,
                    geometry::AlignMode::CenterHorizontal,
                    geometry::AlignMode::Right,
                    geometry::AlignMode::Top,
                    geometry::AlignMode::CenterVertical,
                    geometry::AlignMode::Bottom
                };
                const wchar_t* align_labels_en[] = {L"Left", L"Center X", L"Right", L"Top", L"Center Y", L"Bottom"};
                const wchar_t* align_labels_vi[] = {L"Trái", L"Giữa X", L"Phải", L"Trên", L"Giữa Y", L"Dưới"};
                const int align_row_gap = Metrics::align_button_height + Metrics::align_row_gap;
                for (int i = 0; i < 6; ++i) {
                    const int row = i / 3;
                    const int column = i % 3;
                    const int y = Metrics::align_top + row * align_row_gap;
                    draw_panel_button(panel_left + column * (three_button_width + 6), y,
                        three_button_width, Metrics::align_button_height,
                        is_vi ? align_labels_vi[i] : align_labels_en[i],
                        workflow().can_align_selected(align_modes[i], align_reference_, key_object_id_));
                }

                draw_heading(300, is_vi ? L"PHÂN BỔ ĐỀU" : L"DISTRIBUTE EVENLY");
                SetTextColor(mem_dc, col_text_muted);
                RECT horizontal_label{panel_left, 320, panel_left + Metrics::content_width, 338};
                DrawTextW(mem_dc, is_vi ? L"Ngang" : L"Horizontal", -1, &horizontal_label,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                constexpr geometry::DistributeMode horizontal_modes[] = {
                    geometry::DistributeMode::SpacingHorizontal,
                    geometry::DistributeMode::CentersHorizontal,
                    geometry::DistributeMode::LeftEdges,
                    geometry::DistributeMode::RightEdges
                };
                const wchar_t* horizontal_labels_en[] = {L"Equal gap", L"Centers", L"Left edges", L"Right edges"};
                const wchar_t* horizontal_labels_vi[] = {L"Khoảng hở", L"Tâm", L"Mép trái", L"Mép phải"};
                const int distribute_row_gap = Metrics::distribute_button_height + Metrics::distribute_row_gap;
                for (int i = 0; i < 4; ++i) {
                    const int row = i / 2;
                    const int column = i % 2;
                    draw_panel_button(panel_left + column * (two_button_width + 6),
                        Metrics::horizontal_top + row * distribute_row_gap,
                        two_button_width, Metrics::distribute_button_height,
                        is_vi ? horizontal_labels_vi[i] : horizontal_labels_en[i],
                        workflow().can_distribute_selected(horizontal_modes[i]));
                }

                RECT vertical_label{panel_left, 400, panel_left + Metrics::content_width, 418};
                SetTextColor(mem_dc, col_text_muted);
                DrawTextW(mem_dc, is_vi ? L"Dọc" : L"Vertical", -1, &vertical_label,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                constexpr geometry::DistributeMode vertical_modes[] = {
                    geometry::DistributeMode::SpacingVertical,
                    geometry::DistributeMode::CentersVertical,
                    geometry::DistributeMode::TopEdges,
                    geometry::DistributeMode::BottomEdges
                };
                const wchar_t* vertical_labels_en[] = {L"Equal gap", L"Centers", L"Top edges", L"Bottom edges"};
                const wchar_t* vertical_labels_vi[] = {L"Khoảng hở", L"Tâm", L"Mép trên", L"Mép dưới"};
                for (int i = 0; i < 4; ++i) {
                    const int row = i / 2;
                    const int column = i % 2;
                    draw_panel_button(panel_left + column * (two_button_width + 6),
                        Metrics::vertical_top + row * distribute_row_gap,
                        two_button_width, Metrics::distribute_button_height,
                        is_vi ? vertical_labels_vi[i] : vertical_labels_en[i],
                        workflow().can_distribute_selected(vertical_modes[i]));
                }

                draw_heading(481, is_vi ? L"KHOẢNG HỞ CỐ ĐỊNH (mm)" : L"FIXED GAP (mm)");
                const bool spacing_editing = active_num_field_ == NumericFieldId::DistributeSpacing;
                std::wostringstream spacing_text;
                if (spacing_editing) spacing_text << utf8_to_wide(num_input_buffer_);
                else spacing_text << std::fixed << std::setprecision(1) << distribute_spacing_mm_;
                RECT spacing_field{panel_left, Metrics::gap_field_top,
                                   panel_left + 166, Metrics::gap_field_top + Metrics::gap_field_height};
                HBRUSH spacing_brush = CreateSolidBrush(spacing_editing ? col_active_tab : col_btn_bg);
                HPEN spacing_pen = CreatePen(PS_SOLID, 1, spacing_editing ? col_teal : col_btn_bdr);
                const auto old_spacing_pen = SelectObject(mem_dc, spacing_pen);
                const auto old_spacing_brush = SelectObject(mem_dc, spacing_brush);
                RoundRect(mem_dc, spacing_field.left, spacing_field.top, spacing_field.right,
                          spacing_field.bottom, 4, 4);
                SelectObject(mem_dc, old_spacing_pen);
                SelectObject(mem_dc, old_spacing_brush);
                DeleteObject(spacing_pen);
                DeleteObject(spacing_brush);
                RECT spacing_value{spacing_field.left + 8, spacing_field.top,
                                   spacing_field.right - 8, spacing_field.bottom};
                SetTextColor(mem_dc, spacing_editing ? col_teal : col_text_white);
                const auto spacing_value_text = spacing_text.str();
                DrawTextW(mem_dc, spacing_value_text.c_str(), -1, &spacing_value,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                RECT spacing_unit{panel_left + 170, Metrics::gap_field_top,
                                  panel_left + Metrics::content_width, Metrics::gap_field_top + Metrics::gap_field_height};
                SetTextColor(mem_dc, col_text_muted);
                DrawTextW(mem_dc, L"mm", -1, &spacing_unit, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                draw_panel_button(panel_left, Metrics::apply_top, two_button_width, Metrics::apply_height,
                    is_vi ? L"Áp ngang" : L"Apply Horizontal",
                    workflow().can_distribute_selected(geometry::DistributeMode::SpacingHorizontal, distribute_spacing_mm_));
                draw_panel_button(panel_left + two_button_width + 6, Metrics::apply_top,
                    two_button_width, Metrics::apply_height,
                    is_vi ? L"Áp dọc" : L"Apply Vertical",
                    workflow().can_distribute_selected(geometry::DistributeMode::SpacingVertical, distribute_spacing_mm_));

                SetTextColor(mem_dc, col_text_muted);
                RECT panel_hint{panel_left, 570, panel_left + Metrics::content_width, 626};
                DrawTextW(mem_dc,
                    is_vi ? L"Chia khoảng hở tự động cần 3 đối tượng; khoảng hở cố định cần 2. Ctrl+Z để hoàn tác."
                          : L"Automatic gaps need 3 objects; fixed gaps need 2. Ctrl+Z to undo.",
                    -1, &panel_hint, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            } else if (show_style_inspector_) {
                paint_style_inspector(mem_dc, insp_x, content_y + 24, h - status_h);
            } else {
            // The Objects tab already names this section; a second heading overlaps it.
            content << (is_vi ? L"Số đối tượng: " : L"Selected objects: ") << selection.size();
            if (selection.size() == 1) {
                const auto sel_id = selection.front();
                const auto object = snapshot->objects().get(sel_id);
                if (object) {
                    const auto name = std::visit([](const auto& value) { return value.name; }, *object);
                    content << L"\n" << utf8_to_wide(name);
                    if (workflow().is_group(sel_id)) {
                        const auto& grp = std::get<document::GroupObject>(*object);
                        content << (is_vi ? L"\n[Nhóm đối tượng - " : L"\n[Group - ")
                                << grp.children_ids.size() << (is_vi ? L" hình]" : L" items]");
                    }
                    if (const auto* im = std::get_if<document::ImageObject>(&*object)) {
                        content << (is_vi ? L"\n[Ảnh Bitmap - " : L"\n[Raster Image - ")
                                << im->width_px << L"×" << im->height_px << (is_vi ? L" px]" : L" px]");
                    }
                    if (const auto bounds = workflow().get_object_bounds(sel_id)) {
                        content << L"\nX: " << std::fixed << std::setprecision(1) << bounds->x
                                << L" mm\nY: " << bounds->y << L" mm\nW: " << bounds->width
                                << L" mm\nH: " << bounds->height << L" mm";
                    }
                    vector::RectCornerSettings rcs;
                    if (is_selected_rectangle(nullptr, &rcs)) {
                        const wchar_t* type_name = rcs.type == vector::RectCornerType::Round
                            ? (is_vi ? L"Tròn (Fillet)" : L"Round")
                            : rcs.type == vector::RectCornerType::Scallop
                                ? (is_vi ? L"Lõm (Scallop)" : L"Scallop")
                                : (is_vi ? L"Vát (Chamfer)" : L"Chamfer");
                        content << (is_vi ? L"\n[Bo góc: " : L"\n[Corners: ") << type_name << L"]"
                                << L"\n  TL: " << std::fixed << std::setprecision(1) << rcs.tl << L" mm"
                                << L" · TR: " << rcs.tr << L" mm"
                                << L"\n  BL: " << rcs.bl << L" mm"
                                << L" · BR: " << rcs.br << L" mm"
                                << (rcs.locked ? (is_vi ? L" [Khóa]" : L" [Locked]") : L"")
                                << (rcs.scale_relative ? (is_vi ? L" [Tỷ lệ]" : L" [Scaled]") : L"");
                    }
                    if (workflow().is_group(sel_id) && !is_in_isolation_mode()) {
                        content << (is_vi ? L"\nNhấp đúp/Enter để chui vào nhóm."
                                          : L"\nDouble-click/Enter to edit inside.");
                    }
                }
            } else {
                content << (is_vi ? L"\nNhấp một đối tượng để xem kích thước (mm)."
                                  : L"\nSelect an object to inspect its size in mm.");
            }
            if (is_in_isolation_mode()) {
                content << (is_vi ? L"\n\n⚡ ĐANG CÔ LẬP NHÓM\nNhấn Esc để thoát."
                                  : L"\n\n⚡ GROUP ISOLATION MODE\nPress Esc to exit.");
            }
            RECT body = { insp_x + 14, content_y + 28, w - 14, content_y + 190 };
            const auto text = content.str();
            DrawTextW(mem_dc, text.c_str(), -1, &body, DT_LEFT | DT_NOPREFIX);
            SelectObject(mem_dc, font_secondary);
            SetTextColor(mem_dc, col_text_muted);
            RECT action_hint = { insp_x + 14, content_y + 198, w - 14, content_y + 254 };
            const wchar_t* action_hint_text = selection.size() >= 2
                ? (is_vi ? L"Lệnh căn chỉnh và tạo hình đủ điều kiện xuất hiện trên thanh ngữ cảnh."
                         : L"Eligible Align and Shaping commands appear on the context bar.")
                : (is_vi ? L"Chọn đối tượng để hiện lệnh căn chỉnh và tạo hình phù hợp."
                         : L"Select objects to show eligible Align and Shaping commands.");
            DrawTextW(mem_dc, action_hint_text, -1, &action_hint,
                      DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            }
        } else if (active_dock_tab_ == 1) {
            paint_layers_dock(mem_dc, insp_x, content_y, w, h, status_h);
        } else {
            DrawTextW(mem_dc, is_vi ? L"LỊCH SỬ LỆNH" : L"COMMAND HISTORY", -1,
                      &rc_head, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            content << L"Revision: " << snapshot->revision() << L"\n"
                    << (root().document().is_dirty()
                            ? (is_vi ? L"● Có thay đổi chưa lưu\n" : L"● Unsaved changes\n")
                            : (is_vi ? L"○ Đã lưu\n" : L"○ Saved\n"))
                    << (root().document().can_undo()
                            ? (is_vi ? L"● Có thể hoàn tác\n" : L"● Undo available\n")
                            : (is_vi ? L"○ Không có lệnh Undo\n" : L"○ Nothing to undo\n"))
                    << (root().document().can_redo()
                            ? (is_vi ? L"● Có thể làm lại" : L"● Redo available")
                            : (is_vi ? L"○ Không có lệnh Redo" : L"○ Nothing to redo"));
            RECT body = { insp_x + 14, content_y + 28, w - 14, content_y + 220 };
            const auto text = content.str();
            DrawTextW(mem_dc, text.c_str(), -1, &body, DT_LEFT | DT_NOPREFIX);
        }
}

#endif

} // namespace pikdraw::desktop
