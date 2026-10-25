#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_paint_icons.hpp"
#include "native_window_toolbox.hpp"
#include "native_window_gradient_properties_paint.hpp"
#include "native_window_text_properties.hpp"
#include "native_window_length_input.hpp"
#include "native_window_raster_dock_metrics.hpp"
#include "pikdraw/raster/analysis/histogram_service.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "../tools/gradient_tool_controller.hpp"
#include "../tools/dimension_tool_controller.hpp"
#include "../tools/annotation/annotation_tool_controller.hpp"
#include <algorithm>
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
    int topbar_h = 38, tabbar_h = 28, ribbon_h = 36, status_h = 26;
    int tool_w = toolbox_width();
    int content_top = topbar_h + tabbar_h + ribbon_h;

        // 5. Left Tool Strip: direct tools plus Quick Shapes, Path Drawing and Appearance groups.
        HBRUSH br_tools = CreateSolidBrush(col_header);
        RECT rc_tools = { 0, content_top, tool_w, h - status_h };
        FillRect(mem_dc, &rc_tools, br_tools);
        DeleteObject(br_tools);

        // Draw 1-column / 2-column Toggle Button at the top of toolbox strip
        {
            const RECT rc_tog = get_toolbox_toggle_columns_rect();
            HBRUSH br_tog = CreateSolidBrush(col_header);
            HPEN pen_tog = CreatePen(PS_SOLID, 1, col_border);
            const HGDIOBJ old_p = SelectObject(mem_dc, pen_tog);
            const HGDIOBJ old_b = SelectObject(mem_dc, br_tog);
            RoundRect(mem_dc, rc_tog.left, rc_tog.top, rc_tog.right, rc_tog.bottom, 4, 4);
            SelectObject(mem_dc, old_b);
            SelectObject(mem_dc, old_p);
            DeleteObject(pen_tog);
            DeleteObject(br_tog);

            SetTextColor(mem_dc, col_teal);
            SelectObject(mem_dc, font_secondary);
            RECT rc_txt = rc_tog;
            DrawTextW(mem_dc, toolbox_columns_ == 2 ? L"«" : L"»", -1, &rc_txt,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        const auto slots = visible_toolbox_slots();
        const int num_slots = static_cast<int>(slots.size());

        for (int i = 0; i < num_slots; ++i) {
            const auto slot = slots[i];
            const auto info = get_toolbox_slot_info(slot);
            const int group = info.group_id;
            const RECT rc_tb = get_toolbox_slot_rect(i);

            const bool active = (slot == ToolboxSlotId::Pick) ? (active_tool_ == 0)
                              : (slot == ToolboxSlotId::Node) ? (active_tool_ == 11)
                              : (slot == ToolboxSlotId::QuickShapes) ? (active_tool_ == 1 || active_tool_ == 2 || active_tool_ == 9)
                              : (slot == ToolboxSlotId::PathDrawing) ? ((active_tool_ >= 6 && active_tool_ <= 8))
                              : (slot == ToolboxSlotId::Pan) ? (active_tool_ == 3)
                              : (slot == ToolboxSlotId::Text) ? (active_tool_ == 4)
                              : (slot == ToolboxSlotId::Knife) ? (active_tool_ == 5)
                              : (slot == ToolboxSlotId::Blur) ? (toolbox_open_group_ == 3)
                              : (slot == ToolboxSlotId::DropShadow) ? false
                              : (slot == ToolboxSlotId::Contour) ? (active_tool_ == 12 || (contour_tool_ && contour_tool_->active()))
                              : (slot == ToolboxSlotId::Gradient) ? (active_tool_ == 13 || (gradient_tool_ && gradient_tool_->active()))
                              : (slot == ToolboxSlotId::Eyedropper) ? (active_tool_ == 15)
                              : (slot == ToolboxSlotId::Dimension) ? (active_tool_ == 10 || (dimension_tool_ && dimension_tool_->active()))
                              : (slot == ToolboxSlotId::Artboard) ? (active_tool_ == 14)
                              : (slot == ToolboxSlotId::Annotation) ? (active_tool_ == 17 || (annotation_tool_ && annotation_tool_->active()))
                              : false;

            const int btn_cx = (rc_tb.left + rc_tb.right) / 2;
            const int btn_cy = (rc_tb.top + rc_tb.bottom) / 2;
            const int icon = (slot == ToolboxSlotId::Node ||
                              (slot == ToolboxSlotId::PathDrawing && path_drawing_last_tool_ == 11)) ? 15
                           : (slot == ToolboxSlotId::Eyedropper) ? 18
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
                draw_toolbox_flyout_marker(mem_dc, rc_tb.right - 4, rc_tb.bottom - 4,
                                           active ? col_teal : RGB(100, 116, 139));
            }

            // Quick Remove badge in edit mode (except Pick)
            if (toolbox_edit_mode_ && slot != ToolboxSlotId::Pick) {
                const int badge_x = rc_tb.right - 5;
                const int badge_y = rc_tb.top + 5;
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

            if (toolbox_keyboard_focus_ && i == toolbox_focus_slot_index_) {
                RECT focus = rc_tb;
                InflateRect(&focus, -3, -3);
                DrawFocusRect(mem_dc, &focus);
            }
        }

        // Draw Quick Customize [+] button at bottom of toolbox
        {
            const RECT rc_plus = get_toolbox_plus_rect(num_slots);
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

        if (toolbox_open_group_ >= 1 && toolbox_open_group_ <= 7) {
            const ToolboxSlotId slot_for_group = toolbox_open_group_ == 1 ? ToolboxSlotId::QuickShapes
                                               : toolbox_open_group_ == 2 ? ToolboxSlotId::PathDrawing
                                               : toolbox_open_group_ == 3 ? ToolboxSlotId::Blur
                                               : toolbox_open_group_ == 4 ? ToolboxSlotId::Gradient
                                               : toolbox_open_group_ == 5 ? ToolboxSlotId::Eyedropper
                                               : toolbox_open_group_ == 6 ? ToolboxSlotId::Knife
                                               : ToolboxSlotId::Annotation;
            const int slot_idx = get_visible_index_for_slot(slot_for_group);
            const RECT slot_rc = get_toolbox_slot_rect(slot_idx >= 0 ? slot_idx : 0);
            const int flyout_top = slot_rc.top;
            const int flyout_left = tool_w + 4;
            const int flyout_width = (toolbox_open_group_ == 6 || toolbox_open_group_ == 7) ? 220 : (toolbox_open_group_ == 3 || toolbox_open_group_ == 5) ? 214 : 188;
            const int flyout_height = toolbox_open_group_ == 6 ? 90
                : toolbox_open_group_ == 7 ? 116
                : toolbox_open_group_ == 3 ? 174
                : (toolbox_open_group_ == 2) ? 145
                : (toolbox_open_group_ == 5) ? 116
                : (toolbox_open_group_ == 4) ? 58
                : 116;
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
            const bool gradient_grp = toolbox_open_group_ == 4;
            const bool eyedropper_grp = toolbox_open_group_ == 5;
            const bool cut_shape_grp = toolbox_open_group_ == 6;
            const bool annotation_grp = toolbox_open_group_ == 7;

            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT flyout_title{flyout_left + 10, flyout_top + 3, flyout.right - 8, flyout_top + 25};
            const wchar_t *title = annotation_grp
                ? (is_vi ? L"Chú thích · Annotation" : L"Annotation & Detail Views")
                : cut_shape_grp
                ? (is_vi ? L"Cắt & Tạo vùng · Cut & Shape" : L"Cut & Shape Tools")
                : quick_shapes
                ? (is_vi ? L"Hình cơ bản · Quick Shapes" : L"Quick Shapes")
                : path_drawing
                    ? (is_vi ? L"Đường vẽ · Path Drawing" : L"Path Drawing")
                    : gradient_grp
                        ? (is_vi ? L"Chuyển sắc · Gradient" : L"Gradient Tool")
                        : eyedropper_grp
                            ? (is_vi ? L"Hút màu & Sao chép kiểu" : L"Eyedropper & Sampler")
                            : (is_vi ? L"Hiệu ứng · Appearance" : L"Effects · Appearance");
            DrawTextW(mem_dc, title,
                -1, &flyout_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            const int flyout_tools[] = {
                quick_shapes ? 1 : path_drawing ? 6 : gradient_grp ? 13 : eyedropper_grp ? 18 : 10,
                quick_shapes ? 2 : path_drawing ? 7 : eyedropper_grp ? 13 : 11,
                quick_shapes ? 9 : path_drawing ? 8 : eyedropper_grp ? 10 : 12,
                appearance ? 14 : path_drawing ? 11 : 13,
                13};
            const wchar_t* labels_en[] = {L"Rectangle", L"Ellipse", L"Polygon", L"", L""};
            const wchar_t* labels_vi[] = {L"Hình chữ nhật", L"Hình elip", L"Đa giác", L"", L""};
            const wchar_t* path_en[] = {L"Line", L"Polyline", L"Pen", L"Node", L""};
            const wchar_t* path_vi[] = {L"Đoạn thẳng", L"Đường gấp khúc", L"Pen", L"Node", L""};
            const wchar_t* appearance_en[] = {L"Appearance Properties", L"Gaussian Blur", L"Drop Shadow", L"Contour", L"Gradient"};
            const wchar_t* appearance_vi[] = {L"Thuộc tính diện mạo", L"Gaussian Blur", L"Drop Shadow", L"Viền Contour", L"Chuyển sắc"};
            const wchar_t* gradient_en[] = {L"Interactive Gradient (G)", L"", L"", L"", L""};
            const wchar_t* gradient_vi[] = {L"Chuyển sắc tương tác (G)", L"", L"", L"", L""};
            const wchar_t* eyedropper_en[] = {L"Color Eyedropper (I)", L"Copy Gradient Paint", L"Copy Object Style", L"", L""};
            const wchar_t* eyedropper_vi[] = {L"Ống hút màu đơn sắc (I)", L"Sao chép màu Gradient", L"Sao chép thuộc tính đối tượng", L"", L""};
            const wchar_t* annotation_en[] = {L"Detail View", L"Leader Note", L"Plain Note"};
            const wchar_t* annotation_vi[] = {L"Ô chi tiết (Detail View)", L"Ghi chú mũi tên (Leader Note)", L"Ghi chú nhanh (Plain Note)"};

            const bool has_blur = appearance && workflow().selected_effect_index(
                document::GaussianBlurEffect{1.0, 1.0}).has_value();
            const bool has_shadow = appearance && workflow().selected_effect_index(
                document::DropShadowEffect{}).has_value();
            const bool is_contour = appearance && (active_tool_ == 12 || (contour_tool_ && contour_tool_->active()));
            const int max_items = toolbox_open_group_ == 6 ? 2
                : toolbox_open_group_ == 7 ? 3
                : toolbox_open_group_ == 3 ? 5
                : toolbox_open_group_ == 2 ? 4
                : toolbox_open_group_ == 5 ? 3
                : toolbox_open_group_ == 4 ? 1
                : 3;
            const wchar_t* cut_en[] = {L"Knife Tool (K)", L"Vector Eraser (Shift+E)"};
            const wchar_t* cut_vi[] = {L"Dao cắt Knife (K)", L"Tẩy vector (Shift+E)"};
            const int cut_tools[] = {5, 16};
            const int cut_icons[] = {5, 19};
            for (int item = 0; item < max_items; ++item) {
                const int item_top = flyout_top + 26 + item * 29;
                const bool selected = annotation_grp
                    ? (active_tool_ == 17 && annotation_tool_ && static_cast<int>(annotation_tool_->mode()) == item)
                    : cut_shape_grp
                    ? (active_tool_ == cut_tools[item])
                    : eyedropper_grp
                    ? (active_tool_ == 15 && eyedropper_mode_ == item)
                    : gradient_grp
                        ? (active_tool_ == 13)
                        : appearance
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
                const int icon = cut_shape_grp ? cut_icons[item]
                    : annotation_grp ? 20
                    : (path_drawing && flyout_tools[item] == 11)
                    ? 15 : flyout_tools[item];
                draw_tool_icon(mem_dc, icon, flyout_left + 20,
                               item_top + 14, !available ? col_text_muted : selected ? col_teal : col_text_white);
                SetTextColor(mem_dc, !available ? col_text_muted : selected ? col_teal : col_text_white);
                SelectObject(mem_dc, selected ? font_be_vietnam_bold : font_be_vietnam);
                RECT item_label{flyout_left + 35, item_top, flyout.right - 8, item_top + 28};
                const wchar_t* label = annotation_grp
                    ? (is_vi ? annotation_vi[item] : annotation_en[item])
                    : cut_shape_grp
                    ? (is_vi ? cut_vi[item] : cut_en[item])
                    : quick_shapes
                    ? (is_vi ? labels_vi[item] : labels_en[item])
                    : path_drawing
                        ? (is_vi ? path_vi[item] : path_en[item])
                        : gradient_grp
                            ? (is_vi ? gradient_vi[item] : gradient_en[item])
                            : eyedropper_grp
                                ? (is_vi ? eyedropper_vi[item] : eyedropper_en[item])
                                : (is_vi ? appearance_vi[item] : appearance_en[item]);
                DrawTextW(mem_dc, label, -1, &item_label,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
        }

        if (show_toolbox_customize_flyout_) {
            const RECT rc_plus = get_toolbox_plus_rect(num_slots);
            const int flyout_left = tool_w + 4;
            const int flyout_top = std::max(content_top + 10, std::min(static_cast<int>(rc_plus.top) - 200, h - status_h - 460));
            const int flyout_w = 264;
            const int flyout_h = 455;
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

            // Checklist of all toolbox slots
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
            const int bottom_y = flyout_top + 385;
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

int NativeWindow::inspector_width() const noexcept {
    return 280;
}

int NativeWindow::properties_dock_content_height() {
    if (active_dock_tab_ != 0) return 0;
    const auto snapshot = root().document().current_snapshot();
    const auto& selection = root().vector_controller().selection().selected_ids();
    if (selection.size() == 1 && snapshot) {
        const auto* selected = snapshot->objects().lookup_ref(selection.front());
        if (selected && std::holds_alternative<document::ImageObject>(*selected)) {
            int tab_h = 370;
            if (raster_adjustment_inspector_tab_ == 2) tab_h = 380;
            else if (raster_adjustment_inspector_tab_ == 3) tab_h = 340;
            else if (raster_adjustment_inspector_tab_ == 4) tab_h = 260;
            else if (raster_adjustment_inspector_tab_ == 5) tab_h = 300;
            else if (raster_adjustment_inspector_tab_ == 6) tab_h = 380;
            return 136 + 126 + 26 + tab_h + 30;
        }
        if (is_selected_rectangle()) {
            return 360;
        }
    }
    if (active_tool_ == 14) return 380;
    if (show_snap_properties_) return 420;
    if (active_tool_ == 9) return 320;
    if (has_text_properties_target(*this)) return 460;
    if (active_tool_ == 13) return 400;
    if (active_tool_ == 12) return 380;
    if (active_tool_ == 10) return 400;
    return 340;
}

void NativeWindow::scroll_properties_dock(int wheel_delta) {
    if (active_dock_tab_ != 0) return;
    const int topbar_h = 38, tabbar_h = 28, ribbon_h = 36, status_h = 26;
    const int content_top = topbar_h + tabbar_h + ribbon_h;
    const int viewport_height = std::max(0, (height_ - status_h) - (content_top + 28));
    const int max_scroll = std::max(0, properties_dock_content_height() - viewport_height);
    const int notches = wheel_delta / 120;
    if (notches == 0 || max_scroll <= 0) return;
    const int next = std::clamp(properties_dock_scroll_ - notches * 44, 0, max_scroll);
    if (next == properties_dock_scroll_) return;
    properties_dock_scroll_ = next;
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
        UpdateWindow(hwnd_);
    }
}

void NativeWindow::reset_properties_dock_scroll() noexcept {
    properties_dock_scroll_ = 0;
}

namespace {
struct DockClipGuard {
    HDC dc;
    int w, top, bottom, scroll, max_s, content_h;
    bool is_dark;
    DockClipGuard(HDC d, int left, int t, int right, int b, int s, int ms, int ch, bool dark)
        : dc(d), w(right), top(t), bottom(b), scroll(s), max_s(ms), content_h(ch), is_dark(dark) {
        HRGN rgn = CreateRectRgn(left, top, right - (ms > 0 ? 6 : 0), bottom);
        SelectClipRgn(dc, rgn);
        DeleteObject(rgn);
    }
    ~DockClipGuard() {
        SelectClipRgn(dc, nullptr);
        if (max_s > 0) {
            const int vh = std::max(1, bottom - top);
            const int sb_x = w - 5;
            const int sb_top = top + 4;
            const int sb_h = std::max(28, vh - 8);
            const int ch = std::max(vh + 1, content_h);
            const int thumb_h = std::clamp((sb_h * vh) / ch, 20, sb_h);
            const int thumb_y = sb_top + (scroll * (sb_h - thumb_h)) / max_s;
            RECT rc_thumb = { sb_x, thumb_y, w - 1, thumb_y + thumb_h };
            HBRUSH br_thumb = CreateSolidBrush(is_dark ? RGB(70, 85, 110) : RGB(180, 195, 210));
            FillRect(dc, &rc_thumb, br_thumb);
            DeleteObject(br_thumb);
        }
    }
};
} // namespace

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
    int topbar_h = 38, tabbar_h = 28, ribbon_h = 36, status_h = 26, inspect_w = inspector_width();
    int content_top = topbar_h + tabbar_h + ribbon_h;

    const auto snapshot = root().document().current_snapshot();
    const auto& selection = root().vector_controller().selection().selected_ids();
    bool histogram_requested = false;
    if (raster_histogram_service_ && active_dock_tab_ == 0 && active_tool_ == 0 &&
        !show_style_inspector_ && !show_align_panel_ && selection.size() == 1 &&
        snapshot) {
        const auto* selected = snapshot->objects().lookup_ref(selection.front());
        if (selected && std::holds_alternative<document::ImageObject>(*selected)) {
            const HWND notify_window = hwnd_;
            raster_histogram_service_->request(snapshot, selection.front(),
                [notify_window] {
                    if (notify_window)
                        (void)PostMessageW(notify_window,
                            kRasterHistogramWorkerMessage, 0, 0);
                }, raster::RasterHistogramMode::Edited);
            histogram_requested = true;
        }
    }
    if (raster_histogram_service_ && !histogram_requested) {
        raster_histogram_service_->cancel();
    }

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

        const int dock_viewport_top = content_top + 28;
        const int dock_viewport_bottom = h - status_h;
        const int dock_viewport_height = std::max(0, dock_viewport_bottom - dock_viewport_top);
        const int content_h = properties_dock_content_height();
        const int max_scroll = std::max(0, content_h - dock_viewport_height);
        if (active_dock_tab_ == 0) {
            properties_dock_scroll_ = std::clamp(properties_dock_scroll_, 0, max_scroll);
        } else {
            properties_dock_scroll_ = 0;
        }

        std::optional<DockClipGuard> clip_guard;
        if (active_dock_tab_ == 0) {
            clip_guard.emplace(mem_dc, insp_x, dock_viewport_top, w, dock_viewport_bottom,
                               properties_dock_scroll_, max_scroll, content_h, is_dark);
        }

        const int content_y = content_top + 36 - (active_dock_tab_ == 0 ? properties_dock_scroll_ : 0);
        SelectObject(mem_dc, font_be_vietnam_bold);
        SetTextColor(mem_dc, col_teal);
        RECT rc_head = { insp_x + 14, content_y,
                         active_dock_tab_ == 0 ? w - 100 : w - 14, content_y + 22 };
        SelectObject(mem_dc, font_secondary);
        SetTextColor(mem_dc, col_text_white);

        if (active_dock_tab_ == 0 && active_tool_ == 14) {
            paint_artboard_properties_panel(mem_dc, w, insp_x, content_y);
            return;
        }
        if (active_dock_tab_ == 0 && show_snap_properties_) {
            paint_snap_properties_panel(mem_dc, w, insp_x, content_y);
            return;
        }

        if (active_dock_tab_ == 0 && active_tool_ == 0 && !show_style_inspector_ && !show_align_panel_ &&
            paint_powerclip_controls(mem_dc, insp_x+14,content_y,w-14,true)) return;

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
                const auto formatted = format_mm_display(value, display_length_unit_, 7);
                const std::wstring shown = focused ? utf8_to_wide(num_input_buffer_) + L"|"
                    : enabled ? formatted
                    : node_count > 1 ? (is_vi ? L"Hỗn hợp" : L"Mixed") : L"—";
                RECT value_rect{input.left + 8, input.top, input.right - 8, input.bottom};
                SetTextColor(mem_dc, !enabled ? col_text_muted : focused ? col_teal : col_text_white);
                SelectObject(mem_dc, font_mono);
                DrawTextW(mem_dc, shown.c_str(), -1, &value_rect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            };
            const auto coordinate_suffix = L" (" + display_length_unit_suffix_wide(display_length_unit_) + L")";
            draw_coordinate(content_y + 48, (L"X" + coordinate_suffix).c_str(), NumericFieldId::ObjectX,
                            position ? position->x : 0.0);
            draw_coordinate(content_y + 88, (L"Y" + coordinate_suffix).c_str(), NumericFieldId::ObjectY,
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
            SetTextColor(mem_dc, col_teal);
            SelectObject(mem_dc, font_be_vietnam_bold);
            RECT seg_label{insp_x + 14, content_y + 174, w - 14, content_y + 192};
            DrawTextW(mem_dc, is_vi ? L"CHUYỂN ĐOẠN (SEGMENT)" : L"CONVERT SEGMENT", -1,
                      &seg_label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            const int two_btn_width = (AlignPanelMetrics::content_width - 6) / 2;
            const wchar_t* seg_names_vi[] = {L"╱ Đoạn thẳng", L"⌒ Đoạn cong"};
            const wchar_t* seg_names_en[] = {L"╱ Line", L"⌒ Curve"};
            for (int i = 0; i < 2; ++i) {
                const int left = insp_x + AlignPanelMetrics::content_left + i * (two_btn_width + 6);
                const bool enabled = node_count > 0;
                HBRUSH brush = CreateSolidBrush(!enabled ? col_header : col_btn_bg);
                HPEN pen = CreatePen(PS_SOLID, 1, !enabled ? col_border : col_btn_bdr);
                const auto old_pen = SelectObject(mem_dc, pen);
                const auto old_brush = SelectObject(mem_dc, brush);
                RoundRect(mem_dc, left, content_y + 196, left + two_btn_width, content_y + 224, 5, 5);
                SelectObject(mem_dc, old_brush);
                SelectObject(mem_dc, old_pen);
                DeleteObject(brush);
                DeleteObject(pen);
                RECT text{left + 2, content_y + 196, left + two_btn_width - 2, content_y + 224};
                SetTextColor(mem_dc, !enabled ? col_text_muted : col_text_white);
                SelectObject(mem_dc, font_be_vietnam);
                DrawTextW(mem_dc, is_vi ? seg_names_vi[i] : seg_names_en[i], -1,
                          &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }

            SetTextColor(mem_dc, col_teal);
            SelectObject(mem_dc, font_be_vietnam_bold);
            RECT ops_label{insp_x + 14, content_y + 234, w - 14, content_y + 252};
            DrawTextW(mem_dc, is_vi ? L"THAO TÁC ĐƯỜNG VẼ (ACTIONS)" : L"PATH OPERATIONS", -1,
                      &ops_label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            const wchar_t* ops_r1_vi[] = {L"+● Thêm", L"-● Xóa", L"✂ Tách"};
            const wchar_t* ops_r1_en[] = {L"+● Add", L"-● Delete", L"✂ Break"};
            for (int i = 0; i < 3; ++i) {
                const int left = insp_x + AlignPanelMetrics::content_left + i * (button_width + 6);
                const bool enabled = (i == 0) || (node_count > 0);
                HBRUSH brush = CreateSolidBrush(!enabled ? col_header : col_btn_bg);
                HPEN pen = CreatePen(PS_SOLID, 1, !enabled ? col_border : col_btn_bdr);
                const auto old_pen = SelectObject(mem_dc, pen);
                const auto old_brush = SelectObject(mem_dc, brush);
                RoundRect(mem_dc, left, content_y + 256, left + button_width, content_y + 284, 5, 5);
                SelectObject(mem_dc, old_brush);
                SelectObject(mem_dc, old_pen);
                DeleteObject(brush);
                DeleteObject(pen);
                RECT text{left + 2, content_y + 256, left + button_width - 2, content_y + 284};
                SetTextColor(mem_dc, !enabled ? col_text_muted : (i == 1 ? RGB(239, 68, 68) : col_text_white));
                SelectObject(mem_dc, font_be_vietnam);
                DrawTextW(mem_dc, is_vi ? ops_r1_vi[i] : ops_r1_en[i], -1,
                          &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }

            const wchar_t* ops_r2_vi[] = {L"☍ Nối", L"⟲ Đóng", L"⇄ Đảo"};
            const wchar_t* ops_r2_en[] = {L"☍ Join", L"⟲ Close", L"⇄ Reverse"};
            for (int i = 0; i < 3; ++i) {
                const int left = insp_x + AlignPanelMetrics::content_left + i * (button_width + 6);
                const bool enabled = node_count > 0;
                HBRUSH brush = CreateSolidBrush(!enabled ? col_header : col_btn_bg);
                HPEN pen = CreatePen(PS_SOLID, 1, !enabled ? col_border : col_btn_bdr);
                const auto old_pen = SelectObject(mem_dc, pen);
                const auto old_brush = SelectObject(mem_dc, brush);
                RoundRect(mem_dc, left, content_y + 290, left + button_width, content_y + 318, 5, 5);
                SelectObject(mem_dc, old_brush);
                SelectObject(mem_dc, old_pen);
                DeleteObject(brush);
                DeleteObject(pen);
                RECT text{left + 2, content_y + 290, left + button_width - 2, content_y + 318};
                SetTextColor(mem_dc, !enabled ? col_text_muted : col_text_white);
                SelectObject(mem_dc, font_be_vietnam);
                DrawTextW(mem_dc, is_vi ? ops_r2_vi[i] : ops_r2_en[i], -1,
                          &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }


        } else if (active_dock_tab_ == 0 &&
                   ((active_tool_ == 13 && gradient_tool_ && gradient_tool_->active()) ||
                    (active_tool_ == 12 && contour_tool_ && contour_tool_->active()))) {
        } else if (active_dock_tab_ == 0 && has_text_properties_target(*this)) {
            paint_text_properties_panel(mem_dc, insp_x, inspect_w, content_y, h - status_h,
                theme_, language_, text_properties_view(*this), active_num_field_, num_input_buffer_,
                display_length_unit_);
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
                    num_input_buffer_, effective_color_preview(), display_length_unit_);
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
                const auto spacing = format_mm_display(contour_tool_->spacing_mm(), display_length_unit_, 7);
                draw_field(104, is_vi ? L"Khoảng cách" : L"Spacing", spacing, NumericFieldId::ContourSpacing, can_edit);
                draw_field(140, is_vi ? L"Số bước" : L"Steps", std::to_wstring(contour_tool_->steps()),
                           NumericFieldId::ContourSteps, can_edit);
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
                draw_field(228, L"Miter limit", miter.str(), NumericFieldId::ContourMiterLimit,
                           can_edit && contour_tool_->join_type() == geometry::JoinType::Miter);
                const auto tolerance = format_mm_display(contour_tool_->arc_tolerance_mm(), display_length_unit_, 7);
                draw_field(264, is_vi ? L"Sai số cung" : L"Arc tolerance", tolerance,
                           NumericFieldId::ContourTolerance, can_edit);
                SelectObject(mem_dc, font_secondary); SetTextColor(mem_dc, col_text_muted);
                RECT hint{panel_left, content_y + 300, panel_right, content_y + 346};
                DrawTextW(mem_dc, is_vi
                    ? L"Độ chính xác tối thiểu 0,02 mm. Nhấn Enter hoặc nút Áp dụng để lưu viền vào đối tượng. Xóa để bỏ viền; Tách (Ctrl+K) thành các Path độc lập."
                    : L"Minimum tolerance is 0.02 mm. Press Enter or Apply to save contour to object. Remove deletes the contour; Break (Ctrl+K) creates independent Paths.",
                    -1, &hint, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                const int action_width = (panel_width - 18) / 4;
                const wchar_t* action_names_en[] = {L"Apply", L"Remove", L"Break", L"Cancel"};
                const wchar_t* action_names_vi[] = {L"Áp dụng", L"Xóa", L"Tách", L"Hủy"};
                for (int i = 0; i < 4; ++i)
                    draw_button(panel_left + i * (action_width + 6), content_y + 352, action_width,
                        is_vi ? action_names_vi[i] : action_names_en[i],
                        i == 0 ? can_edit && contour_tool_->has_preview() : i < 3 ? has_contour : can_edit);
            } else if ((active_tool_ == 10 || is_dimension_object_selected()) && !show_style_inspector_ && !show_align_panel_) {
                paint_dimension_properties_panel(mem_dc, insp_x, inspect_w, content_y, h - status_h);
                return;
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

                draw_heading(481, is_vi ? L"KHOẢNG HỞ CỐ ĐỊNH" : L"FIXED GAP");
                const bool spacing_editing = active_num_field_ == NumericFieldId::DistributeSpacing;
                std::wostringstream spacing_text;
                if (spacing_editing) spacing_text << utf8_to_wide(num_input_buffer_);
                else spacing_text << format_mm_display(distribute_spacing_mm_, display_length_unit_, 7);
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
                const auto gap_suffix = display_length_unit_suffix_wide(display_length_unit_);
                DrawTextW(mem_dc, gap_suffix.c_str(), -1, &spacing_unit, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

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
            const int panel_left = insp_x + 14;
            const int panel_width = (w - 14) - panel_left;
            const int half_w = (panel_width - 6) / 2;

            if (selection.empty()) {
                const auto& page = snapshot->page();
                content << (is_vi ? L"Khổ trang: " : L"Page size: ")
                        << format_length_pair_mm(page.width_mm, page.height_mm, display_length_unit_, 5);
                RECT body = { panel_left, content_y + 64, w - 14, content_y + 120 };
                const auto text = content.str();
                DrawTextW(mem_dc, text.c_str(), -1, &body, DT_LEFT | DT_NOPREFIX);
            } else {
                // Header: name & count
                SelectObject(mem_dc, font_secondary);
                SetTextColor(mem_dc, col_text_muted);
                RECT header_rc{panel_left, content_y + 4, w - 14, content_y + 24};
                std::wstring hdr_txt;
                if (selection.size() == 1) {
                    const auto sel_id = selection.front();
                    const auto object = snapshot->objects().get(sel_id);
                    if (object) {
                        const auto name = std::visit([](const auto& value) { return value.name; }, *object);
                        hdr_txt = utf8_to_wide(name);
                        if (workflow().is_group(sel_id)) {
                            const auto& grp = std::get<document::GroupObject>(*object);
                            hdr_txt += is_vi ? (L" [Nhóm - " + std::to_wstring(grp.children_ids.size()) + L" hình]")
                                            : (L" [Group - " + std::to_wstring(grp.children_ids.size()) + L" items]");
                        } else if (const auto* im = std::get_if<document::ImageObject>(&*object)) {
                            hdr_txt += L" [" + std::to_wstring(im->width_px) + L"×" + std::to_wstring(im->height_px) + L" px]";
                        }
                    }
                } else {
                    hdr_txt = is_vi ? (std::to_wstring(selection.size()) + L" đối tượng được chọn")
                                    : (std::to_wstring(selection.size()) + L" objects selected");
                }
                DrawTextW(mem_dc, hdr_txt.c_str(), -1, &header_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

                // Section: BIẾN ĐỔI (TRANSFORM)
                SelectObject(mem_dc, font_be_vietnam_bold);
                SetTextColor(mem_dc, col_teal);
                RECT transform_hdr{panel_left, content_y + 28, w - 14, content_y + 46};
                DrawTextW(mem_dc, is_vi ? L"BIẾN ĐỔI (TRANSFORM)" : L"TRANSFORM", -1,
                          &transform_hdr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                geometry::Rect2D bounds{0, 0, 0, 0};
                bool has_bounds = false;
                if (selection.size() == 1) {
                    if (const auto b = workflow().get_object_bounds(selection.front())) {
                        bounds = *b;
                        has_bounds = true;
                    }
                } else if (snapshot) {
                    if (const auto b = root().vector_controller().selection().bounds(*snapshot)) {
                        bounds = *b;
                        has_bounds = true;
                    }
                }

                double rot_deg = 0.0;
                if (selection.size() == 1 && snapshot) {
                    const auto node = snapshot->hierarchy().find(selection.front());
                    if (node) {
                        rot_deg = std::atan2(node->world.b, node->world.a) * (180.0 / 3.14159265358979323846);
                        if (rot_deg < 0.0) rot_deg += 360.0;
                    }
                }

                auto draw_transform_dock_field = [&](int left, int top, int width, const wchar_t* label,
                                                     const std::wstring& val_str, NumericFieldId field,
                                                     bool enabled) {
                    const bool focused = enabled && (active_num_field_ == field);
                    HBRUSH brush = CreateSolidBrush(focused ? col_active_tab : col_btn_bg);
                    HPEN pen = CreatePen(PS_SOLID, focused ? 2 : 1,
                        focused ? col_teal : (enabled ? col_btn_bdr : col_border));
                    const auto old_pen = SelectObject(mem_dc, pen);
                    const auto old_brush = SelectObject(mem_dc, brush);
                    RoundRect(mem_dc, left, top, left + width, top + 26, 4, 4);
                    SelectObject(mem_dc, old_brush);
                    SelectObject(mem_dc, old_pen);
                    DeleteObject(brush);
                    DeleteObject(pen);

                    const std::wstring shown = focused ? (utf8_to_wide(num_input_buffer_) + L"|") : val_str;
                    const std::wstring full = std::wstring(label) + L" " + shown;
                    RECT val_rect{left + 6, top, left + width - 6, top + 26};
                    SetTextColor(mem_dc, focused ? col_teal
                        : (enabled ? col_text_white : col_text_muted));
                    SelectObject(mem_dc, font_mono);
                    DrawTextW(mem_dc, full.c_str(), -1, &val_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                };

                const int tf_y = content_y + 50;
                // Row 1: X, Y
                draw_transform_dock_field(panel_left, tf_y, half_w, L"X",
                    has_bounds ? format_mm_display(bounds.x, display_length_unit_, 5) : L"—",
                    NumericFieldId::ObjectX, true);
                draw_transform_dock_field(panel_left + half_w + 6, tf_y, half_w, L"Y",
                    has_bounds ? format_mm_display(bounds.y, display_length_unit_, 5) : L"—",
                    NumericFieldId::ObjectY, true);

                // Row 2: W, H
                draw_transform_dock_field(panel_left, tf_y + 30, half_w, L"W",
                    has_bounds ? format_mm_display(bounds.width, display_length_unit_, 5) : L"—",
                    NumericFieldId::ObjectWidth, true);
                draw_transform_dock_field(panel_left + half_w + 6, tf_y + 30, half_w, L"H",
                    has_bounds ? format_mm_display(bounds.height, display_length_unit_, 5) : L"—",
                    NumericFieldId::ObjectHeight, true);

                // Row 3: Rotation & Lock Aspect Ratio
                std::wostringstream rot_ss;
                rot_ss << std::fixed << std::setprecision(1) << rot_deg;
                std::wstring rot_str = rot_ss.str();
                if (rot_str.size() > 2 && rot_str.substr(rot_str.size() - 2) == L".0")
                    rot_str.erase(rot_str.size() - 2);
                rot_str += L"°";
                draw_transform_dock_field(panel_left, tf_y + 60, half_w, L"∡", rot_str,
                    NumericFieldId::ObjectRotation, true);

                const auto* selected_object = selection.size() == 1 && snapshot
                    ? snapshot->objects().lookup_ref(selection.front()) : nullptr;
                const bool selected_bitmap = selected_object &&
                    std::holds_alternative<document::ImageObject>(*selected_object);
                const auto exposure_ev = selected_bitmap ? selected_bitmap_exposure_ev()
                                                         : std::optional<double>{};
                if (exposure_ev && can_edit_selected_bitmap(true)) {
                    std::wostringstream exposure_text;
                    exposure_text << std::fixed << std::setprecision(2) << *exposure_ev;
                    draw_transform_dock_field(panel_left + half_w + 6, tf_y + 60, half_w,
                        L"EV", exposure_text.str(), NumericFieldId::RasterExposureEv, true);
                } else {
                    // Aspect ratio lock toggle
                    HBRUSH lk_br = CreateSolidBrush(aspect_ratio_locked_ ? col_active_tab : col_btn_bg);
                    HPEN lk_pen = CreatePen(PS_SOLID, aspect_ratio_locked_ ? 2 : 1, aspect_ratio_locked_ ? col_teal : col_btn_bdr);
                    SelectObject(mem_dc, lk_pen);
                    SelectObject(mem_dc, lk_br);
                    RoundRect(mem_dc, panel_left + half_w + 6, tf_y + 60, panel_left + panel_width, tf_y + 86, 4, 4);
                    DeleteObject(lk_br);
                    DeleteObject(lk_pen);
                    RECT lk_txt_rc{panel_left + half_w + 8, tf_y + 60, panel_left + panel_width - 8, tf_y + 86};
                    SetTextColor(mem_dc, aspect_ratio_locked_ ? col_teal : col_text_white);
                    SelectObject(mem_dc, aspect_ratio_locked_ ? font_be_vietnam_bold : font_be_vietnam);
                    DrawTextW(mem_dc, aspect_ratio_locked_ ? (is_vi ? L"🔒 Tỉ lệ cố định" : L"🔒 Locked Ratio")
                                                           : (is_vi ? L"🔓 Tỉ lệ tự do" : L"🔓 Free Ratio"),
                              -1, &lk_txt_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }

                if (selection.size() == 1 && snapshot && raster_histogram_service_) {
                    paint_raster_inspector_dock(mem_dc, panel_left, panel_width, content_y);
                }
            }

            vector::RectCornerSettings rcs;
            const bool is_rect_single = selection.size() == 1 && is_selected_rectangle(nullptr, &rcs);
            const bool is_rect_tool = selection.empty() && active_tool_ == 1;
            if (is_rect_single || is_rect_tool) {
                if (is_rect_tool) {
                    rcs.type = creation_rect_corner_type_;
                    rcs.tl = creation_rect_corner_tl_;
                    rcs.tr = creation_rect_corner_tr_;
                    rcs.bl = creation_rect_corner_bl_;
                    rcs.br = creation_rect_corner_br_;
                    rcs.locked = creation_rect_corners_locked_;
                    rcs.scale_relative = creation_rect_corners_scale_relative_;
                }
                const int corner_y = content_y + (is_rect_tool ? 48 : 155);

                SelectObject(mem_dc, font_be_vietnam_bold);
                SetTextColor(mem_dc, col_teal);
                RECT corner_hdr{panel_left, corner_y - 20, w - 14, corner_y - 2};
                DrawTextW(mem_dc, is_vi ? L"BO GÓC CHỮ NHẬT" : L"RECTANGLE CORNERS", -1,
                          &corner_hdr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // 3 Mode buttons (Round, Scallop, Chamfer)
                const int triple_w = (panel_width - 12) / 3;
                const wchar_t* mode_lbls_vi[] = {L"Tròn", L"Lõm", L"Vát"};
                const wchar_t* mode_lbls_en[] = {L"Round", L"Scallop", L"Chamfer"};
                constexpr vector::RectCornerType ctypes[] = {
                    vector::RectCornerType::Round, vector::RectCornerType::Scallop, vector::RectCornerType::Chamfer
                };
                for (int i = 0; i < 3; ++i) {
                    const int btn_x = panel_left + i * (triple_w + 6);
                    const bool active = rcs.type == ctypes[i];
                    HBRUSH brush = CreateSolidBrush(active ? col_active_tab : col_btn_bg);
                    HPEN pen = CreatePen(PS_SOLID, active ? 2 : 1, active ? col_teal : col_btn_bdr);
                    const auto old_pen = SelectObject(mem_dc, pen);
                    const auto old_brush = SelectObject(mem_dc, brush);
                    RoundRect(mem_dc, btn_x, corner_y, btn_x + triple_w, corner_y + 26, 4, 4);
                    SelectObject(mem_dc, old_brush);
                    SelectObject(mem_dc, old_pen);
                    DeleteObject(brush);
                    DeleteObject(pen);
                    RECT txt_rect{btn_x + 2, corner_y, btn_x + triple_w - 2, corner_y + 26};
                    SetTextColor(mem_dc, active ? col_teal : col_text_white);
                    SelectObject(mem_dc, active ? font_be_vietnam_bold : font_be_vietnam);
                    DrawTextW(mem_dc, is_vi ? mode_lbls_vi[i] : mode_lbls_en[i], -1,
                              &txt_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }

                // 4 Interactive numeric inputs (TL, TR on Row 1; BL, BR on Row 2)
                const int half_w = (panel_width - 6) / 2;
                auto draw_corner_dock_input = [&](int left, int top, int width, const wchar_t* label,
                                                 double val_mm, NumericFieldId field) {
                    const bool focused = active_num_field_ == field;
                    HBRUSH brush = CreateSolidBrush(focused ? col_active_tab : col_btn_bg);
                    HPEN pen = CreatePen(PS_SOLID, focused ? 2 : 1, focused ? col_teal : col_btn_bdr);
                    const auto old_pen = SelectObject(mem_dc, pen);
                    const auto old_brush = SelectObject(mem_dc, brush);
                    RoundRect(mem_dc, left, top, left + width, top + 26, 4, 4);
                    SelectObject(mem_dc, old_brush);
                    SelectObject(mem_dc, old_pen);
                    DeleteObject(brush);
                    DeleteObject(pen);

                    const std::wstring val_str = focused ? utf8_to_wide(num_input_buffer_) + L"|"
                                                         : format_mm_display(val_mm, display_length_unit_, 5);
                    const std::wstring full_str = std::wstring(label) + L" " + val_str;
                    RECT val_rect{left + 6, top, left + width - 6, top + 26};
                    SetTextColor(mem_dc, focused ? col_teal : col_text_white);
                    SelectObject(mem_dc, font_mono);
                    DrawTextW(mem_dc, full_str.c_str(), -1, &val_rect,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                };

                draw_corner_dock_input(panel_left, corner_y + 32, half_w, L"TL ⌜:", rcs.tl, NumericFieldId::RectCornerRadiusTL);
                draw_corner_dock_input(panel_left + half_w + 6, corner_y + 32, half_w, L"TR ⌝:", rcs.tr, NumericFieldId::RectCornerRadiusTR);
                draw_corner_dock_input(panel_left, corner_y + 64, half_w, L"BL ⌞:", rcs.bl, NumericFieldId::RectCornerRadiusBL);
                draw_corner_dock_input(panel_left + half_w + 6, corner_y + 64, half_w, L"BR ⌟:", rcs.br, NumericFieldId::RectCornerRadiusBR);

                // Toggles Row: Lock & Scale
                auto draw_toggle_btn = [&](int left, int top, int width, const wchar_t* label, bool active) {
                    HBRUSH brush = CreateSolidBrush(active ? col_active_tab : col_btn_bg);
                    HPEN pen = CreatePen(PS_SOLID, active ? 2 : 1, active ? col_teal : col_btn_bdr);
                    const auto old_pen = SelectObject(mem_dc, pen);
                    const auto old_brush = SelectObject(mem_dc, brush);
                    RoundRect(mem_dc, left, top, left + width, top + 26, 4, 4);
                    SelectObject(mem_dc, old_brush);
                    SelectObject(mem_dc, old_pen);
                    DeleteObject(brush);
                    DeleteObject(pen);
                    RECT txt_rect{left + 2, top, left + width - 2, top + 26};
                    SetTextColor(mem_dc, active ? col_teal : col_text_white);
                    SelectObject(mem_dc, active ? font_be_vietnam_bold : font_be_vietnam);
                    DrawTextW(mem_dc, label, -1, &txt_rect,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                };
                draw_toggle_btn(panel_left, corner_y + 98, half_w,
                                rcs.locked ? (is_vi ? L"🔒 Đã khóa 4 góc" : L"🔒 Locked")
                                           : (is_vi ? L"🔓 Mở khóa góc" : L"🔓 Unlocked"), rcs.locked);
                draw_toggle_btn(panel_left + half_w + 6, corner_y + 98, half_w,
                                is_vi ? L"⇲ Theo tỉ lệ" : L"⇲ Relative", rcs.scale_relative);

                // Action: Convert to curves
                if (is_rect_single) {
                    HBRUSH brush = CreateSolidBrush(col_btn_bg);
                    HPEN pen = CreatePen(PS_SOLID, 1, col_btn_bdr);
                    const auto old_pen = SelectObject(mem_dc, pen);
                    const auto old_brush = SelectObject(mem_dc, brush);
                    RoundRect(mem_dc, panel_left, corner_y + 132, panel_left + panel_width, corner_y + 158, 4, 4);
                    SelectObject(mem_dc, old_brush);
                    SelectObject(mem_dc, old_pen);
                    DeleteObject(brush);
                    DeleteObject(pen);
                    RECT txt_rect{panel_left + 2, corner_y + 132, panel_left + panel_width - 2, corner_y + 158};
                    SetTextColor(mem_dc, col_teal);
                    SelectObject(mem_dc, font_be_vietnam_bold);
                    DrawTextW(mem_dc, is_vi ? L"Chuyển thành đường cong (Ctrl+Q)" : L"Convert to Curves (Ctrl+Q)",
                              -1, &txt_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
            }
            if (selection.empty()) {
                const int panel_left = insp_x + AlignPanelMetrics::content_left;
                const int row_top = content_y + PageUnitPropertiesMetrics::row_top_in_content;
                RECT label{panel_left, row_top, panel_left + PageUnitPropertiesMetrics::field_left_in_content - 6,
                           row_top + PageUnitPropertiesMetrics::row_height};
                SetTextColor(mem_dc, col_text_white);
                SelectObject(mem_dc, font_be_vietnam);
                DrawTextW(mem_dc, is_vi ? L"Đơn vị trang" : L"Page units", -1, &label,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

                const int field_left = panel_left + PageUnitPropertiesMetrics::field_left_in_content;
                RECT field{field_left, row_top, panel_left + AlignPanelMetrics::content_width,
                           row_top + PageUnitPropertiesMetrics::row_height};
                HBRUSH field_brush = CreateSolidBrush(show_page_units_flyout_ ? col_active_tab : col_btn_bg);
                HPEN field_pen = CreatePen(PS_SOLID, show_page_units_flyout_ ? 2 : 1,
                                           show_page_units_flyout_ ? col_teal : col_btn_bdr);
                const auto old_field_pen = SelectObject(mem_dc, field_pen);
                const auto old_field_brush = SelectObject(mem_dc, field_brush);
                RoundRect(mem_dc, field.left, field.top, field.right, field.bottom, 5, 5);
                SelectObject(mem_dc, old_field_brush);
                SelectObject(mem_dc, old_field_pen);
                DeleteObject(field_brush);
                DeleteObject(field_pen);
                RECT field_text{field.left + 8, field.top, field.right - 8, field.bottom};
                const auto unit_label = display_length_unit_suffix_wide(display_length_unit_);
                DrawTextW(mem_dc, unit_label.c_str(), -1, &field_text,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                RECT field_chevron{field.right - 24, field.top, field.right - 6, field.bottom};
                DrawTextW(mem_dc, show_page_units_flyout_ ? L"▴" : L"▾", -1, &field_chevron,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            const bool is_bitmap_single = selection.size() == 1 && snapshot &&
                snapshot->objects().lookup_ref(selection.front()) &&
                std::holds_alternative<document::ImageObject>(
                    *snapshot->objects().lookup_ref(selection.front()));
            if (!is_rect_single && !is_rect_tool && !is_bitmap_single) {
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
            if (selection.empty() && show_page_units_flyout_) {
                constexpr MenuActionId unit_actions[] = {
                    MenuActionId::LayoutUnitMillimeter, MenuActionId::LayoutUnitCentimeter,
                    MenuActionId::LayoutUnitMeter, MenuActionId::LayoutUnitInch,
                    MenuActionId::LayoutUnitFoot, MenuActionId::LayoutUnitYard,
                    MenuActionId::LayoutUnitPoint, MenuActionId::LayoutUnitPica};
                const int menu_left = insp_x + AlignPanelMetrics::content_left;
                const int menu_top = content_y + PageUnitPropertiesMetrics::row_top_in_content +
                    PageUnitPropertiesMetrics::row_height + PageUnitPropertiesMetrics::menu_gap;
                RECT menu{menu_left, menu_top, menu_left + PageUnitPropertiesMetrics::menu_width,
                          menu_top + 8 * PageUnitPropertiesMetrics::item_height};
                HBRUSH menu_brush = CreateSolidBrush(is_dark ? RGB(20, 31, 46) : RGB(250, 252, 255));
                HPEN menu_pen = CreatePen(PS_SOLID, 1, col_border);
                const auto old_menu_pen = SelectObject(mem_dc, menu_pen);
                const auto old_menu_brush = SelectObject(mem_dc, menu_brush);
                RoundRect(mem_dc, menu.left, menu.top, menu.right, menu.bottom, 5, 5);
                SelectObject(mem_dc, old_menu_brush);
                SelectObject(mem_dc, old_menu_pen);
                DeleteObject(menu_brush);
                DeleteObject(menu_pen);
                for (int index = 0; index < 8; ++index) {
                    const auto item = std::find_if(kLayoutMenuItems.begin(), kLayoutMenuItems.end(),
                        [action = unit_actions[index]](const MenuItem& candidate) { return candidate.action == action; });
                    if (item == kLayoutMenuItems.end()) continue;
                    const int item_top = menu_top + index * PageUnitPropertiesMetrics::item_height;
                    RECT item_rect{menu.left + 4, item_top, menu.right - 4,
                                   item_top + PageUnitPropertiesMetrics::item_height};
                    const bool selected_unit = static_cast<int>(display_length_unit_) == index;
                    const bool focused_item = page_units_flyout_focus_index_ == index;
                    if (selected_unit || focused_item) {
                        HBRUSH selected_brush = CreateSolidBrush(col_active_tab);
                        FillRect(mem_dc, &item_rect, selected_brush);
                        DeleteObject(selected_brush);
                    }
                    RECT item_label{item_rect.left + 8, item_rect.top, item_rect.right - 8, item_rect.bottom};
                    SetTextColor(mem_dc, selected_unit || focused_item ? col_teal : col_text_white);
                    SelectObject(mem_dc, selected_unit || focused_item ? font_be_vietnam_bold : font_be_vietnam);
                    DrawTextW(mem_dc, is_vi ? item->label_vi : item->label_en, -1, &item_label,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    if (selected_unit) {
                        RECT check{item_rect.right - 24, item_rect.top, item_rect.right - 6, item_rect.bottom};
                        DrawTextW(mem_dc, L"✓", -1, &check,
                                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    }
                }
            }
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
