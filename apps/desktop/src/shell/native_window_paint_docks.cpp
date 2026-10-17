#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_paint_icons.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "../tools/gradient_tool_controller.hpp"
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

        const int toolbox_tools[] = {0, quick_shapes_last_tool_, path_drawing_last_tool_, 3, 4, 5, 10};
        const int group_for_row[] = {0, 1, 2, 0, 0, 0, 3};
        for (int row = 0; row < 7; ++row) {
            const int tool = toolbox_tools[row];
            const int group = group_for_row[row];
            const int ty = content_top + 10 + row * 42;
            RECT rc_tb = { 6, ty, tool_w - 6, ty + 36 };
            const bool active = group == 1 ? (active_tool_ == 1 || active_tool_ == 2 || active_tool_ == 9)
                              : group == 2 ? (active_tool_ >= 6 && active_tool_ <= 8)
                              : group == 3 ? (toolbox_open_group_ == 3)
                                           : active_tool_ == tool;
            const int btn_cx = (rc_tb.left + rc_tb.right) / 2;
            const int btn_cy = (rc_tb.top + rc_tb.bottom) / 2;
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
                draw_tool_icon(mem_dc, tool, btn_cx, btn_cy, col_teal);
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
                draw_tool_icon(mem_dc, tool, btn_cx, btn_cy, col_text_muted);
            }
            if (group != 0) {
                draw_toolbox_flyout_marker(mem_dc, rc_tb.right - 5, rc_tb.bottom - 5,
                                           active ? col_teal : RGB(100, 116, 139));
            }
            if (toolbox_keyboard_focus_ && group == toolbox_focus_group_) {
                RECT focus = rc_tb;
                InflateRect(&focus, -3, -3);
                DrawFocusRect(mem_dc, &focus);
            }
        }

        if (toolbox_open_group_ >= 1 && toolbox_open_group_ <= 3) {
            const int group_row = toolbox_open_group_ == 1 ? 1
                                : toolbox_open_group_ == 2 ? 2 : 6;
            const int flyout_top = content_top + 10 + group_row * 42;
            const int flyout_left = 54;
            const int flyout_width = toolbox_open_group_ == 3 ? 208 : 188;
            const int flyout_height = toolbox_open_group_ == 3 ? 145 : 116;
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
                13};
            const wchar_t* labels_en[] = {L"Rectangle", L"Ellipse", L"Polygon", L""};
            const wchar_t* labels_vi[] = {L"Hình chữ nhật", L"Hình elip", L"Đa giác", L""};
            const wchar_t* path_en[] = {L"Line", L"Polyline", L"Pen", L""};
            const wchar_t* path_vi[] = {L"Đoạn thẳng", L"Đường gấp khúc", L"Pen", L""};
            const wchar_t* appearance_en[] = {L"Appearance Properties", L"Gaussian Blur", L"Drop Shadow", L"Gradient"};
            const wchar_t* appearance_vi[] = {L"Thuộc tính diện mạo", L"Gaussian Blur", L"Drop Shadow", L"Chuyển sắc"};
            const bool has_blur = appearance && workflow().selected_effect_index(
                document::GaussianBlurEffect{1.0, 1.0}).has_value();
            const bool has_shadow = appearance && workflow().selected_effect_index(
                document::DropShadowEffect{}).has_value();
            const int max_items = toolbox_open_group_ == 3 ? 4 : 3;
            for (int item = 0; item < max_items; ++item) {
                const int item_top = flyout_top + 26 + item * 29;
                const bool selected = appearance
                    ? (item == 1 && has_blur) || (item == 2 && has_shadow) || (item == 3 && active_tool_ == 13)
                    : active_tool_ == flyout_tools[item];
                const bool available = !appearance || item == 0 || item == 3 ||
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
                draw_tool_icon(mem_dc, flyout_tools[item], flyout_left + 20,
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
            if (show_align_panel_) {
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
            DrawTextW(mem_dc, is_vi ? L"THUỘC TÍNH ĐANG CHỌN" : L"SELECTION", -1,
                      &rc_head, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
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
            DrawTextW(mem_dc, is_vi ? L"ĐỐI TƯỢNG TRONG TÀI LIỆU" : L"DOCUMENT OBJECTS", -1,
                      &rc_head, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            size_t shown = 0;
            for (const auto& [id, object] : snapshot->objects().all()) {
                const auto name = std::visit([](const auto& value) { return value.name; }, object);
                content << L"• " << utf8_to_wide(name) << L"  (#" << id << L")\n";
                if (++shown == 20)
                    break;
            }
            if (snapshot->object_count() == 0)
                content << (is_vi ? L"Tài liệu chưa có đối tượng." : L"The document is empty.");
            else if (snapshot->object_count() > shown)
                content << L"…";
            RECT body = { insp_x + 14, content_y + 28, w - 14, h - status_h - 8 };
            const auto text = content.str();
            DrawTextW(mem_dc, text.c_str(), -1, &body, DT_LEFT | DT_NOPREFIX);
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
