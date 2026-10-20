#include "pikdraw/desktop/shell/native_window.hpp"
#include "../tools/dimension_tool_controller.hpp"
#include "pikdraw/document/annotations/dimension_style.hpp"
#include "pikdraw/document/annotations/dimension_object.hpp"
#include "pikdraw/document/annotations/dimension_layout.hpp"
#include "pikdraw/document/commands/dimension_commands.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace pikdraw::desktop {

bool NativeWindow::is_dimension_object_selected() const {
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return false;
    const auto& selection = root().vector_controller().selection().selected_ids();
    if (selection.size() != 1) return false;
    const auto obj = snapshot->objects().get(selection[0]);
    if (!obj.has_value()) return false;
    const auto* path = std::get_if<document::PathObject>(&(*obj));
    return path != nullptr && path->contour_spec.rfind("dimension:v1", 0) == 0;
}

void NativeWindow::paint_dimension_property_bar(HDC mem_dc, int w, int content_top, int topbar_h, int tabbar_h) {
    ensure_ui_fonts();
    const bool is_dark = (theme_ == ThemeMode::CyberDark);
    const bool is_vi = (language_ == UILanguage::Vietnamese);

    (void)w;
    const COLORREF col_btn_bg = is_dark ? RGB(19, 28, 43) : RGB(241, 245, 249);
    const COLORREF col_btn_bdr = is_dark ? RGB(37, 50, 72) : RGB(203, 213, 225);
    const COLORREF col_teal = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    const COLORREF col_text_white = is_dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    const COLORREF col_active_tab = is_dark ? RGB(22, 37, 54) : RGB(236, 242, 248);

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
        SelectObject(mem_dc, active ? font_be_vietnam_bold_ : font_be_vietnam_);
        DrawTextW(mem_dc, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    };

    // Label: "Dim:" / "Kích thước:"
    SelectObject(mem_dc, font_be_vietnam_bold_);
    SetTextColor(mem_dc, col_teal);
    RECT rc_lbl = { 10, topbar_h + tabbar_h, 48, content_top };
    DrawTextW(mem_dc, is_vi ? L"Dim:" : L"Dim:", -1, &rc_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Current Style
    document::DimensionStyle cur_style = dimension_tool_ ? dimension_tool_->current_style() : document::DimensionStyle::make_default();
    geometry::LinearOrientation cur_ori = dimension_tool_ ? dimension_tool_->linear_orientation() : geometry::LinearOrientation::Aligned;
    document::DimensionKind cur_kind = dimension_tool_ ? dimension_tool_->current_kind() : document::DimensionKind::Aligned;

    const auto snapshot = root().document().current_snapshot();
    const auto& selection = root().vector_controller().selection().selected_ids();
    if (selection.size() == 1 && snapshot) {
        const auto sel_dim = document::get_dimension(root().document(), selection[0]);
        if (sel_dim) {
            cur_style = sel_dim->style;
            cur_ori = sel_dim->linear_orientation;
            cur_kind = sel_dim->kind;
        }
    }

    // 1. Orientation / Kind buttons
    const bool is_aligned = (cur_kind == document::DimensionKind::Aligned && cur_ori == geometry::LinearOrientation::Aligned);
    const bool is_horiz = (cur_ori == geometry::LinearOrientation::Horizontal);
    const bool is_vert = (cur_ori == geometry::LinearOrientation::Vertical);
    const bool is_radius = (cur_kind == document::DimensionKind::Radius || cur_kind == document::DimensionKind::Diameter);
    const bool is_angular = (cur_kind == document::DimensionKind::Angular);

    draw_rib_btn(52, 112, is_vi ? L"Xiên" : L"Aligned", is_aligned, is_aligned ? col_teal : col_text_white);
    draw_rib_btn(116, 174, is_vi ? L"Ngang" : L"Horiz", is_horiz, is_horiz ? col_teal : col_text_white);
    draw_rib_btn(178, 230, is_vi ? L"Dọc" : L"Vert", is_vert, is_vert ? col_teal : col_text_white);
    draw_rib_btn(234, 286, is_vi ? L"R / Ø" : L"Radius", is_radius, is_radius ? col_teal : col_text_white);
    draw_rib_btn(290, 342, is_vi ? L"Góc" : L"Angle", is_angular, is_angular ? col_teal : col_text_white);

    // 2. Continuous Dimension & Quick Dimension Tools
    const bool is_cont = dimension_tool_ && dimension_tool_->continuous_mode();
    draw_rib_btn(346, 438, is_vi ? L"🔗 Dim liên tục" : L"🔗 Continue", is_cont, is_cont ? col_teal : col_text_white);
    draw_rib_btn(442, 534, is_vi ? L"⚡ Đo nhanh W×H" : L"⚡ Quick W×H", false, col_teal);
    draw_rib_btn(538, 626, is_vi ? L"🔄 Đo chu vi" : L"🔄 Perimeter", false, col_teal);

    // 3. Decimal precision: 0, 0.0, 0.00, 0.000
    std::wstring dec_lbl = L"0.00";
    if (cur_style.decimals == 0) dec_lbl = L"0";
    else if (cur_style.decimals == 1) dec_lbl = L"0.0";
    else if (cur_style.decimals == 2) dec_lbl = L"0.00";
    else if (cur_style.decimals == 3) dec_lbl = L"0.000";
    draw_rib_btn(630, 686, dec_lbl.c_str(), false, col_text_white);

    // 4. Units: mm, cm, m, in
    std::wstring unit_lbl = L"mm ▾";
    if (cur_style.units == document::UnitType::Centimeter) unit_lbl = L"cm ▾";
    else if (cur_style.units == document::UnitType::Meter) unit_lbl = L"m ▾";
    else if (cur_style.units == document::UnitType::Inch) unit_lbl = L"in ▾";
    draw_rib_btn(690, 748, unit_lbl.c_str(), false, col_text_white);

    // 5. Arrowhead style: ◀-▶, /--/, ●--●
    std::wstring arr_lbl = L"◀-▶ ▾";
    if (cur_style.arrow_type == geometry::ArrowType::ArchitecturalTick) arr_lbl = L"/--/ ▾";
    else if (cur_style.arrow_type == geometry::ArrowType::Dot) arr_lbl = L"●--● ▾";
    draw_rib_btn(752, 812, arr_lbl.c_str(), false, col_text_white);

    // 6. Line Width: 0.15, 0.25, 0.5, 0.1
    std::wostringstream lw_ss;
    lw_ss << std::fixed << std::setprecision(2) << cur_style.line_width_mm << L" mm ▾";
    draw_rib_btn(816, 886, lw_ss.str().c_str(), false, col_text_white);

    // 7. Text size: 2.5, 3.0, 3.5, 5.0
    std::wostringstream ts_ss;
    ts_ss << L"T: " << std::fixed << std::setprecision(1) << cur_style.text_size_mm << L" mm";
    draw_rib_btn(890, 960, ts_ss.str().c_str(), false, col_text_white);

    // 8. Smart Snap status badge
    draw_rib_btn(964, 1056, is_vi ? L"⌖ Bắt điểm: BẬT" : L"⌖ Snap: ON", true, col_teal);
}

bool NativeWindow::handle_dimension_property_bar_click(int x, int y) {
    (void)y;
    if (!dimension_tool_) {
        try {
            dimension_tool_ = std::make_unique<tools::DimensionToolController>(root().document());
        } catch (...) {
            return false;
        }
    }

    if (x >= 52 && x <= 112) {
        dimension_tool_->activate(document::DimensionKind::Aligned, false);
        dimension_tool_->set_linear_orientation(geometry::LinearOrientation::Aligned);
        set_status_message(language_ == UILanguage::Vietnamese ? "Chế độ đo: Xiên / Song song (Aligned)" : "Dimension mode: Aligned");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (x >= 116 && x <= 174) {
        dimension_tool_->activate(document::DimensionKind::Linear, false);
        dimension_tool_->set_linear_orientation(geometry::LinearOrientation::Horizontal);
        set_status_message(language_ == UILanguage::Vietnamese ? "Chế độ đo: Ngang (Horizontal)" : "Dimension mode: Horizontal");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (x >= 178 && x <= 230) {
        dimension_tool_->activate(document::DimensionKind::Linear, false);
        dimension_tool_->set_linear_orientation(geometry::LinearOrientation::Vertical);
        set_status_message(language_ == UILanguage::Vietnamese ? "Chế độ đo: Dọc (Vertical)" : "Dimension mode: Vertical");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (x >= 234 && x <= 286) {
        dimension_tool_->activate(document::DimensionKind::Radius, false);
        set_status_message(language_ == UILanguage::Vietnamese ? "Chế độ đo: Bán kính / Đường kính (Radius)" : "Dimension mode: Radius");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (x >= 290 && x <= 342) {
        dimension_tool_->activate(document::DimensionKind::Angular, false);
        set_status_message(language_ == UILanguage::Vietnamese ? "Chế độ đo: Góc (Angular)" : "Dimension mode: Angular");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    // Continuous Dimension (🔗 Dim liên tục)
    if (x >= 346 && x <= 438) {
        const auto& sel = root().vector_controller().selection().selected_ids();
        if (sel.size() == 1) {
            const auto sel_dim = document::get_dimension(root().document(), sel[0]);
            if (sel_dim && sel_dim->references.size() >= 2) {
                const auto base_pt = sel_dim->references[1].explicit_point;
                dimension_tool_->start_continue_from(base_pt, sel_dim->placement_point, sel_dim->kind, sel_dim->linear_orientation);
                active_tool_ = 10;
                set_status_message(language_ == UILanguage::Vietnamese
                    ? "🔗 Dim liên tục: Đã bắt điểm cuối, nhấp điểm tiếp theo để tạo chuỗi kích thước (Esc để dừng)"
                    : "🔗 Continue Dimension: Started from end point, click next point to chain (Esc to stop)");
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
        }
        const bool new_mode = !dimension_tool_->continuous_mode();
        dimension_tool_->set_continuous_mode(new_mode);
        set_status_message(new_mode
            ? (language_ == UILanguage::Vietnamese ? "🔗 Chế độ Dim liên tục: BẬT (1-click nối tiếp các đoạn đo)" : "🔗 Continuous Dimension: ON")
            : (language_ == UILanguage::Vietnamese ? "Chế độ Dim liên tục: TẮT" : "Continuous Dimension: OFF"));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    // Quick Dimension W×H
    if (x >= 442 && x <= 534) {
        execute_quick_dimension();
        return true;
    }

    // Perimeter Dimension
    if (x >= 538 && x <= 626) {
        execute_perimeter_dimension();
        return true;
    }

    // Cycle Decimals
    if (x >= 630 && x <= 686) {
        cycle_dimension_decimals();
        return true;
    }

    // Cycle Units
    if (x >= 690 && x <= 748) {
        cycle_dimension_unit();
        return true;
    }

    // Cycle Arrow Type
    if (x >= 752 && x <= 812) {
        cycle_dimension_arrow_type();
        return true;
    }

    // Cycle Line Width
    if (x >= 816 && x <= 886) {
        cycle_dimension_line_width();
        return true;
    }

    // Cycle Text Size
    if (x >= 890 && x <= 960) {
        cycle_dimension_text_size();
        return true;
    }

    // Toggle snap indicator
    if (x >= 964 && x <= 1056) {
        set_status_message(language_ == UILanguage::Vietnamese ? "Bắt điểm thông minh (OSnap): Đang bật" : "Smart Snap (OSnap): Active");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    return false;
}

void NativeWindow::execute_quick_dimension() {
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return;

    const auto& selection = root().vector_controller().selection().selected_ids();
    geometry::Rect2D target_bounds{0.0, 0.0, 100.0, 60.0};
    document::ObjectId target_id = document::kInvalidObjectId;

    if (!selection.empty()) {
        target_id = selection[0];
        const auto b_opt = workflow().get_object_bounds(target_id);
        if (b_opt) target_bounds = *b_opt;
    } else {
        // Measure first visible object if nothing selected
        for (const auto& [id, obj] : snapshot->objects().all()) {
            if (const auto* path = std::get_if<document::PathObject>(&obj)) {
                if (path->visible && !path->contour_spec.starts_with("dimension:v1")) {
                    target_id = id;
                    target_bounds = path->bounds();
                    break;
                }
            }
        }
    }

    document::DimensionStyle cur_style = dimension_tool_ ? dimension_tool_->current_style() : document::DimensionStyle::make_default();

    // 1. Horizontal dimension (Width)
    document::DimensionObject dim_w;
    dim_w.id = document::generate_object_id();
    dim_w.name = "Dim W";
    dim_w.kind = document::DimensionKind::Linear;
    dim_w.linear_orientation = geometry::LinearOrientation::Horizontal;
    dim_w.placement_point = {target_bounds.x + target_bounds.width * 0.5, target_bounds.y - 12.0};
    dim_w.style = cur_style;

    document::SourceReference rw1;
    rw1.source_object_id = target_id;
    rw1.ref_kind = document::SourceRefKind::ExplicitPoint;
    rw1.explicit_point = {target_bounds.x, target_bounds.y};

    document::SourceReference rw2;
    rw2.source_object_id = target_id;
    rw2.ref_kind = document::SourceRefKind::ExplicitPoint;
    rw2.explicit_point = {target_bounds.right(), target_bounds.y};

    dim_w.references = {rw1, rw2};
    root().document().execute_command(std::make_unique<document::AddDimensionCommand>(dim_w, 0));

    // 2. Vertical dimension (Height)
    document::DimensionObject dim_h;
    dim_h.id = document::generate_object_id();
    dim_h.name = "Dim H";
    dim_h.kind = document::DimensionKind::Linear;
    dim_h.linear_orientation = geometry::LinearOrientation::Vertical;
    dim_h.placement_point = {target_bounds.right() + 12.0, target_bounds.y + target_bounds.height * 0.5};
    dim_h.style = cur_style;

    document::SourceReference rh1;
    rh1.source_object_id = target_id;
    rh1.ref_kind = document::SourceRefKind::ExplicitPoint;
    rh1.explicit_point = {target_bounds.right(), target_bounds.y};

    document::SourceReference rh2;
    rh2.source_object_id = target_id;
    rh2.ref_kind = document::SourceRefKind::ExplicitPoint;
    rh2.explicit_point = {target_bounds.right(), target_bounds.bottom()};

    dim_h.references = {rh1, rh2};
    root().document().execute_command(std::make_unique<document::AddDimensionCommand>(dim_h, 0));

    set_status_message(language_ == UILanguage::Vietnamese
        ? "Đã tạo kích thước nhanh W×H thành công"
        : "Quick dimensions WxH created successfully");
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::execute_perimeter_dimension() {
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return;

    const auto& selection = root().vector_controller().selection().selected_ids();
    geometry::Rect2D target_bounds{0.0, 0.0, 100.0, 60.0};
    double perimeter = 320.0;
    document::ObjectId target_id = document::kInvalidObjectId;

    if (!selection.empty()) {
        target_id = selection[0];
        const auto b_opt = workflow().get_object_bounds(target_id);
        if (b_opt) {
            target_bounds = *b_opt;
            perimeter = 2.0 * (target_bounds.width + target_bounds.height);
        }
    } else {
        for (const auto& [id, obj] : snapshot->objects().all()) {
            if (const auto* path = std::get_if<document::PathObject>(&obj)) {
                if (path->visible && !path->contour_spec.starts_with("dimension:v1")) {
                    target_id = id;
                    target_bounds = path->bounds();
                    perimeter = 2.0 * (target_bounds.width + target_bounds.height);
                    break;
                }
            }
        }
    }

    document::DimensionStyle cur_style = dimension_tool_ ? dimension_tool_->current_style() : document::DimensionStyle::make_default();

    document::DimensionObject dim_p;
    dim_p.id = document::generate_object_id();
    dim_p.name = "Chu vi";
    dim_p.kind = document::DimensionKind::MultiLeader;
    dim_p.placement_point = {target_bounds.x - 14.0, target_bounds.y - 14.0};
    dim_p.note_template = (language_ == UILanguage::Vietnamese ? "Chu vi: " : "Perimeter: ") +
                          document::format_measurement_text(perimeter, cur_style, false);
    dim_p.style = cur_style;

    document::SourceReference ref;
    ref.source_object_id = target_id;
    ref.ref_kind = document::SourceRefKind::ExplicitPoint;
    ref.explicit_point = {target_bounds.x, target_bounds.y};
    dim_p.references = {ref};

    root().document().execute_command(std::make_unique<document::AddDimensionCommand>(dim_p, 0));

    set_status_message(language_ == UILanguage::Vietnamese
        ? "Đã tính và ghi chú chu vi thành công"
        : "Perimeter calculated and annotated successfully");
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::cycle_dimension_unit() {
    document::DimensionStyle style = dimension_tool_ ? dimension_tool_->current_style() : document::DimensionStyle::make_default();
    if (style.units == document::UnitType::Millimeter) style.units = document::UnitType::Centimeter;
    else if (style.units == document::UnitType::Centimeter) style.units = document::UnitType::Meter;
    else if (style.units == document::UnitType::Meter) style.units = document::UnitType::Inch;
    else style.units = document::UnitType::Millimeter;

    if (dimension_tool_) dimension_tool_->set_current_style(style);

    const auto& selection = root().vector_controller().selection().selected_ids();
    if (selection.size() == 1) {
        root().document().execute_command(std::make_unique<document::UpdateDimensionStyleCommand>(selection[0], style));
    }

    set_status_message(language_ == UILanguage::Vietnamese ? "Đã đổi đơn vị đo kích thước" : "Dimension unit changed");
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::cycle_dimension_decimals() {
    document::DimensionStyle style = dimension_tool_ ? dimension_tool_->current_style() : document::DimensionStyle::make_default();
    if (style.decimals == 2) style.decimals = 0;
    else if (style.decimals == 0) style.decimals = 1;
    else if (style.decimals == 1) style.decimals = 3;
    else style.decimals = 2;

    if (dimension_tool_) dimension_tool_->set_current_style(style);

    const auto& selection = root().vector_controller().selection().selected_ids();
    if (selection.size() == 1) {
        root().document().execute_command(std::make_unique<document::UpdateDimensionStyleCommand>(selection[0], style));
    }

    set_status_message(language_ == UILanguage::Vietnamese ? "Đã đổi độ chính xác số thập phân" : "Dimension precision changed");
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::cycle_dimension_arrow_type() {
    document::DimensionStyle style = dimension_tool_ ? dimension_tool_->current_style() : document::DimensionStyle::make_default();
    if (style.arrow_type == geometry::ArrowType::ClosedFilled) style.arrow_type = geometry::ArrowType::ArchitecturalTick;
    else if (style.arrow_type == geometry::ArrowType::ArchitecturalTick) style.arrow_type = geometry::ArrowType::Dot;
    else style.arrow_type = geometry::ArrowType::ClosedFilled;

    if (dimension_tool_) dimension_tool_->set_current_style(style);

    const auto& selection = root().vector_controller().selection().selected_ids();
    if (selection.size() == 1) {
        root().document().execute_command(std::make_unique<document::UpdateDimensionStyleCommand>(selection[0], style));
    }

    set_status_message(language_ == UILanguage::Vietnamese ? "Đã đổi kiểu mũi tên đầu đo (Arrowhead)" : "Arrowhead style changed");
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::cycle_dimension_line_width() {
    document::DimensionStyle style = dimension_tool_ ? dimension_tool_->current_style() : document::DimensionStyle::make_default();
    if (std::abs(style.line_width_mm - 0.15) < 0.01) style.line_width_mm = 0.25;
    else if (std::abs(style.line_width_mm - 0.25) < 0.01) style.line_width_mm = 0.50;
    else if (std::abs(style.line_width_mm - 0.50) < 0.01) style.line_width_mm = 0.10;
    else style.line_width_mm = 0.15;

    style.ext_width_mm = style.line_width_mm;
    if (dimension_tool_) dimension_tool_->set_current_style(style);

    const auto& selection = root().vector_controller().selection().selected_ids();
    if (selection.size() == 1) {
        root().document().execute_command(std::make_unique<document::UpdateDimensionStyleCommand>(selection[0], style));
    }

    set_status_message(language_ == UILanguage::Vietnamese ? "Đã đổi độ dày nét đo (Stroke width)" : "Dimension stroke width changed");
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::cycle_dimension_text_size() {
    document::DimensionStyle style = dimension_tool_ ? dimension_tool_->current_style() : document::DimensionStyle::make_default();
    if (std::abs(style.text_size_mm - 3.0) < 0.1) style.text_size_mm = 3.5;
    else if (std::abs(style.text_size_mm - 3.5) < 0.1) style.text_size_mm = 5.0;
    else if (std::abs(style.text_size_mm - 5.0) < 0.1) style.text_size_mm = 2.5;
    else style.text_size_mm = 3.0;

    if (dimension_tool_) dimension_tool_->set_current_style(style);

    const auto& selection = root().vector_controller().selection().selected_ids();
    if (selection.size() == 1) {
        root().document().execute_command(std::make_unique<document::UpdateDimensionStyleCommand>(selection[0], style));
    }

    set_status_message(language_ == UILanguage::Vietnamese ? "Đã đổi cỡ chữ số đo (Text size)" : "Dimension text size changed");
    InvalidateRect(hwnd_, nullptr, FALSE);
}

} // namespace pikdraw::desktop
#endif
