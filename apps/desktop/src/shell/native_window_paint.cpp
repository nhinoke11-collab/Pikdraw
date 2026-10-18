#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/shell/native_font_picker.hpp"
#include "native_window_paint_icons.hpp"
#include "native_window_text_properties.hpp"
#include "native_window_file_menu.hpp"
#include "native_window_menus.hpp"
#include "native_window_page_presets.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "../tools/gradient_tool_controller.hpp"
#include "native_window_drop_shadow_paint.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <numbers>
#include <type_traits>
#include <variant>

namespace pikdraw::desktop {

#ifdef _WIN32

void NativeWindow::paint(HDC hdc, int w, int h) {
    if (text_editor_ && (active_tool_ != 4 || text_editor_->base != root().document().current_snapshot()))
        refresh_inline_text_preview();
    ensure_ui_fonts();
    const int device_w = dip(w), device_h = dip(h);

    if (!backbuffer_dc_) {
        backbuffer_dc_ = CreateCompatibleDC(hdc);
    }
    if (backbuffer_bmp_ && (device_w != backbuffer_w_ || device_h != backbuffer_h_)) {
        SelectObject(backbuffer_dc_, backbuffer_old_bmp_);
        DeleteObject(backbuffer_bmp_);
        backbuffer_bmp_ = nullptr;
        backbuffer_old_bmp_ = nullptr;
    }
    if (!backbuffer_bmp_) {
        backbuffer_bmp_ = CreateCompatibleBitmap(hdc, device_w, device_h);
        backbuffer_old_bmp_ = SelectObject(backbuffer_dc_, backbuffer_bmp_);
        backbuffer_w_ = device_w;
        backbuffer_h_ = device_h;
    }
    HDC mem_dc = backbuffer_dc_;
    const int saved_dc = SaveDC(mem_dc);
    configure_ui_dc(mem_dc);

    HFONT font_be_vietnam = font_be_vietnam_;
    HFONT font_be_vietnam_bold = font_be_vietnam_bold_;
    HFONT font_secondary = font_secondary_;
    HFONT font_mono = font_mono_;

    HGDIOBJ old_font = SelectObject(mem_dc, font_be_vietnam);

    bool is_dark = (theme_ == ThemeMode::CyberDark);
    bool is_vi = (language_ == UILanguage::Vietnamese);

    // 2026 Dual Theme Palette: Dark (Obsidian Teal) / Light (Silver Tech)
    COLORREF col_canvas = canvas_background_colorref();
    COLORREF col_header = is_dark ? RGB(12, 19, 29) : RGB(255, 255, 255);
    COLORREF col_ribbon = is_dark ? RGB(14, 22, 34) : RGB(248, 250, 252);
    COLORREF col_border = is_dark ? RGB(29, 38, 54) : RGB(203, 213, 225);
    COLORREF col_teal = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    COLORREF col_text_white = is_dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    COLORREF col_text_muted = is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    COLORREF col_active_tab = is_dark ? RGB(22, 37, 54) : RGB(236, 242, 248);
    COLORREF col_btn_bg = is_dark ? RGB(18, 28, 42) : RGB(241, 245, 249);
    COLORREF col_btn_bdr = is_dark ? RGB(29, 38, 54) : RGB(203, 213, 225);

    int topbar_h = 38, tabbar_h = 28, ribbon_h = 36, status_h = 26, tool_w = 52, inspect_w = 280;
    int content_top = topbar_h + tabbar_h + ribbon_h;

    SetBkMode(mem_dc, TRANSPARENT);

    if (show_home_screen_) {
        // Render Home Screen (Trang Home)
        paint_home_screen(mem_dc, w, h);
    } else {
        // Canvas is rasterized from the current immutable document snapshot.
        HBRUSH br_canvas = CreateSolidBrush(col_canvas);
        RECT rc_full = { 0, 0, w, h };
        FillRect(mem_dc, &rc_full, br_canvas);
        DeleteObject(br_canvas);
        paint_document_canvas(mem_dc, w, h);

        // A focused Drop Shadow exposes a direct-manipulation handle. Its draft preview
        // is drawn at the candidate offset and commits only when the drag is released.
        const auto& effect_selection = root().vector_controller().selection().selected_ids();
        const auto effect_snapshot = root().document().current_snapshot();
        if (active_dock_tab_ == 0 && show_style_inspector_ && effect_selection.size() == 1 &&
            effect_snapshot && workflow().can_edit_selected_effect(property_effect_index_)) {
            const auto effect_appearance = selection_appearance(*effect_snapshot, effect_selection);
            if (effect_appearance.can_edit_effect(property_effect_index_)) {
                const auto& selected_effect = effect_appearance.effects[property_effect_index_];
                const auto* shadow = std::get_if<document::DropShadowEffect>(
                    &selected_effect.parameters);
                const auto draft = workflow().effect_draft();
                if (draft && draft->editing_existing &&
                    property_effect_index_ < effect_appearance.effects.size()) {
                    if (const auto* draft_shadow = std::get_if<document::DropShadowEffect>(
                            &draft->parameters))
                        shadow = draft_shadow;
                }
                const auto bounds = root().vector_controller().selection().bounds(*effect_snapshot);
                if (shadow && bounds) {
                    const auto center = bounds->center();
                    const geometry::Point2D handle_world{
                        center.x + shadow->dx_mm, center.y + shadow->dy_mm};
                    const auto area = canvas_rect(w, h);
                    const auto center_screen = root().renderer().camera().world_to_screen(center);
                    const auto handle_screen = root().renderer().camera().world_to_screen(handle_world);
                    const int cx = area.left + static_cast<int>(std::lround(center_screen.x));
                    const int cy = area.top + static_cast<int>(std::lround(center_screen.y));
                    const int hx = area.left + static_cast<int>(std::lround(handle_screen.x));
                    const int hy = area.top + static_cast<int>(std::lround(handle_screen.y));
                    const auto handle_pen = CreatePen(PS_SOLID, 2, col_teal);
                    const auto old_handle_pen = SelectObject(mem_dc, handle_pen);
                    MoveToEx(mem_dc, cx, cy, nullptr);
                    LineTo(mem_dc, hx, hy);
                    const auto handle_brush = CreateSolidBrush(col_text_white);
                    const auto old_handle_brush = SelectObject(mem_dc, handle_brush);
                    Ellipse(mem_dc, hx - 7, hy - 7, hx + 8, hy + 8);
                    SelectObject(mem_dc, old_handle_brush);
                    SelectObject(mem_dc, old_handle_pen);
                    DeleteObject(handle_brush);
                    DeleteObject(handle_pen);
                }
            }
        }

        // Gradient Tool on-canvas handles overlay
        if (active_tool_ == 13 && gradient_tool_ && gradient_tool_->active()) {
            const auto handles = gradient_tool_->tool().get_handles();
            if (!handles.empty()) {
                const auto area = canvas_rect(w, h);
                const auto pen_axis = CreatePen(PS_DASH, 1, col_teal);
                const auto old_pen = SelectObject(mem_dc, pen_axis);

                geometry::Point2D p_start{0, 0};
                geometry::Point2D p_end{0, 0};
                bool has_axis = false;
                for (const auto& handle : handles) {
                    if (handle.kind == vector::tools::GradientHandleKind::LinearStart ||
                        handle.kind == vector::tools::GradientHandleKind::RadialCenter) {
                        p_start = handle.position_object;
                        has_axis = true;
                    } else if (handle.kind == vector::tools::GradientHandleKind::LinearEnd ||
                               handle.kind == vector::tools::GradientHandleKind::RadialRadius) {
                        p_end = handle.position_object;
                    }
                }
                if (has_axis) {
                    const auto s_screen = root().renderer().camera().world_to_screen(p_start);
                    const auto e_screen = root().renderer().camera().world_to_screen(p_end);
                    MoveToEx(mem_dc, area.left + static_cast<int>(std::lround(s_screen.x)),
                             area.top + static_cast<int>(std::lround(s_screen.y)), nullptr);
                    LineTo(mem_dc, area.left + static_cast<int>(std::lround(e_screen.x)),
                           area.top + static_cast<int>(std::lround(e_screen.y)));
                }
                SelectObject(mem_dc, old_pen);
                DeleteObject(pen_axis);

                for (const auto& handle : handles) {
                    const auto screen_pt = root().renderer().camera().world_to_screen(handle.position_object);
                    const int hx = area.left + static_cast<int>(std::lround(screen_pt.x));
                    const int hy = area.top + static_cast<int>(std::lround(screen_pt.y));
                    COLORREF fill_color = col_text_white;
                    if (handle.kind == vector::tools::GradientHandleKind::Stop) {
                        fill_color = RGB(255, 200, 50);
                    } else if (handle.kind == vector::tools::GradientHandleKind::LinearStart ||
                               handle.kind == vector::tools::GradientHandleKind::RadialCenter) {
                        fill_color = col_teal;
                    }
                    const auto h_brush = CreateSolidBrush(fill_color);
                    const auto h_pen = CreatePen(PS_SOLID, 1, RGB(20, 24, 33));
                    const auto op = SelectObject(mem_dc, h_pen);
                    const auto ob = SelectObject(mem_dc, h_brush);
                    if (handle.kind == vector::tools::GradientHandleKind::Stop) {
                        POINT pts[4] = {{hx, hy - 5}, {hx + 5, hy}, {hx, hy + 5}, {hx - 5, hy}};
                        Polygon(mem_dc, pts, 4);
                    } else {
                        Ellipse(mem_dc, hx - 5, hy - 5, hx + 6, hy + 6);
                    }
                    SelectObject(mem_dc, ob);
                    SelectObject(mem_dc, op);
                    DeleteObject(h_pen);
                    DeleteObject(h_brush);
                }
            }
        }

        // Document Tab Bar
        paint_document_tab_bar(mem_dc, w, topbar_h, tabbar_h);

        // Context Tool Ribbon
        HBRUSH br_ribbon = CreateSolidBrush(col_ribbon);
        RECT rc_ribbon = { 0, topbar_h + tabbar_h, w, content_top };
        FillRect(mem_dc, &rc_ribbon, br_ribbon);
        DeleteObject(br_ribbon);

        const auto snapshot = root().document().current_snapshot();
        const auto& selection = root().vector_controller().selection().selected_ids();

        auto draw_rib_btn = [&](int left, int right, const wchar_t* label, bool active, COLORREF text_col) {
            RECT rect{left, topbar_h + tabbar_h + 4, right, content_top - 4};
            HBRUSH brush = CreateSolidBrush(active ? col_active_tab : col_btn_bg);
            HPEN pen = CreatePen(PS_SOLID, 1, active ? col_teal : col_btn_bdr);
            HGDIOBJ old_p = SelectObject(mem_dc, pen);
            HGDIOBJ old_b = SelectObject(mem_dc, brush);
            RoundRect(mem_dc, rect.left, rect.top, rect.right, rect.bottom, 4, 4);
            SelectObject(mem_dc, old_p);
            SelectObject(mem_dc, old_b);
            DeleteObject(pen);
            DeleteObject(brush);
            SetTextColor(mem_dc, text_col);
            SelectObject(mem_dc, active ? font_be_vietnam_bold : font_be_vietnam);
            DrawTextW(mem_dc, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        };

        auto draw_num_input = [&](int left, int right, const wchar_t* prefix,
                                  const std::string& current_val, NumericFieldId field_id,
                                  bool append_mm = true) {
            bool is_focused = (active_num_field_ == field_id);
            RECT rect{left, topbar_h + tabbar_h + 4, right, content_top - 4};
            HBRUSH brush = CreateSolidBrush(is_focused ? col_active_tab : col_btn_bg);
            HPEN pen = CreatePen(PS_SOLID, is_focused ? 2 : 1, is_focused ? col_teal : col_btn_bdr);
            HGDIOBJ old_p = SelectObject(mem_dc, pen);
            HGDIOBJ old_b = SelectObject(mem_dc, brush);
            RoundRect(mem_dc, rect.left, rect.top, rect.right, rect.bottom, 4, 4);
            SelectObject(mem_dc, old_p);
            SelectObject(mem_dc, old_b);
            DeleteObject(pen);
            DeleteObject(brush);

            std::wstring display_text;
            if (prefix && *prefix) {
                display_text += prefix;
                display_text += L" ";
            }
            if (is_focused) {
                display_text += utf8_to_wide(num_input_buffer_);
                display_text += L"|";
            } else {
                display_text += utf8_to_wide(current_val);
                if (append_mm)
                    display_text += L" mm";
            }

            SetTextColor(mem_dc, is_focused ? col_teal : col_text_white);
            SelectObject(mem_dc, font_mono);
            RECT rc_txt = { rect.left + 4, rect.top, rect.right - 4, rect.bottom };
            DrawTextW(mem_dc, display_text.c_str(), -1, &rc_txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        };

        const auto opacity_percent_text = [&](double opacity) {
            std::ostringstream value;
            value << std::fixed << std::setprecision(1) << opacity * 100.0;
            std::string text = value.str();
            if (text.size() > 2 && text.substr(text.size() - 2) == ".0")
                text.erase(text.size() - 2);
            return text + "%";
        };

        bool is_text_mode = (active_tool_ == 4 || is_inline_text_editing_ || is_text_object_selected());

        if ((active_tool_ == 8 && pen_tool_ && pen_tool_->active()) ||
            ((active_tool_ == 6 || active_tool_ == 7) && drawing_tool_ && drawing_tool_->active())) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 48, content_top};
            const wchar_t* tool_name = (active_tool_ == 8) ? L"Pen:" : (active_tool_ == 6 ? L"Line:" : L"Poly:");
            DrawTextW(mem_dc, tool_name, -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            draw_num_input(52, 185, L"L:", pen_numeric_length_, NumericFieldId::PenLength, true);
            draw_num_input(192, 315, is_vi ? L"Góc:" : L"Angle:", pen_numeric_angle_, NumericFieldId::PenAngle, false);

            draw_rib_btn(325, 415, is_vi ? L"↵ Kết thúc" : L"↵ Finish",
                         false, col_teal);
            draw_rib_btn(422, 492, is_vi ? L"Esc Hủy" : L"Esc Cancel",
                         false, RGB(239, 68, 68));
            RECT hint{504, topbar_h + tabbar_h, 990, content_top};
            SetTextColor(mem_dc, col_text_muted);
            DrawTextW(mem_dc,
                is_vi ? L"Phím L: nhập dài · Tab: góc (° theo trục X) · Enter: tạo đoạn / kết thúc"
                      : L"Key L: length · Tab: angle (° from X axis) · Enter: create / finish",
                -1, &hint, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        } else if (active_tool_ == 11 && node_tool_ && node_tool_->active()) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 85, content_top};
            DrawTextW(mem_dc, L"Node (F10):", -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            const auto mode_opt = node_tool_->selected_nodes_mode();
            const bool is_cusp = mode_opt && (*mode_opt == document::PathNodeMode::Cusp);
            const bool is_smooth = mode_opt && (*mode_opt == document::PathNodeMode::Smooth);
            const bool is_symm = mode_opt && (*mode_opt == document::PathNodeMode::Symmetric);

            draw_rib_btn(90, 165, is_vi ? L"+ Node" : L"+ Node", false, col_teal);
            draw_rib_btn(170, 245, is_vi ? L"- Node" : L"- Node", false, RGB(239, 68, 68));
            draw_rib_btn(250, 325, is_vi ? L"Tách" : L"Break", false, col_text_white);
            draw_rib_btn(330, 405, is_vi ? L"Nối" : L"Join", false, col_text_white);
            draw_rib_btn(410, 485, is_vi ? L"Đóng" : L"Close", false, col_text_white);
            draw_rib_btn(490, 570, is_vi ? L"Cusp" : L"Cusp", is_cusp, is_cusp ? col_teal : col_text_white);
            draw_rib_btn(575, 655, is_vi ? L"Smooth" : L"Smooth", is_smooth, is_smooth ? col_teal : col_text_white);
            draw_rib_btn(660, 740, is_vi ? L"Symm" : L"Symm", is_symm, is_symm ? col_teal : col_text_white);
            draw_rib_btn(745, 830, is_vi ? L"→ Thẳng" : L"→ Line", false, col_text_white);
            draw_rib_btn(835, 920, is_vi ? L"→ Cong" : L"→ Curve", false, col_text_white);
            draw_rib_btn(925, 1010, is_vi ? L"Đảo chiều" : L"Reverse", false, col_text_white);
        } else if (active_tool_ == 12 && contour_tool_ && contour_tool_->active()) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 75, content_top};
            DrawTextW(mem_dc, L"Contour:", -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            const auto dir = contour_tool_->direction();
            draw_rib_btn(80, 145, is_vi ? L"Ngoài" : L"Outside",
                         dir == geometry::ContourDirection::Outside,
                         dir == geometry::ContourDirection::Outside ? col_teal : col_text_white);
            draw_rib_btn(150, 215, is_vi ? L"Trong" : L"Inside",
                         dir == geometry::ContourDirection::Inside,
                         dir == geometry::ContourDirection::Inside ? col_teal : col_text_white);
            draw_rib_btn(220, 295, is_vi ? L"Vào tâm" : L"To Center",
                         dir == geometry::ContourDirection::ToCenter,
                         dir == geometry::ContourDirection::ToCenter ? col_teal : col_text_white);

            draw_rib_btn(305, 330, L"-", false, col_text_white);
            std::wstringstream ss_sp;
            ss_sp << std::fixed << std::setprecision(1) << contour_tool_->spacing_mm() << L" mm";
            draw_rib_btn(335, 405, ss_sp.str().c_str(), false, col_teal);
            draw_rib_btn(410, 435, L"+", false, col_text_white);

            draw_rib_btn(445, 470, L"-", false, col_text_white);
            const int contour_steps = contour_tool_->steps();
            std::wstring st_label = std::to_wstring(contour_steps) +
                (is_vi ? L" bước" : contour_steps == 1 ? L" step" : L" steps");
            draw_rib_btn(475, 535, st_label.c_str(), false, col_teal);
            draw_rib_btn(540, 565, L"+", false, col_text_white);

            const auto join = contour_tool_->join_type();
            draw_rib_btn(575, 630, is_vi ? L"Nhọn" : L"Miter",
                         join == geometry::JoinType::Miter,
                         join == geometry::JoinType::Miter ? col_teal : col_text_white);
            draw_rib_btn(635, 690, is_vi ? L"Tròn" : L"Round",
                         join == geometry::JoinType::Round,
                         join == geometry::JoinType::Round ? col_teal : col_text_white);
            draw_rib_btn(695, 750, is_vi ? L"Vát" : L"Bevel",
                         join == geometry::JoinType::Bevel,
                         join == geometry::JoinType::Bevel ? col_teal : col_text_white);

            draw_rib_btn(760, 825, is_vi ? L"Áp dụng" : L"Apply", false, col_teal);
            draw_rib_btn(830, 940, is_vi ? L"Rã viền (Ctrl+K)" : L"Break (Ctrl+K)", false, col_text_white);
        } else if (active_tool_ == 13 && gradient_tool_ && gradient_tool_->active()) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 75, content_top};
            DrawTextW(mem_dc, is_vi ? L"Chuyển sắc:" : L"Gradient:", -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            const auto* draft = gradient_tool_->presenter().draft_paint();
            const auto kind = draft ? draft->kind : gradient_tool_->tool().kind();
            const auto spread = draft ? draft->spread : gradient_tool_->tool().spread();

            // 80..145: Linear
            draw_rib_btn(80, 145, is_vi ? L"Tuyến tính" : L"Linear",
                         kind == document::GradientKind::Linear,
                         kind == document::GradientKind::Linear ? col_teal : col_text_white);
            // 150..215: Radial
            draw_rib_btn(150, 215, is_vi ? L"Tỏa tròn" : L"Radial",
                         kind == document::GradientKind::Radial,
                         kind == document::GradientKind::Radial ? col_teal : col_text_white);
            // 225..275: Pad
            draw_rib_btn(225, 275, is_vi ? L"Chặn" : L"Pad",
                         spread == document::GradientSpread::Pad,
                         spread == document::GradientSpread::Pad ? col_teal : col_text_white);
            // 280..345: Repeat
            draw_rib_btn(280, 345, is_vi ? L"Lặp" : L"Repeat",
                         spread == document::GradientSpread::Repeat,
                         spread == document::GradientSpread::Repeat ? col_teal : col_text_white);
            // 350..420: Reflect
            draw_rib_btn(350, 420, is_vi ? L"Đối xứng" : L"Reflect",
                         spread == document::GradientSpread::Reflect,
                         spread == document::GradientSpread::Reflect ? col_teal : col_text_white);
            // 430..500: Reverse
            draw_rib_btn(430, 500, is_vi ? L"Đảo màu" : L"Reverse", false, col_text_white);
            // 505..575: Fit Bounds
            draw_rib_btn(505, 575, is_vi ? L"Khớp khung" : L"Fit Bounds", false, col_text_white);
            // 585..665: Angle (+45 deg)
            const auto proj = gradient_tool_->presenter().geometry_projection();
            std::wstring angle_str = std::to_wstring(static_cast<int>(proj.angle_degrees)) + L"° (+45°)";
            draw_rib_btn(585, 665, angle_str.c_str(), false, col_teal);
            // 675..735: + Stop
            draw_rib_btn(675, 735, is_vi ? L"+ Điểm" : L"+ Stop", false, col_text_white);
            // 745..815: Apply
            draw_rib_btn(745, 815, is_vi ? L"Áp dụng" : L"Apply", false, col_teal);
            // 820..880: Cancel
            draw_rib_btn(820, 880, is_vi ? L"Hủy" : L"Cancel", false, col_text_white);
        } else if (active_tool_ == 9 && selection.empty()) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 52, content_top};
            DrawTextW(mem_dc, is_vi ? L"Đa giác:" : L"Poly:", -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            draw_num_input(58, 164, is_vi ? L"Cạnh:" : L"Sides:",
                           std::to_string(polygon_sides_), NumericFieldId::PolygonSides, false);
            SetTextColor(mem_dc, col_text_muted);
            SelectObject(mem_dc, font_secondary);
            RECT hint{174, topbar_h + tabbar_h, 780, content_top};
            DrawTextW(mem_dc,
                is_vi ? L"3–256 cạnh · Shift khóa góc 15° · Ctrl tạo từ tâm · Properties có thiết lập nâng cao"
                      : L"3–256 sides · Shift snaps angle to 15° · Ctrl draws from center · see Properties for advanced settings",
                -1, &hint, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        } else if (is_text_mode) {
            paint_text_property_bar(mem_dc, w, content_top, topbar_h, tabbar_h);
        } else if (selection.empty()) {
            // PAGE SIZING MODE (CorelDRAW Page Property Bar - Direct Numeric Input + Compact Preset Combobox)
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT rc_p_lbl = { 10, topbar_h + tabbar_h, 50, content_top };
            DrawTextW(mem_dc, is_vi ? L"Khổ:" : L"Page:", -1, &rc_p_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            const double pw = snapshot->page().width_mm;
            const double ph = snapshot->page().height_mm;

            // Detect current preset
            int active_preset_idx = -1;
            for (size_t i = 0; i < kPagePresetEntries.size(); ++i) {
                const auto& p = kPagePresetEntries[i];
                if ((std::abs(pw - p.width_mm) < 1.0 && std::abs(ph - p.height_mm) < 1.0) ||
                    (std::abs(pw - p.height_mm) < 1.0 && std::abs(ph - p.width_mm) < 1.0)) {
                    active_preset_idx = static_cast<int>(i);
                    break;
                }
            }
            std::wstring preset_lbl = (active_preset_idx >= 0)
                ? kPagePresetEntries[active_preset_idx].name
                : (is_vi ? L"Tùy chỉnh" : L"Custom");

            // Combobox Button: [ A4              ▾ ]
            RECT rc_cbo = { 52, topbar_h + tabbar_h + 4, 165, content_top - 4 };
            HBRUSH br_cbo = CreateSolidBrush(is_dark ? RGB(20, 32, 48) : RGB(241, 245, 249));
            HPEN pen_cbo = CreatePen(PS_SOLID, 1, show_page_preset_dropdown_ ? col_teal : col_border);
            HGDIOBJ old_pcbo = SelectObject(mem_dc, pen_cbo);
            HGDIOBJ old_bcbo = SelectObject(mem_dc, br_cbo);
            RoundRect(mem_dc, rc_cbo.left, rc_cbo.top, rc_cbo.right, rc_cbo.bottom, 4, 4);
            SelectObject(mem_dc, old_pcbo);
            SelectObject(mem_dc, old_bcbo);
            DeleteObject(pen_cbo);
            DeleteObject(br_cbo);

            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, show_page_preset_dropdown_ ? col_teal : col_text_white);
            RECT rc_cbo_txt = { rc_cbo.left + 8, rc_cbo.top, rc_cbo.right - 22, rc_cbo.bottom };
            DrawTextW(mem_dc, preset_lbl.c_str(), -1, &rc_cbo_txt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            SetTextColor(mem_dc, show_page_preset_dropdown_ ? col_teal : col_text_muted);
            RECT rc_cbo_arr = { rc_cbo.right - 20, rc_cbo.top, rc_cbo.right - 6, rc_cbo.bottom };
            DrawTextW(mem_dc, show_page_preset_dropdown_ ? L"▲" : L"▾", -1, &rc_cbo_arr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            std::ostringstream spw, sph;
            spw << std::fixed << std::setprecision(1) << pw;
            sph << std::fixed << std::setprecision(1) << ph;
            std::string pw_str = spw.str(), ph_str = sph.str();
            if (pw_str.ends_with(".0")) pw_str.erase(pw_str.size() - 2);
            if (ph_str.ends_with(".0")) ph_str.erase(ph_str.size() - 2);

            draw_num_input(175, 275, L"W:", pw_str, NumericFieldId::PageWidth);
            draw_num_input(283, 383, L"H:", ph_str, NumericFieldId::PageHeight);

            bool is_land = (pw > ph);
            draw_rib_btn(391, 455, is_vi ? L"▯ Dọc" : L"▯ Port", !is_land, !is_land ? col_teal : col_text_white);
            draw_rib_btn(461, 525, is_vi ? L"▭ Ngang" : L"▭ Land", is_land, is_land ? col_teal : col_text_white);
            if (workflow().is_array_session_active()) {
                std::wstring array_text = is_vi ? L"Mảng sao chép: " : L"Array: ";
                if (!array_input_buffer_.empty()) {
                    for (char c : array_input_buffer_) array_text += static_cast<wchar_t>(c);
                } else {
                    array_text += (workflow().array_session().is_multiplier ? L"*" : L"/") +
                                  std::to_wstring(workflow().array_session().current_count);
                }
                array_text += is_vi ? L" (Gõ *N hoặc /N · Esc kết thúc)" : L" (Type *N or /N · Esc to end)";
                draw_rib_btn(675, (std::min)(960, w - inspect_w - 10), array_text.c_str(), true, col_teal);
            }
        } else {
            size_t shadow_effect_idx = 0;
            document::DropShadowEffect shadow_val;
            const bool has_shadow = selection.size() == 1 &&
                DropShadowRibbon::get_shadow(*this, shadow_effect_idx, shadow_val);
            if (ribbon_drop_shadow_mode_ && has_shadow) {
                DropShadowRibbon::paint(*this, mem_dc, w, topbar_h, tabbar_h, content_top,
                                        inspect_w, col_teal, col_text_white, col_text_muted,
                                        col_border, is_dark, is_vi, draw_rib_btn, draw_num_input);
            } else {
                // OBJECT TRANSFORM & EDIT MODE (Selection Ribbon - Direct Numeric Input)
                const auto obj_id = selection.front();
            auto bounds_opt = workflow().get_object_bounds(obj_id);
            double obj_x = bounds_opt ? bounds_opt->x : 0.0;
            double obj_y = bounds_opt ? bounds_opt->y : 0.0;
            double obj_w = bounds_opt ? bounds_opt->width : 0.0;
            double obj_h = bounds_opt ? bounds_opt->height : 0.0;

            std::ostringstream sx, sy, sw, sh, srot;
            sx << std::fixed << std::setprecision(1) << obj_x; sy << std::fixed << std::setprecision(1) << obj_y;
            sw << std::fixed << std::setprecision(1) << obj_w; sh << std::fixed << std::setprecision(1) << obj_h;
            double obj_rot = 0.0;
            if (const auto obj_opt = snapshot->objects().get(obj_id)) {
                const auto transform = std::visit([](const auto& o) { return o.transform; }, *obj_opt);
                obj_rot = std::atan2(transform.b, transform.a) * 180.0 / 3.14159265358979323846;
                if (std::abs(obj_rot) < 0.05) obj_rot = 0.0;
            }
            srot << std::fixed << std::setprecision(1) << obj_rot;

            draw_num_input(10, 95, L"X:", sx.str(), NumericFieldId::ObjectX);
            draw_num_input(100, 185, L"Y:", sy.str(), NumericFieldId::ObjectY);
            draw_num_input(190, 275, L"W:", sw.str(), NumericFieldId::ObjectWidth);

            const int btn_y = (topbar_h + tabbar_h + 4 + content_top - 4) / 2;
            draw_rib_btn(280, 335, L"", aspect_ratio_locked_, aspect_ratio_locked_ ? col_teal : col_text_muted);
            draw_lock_icon(mem_dc, 292, btn_y, aspect_ratio_locked_, aspect_ratio_locked_ ? col_teal : col_text_muted);
            RECT r_lock_txt{ 304, topbar_h + tabbar_h + 4, 333, content_top - 4 };
            DrawTextW(mem_dc, aspect_ratio_locked_ ? (is_vi ? L"Khóa" : L"Lock") : (is_vi ? L"Mở" : L"Free"),
                      -1, &r_lock_txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            draw_num_input(340, 425, L"H:", sh.str(), NumericFieldId::ObjectHeight);
            draw_num_input(435, 525, L"∡:", srot.str() + "°", NumericFieldId::ObjectRotation);

            if (is_in_isolation_mode()) {
                draw_rib_btn(580, 670, is_vi ? L"⬅ Thoát (Esc)" : L"⬅ Exit (Esc)", true, col_teal);
            } else {
                if (selection.size() >= 2) {
                    const auto buttons = get_multi_object_context_buttons(workflow(), workflow().can_group_selected(), w - inspect_w);
                    const int button_y = (topbar_h + tabbar_h + 4 + content_top - 4) / 2;
                    for (const auto& button : buttons) {
                        if (button.starts_group) {
                            HPEN separator = CreatePen(PS_SOLID, 1, col_border);
                            const HGDIOBJ old_pen = SelectObject(mem_dc, separator);
                            MoveToEx(mem_dc, button.left - 4, topbar_h + tabbar_h + 8, nullptr);
                            LineTo(mem_dc, button.left - 4, content_top - 8);
                            SelectObject(mem_dc, old_pen);
                            DeleteObject(separator);
                        }
                        draw_rib_btn(button.left, button.left + button.width, L"", false, col_text_white);
                        draw_object_context_icon(mem_dc, button.action,
                            button.left + button.width / 2, button_y, col_text_white);
                    }
                } else if (selection.size() == 1) {
                    const int button_y = (topbar_h + tabbar_h + 4 + content_top - 4) / 2;
                    int rx = 540;
                    for (auto act : {ObjectContextAction::FlipHorizontal, ObjectContextAction::FlipVertical,
                                     ObjectContextAction::Rotate90CW, ObjectContextAction::Rotate90CCW}) {
                        draw_rib_btn(rx, rx + 28, L"", false, col_text_white);
                        draw_object_context_icon(mem_dc, act, rx + 14, button_y, col_text_white);
                        rx += 32;
                    }
                    rx += 4;
                    size_t sidx = 0;
                    document::DropShadowEffect sval;
                    if (DropShadowRibbon::get_shadow(*this, sidx, sval)) {
                        draw_rib_btn(rx, rx + 66, is_vi ? L"◐ Bóng" : L"◐ Shadow", false, col_teal);
                        rx += 72;
                    }
                    if (workflow().is_group(selection.front())) {
                        draw_rib_btn(rx, rx + 66, is_vi ? L"⛶ Rã" : L"⛶ Ungroup", false, col_teal);
                        rx += 72;
                        draw_rib_btn(rx, rx + 66, is_vi ? L"🔍 Chui vào" : L"🔍 Edit", true, col_teal);
                        rx += 72;
                    }
                }
            }

            if (workflow().is_array_session_active()) {
                std::wstring array_text = is_vi ? L"Mảng sao chép: " : L"Array: ";
                if (!array_input_buffer_.empty()) {
                    for (char c : array_input_buffer_) array_text += static_cast<wchar_t>(c);
                } else {
                    array_text += (workflow().array_session().is_multiplier ? L"*" : L"/") +
                                  std::to_wstring(workflow().array_session().current_count);
                }
                array_text += is_vi ? L" (Gõ *N hoặc /N · Esc kết thúc)" : L" (Type *N or /N · Esc to end)";
                draw_rib_btn((std::max)(10, w - inspect_w - 280), w - inspect_w - 10, array_text.c_str(), true, col_teal);
            }

            if (selection.size() == 1 && workflow().can_set_selected_object_opacity() &&
                844 <= w - inspect_w) {
                const auto appearance = selection_appearance(*snapshot, selection);
                draw_num_input(720, 844, is_vi ? L"Độ mờ:" : L"Opacity:",
                               opacity_percent_text(appearance.object_opacity),
                               NumericFieldId::ObjectOpacity, false);
            }
            }
        }

        // 5. Left Tool Strip
        paint_left_toolbox(mem_dc, w, h);

        // 6. Right Inspector & Layers Dock
        paint_right_inspector_dock(mem_dc, w, h);

        int insp_x = w - inspect_w;

        // Hairline borders between layout blocks
        HPEN pen_bdr = CreatePen(PS_SOLID, 1, col_border);
        HGDIOBJ old_bdr = SelectObject(mem_dc, pen_bdr);

        MoveToEx(mem_dc, 0, content_top, NULL); LineTo(mem_dc, w, content_top);
        MoveToEx(mem_dc, tool_w, content_top, NULL); LineTo(mem_dc, tool_w, h - status_h);
        MoveToEx(mem_dc, insp_x, content_top, NULL); LineTo(mem_dc, insp_x, h - status_h);

        SelectObject(mem_dc, old_bdr);
        DeleteObject(pen_bdr);
    }

    // 3. Topbar (Always on top)
    HBRUSH br_topbar = CreateSolidBrush(col_header);
    RECT rc_topbar = { 0, 0, w, topbar_h };
    FillRect(mem_dc, &rc_topbar, br_topbar);
    DeleteObject(br_topbar);

    // Hairline border under topbar
    HPEN pen_bdr_tb = CreatePen(PS_SOLID, 1, col_border);
    HGDIOBJ old_bdr_tb = SelectObject(mem_dc, pen_bdr_tb);
    MoveToEx(mem_dc, 0, topbar_h, NULL); LineTo(mem_dc, w, topbar_h);
    SelectObject(mem_dc, old_bdr_tb);
    DeleteObject(pen_bdr_tb);

    // Logo PK badge (bo nhẹ 6px)
    HBRUSH br_logo = CreateSolidBrush(col_teal);
    HPEN pen_logo = CreatePen(PS_SOLID, 1, col_teal);
    SelectObject(mem_dc, pen_logo);
    SelectObject(mem_dc, br_logo);
    RoundRect(mem_dc, 10, 6, 36, 32, 6, 6);
    DeleteObject(pen_logo);
    DeleteObject(br_logo);
    SelectObject(mem_dc, font_be_vietnam_bold);
    SetTextColor(mem_dc, is_dark ? RGB(7, 11, 16) : RGB(255, 255, 255));
    RECT rc_logo = { 10, 6, 36, 32 };
    DrawTextW(mem_dc, L"PK", -1, &rc_logo, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // App brand title
    SetTextColor(mem_dc, col_text_white);
    RECT rc_app_title = { 42, 6, 145, 32 };
    DrawTextW(mem_dc, L"PikDraw PRO", -1, &rc_app_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    paint_main_menu_bar(mem_dc, w, topbar_h);

    paint_status_bar(mem_dc, w, h, status_h);

    // 8. Paint Settings Modal Dialog if open
    if (show_settings_modal_) {
        paint_settings_modal(mem_dc, w, h);
    }

    // 8b. Paint New Document Modal Dialog if open
    if (show_new_doc_modal_) {
        paint_new_document_modal(mem_dc, w, h);
    }

    if (show_recovery_modal_) {
        paint_recovery_modal(mem_dc, w, h);
    }

    // 9. Paint the selected main-menu dropdown.
    if (open_menu_id_ >= 0) {
        const RECT anchor = get_menu_bar_item_rect(open_menu_id_, topbar_h);
        paint_dropdown_menu(mem_dc, open_menu_id_, anchor.left, topbar_h);
    }

    // 9b. Paint Page Preset Dropdown List if open (CorelDRAW / Illustrator Reference)
    if (show_page_preset_dropdown_) {
        paint_page_preset_dropdown(mem_dc, 52, topbar_h + tabbar_h + ribbon_h, kPagePresetDropdownWidth);
    }

    // 10. Paint Floating Tooltip Popover (Always topmost)
    if (tooltip_.visible && !tooltip_.text.empty()) {
        SelectObject(mem_dc, font_secondary ? font_secondary : font_be_vietnam);
        RECT calc_rc = { 0, 0, 0, 0 };
        DrawTextW(mem_dc, tooltip_.text.c_str(), -1, &calc_rc, DT_CALCRECT | DT_NOPREFIX);

        int tip_pad_x = 10;
        int tip_pad_y = 6;
        int tip_w = (calc_rc.right - calc_rc.left) + tip_pad_x * 2;
        int tip_h = (calc_rc.bottom - calc_rc.top) + tip_pad_y * 2;

        int tip_x = tooltip_.x + 12;
        int tip_y = tooltip_.y + 18;

        if (tip_x + tip_w > w - 8) {
            tip_x = w - tip_w - 8;
        }
        if (tip_x < 8) tip_x = 8;

        if (tip_y + tip_h > h - 28) {
            tip_y = tooltip_.y - tip_h - 6;
        }
        if (tip_y < 8) tip_y = 8;

        RECT rc_shadow = { tip_x + 2, tip_y + 2, tip_x + tip_w + 2, tip_y + tip_h + 2 };
        const HBRUSH br_s = CreateSolidBrush(RGB(15, 23, 42));
        const HPEN p_s = CreatePen(PS_SOLID, 1, RGB(15, 23, 42));
        const auto op_s = SelectObject(mem_dc, p_s), ob_s = SelectObject(mem_dc, br_s);
        RoundRect(mem_dc, rc_shadow.left, rc_shadow.top, rc_shadow.right, rc_shadow.bottom, 6, 6);
        SelectObject(mem_dc, op_s); SelectObject(mem_dc, ob_s); DeleteObject(p_s); DeleteObject(br_s);

        const HBRUSH br_t = CreateSolidBrush(RGB(24, 24, 27));
        const HPEN p_t = CreatePen(PS_SOLID, 1, RGB(0, 242, 195));
        const auto op_t = SelectObject(mem_dc, p_t), ob_t = SelectObject(mem_dc, br_t);
        RoundRect(mem_dc, tip_x, tip_y, tip_x + tip_w, tip_y + tip_h, 6, 6);
        SelectObject(mem_dc, op_t); SelectObject(mem_dc, ob_t); DeleteObject(p_t); DeleteObject(br_t);

        RECT rc_text = { tip_x + tip_pad_x, tip_y + tip_pad_y, tip_x + tip_w - tip_pad_x, tip_y + tip_h - tip_pad_y };
        SetBkMode(mem_dc, TRANSPARENT);
        SetTextColor(mem_dc, RGB(255, 255, 255));
        DrawTextW(mem_dc, tooltip_.text.c_str(), -1, &rc_text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    if (font_picker_ && font_picker_->is_open()) font_picker_->paint(mem_dc);

    SetMapMode(mem_dc, MM_TEXT);
    BitBlt(hdc, 0, 0, device_w, device_h, mem_dc, 0, 0, SRCCOPY);

    SelectObject(mem_dc, old_font);
    RestoreDC(mem_dc, saved_dc);
}

#endif

} // namespace pikdraw::desktop
