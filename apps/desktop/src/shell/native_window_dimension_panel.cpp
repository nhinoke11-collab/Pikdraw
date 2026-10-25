#include "pikdraw/desktop/shell/native_window.hpp"
#include "../tools/dimension_tool_controller.hpp"
#include "pikdraw/document/annotations/dimension_style.hpp"
#include "pikdraw/document/annotations/dimension_object.hpp"
#include "pikdraw/document/annotations/dimension_layout.hpp"
#include "pikdraw/document/commands/dimension_commands.hpp"
#include "pikdraw/document/commands/layer_commands.hpp"
#include "native_window_length_input.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace pikdraw::desktop {

void NativeWindow::paint_dimension_properties_panel(HDC mem_dc, int insp_x, int inspect_w, int content_y, int max_h) {
    (void)max_h;
    ensure_ui_fonts();
    const bool is_dark = (theme_ == ThemeMode::CyberDark);
    const bool is_vi = (language_ == UILanguage::Vietnamese);

    const COLORREF col_bg = is_dark ? RGB(15, 23, 42) : RGB(255, 255, 255);
    const COLORREF col_btn_bg = is_dark ? RGB(19, 28, 43) : RGB(241, 245, 249);
    const COLORREF col_btn_bdr = is_dark ? RGB(37, 50, 72) : RGB(203, 213, 225);
    const COLORREF col_teal = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    const COLORREF col_text_white = is_dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    const COLORREF col_text_muted = is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    const COLORREF col_active_tab = is_dark ? RGB(22, 37, 54) : RGB(236, 242, 248);

    const int panel_left = insp_x + 14;
    const int panel_right = insp_x + inspect_w - 14;
    const int panel_width = panel_right - panel_left;

    const auto snapshot = root().document().current_snapshot();
    const auto& selection = root().vector_controller().selection().selected_ids();

    // Count dimensions in document
    size_t total_dims = 0;
    bool any_dim_visible = false;
    bool any_dim_unlocked = false;
    if (snapshot) {
        for (const auto& [id, obj] : snapshot->objects().all()) {
            if (const auto* path = std::get_if<document::PathObject>(&obj)) {
                if (path->contour_spec.rfind("dimension:v1", 0) == 0) {
                    total_dims++;
                    if (path->visible) any_dim_visible = true;
                    if (!path->locked) any_dim_unlocked = true;
                }
            }
        }
    }

    // Current Style & state
    document::DimensionStyle cur_style = dimension_tool_ ? dimension_tool_->current_style() : document::DimensionStyle::make_default();
    geometry::LinearOrientation cur_ori = dimension_tool_ ? dimension_tool_->linear_orientation() : geometry::LinearOrientation::Aligned;
    document::DimensionKind cur_kind = dimension_tool_ ? dimension_tool_->current_kind() : document::DimensionKind::Aligned;

    if (selection.size() == 1 && snapshot) {
        const auto sel_dim = document::get_dimension(root().document(), selection[0]);
        if (sel_dim) {
            cur_style = sel_dim->style;
            cur_ori = sel_dim->linear_orientation;
            cur_kind = sel_dim->kind;
        }
    }

    auto draw_panel_btn = [&](int x, int y, int width, int height, const wchar_t* label, bool active, bool highlighted = false) {
        RECT rect{x, y, x + width, y + height};
        HBRUSH brush = CreateSolidBrush(active ? col_active_tab : highlighted ? (is_dark ? RGB(18, 48, 56) : RGB(220, 245, 240)) : col_btn_bg);
        HPEN pen = CreatePen(PS_SOLID, active || highlighted ? 2 : 1, active || highlighted ? col_teal : col_btn_bdr);
        HGDIOBJ old_p = SelectObject(mem_dc, pen);
        HGDIOBJ old_b = SelectObject(mem_dc, brush);
        RoundRect(mem_dc, rect.left, rect.top, rect.right, rect.bottom, 4, 4);
        SelectObject(mem_dc, old_p);
        SelectObject(mem_dc, old_b);
        DeleteObject(pen);
        DeleteObject(brush);
        SetTextColor(mem_dc, active || highlighted ? col_teal : col_text_white);
        SelectObject(mem_dc, (active || highlighted) ? font_be_vietnam_bold_ : font_be_vietnam_);
        DrawTextW(mem_dc, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    };

    auto draw_heading = [&](int y, const wchar_t* text) {
        SelectObject(mem_dc, font_be_vietnam_bold_);
        SetTextColor(mem_dc, col_teal);
        RECT rc = { panel_left, y, panel_right, y + 18 };
        DrawTextW(mem_dc, text, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    };

    // 1. Header
    SelectObject(mem_dc, font_be_vietnam_bold_);
    SetTextColor(mem_dc, col_teal);
    RECT title_rc = { panel_left, content_y + 2, panel_right, content_y + 22 };
    DrawTextW(mem_dc, is_vi ? L"THUỘC TÍNH · KÍCH THƯỚC" : L"PROPERTIES · DIMENSION", -1, &title_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(mem_dc, font_secondary_);
    SetTextColor(mem_dc, col_text_muted);
    std::wostringstream sub_info;
    sub_info << (is_vi ? L"Tổng số: " : L"Total: ") << total_dims << (is_vi ? L" đường đo" : L" dims");
    if (!selection.empty()) {
        sub_info << (is_vi ? L" · Đang chọn: " : L" · Selected: ") << selection.size();
    }
    RECT sub_rc = { panel_left, content_y + 23, panel_right, content_y + 39 };
    DrawTextW(mem_dc, sub_info.str().c_str(), -1, &sub_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 2. Layer Dimension Control
    draw_heading(content_y + 44, is_vi ? L"QUẢN LÝ LỚP KÍCH THƯỚC" : L"DIMENSIONS LAYER");
    const int half_w = (panel_width - 6) / 2;
    const std::wstring vis_text = any_dim_visible ? (is_vi ? L"👁 Ẩn tất cả Dim" : L"👁 Hide All") : (is_vi ? L"👁 Hiện tất cả Dim" : L"👁 Show All");
    draw_panel_btn(panel_left, content_y + 64, half_w, 24, vis_text.c_str(), false);

    const std::wstring lock_text = any_dim_unlocked ? (is_vi ? L"🔒 Khóa tất cả" : L"🔒 Lock All") : (is_vi ? L"🔓 Mở tất cả" : L"🔓 Unlock All");
    draw_panel_btn(panel_left + half_w + 6, content_y + 64, half_w, 24, lock_text.c_str(), false);

    // 3. Direction & Dimension Modes
    draw_heading(content_y + 96, is_vi ? L"CHẾ ĐỘ ĐO & KHÓA TRỤC" : L"DIMENSION MODES");
    const int third_w = (panel_width - 8) / 3;

    const bool is_aligned = (cur_kind == document::DimensionKind::Aligned && cur_ori == geometry::LinearOrientation::Aligned);
    const bool is_horiz = (cur_ori == geometry::LinearOrientation::Horizontal);
    const bool is_vert = (cur_ori == geometry::LinearOrientation::Vertical);
    const bool is_axis45 = dimension_tool_ && dimension_tool_->axis_lock() == tools::DimensionAxisLock::Angle45;
    const bool is_radius = (cur_kind == document::DimensionKind::Radius || cur_kind == document::DimensionKind::Diameter);
    const bool is_angular = (cur_kind == document::DimensionKind::Angular);

    draw_panel_btn(panel_left, content_y + 116, third_w, 24, is_vi ? L"Xiên" : L"Aligned", is_aligned);
    draw_panel_btn(panel_left + third_w + 4, content_y + 116, third_w, 24, is_vi ? L"Ngang" : L"Horiz", is_horiz);
    draw_panel_btn(panel_left + 2 * (third_w + 4), content_y + 116, third_w, 24, is_vi ? L"Dọc" : L"Vert", is_vert);

    draw_panel_btn(panel_left, content_y + 144, third_w, 24, L"45°", is_axis45);
    draw_panel_btn(panel_left + third_w + 4, content_y + 144, third_w, 24, L"R / Ø", is_radius);
    draw_panel_btn(panel_left + 2 * (third_w + 4), content_y + 144, third_w, 24, is_vi ? L"Góc" : L"Angle", is_angular);

    const bool is_cont = dimension_tool_ && dimension_tool_->continuous_mode();
    const bool is_base = dimension_tool_ && dimension_tool_->baseline_mode();
    draw_panel_btn(panel_left, content_y + 172, half_w, 24, is_vi ? L"🔗 Dim liên tục" : L"🔗 Continue", is_cont);
    draw_panel_btn(panel_left + half_w + 6, content_y + 172, half_w, 24, is_vi ? L"📶 Xếp tầng" : L"📶 Baseline", is_base);

    // 4. Style Settings & Layout
    draw_heading(content_y + 204, is_vi ? L"THUỘC TÍNH & BỐ CỤC CHỮ" : L"STYLE & TEXT PLACEMENT");

    // Unit & Decimals
    std::wstring u_label = L"ĐV: mm ▾";
    if (cur_style.units == document::UnitType::Centimeter) u_label = L"ĐV: cm ▾";
    else if (cur_style.units == document::UnitType::Meter) u_label = L"ĐV: m ▾";
    else if (cur_style.units == document::UnitType::Inch) u_label = L"ĐV: in ▾";
    else if (cur_style.units == document::UnitType::Foot) u_label = L"ĐV: ft ▾";
    draw_panel_btn(panel_left, content_y + 224, half_w, 24, u_label.c_str(), false);

    std::wstring d_label = L"Số lẻ: 0.00 ▾";
    if (cur_style.decimals == 0) d_label = L"Số lẻ: 0 ▾";
    else if (cur_style.decimals == 1) d_label = L"Số lẻ: 0.0 ▾";
    else if (cur_style.decimals == 3) d_label = L"Số lẻ: 0.000 ▾";
    draw_panel_btn(panel_left + half_w + 6, content_y + 224, half_w, 24, d_label.c_str(), false);

    // Arrowhead
    std::wstring arr_name = is_vi ? L"Mũi tên: ◀-▶ Đặc ▾" : L"Arrow: ◀-▶ Filled ▾";
    if (cur_style.arrow_type == geometry::ArrowType::ArchitecturalTick)
        arr_name = is_vi ? L"Mũi tên: /--/ Kiến trúc ▾" : L"Arrow: /--/ Tick ▾";
    else if (cur_style.arrow_type == geometry::ArrowType::Dot)
        arr_name = is_vi ? L"Mũi tên: ●--● Chấm tròn ▾" : L"Arrow: ●--● Dot ▾";
    else if (cur_style.arrow_type == geometry::ArrowType::Open)
        arr_name = is_vi ? L"Mũi tên: <- > Hở ▾" : L"Arrow: <- > Open ▾";
    draw_panel_btn(panel_left, content_y + 252, panel_width, 24, arr_name.c_str(), false);

    // Stroke width & Text size
    std::wstring lw_str = L"Nét: " + format_mm_display(cur_style.line_width_mm, display_length_unit_, 4) + L" ▾";
    draw_panel_btn(panel_left, content_y + 280, half_w, 24, lw_str.c_str(), false);

    std::wstring ts_str = L"Chữ: " + format_mm_display(cur_style.text_size_mm, display_length_unit_, 4) + L" ▾";
    draw_panel_btn(panel_left + half_w + 6, content_y + 280, half_w, 24, ts_str.c_str(), false);

    // Text position
    std::wstring pos_str = (cur_style.text_position == document::DimensionTextPosition::Above)
        ? (is_vi ? L"Vị trí: Trên đường nét (TCVN/ISO) ▾" : L"Text: Above Line (ISO) ▾")
        : (is_vi ? L"Vị trí: Nằm giữa ngắt nét (AutoCAD) ▾" : L"Text: In-line Break (CAD) ▾");
    draw_panel_btn(panel_left, content_y + 308, panel_width, 24, pos_str.c_str(), false);

    // Batch Apply Button
    draw_panel_btn(panel_left, content_y + 338, panel_width, 28,
                   is_vi ? L"⚡ Áp dụng cho tất cả đường Dim" : L"⚡ Apply Style to All Dimensions", false, true);

    // 5. Quick Guide
    draw_heading(content_y + 376, is_vi ? L"HƯỚNG DẪN SỬ DỤNG" : L"QUICK GUIDE");
    RECT guide_box = { panel_left, content_y + 396, panel_right, content_y + 514 };
    HBRUSH guide_b = CreateSolidBrush(is_dark ? RGB(18, 27, 41) : RGB(248, 250, 252));
    HPEN guide_p = CreatePen(PS_SOLID, 1, col_btn_bdr);
    HGDIOBJ ogp = SelectObject(mem_dc, guide_p);
    HGDIOBJ ogb = SelectObject(mem_dc, guide_b);
    RoundRect(mem_dc, guide_box.left, guide_box.top, guide_box.right, guide_box.bottom, 6, 6);
    SelectObject(mem_dc, ogp);
    SelectObject(mem_dc, ogb);
    DeleteObject(guide_p);
    DeleteObject(guide_b);

    RECT guide_text = { panel_left + 8, content_y + 402, panel_right - 8, content_y + 508 };
    SelectObject(mem_dc, font_secondary_);
    SetTextColor(mem_dc, col_text_muted);
    const wchar_t* guide_msg = is_vi
        ? L"• Nhấp 2 điểm hoặc kéo để đo khoảng cách.\n"
          L"• Di chuyển chuột để đặt vị trí đường kích thước.\n"
          L"• Bật [🔗 Nối tiếp] để đo chuỗi thẳng hàng liên tục.\n"
          L"• Khóa góc: Giữ Shift hoặc chọn [Ngang]/[Dọc]/[45°].\n"
          L"• Bắt điểm OSnap tự động: ▲ Giữa, ◎ Tâm, ⦜ Vuông góc."
        : L"• Click 2 points or drag to measure.\n"
          L"• Move cursor to place dimension line.\n"
          L"• Turn on [🔗 Continue] for collinear chain.\n"
          L"• Axis lock: Hold Shift or pick Horiz/Vert/45°.\n"
          L"• Smart Snap: ▲ Midpoint, ◎ Center, ⦜ Perpendicular.";
    DrawTextW(mem_dc, guide_msg, -1, &guide_text, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
}

bool NativeWindow::handle_dimension_dock_click(int x, int y) {
    if (!dimension_tool_) {
        try {
            dimension_tool_ = std::make_unique<tools::DimensionToolController>(root().document());
            apply_preferred_dimension_unit();
        } catch (...) {
            return false;
        }
    }

    const int w = width_;
    const int insp_x = w - 280;
    const int panel_left = insp_x + 14;
    const int panel_right = insp_x + 280 - 14;
    const int panel_width = panel_right - panel_left;
    const int half_w = (panel_width - 6) / 2;
    const int third_w = (panel_width - 8) / 3;
    const int content_y = 138 - properties_dock_scroll_;

    if (x < panel_left || x > panel_right) return false;

    // 1. Layer Visibility & Lock (y: content_y + 64 .. 88)
    if (y >= content_y + 64 && y <= content_y + 88) {
        if (x <= panel_left + half_w) {
            toggle_all_dimensions_visibility();
            return true;
        } else if (x >= panel_left + half_w + 6) {
            toggle_all_dimensions_lock();
            return true;
        }
    }

    // 2. Modes Row 1 (y: content_y + 116 .. 140)
    if (y >= content_y + 116 && y <= content_y + 140) {
        if (x <= panel_left + third_w) {
            dimension_tool_->activate(document::DimensionKind::Aligned, false);
            dimension_tool_->set_linear_orientation(geometry::LinearOrientation::Aligned);
            dimension_tool_->set_axis_lock(tools::DimensionAxisLock::Aligned);
            set_status_message(language_ == UILanguage::Vietnamese ? "Chế độ đo: Xiên / Song song" : "Dimension mode: Aligned");
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (x >= panel_left + third_w + 4 && x <= panel_left + 2 * third_w + 4) {
            dimension_tool_->activate(document::DimensionKind::Linear, false);
            dimension_tool_->set_linear_orientation(geometry::LinearOrientation::Horizontal);
            dimension_tool_->set_axis_lock(tools::DimensionAxisLock::Horizontal);
            set_status_message(language_ == UILanguage::Vietnamese ? "Chế độ đo: Khóa Ngang (Horizontal)" : "Dimension mode: Horizontal");
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (x >= panel_left + 2 * (third_w + 4)) {
            dimension_tool_->activate(document::DimensionKind::Linear, false);
            dimension_tool_->set_linear_orientation(geometry::LinearOrientation::Vertical);
            dimension_tool_->set_axis_lock(tools::DimensionAxisLock::Vertical);
            set_status_message(language_ == UILanguage::Vietnamese ? "Chế độ đo: Khóa Dọc (Vertical)" : "Dimension mode: Vertical");
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
    }

    // Modes Row 2 (y: content_y + 144 .. 168)
    if (y >= content_y + 144 && y <= content_y + 168) {
        if (x <= panel_left + third_w) {
            dimension_tool_->set_axis_lock(tools::DimensionAxisLock::Angle45);
            set_status_message(language_ == UILanguage::Vietnamese ? "Chế độ đo: Khóa góc 45°" : "Dimension mode: Angle 45° lock");
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (x >= panel_left + third_w + 4 && x <= panel_left + 2 * third_w + 4) {
            dimension_tool_->activate(document::DimensionKind::Radius, false);
            set_status_message(language_ == UILanguage::Vietnamese ? "Chế độ đo: Bán kính / Đường kính" : "Dimension mode: Radius / Diameter");
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (x >= panel_left + 2 * (third_w + 4)) {
            dimension_tool_->activate(document::DimensionKind::Angular, false);
            set_status_message(language_ == UILanguage::Vietnamese ? "Chế độ đo: Góc" : "Dimension mode: Angular");
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
    }

    // Modes Row 3 (y: content_y + 172 .. 196): Continue & Baseline
    if (y >= content_y + 172 && y <= content_y + 196) {
        if (x <= panel_left + half_w) {
            const bool new_mode = !dimension_tool_->continuous_mode();
            dimension_tool_->set_continuous_mode(new_mode);
            if (new_mode) dimension_tool_->set_baseline_mode(false);
            set_status_message(new_mode
                ? (language_ == UILanguage::Vietnamese ? "🔗 Chế độ Dim liên tục: BẬT" : "🔗 Continue Dimension: ON")
                : (language_ == UILanguage::Vietnamese ? "Chế độ Dim liên tục: TẮT" : "Continue Dimension: OFF"));
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (x >= panel_left + half_w + 6) {
            const bool new_mode = !dimension_tool_->baseline_mode();
            dimension_tool_->set_baseline_mode(new_mode);
            if (new_mode) dimension_tool_->set_continuous_mode(false);
            set_status_message(new_mode
                ? (language_ == UILanguage::Vietnamese ? "📶 Chế độ Dim xếp tầng (Baseline): BẬT" : "📶 Baseline Dimension: ON")
                : (language_ == UILanguage::Vietnamese ? "Chế độ Dim xếp tầng: TẮT" : "Baseline Dimension: OFF"));
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
    }

    // 3. Dropdowns in Dock
    POINT pt = { x, y };
    ClientToScreen(hwnd_, &pt);

    // Unit & Decimals (y: content_y + 224 .. 248)
    if (y >= content_y + 224 && y <= content_y + 248) {
        if (x <= panel_left + half_w) {
            show_dimension_unit_menu(pt.x, pt.y);
            return true;
        } else if (x >= panel_left + half_w + 6) {
            show_dimension_decimals_menu(pt.x, pt.y);
            return true;
        }
    }

    // Arrowhead (y: content_y + 252 .. 276)
    if (y >= content_y + 252 && y <= content_y + 276) {
        show_dimension_arrow_type_menu(pt.x, pt.y);
        return true;
    }

    // Stroke width & Text size (y: content_y + 280 .. 304)
    if (y >= content_y + 280 && y <= content_y + 304) {
        if (x <= panel_left + half_w) {
            show_dimension_line_width_menu(pt.x, pt.y);
            return true;
        } else if (x >= panel_left + half_w + 6) {
            show_dimension_text_size_menu(pt.x, pt.y);
            return true;
        }
    }

    // Text position (y: content_y + 308 .. 332)
    if (y >= content_y + 308 && y <= content_y + 332) {
        show_dimension_text_position_menu(pt.x, pt.y);
        return true;
    }

    // Batch Apply (y: content_y + 338 .. 366)
    if (y >= content_y + 338 && y <= content_y + 366) {
        apply_dimension_style_to_all();
        return true;
    }

    return false;
}

void NativeWindow::toggle_all_dimensions_visibility() {
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return;

    bool any_visible = false;
    std::vector<document::ObjectId> dim_ids;
    for (const auto& [id, obj] : snapshot->objects().all()) {
        if (const auto* path = std::get_if<document::PathObject>(&obj)) {
            if (path->contour_spec.rfind("dimension:v1", 0) == 0) {
                dim_ids.push_back(id);
                if (path->visible) any_visible = true;
            }
        }
    }
    const bool target_vis = !any_visible;
    for (auto id : dim_ids) {
        root().document().execute_command(std::make_unique<document::SetObjectVisibilityCommand>(id, target_vis));
    }
    const bool is_vi = (language_ == UILanguage::Vietnamese);
    set_status_message(target_vis
        ? (is_vi ? "Đã hiện toàn bộ lớp đường kích thước" : "All dimensions are now visible")
        : (is_vi ? "Đã ẩn toàn bộ lớp đường kích thước" : "All dimensions are now hidden"));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::toggle_all_dimensions_lock() {
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return;

    bool any_unlocked = false;
    std::vector<document::ObjectId> dim_ids;
    for (const auto& [id, obj] : snapshot->objects().all()) {
        if (const auto* path = std::get_if<document::PathObject>(&obj)) {
            if (path->contour_spec.rfind("dimension:v1", 0) == 0) {
                dim_ids.push_back(id);
                if (!path->locked) any_unlocked = true;
            }
        }
    }
    const bool target_locked = any_unlocked;
    for (auto id : dim_ids) {
        root().document().execute_command(std::make_unique<document::SetObjectLockedCommand>(id, target_locked));
    }
    const bool is_vi = (language_ == UILanguage::Vietnamese);
    set_status_message(target_locked
        ? (is_vi ? "Đã khóa toàn bộ lớp đường kích thước (tránh click nhầm)" : "All dimensions are now locked")
        : (is_vi ? "Đã mở khóa toàn bộ lớp đường kích thước" : "All dimensions are now unlocked"));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::apply_dimension_style_to_all() {
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return;

    document::DimensionStyle cur_style = dimension_tool_ ? dimension_tool_->current_style() : document::DimensionStyle::make_default();
    const auto& selection = root().vector_controller().selection().selected_ids();
    if (selection.size() == 1) {
        const auto sel_dim = document::get_dimension(root().document(), selection[0]);
        if (sel_dim) cur_style = sel_dim->style;
    }

    size_t count = 0;
    for (const auto& [id, obj] : snapshot->objects().all()) {
        if (const auto* path = std::get_if<document::PathObject>(&obj)) {
            if (path->contour_spec.rfind("dimension:v1", 0) == 0) {
                root().document().execute_command(std::make_unique<document::UpdateDimensionStyleCommand>(id, cur_style));
                count++;
            }
        }
    }

    const bool is_vi = (language_ == UILanguage::Vietnamese);
    set_status_message(is_vi
        ? ("Đã áp dụng kiểu dáng cho toàn bộ " + std::to_string(count) + " đường kích thước").c_str()
        : ("Updated style for all " + std::to_string(count) + " dimensions").c_str());
    InvalidateRect(hwnd_, nullptr, FALSE);
}

} // namespace pikdraw::desktop
#endif
