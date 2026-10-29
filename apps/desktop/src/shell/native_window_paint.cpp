#include "pikdraw/desktop/shell/native_window.hpp"
#include "../performance/canvas_trace.hpp"
#include "pikdraw/desktop/shell/native_font_picker.hpp"
#include "native_window_paint_icons.hpp"
#include "native_window_text_properties.hpp"
#include "native_window_file_menu.hpp"
#include "native_window_length_input.hpp"
#include "native_window_menus.hpp"
#include "native_window_page_presets.hpp"
#include "native_window_page_navigation.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "../tools/gradient_tool_controller.hpp"
#include "../tools/annotation/annotation_tool_controller.hpp"
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

    int topbar_h = 38, tabbar_h = 28, ribbon_h = 36;
    int status_h = page_navigation::kBottomChromeHeight, tool_w = toolbox_width(), inspect_w = inspector_width();
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
        if (canvas_trace_)
            record_performance_event(CanvasTraceKind::CanvasOverlayBegin,
                active_trace_event_id_, active_trace_frame_id_);
        paint_document_canvas(mem_dc, w, h);

        if (!direct_present_frame_)
            paint_canvas_interaction_handles(mem_dc, w, h);
        if (canvas_trace_)
            record_performance_event(CanvasTraceKind::CanvasOverlayEnd,
                active_trace_event_id_, active_trace_frame_id_);

        // Document Tab Bar
        if (canvas_trace_)
            record_performance_event(CanvasTraceKind::ChromeOverlayBegin,
                active_trace_event_id_, active_trace_frame_id_);
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
                                  bool append_page_unit = false) {
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
                if (append_page_unit)
                    display_text += L" " + display_length_unit_suffix_wide(display_length_unit_);
            }

            SetTextColor(mem_dc, is_focused ? col_teal : col_text_white);
            SelectObject(mem_dc, font_mono);
            RECT rc_txt = { rect.left + 4, rect.top, rect.right - 4, rect.bottom };
            DrawTextW(mem_dc, display_text.c_str(), -1, &rc_txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        };

        auto format_corner_value = [&](double value_mm) {
            const auto value = length_value_from_mm(value_mm, display_length_unit_).value_or(value_mm);
            std::ostringstream formatted;
            formatted << std::fixed << std::setprecision(1) << value;
            return formatted.str();
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

        if ((active_tool_ == 17 && annotation_tool_ && annotation_tool_->active()) ||
            (active_tool_ == 0 && has_annotation_properties_target())) {
            paint_annotation_property_bar(mem_dc, w, content_top, topbar_h, tabbar_h);
        } else if ((active_tool_ == 8 && pen_tool_ && pen_tool_->active()) ||
            ((active_tool_ == 6 || active_tool_ == 7) && drawing_tool_ && drawing_tool_->active())) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 48, content_top};
            const wchar_t* tool_name = (active_tool_ == 8) ? L"Pen:" : (active_tool_ == 6 ? L"Line:" : L"Poly:");
            DrawTextW(mem_dc, tool_name, -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            const auto pen_length_label = L"L (" + display_length_unit_suffix_wide(display_length_unit_) + L"):";
            draw_num_input(52, 185, pen_length_label.c_str(), pen_numeric_length_,
                           NumericFieldId::PenLength, false);
            draw_num_input(192, 315, is_vi ? L"Góc:" : L"Angle:", pen_numeric_angle_, NumericFieldId::PenAngle, false);

            draw_rib_btn(325, 415, is_vi ? L"↵ Kết thúc" : L"↵ Finish",
                         false, col_teal);
            draw_rib_btn(422, 490, is_vi ? L"Esc Hủy" : L"Esc Cancel",
                         false, RGB(239, 68, 68));
            draw_rib_btn(495, 605, is_vi ? L"⚙ Thuộc tính..." : L"⚙ Properties...",
                         false, col_teal);
        } else if (active_tool_ == 0 && paint_powerclip_controls(mem_dc, 10, 70, w - inspect_w - 10, false)) {
        } else if (active_tool_ == 5) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 70, content_top};
            DrawTextW(mem_dc, is_vi ? L"Dao cắt:" : L"Knife:", -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            draw_rib_btn(75, 147, is_vi ? L"Thẳng" : L"Straight",
                         !knife_freehand_mode_, !knife_freehand_mode_ ? col_teal : col_text_white);
            draw_rib_btn(151, 239, is_vi ? L"Tự do" : L"Freehand",
                         knife_freehand_mode_, knife_freehand_mode_ ? col_teal : col_text_white);
            draw_num_input(245, 365, is_vi ? L"Hở:" : L"Gap:",
                           format_mm_display_utf8(knife_gap_mm_, display_length_unit_, 3),
                           NumericFieldId::KnifeGap, false);
            draw_rib_btn(372, 484, is_vi ? L"⚙ Thuộc tính" : L"⚙ Properties",
                         false, col_teal);
        } else if (active_tool_ == 16) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 70, content_top};
            DrawTextW(mem_dc, is_vi ? L"Tẩy vector:" : L"Vector Eraser:", -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            draw_num_input(112, 233, is_vi ? L"Ø:" : L"Ø:",
                           format_mm_display_utf8(vector_eraser_diameter_mm_, display_length_unit_, 3),
                           NumericFieldId::VectorEraserDiameter, false);
            draw_rib_btn(240, 350, vector_eraser_round_nib_
                         ? (is_vi ? L"Nét tròn" : L"Round nib")
                         : (is_vi ? L"Nét vuông" : L"Square nib"),
                         false, col_text_white);
            draw_rib_btn(357, 469, is_vi ? L"⚙ Thuộc tính" : L"⚙ Properties",
                         false, col_teal);
        } else if (active_tool_ == 19) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 150, content_top};
            DrawTextW(mem_dc, is_vi ? L"Kéo cắt (X):" : L"Scissors (X):", -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            draw_rib_btn(160, 286, is_vi ? L"⚙ Hướng dẫn" : L"⚙ Properties",
                         false, col_teal);
        } else if (active_tool_ == 20) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 104, content_top};
            DrawTextW(mem_dc, L"Trim Objects:", -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            draw_rib_btn(110, 222,
                trim_objects_keep_sources_ ? (is_vi ? L"✓ Giữ Source" : L"✓ Keep Source")
                                           : (is_vi ? L"Giữ Source" : L"Keep Source"),
                trim_objects_keep_sources_, trim_objects_keep_sources_ ? col_teal : col_text_white);
            draw_rib_btn(228, 336,
                cut_geometry_preview_ready(CutGeometryOperation::TrimObjects)
                    ? (is_vi ? L"Áp dụng" : L"Apply")
                    : (is_vi ? L"Xem trước" : L"Preview"),
                trim_objects_target_id_ != document::kInvalidObjectId && !trim_objects_source_ids_.empty(), col_teal);
            draw_rib_btn(342, 430, is_vi ? L"Đặt lại" : L"Reset", false, col_text_white);
            draw_rib_btn(436, 548, is_vi ? L"⚙ Thuộc tính" : L"⚙ Properties", false, col_teal);
        } else if (active_tool_ == 21) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 150, content_top};
            DrawTextW(mem_dc, L"Smart Trim:", -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            draw_rib_btn(160, 286, is_vi ? L"⚙ Thuộc tính" : L"⚙ Properties", false, col_teal);
        } else if (active_tool_ == 22) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 104, content_top};
            DrawTextW(mem_dc, L"Shape Builder:", -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            const bool builder_preview_ready =
                cut_geometry_preview_ready(CutGeometryOperation::ShapeBuilder);
            const bool builder_preview_erase = builder_preview_ready &&
                pending_cut_geometry_proposal_->proposal.erase_faces;
            draw_rib_btn(110, 222,
                         builder_preview_ready && !builder_preview_erase
                             ? (is_vi ? L"Áp dụng ghép" : L"Apply Merge")
                             : (is_vi ? L"Xem ghép" : L"Preview Merge"),
                         !shape_builder_seed_points_.empty() || builder_preview_ready,
                         col_teal);
            draw_rib_btn(228, 336,
                         builder_preview_erase
                             ? (is_vi ? L"Áp dụng xóa" : L"Apply Erase")
                             : (is_vi ? L"Xem xóa" : L"Preview Erase"),
                         !shape_builder_seed_points_.empty() || builder_preview_ready,
                         col_text_white);
            draw_rib_btn(342, 430, is_vi ? L"Đặt lại" : L"Reset", false, col_text_white);
            draw_rib_btn(436, 548, is_vi ? L"⚙ Thuộc tính" : L"⚙ Properties", false, col_teal);
        } else if (active_tool_ == 23) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 120, content_top};
            DrawTextW(mem_dc, L"Smart Fill:", -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            draw_rib_btn(130, 252, is_vi ? L"⚙ Thuộc tính" : L"⚙ Properties", false, col_teal);
        } else if (active_tool_ == 24) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 120, content_top};
            DrawTextW(mem_dc, is_vi ? L"Tẩy ảnh:" : L"Raster Eraser:", -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            const std::wstring diameter = L"Ø " + std::to_wstring(
                static_cast<int>(std::lround(raster_eraser_diameter_px_))) + L" px";
            draw_rib_btn(130, 252, diameter.c_str(), false, col_teal);
            draw_rib_btn(260, 382, is_vi ? L"⚙ Thuộc tính" : L"⚙ Properties",
                         false, col_teal);
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

            draw_rib_btn(90, 160, is_vi ? L"+● Thêm" : L"+● Add", false, col_teal);
            draw_rib_btn(165, 235, is_vi ? L"-● Xóa" : L"-● Delete", false, RGB(239, 68, 68));
            draw_rib_btn(245, 315, is_vi ? L"⋀ Cusp" : L"⋀ Cusp", is_cusp, is_cusp ? col_teal : col_text_white);
            draw_rib_btn(320, 390, is_vi ? L"∿ Smooth" : L"∿ Smooth", is_smooth, is_smooth ? col_teal : col_text_white);
            draw_rib_btn(395, 465, is_vi ? L"⎚ Symm" : L"⎚ Symm", is_symm, is_symm ? col_teal : col_text_white);
            draw_rib_btn(475, 580, is_vi ? L"⚙ Thuộc tính..." : L"⚙ Properties...", false, col_teal);
        } else if (active_tool_ == 12 && contour_tool_ && contour_tool_->active()) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 72, content_top};
            DrawTextW(mem_dc, L"Contour:", -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            const auto dir = contour_tool_->direction();
            draw_rib_btn(75, 135, is_vi ? L"◎ Tâm" : L"◎ Center",
                         dir == geometry::ContourDirection::ToCenter,
                         dir == geometry::ContourDirection::ToCenter ? col_teal : col_text_white);
            draw_rib_btn(138, 198, is_vi ? L"⧈ Trong" : L"⧈ Inside",
                         dir == geometry::ContourDirection::Inside,
                         dir == geometry::ContourDirection::Inside ? col_teal : col_text_white);
            draw_rib_btn(201, 261, is_vi ? L"▣ Ngoài" : L"▣ Outside",
                         dir == geometry::ContourDirection::Outside,
                         dir == geometry::ContourDirection::Outside ? col_teal : col_text_white);

            auto draw_spinner = [&](int left, int right) {
                int top = topbar_h + tabbar_h + 4;
                int mid = topbar_h + tabbar_h + 17;
                int bottom = content_top - 4;
                RECT rc_up{left, top, right, mid};
                RECT rc_dn{left, mid + 1, right, bottom};
                HBRUSH brush = CreateSolidBrush(col_btn_bg);
                HPEN pen = CreatePen(PS_SOLID, 1, col_btn_bdr);
                HGDIOBJ old_p = SelectObject(mem_dc, pen);
                HGDIOBJ old_b = SelectObject(mem_dc, brush);
                RoundRect(mem_dc, rc_up.left, rc_up.top, rc_up.right, rc_up.bottom, 2, 2);
                RoundRect(mem_dc, rc_dn.left, rc_dn.top, rc_dn.right, rc_dn.bottom, 2, 2);
                SelectObject(mem_dc, old_p);
                SelectObject(mem_dc, old_b);
                DeleteObject(pen);
                DeleteObject(brush);
                SetTextColor(mem_dc, col_text_white);
                SelectObject(mem_dc, font_mono);
                DrawTextW(mem_dc, L"▲", -1, &rc_up, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                DrawTextW(mem_dc, L"▼", -1, &rc_dn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            };

            const int contour_steps = contour_tool_->steps();
            draw_num_input(270, 330, L"☰", std::to_string(contour_steps), NumericFieldId::ContourSteps, false);
            draw_spinner(332, 352);

            const auto spacing_text = format_mm_display_utf8(
                contour_tool_->spacing_mm(), display_length_unit_, 4);
            draw_num_input(360, 440, L"📏", spacing_text, NumericFieldId::ContourSpacing, false);
            draw_spinner(442, 462);

            draw_rib_btn(470, 580, is_vi ? L"⚙ Thuộc tính..." : L"⚙ Properties...", false, col_teal);
        } else if (active_tool_ == 13 && gradient_tool_ && gradient_tool_->active()) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 75, content_top};
            DrawTextW(mem_dc, is_vi ? L"Chuyển sắc:" : L"Gradient:", -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            const auto* draft = gradient_tool_->presenter().draft_paint();
            const auto kind = draft ? draft->kind : gradient_tool_->tool().kind();

            // 80..145: Linear
            draw_rib_btn(80, 145, is_vi ? L"Tuyến tính" : L"Linear",
                         kind == document::GradientKind::Linear,
                         kind == document::GradientKind::Linear ? col_teal : col_text_white);
            // 150..215: Radial
            draw_rib_btn(150, 215, is_vi ? L"Tỏa tròn" : L"Radial",
                         kind == document::GradientKind::Radial,
                         kind == document::GradientKind::Radial ? col_teal : col_text_white);
            // 220..295: Reverse
            draw_rib_btn(220, 295, is_vi ? L"⇄ Đảo màu" : L"⇄ Reverse", false, col_text_white);
            // 305..415: Properties Jump
            draw_rib_btn(305, 415, is_vi ? L"⚙ Thuộc tính..." : L"⚙ Properties...", false, col_teal);
        } else if (active_tool_ == 1 && selection.empty()) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 75, content_top};
            DrawTextW(mem_dc, is_vi ? L"Chữ nhật:" : L"Rect:", -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            const int btn_y = (topbar_h + tabbar_h + 4 + content_top - 4) / 2;
            int rx = 78;

            draw_rib_btn(rx, rx + 28, L"", creation_rect_corner_type_ == vector::RectCornerType::Round,
                         creation_rect_corner_type_ == vector::RectCornerType::Round ? col_teal : col_text_white);
            draw_corner_round_icon(mem_dc, rx + 14, btn_y,
                                   creation_rect_corner_type_ == vector::RectCornerType::Round ? col_teal : col_text_white);

            draw_rib_btn(rx + 32, rx + 60, L"", creation_rect_corner_type_ == vector::RectCornerType::Scallop,
                         creation_rect_corner_type_ == vector::RectCornerType::Scallop ? col_teal : col_text_white);
            draw_corner_scallop_icon(mem_dc, rx + 46, btn_y,
                                     creation_rect_corner_type_ == vector::RectCornerType::Scallop ? col_teal : col_text_white);

            draw_rib_btn(rx + 64, rx + 92, L"", creation_rect_corner_type_ == vector::RectCornerType::Chamfer,
                         creation_rect_corner_type_ == vector::RectCornerType::Chamfer ? col_teal : col_text_white);
            draw_corner_chamfer_icon(mem_dc, rx + 78, btn_y,
                                     creation_rect_corner_type_ == vector::RectCornerType::Chamfer ? col_teal : col_text_white);
            rx += 98;
            const auto stl = format_corner_value(creation_rect_corner_tl_);
            draw_num_input(rx, rx + 80, L"Bo:", stl, NumericFieldId::RectCornerRadiusTL, false);
            rx += 86;

            draw_rib_btn(rx, rx + 24, L"", creation_rect_corners_locked_, creation_rect_corners_locked_ ? col_teal : col_text_muted);
            draw_lock_icon(mem_dc, rx + 12, btn_y, creation_rect_corners_locked_, creation_rect_corners_locked_ ? col_teal : col_text_muted);
            rx += 30;

            draw_rib_btn(rx, rx + 62, is_vi ? L"⇲ Tỷ lệ" : L"⇲ Scale",
                         creation_rect_corners_scale_relative_, creation_rect_corners_scale_relative_ ? col_teal : col_text_white);
            rx += 68;

            draw_rib_btn(rx, rx + 110, is_vi ? L"⚙ Thuộc tính..." : L"⚙ Properties...", false, col_teal);
        } else if (active_tool_ == 9 && selection.empty()) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 52, content_top};
            DrawTextW(mem_dc, is_vi ? L"Đa giác:" : L"Poly:", -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            draw_num_input(58, 164, is_vi ? L"Cạnh:" : L"Sides:",
                           std::to_string(polygon_sides_), NumericFieldId::PolygonSides, false);
            draw_rib_btn(172, 280, is_vi ? L"⚙ Thuộc tính..." : L"⚙ Properties...", false, col_teal);
        } else if (active_tool_ == 14) {
            const auto& boards = snapshot->active_page().artboards;
            const auto board = std::find_if(boards.begin(), boards.end(), [&](const auto& item) {
                return item.id == active_tab().active_artboard_id;
            });
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT label{10, topbar_h + tabbar_h, 82, content_top};
            DrawTextW(mem_dc, is_vi ? L"Artboard:" : L"Artboard:", -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            if (board != boards.end()) {
                draw_num_input(86, 190, L"X:", format_mm_display_utf8(board->x_mm, display_length_unit_, 5), NumericFieldId::ArtboardX);
                draw_num_input(196, 300, L"Y:", format_mm_display_utf8(board->y_mm, display_length_unit_, 5), NumericFieldId::ArtboardY);
                draw_num_input(306, 410, L"W:", format_mm_display_utf8(board->width_mm, display_length_unit_, 5), NumericFieldId::ArtboardWidth);
                draw_num_input(416, 520, L"H:", format_mm_display_utf8(board->height_mm, display_length_unit_, 5), NumericFieldId::ArtboardHeight);
            }
            draw_rib_btn(530, 650, is_vi ? L"⚙ Thuộc tính..." : L"⚙ Properties...", false, col_teal);
        } else if (active_tool_ == 15) {
            paint_eyedropper_property_bar(mem_dc, w, topbar_h, tabbar_h, content_top);
        } else if (active_tool_ == 10 || is_dimension_object_selected()) {
            paint_dimension_property_bar(mem_dc, w, content_top, topbar_h, tabbar_h);
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

            const auto pw_str = format_mm_display_utf8(pw, display_length_unit_, 4);
            const auto ph_str = format_mm_display_utf8(ph, display_length_unit_, 4);
            draw_num_input(175, 275, L"W:", pw_str, NumericFieldId::PageWidth, false);
            draw_num_input(283, 383, L"H:", ph_str, NumericFieldId::PageHeight, false);

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

            const auto sx = format_mm_display_utf8(obj_x, display_length_unit_, 4);
            const auto sy = format_mm_display_utf8(obj_y, display_length_unit_, 4);
            const auto sw = format_mm_display_utf8(obj_w, display_length_unit_, 4);
            const auto sh = format_mm_display_utf8(obj_h, display_length_unit_, 4);
            std::ostringstream srot;
            double obj_rot = 0.0;
            if (const auto obj_opt = snapshot->objects().get(obj_id)) {
                const auto transform = std::visit([](const auto& o) { return o.transform; }, *obj_opt);
                obj_rot = std::atan2(transform.b, transform.a) * 180.0 / 3.14159265358979323846;
                if (std::abs(obj_rot) < 0.05) obj_rot = 0.0;
            }
            srot << std::fixed << std::setprecision(1) << obj_rot;

            draw_num_input(10, 95, L"X:", sx, NumericFieldId::ObjectX, false);
            draw_num_input(100, 185, L"Y:", sy, NumericFieldId::ObjectY, false);
            draw_num_input(190, 275, L"W:", sw, NumericFieldId::ObjectWidth, false);

            const int btn_y = (topbar_h + tabbar_h + 4 + content_top - 4) / 2;
            draw_rib_btn(280, 335, L"", aspect_ratio_locked_, aspect_ratio_locked_ ? col_teal : col_text_muted);
            draw_lock_icon(mem_dc, 292, btn_y, aspect_ratio_locked_, aspect_ratio_locked_ ? col_teal : col_text_muted);
            RECT r_lock_txt{ 304, topbar_h + tabbar_h + 4, 333, content_top - 4 };
            DrawTextW(mem_dc, aspect_ratio_locked_ ? (is_vi ? L"Khóa" : L"Lock") : (is_vi ? L"Mở" : L"Free"),
                      -1, &r_lock_txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            draw_num_input(340, 425, L"H:", sh, NumericFieldId::ObjectHeight, false);
            draw_num_input(435, 525, L"∡:", srot.str() + "°", NumericFieldId::ObjectRotation);

            if (is_in_isolation_mode()) {
                const auto iso_id = current_isolated_group_id();
                const bool is_clip_iso = iso_id && workflow().is_clipping_container(*iso_id);
                draw_rib_btn(580, is_clip_iso ? 760 : 670,
                             is_clip_iso ? (is_vi ? L"✓ Dừng sửa PowerClip (Esc)" : L"✓ Finish PowerClip (Esc)")
                                         : (is_vi ? L"⬅ Thoát (Esc)" : L"⬅ Exit (Esc)"),
                             true, col_teal);
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
                    int rx = 535;

                    document::ObjectId rect_id;
                    vector::RectCornerSettings rect_s;
                    const bool is_rect = is_selected_rectangle(&rect_id, &rect_s);
                    if (is_rect) {
                        draw_rib_btn(rx, rx + 28, L"", rect_corner_type_ == vector::RectCornerType::Round,
                                     rect_corner_type_ == vector::RectCornerType::Round ? col_teal : col_text_white);
                        draw_corner_round_icon(mem_dc, rx + 14, button_y,
                                               rect_corner_type_ == vector::RectCornerType::Round ? col_teal : col_text_white);

                        draw_rib_btn(rx + 32, rx + 60, L"", rect_corner_type_ == vector::RectCornerType::Scallop,
                                     rect_corner_type_ == vector::RectCornerType::Scallop ? col_teal : col_text_white);
                        draw_corner_scallop_icon(mem_dc, rx + 46, button_y,
                                                 rect_corner_type_ == vector::RectCornerType::Scallop ? col_teal : col_text_white);

                        draw_rib_btn(rx + 64, rx + 92, L"", rect_corner_type_ == vector::RectCornerType::Chamfer,
                                     rect_corner_type_ == vector::RectCornerType::Chamfer ? col_teal : col_text_white);
                        draw_corner_chamfer_icon(mem_dc, rx + 78, button_y,
                                                 rect_corner_type_ == vector::RectCornerType::Chamfer ? col_teal : col_text_white);
                        rx += 98;

                        {
                            HPEN separator = CreatePen(PS_SOLID, 1, col_border);
                            const HGDIOBJ old_pen = SelectObject(mem_dc, separator);
                            MoveToEx(mem_dc, rx - 3, topbar_h + tabbar_h + 8, nullptr);
                            LineTo(mem_dc, rx - 3, content_top - 8);
                            SelectObject(mem_dc, old_pen);
                            DeleteObject(separator);
                        }

                        const auto stl = format_corner_value(rect_corner_tl_);
                        draw_num_input(rx, rx + 75, L"Bo:", stl, NumericFieldId::RectCornerRadiusTL, false);
                        rx += 80;

                        draw_rib_btn(rx, rx + 24, L"", rect_corners_locked_, rect_corners_locked_ ? col_teal : col_text_muted);
                        draw_lock_icon(mem_dc, rx + 12, button_y, rect_corners_locked_, rect_corners_locked_ ? col_teal : col_text_muted);
                        rx += 28;

                        draw_rib_btn(rx, rx + 62, is_vi ? L"⇲ Tỷ lệ" : L"⇲ Scale",
                                     rect_corners_scale_relative_, rect_corners_scale_relative_ ? col_teal : col_text_white);
                        rx += 68;

                        draw_rib_btn(rx, rx + 80, is_vi ? L"⚙ Bo góc" : L"⚙ Corners", false, col_teal);
                        rx += 86;

                        {
                            HPEN separator = CreatePen(PS_SOLID, 1, col_border);
                            const HGDIOBJ old_pen = SelectObject(mem_dc, separator);
                            MoveToEx(mem_dc, rx - 3, topbar_h + tabbar_h + 8, nullptr);
                            LineTo(mem_dc, rx - 3, content_top - 8);
                            SelectObject(mem_dc, old_pen);
                            DeleteObject(separator);
                        }
                    }

                    if (rx + 128 <= w - inspect_w - 10) {
                        for (auto act : {ObjectContextAction::FlipHorizontal, ObjectContextAction::FlipVertical,
                                         ObjectContextAction::Rotate90CW, ObjectContextAction::Rotate90CCW}) {
                            draw_rib_btn(rx, rx + 28, L"", false, col_text_white);
                            draw_object_context_icon(mem_dc, act, rx + 14, button_y, col_text_white);
                            rx += 32;
                        }
                        rx += 4;
                    }
                    size_t sidx = 0;
                    document::DropShadowEffect sval;
                    if (DropShadowRibbon::get_shadow(*this, sidx, sval) && rx + 72 <= w - inspect_w - 10) {
                        draw_rib_btn(rx, rx + 66, is_vi ? L"◐ Bóng" : L"◐ Shadow", false, col_teal);
                        rx += 72;
                    }
                    if (workflow().is_clipping_container(selection.front())) {
                        const auto clip_id = selection.front();
                        const auto* grp_obj = snapshot->objects().lookup_ref(clip_id);
                        bool is_locked = true;
                        if (grp_obj && std::holds_alternative<document::GroupObject>(*grp_obj)) {
                            const auto& grp = std::get<document::GroupObject>(*grp_obj);
                            if (grp.clip) is_locked = grp.clip->lock_contents_to_frame;
                        }
                        if (rx + 76 <= w - inspect_w - 10) {
                            draw_rib_btn(rx, rx + 72, is_vi ? L"✎ Sửa" : L"✎ Edit", true, col_teal);
                            rx += 76;
                        }
                        if (rx + 82 <= w - inspect_w - 10) {
                            draw_rib_btn(rx, rx + 78, is_vi ? L"⛶ Chọn" : L"⛶ Select", false, col_text_white);
                            rx += 82;
                        }
                        if (rx + 92 <= w - inspect_w - 10) {
                            draw_rib_btn(rx, rx + 88, is_vi ? L"⇱ Trích xuất" : L"⇱ Extract", false, col_text_white);
                            rx += 92;
                        }
                        if (rx + 80 <= w - inspect_w - 10) {
                            draw_rib_btn(rx, rx + 76, is_locked ? (is_vi ? L"🔒 Khóa" : L"🔒 Lock") : (is_vi ? L"🔓 Mở" : L"🔓 Free"),
                                         is_locked, is_locked ? col_teal : col_text_muted);
                            rx += 80;
                        }
                        if (rx + 86 <= w - inspect_w - 10) {
                            draw_rib_btn(rx, rx + 82, is_vi ? L"⇲ Giữa" : L"⇲ Center", false, col_text_white);
                            rx += 86;
                        }
                        if (rx + 82 <= w - inspect_w - 10) {
                            draw_rib_btn(rx, rx + 78, is_vi ? L"Vừa ▾" : L"Fit ▾", false, col_text_white);
                            rx += 82;
                        }
                    } else if (workflow().is_group(selection.front()) && rx + 144 <= w - inspect_w - 10) {
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

            const auto exposure_ev = selection.size() == 1
                ? selected_bitmap_exposure_ev() : std::optional<double>{};
            if (exposure_ev && can_edit_selected_bitmap(true) && 844 <= w - inspect_w) {
                std::ostringstream exposure_text;
                exposure_text << std::fixed << std::setprecision(2) << *exposure_ev;
                draw_num_input(720, 844, L"EV:", exposure_text.str(),
                               NumericFieldId::RasterExposureEv, false);
            } else if (selection.size() == 1 && !is_selected_rectangle() &&
                       workflow().can_set_selected_object_opacity() && 844 <= w - inspect_w) {
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

    if (!direct_present_frame_)
        paint_topmost_ui_layer(mem_dc, w, h);

    if (!show_home_screen_ && canvas_trace_)
        record_performance_event(CanvasTraceKind::ChromeOverlayEnd,
            active_trace_event_id_, active_trace_frame_id_);

    SetMapMode(mem_dc, MM_TEXT);
    BitBlt(hdc, 0, 0, device_w, device_h, mem_dc, 0, 0, SRCCOPY);

    SelectObject(mem_dc, old_font);
    RestoreDC(mem_dc, saved_dc);
}

#endif

} // namespace pikdraw::desktop
