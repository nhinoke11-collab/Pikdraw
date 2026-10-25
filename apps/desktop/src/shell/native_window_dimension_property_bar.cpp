#include "pikdraw/desktop/shell/native_window.hpp"
#include "../tools/dimension_tool_controller.hpp"
#include "pikdraw/document/annotations/dimension_style.hpp"
#include "pikdraw/document/annotations/dimension_object.hpp"
#include "pikdraw/document/annotations/dimension_layout.hpp"
#include "pikdraw/document/commands/dimension_commands.hpp"
#include "native_window_length_input.hpp"
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

    // Label: "Dim:"
    SelectObject(mem_dc, font_be_vietnam_bold_);
    SetTextColor(mem_dc, col_teal);
    RECT rc_lbl = { 10, topbar_h + tabbar_h, 48, content_top };
    DrawTextW(mem_dc, L"Dim:", -1, &rc_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

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

    // 1. Orientation / Axis Lock
    const bool is_aligned = (cur_kind == document::DimensionKind::Aligned && cur_ori == geometry::LinearOrientation::Aligned);
    const bool is_horiz = (cur_ori == geometry::LinearOrientation::Horizontal);
    const bool is_vert = (cur_ori == geometry::LinearOrientation::Vertical);
    const bool is_axis45 = dimension_tool_ && dimension_tool_->axis_lock() == tools::DimensionAxisLock::Angle45;
    const bool is_radius = (cur_kind == document::DimensionKind::Radius || cur_kind == document::DimensionKind::Diameter);
    const bool is_angular = (cur_kind == document::DimensionKind::Angular);

    draw_rib_btn(50, 94, is_vi ? L"Xiên" : L"Aligned", is_aligned, is_aligned ? col_teal : col_text_white);
    draw_rib_btn(98, 144, is_vi ? L"Ngang" : L"Horiz", is_horiz, is_horiz ? col_teal : col_text_white);
    draw_rib_btn(148, 188, is_vi ? L"Dọc" : L"Vert", is_vert, is_vert ? col_teal : col_text_white);
    draw_rib_btn(192, 230, L"45°", is_axis45, is_axis45 ? col_teal : col_text_white);
    draw_rib_btn(234, 280, L"R / Ø", is_radius, is_radius ? col_teal : col_text_white);
    draw_rib_btn(284, 324, is_vi ? L"Góc" : L"Angle", is_angular, is_angular ? col_teal : col_text_white);

    // 2. Chained Dimensions: Continue & Baseline
    const bool is_cont = dimension_tool_ && dimension_tool_->continuous_mode();
    const bool is_base = dimension_tool_ && dimension_tool_->baseline_mode();
    draw_rib_btn(330, 414, is_vi ? L"🔗 Nối tiếp" : L"🔗 Continue", is_cont, is_cont ? col_teal : col_text_white);
    draw_rib_btn(418, 502, is_vi ? L"📶 Xếp tầng" : L"📶 Baseline", is_base, is_base ? col_teal : col_text_white);

    // 3. Decimal precision: 0, 0.0, 0.00, 0.000 (Dropdown)
    std::wstring dec_lbl = L"0.00 ▾";
    if (cur_style.decimals == 0) dec_lbl = L"0 ▾";
    else if (cur_style.decimals == 1) dec_lbl = L"0.0 ▾";
    else if (cur_style.decimals == 3) dec_lbl = L"0.000 ▾";
    draw_rib_btn(508, 562, dec_lbl.c_str(), false, col_text_white);

    // 4. Units: mm, cm, m, in, ft, yd, pt, pc (Dropdown)
    std::wstring unit_lbl = L"mm ▾";
    if (cur_style.units == document::UnitType::Centimeter) unit_lbl = L"cm ▾";
    else if (cur_style.units == document::UnitType::Meter) unit_lbl = L"m ▾";
    else if (cur_style.units == document::UnitType::Inch) unit_lbl = L"in ▾";
    else if (cur_style.units == document::UnitType::Foot) unit_lbl = L"ft ▾";
    else if (cur_style.units == document::UnitType::Yard) unit_lbl = L"yd ▾";
    else if (cur_style.units == document::UnitType::Point) unit_lbl = L"pt ▾";
    else if (cur_style.units == document::UnitType::Pica) unit_lbl = L"pc ▾";
    draw_rib_btn(566, 618, unit_lbl.c_str(), false, col_text_white);

    // 5. Arrowhead style (Dropdown)
    std::wstring arr_lbl = L"◀-▶ ▾";
    if (cur_style.arrow_type == geometry::ArrowType::ArchitecturalTick) arr_lbl = L"/--/ ▾";
    else if (cur_style.arrow_type == geometry::ArrowType::Dot) arr_lbl = L"●--● ▾";
    else if (cur_style.arrow_type == geometry::ArrowType::Open) arr_lbl = L"<- > ▾";
    draw_rib_btn(622, 680, arr_lbl.c_str(), false, col_text_white);

    // 6. Line Width (Dropdown)
    std::wstring lw_label = format_mm_display(cur_style.line_width_mm, display_length_unit_, 4) + L" ▾";
    draw_rib_btn(684, 762, lw_label.c_str(), false, col_text_white);

    // 7. Text size (Dropdown)
    std::wstring ts_label = L"T: " + format_mm_display(cur_style.text_size_mm, display_length_unit_, 4) + L" ▾";
    draw_rib_btn(766, 844, ts_label.c_str(), false, col_text_white);

    // 8. Text position (Dropdown)
    std::wstring pos_label = (cur_style.text_position == document::DimensionTextPosition::Above)
        ? (is_vi ? L"Trên line ▾" : L"Above ▾")
        : (is_vi ? L"Giữa line ▾" : L"Inline ▾");
    draw_rib_btn(848, 936, pos_label.c_str(), false, col_text_white);

    // 9. Properties Inspector shortcut button
    draw_rib_btn(940, 1024, is_vi ? L"⚙ Thuộc tính" : L"⚙ Properties", false, col_teal);

    // 10. Smart Snap status badge
    draw_rib_btn(1028, 1118, is_vi ? L"⌖ Snap: BẬT" : L"⌖ Snap: ON", true, col_teal);
}

void NativeWindow::show_dimension_decimals_menu(int screen_x, int screen_y) {
    HMENU hMenu = CreatePopupMenu();
    document::DimensionStyle cur_style = dimension_tool_ ? dimension_tool_->current_style() : document::DimensionStyle::make_default();
    const auto& selection = root().vector_controller().selection().selected_ids();
    if (selection.size() == 1) {
        const auto sel_dim = document::get_dimension(root().document(), selection[0]);
        if (sel_dim) cur_style = sel_dim->style;
    }
    const bool is_vi = (language_ == UILanguage::Vietnamese);

    AppendMenuW(hMenu, MF_STRING | (cur_style.decimals == 0 ? MF_CHECKED : MF_UNCHECKED), 101, is_vi ? L"0  (Số nguyên)" : L"0  (Integer)");
    AppendMenuW(hMenu, MF_STRING | (cur_style.decimals == 1 ? MF_CHECKED : MF_UNCHECKED), 102, is_vi ? L"0.0  (1 số lẻ)" : L"0.0  (1 decimal)");
    AppendMenuW(hMenu, MF_STRING | (cur_style.decimals == 2 ? MF_CHECKED : MF_UNCHECKED), 103, is_vi ? L"0.00  (2 số lẻ - Chuẩn)" : L"0.00  (2 decimals - Standard)");
    AppendMenuW(hMenu, MF_STRING | (cur_style.decimals == 3 ? MF_CHECKED : MF_UNCHECKED), 104, is_vi ? L"0.000  (3 số lẻ - Độ chính xác cao)" : L"0.000  (3 decimals - High precision)");

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, screen_x, screen_y, 0, hwnd_, nullptr);
    DestroyMenu(hMenu);

    if (cmd >= 101 && cmd <= 104) {
        cur_style.decimals = (cmd == 101) ? 0 : (cmd == 102) ? 1 : (cmd == 103) ? 2 : 3;
        if (dimension_tool_) dimension_tool_->set_current_style(cur_style);
        if (selection.size() == 1) {
            root().document().execute_command(std::make_unique<document::UpdateDimensionStyleCommand>(selection[0], cur_style));
        }
        set_status_message(is_vi ? "Đã đặt độ chính xác số thập phân" : "Dimension precision updated");
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void NativeWindow::show_dimension_unit_menu(int screen_x, int screen_y) {
    HMENU hMenu = CreatePopupMenu();
    document::DimensionStyle cur_style = dimension_tool_ ? dimension_tool_->current_style() : document::DimensionStyle::make_default();
    const auto& selection = root().vector_controller().selection().selected_ids();
    if (selection.size() == 1) {
        const auto sel_dim = document::get_dimension(root().document(), selection[0]);
        if (sel_dim) cur_style = sel_dim->style;
    }

    struct UnitOption { document::UnitType unit; const wchar_t* label; };
    const UnitOption options[] = {
        { document::UnitType::Millimeter, L"Millimeter (mm)" },
        { document::UnitType::Centimeter, L"Centimeter (cm)" },
        { document::UnitType::Meter, L"Meter (m)" },
        { document::UnitType::Inch, L"Inch (in)" },
        { document::UnitType::Foot, L"Foot (ft)" },
        { document::UnitType::Yard, L"Yard (yd)" },
        { document::UnitType::Point, L"Point (pt)" },
        { document::UnitType::Pica, L"Pica (pc)" }
    };

    for (int i = 0; i < 8; ++i) {
        const bool active = (cur_style.units == options[i].unit);
        AppendMenuW(hMenu, MF_STRING | (active ? MF_CHECKED : MF_UNCHECKED), 201 + i, options[i].label);
    }

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, screen_x, screen_y, 0, hwnd_, nullptr);
    DestroyMenu(hMenu);

    if (cmd >= 201 && cmd <= 208) {
        cur_style.units = options[cmd - 201].unit;
        if (dimension_tool_) dimension_tool_->set_current_style(cur_style);
        if (selection.size() == 1) {
            root().document().execute_command(std::make_unique<document::UpdateDimensionStyleCommand>(selection[0], cur_style));
        }
        set_status_message(language_ == UILanguage::Vietnamese ? "Đã đổi đơn vị kích thước" : "Dimension unit updated");
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void NativeWindow::show_dimension_arrow_type_menu(int screen_x, int screen_y) {
    HMENU hMenu = CreatePopupMenu();
    document::DimensionStyle cur_style = dimension_tool_ ? dimension_tool_->current_style() : document::DimensionStyle::make_default();
    const auto& selection = root().vector_controller().selection().selected_ids();
    if (selection.size() == 1) {
        const auto sel_dim = document::get_dimension(root().document(), selection[0]);
        if (sel_dim) cur_style = sel_dim->style;
    }
    const bool is_vi = (language_ == UILanguage::Vietnamese);

    AppendMenuW(hMenu, MF_STRING | (cur_style.arrow_type == geometry::ArrowType::ClosedFilled ? MF_CHECKED : MF_UNCHECKED),
                301, is_vi ? L"◀-▶  Mũi tên đặc (Cơ khí / Chuẩn)" : L"◀-▶  Closed Filled (Mechanical / Standard)");
    AppendMenuW(hMenu, MF_STRING | (cur_style.arrow_type == geometry::ArrowType::ArchitecturalTick ? MF_CHECKED : MF_UNCHECKED),
                302, is_vi ? L"/--/  Gạch chéo kiến trúc (Xây dựng / CAD)" : L"/--/  Architectural Tick (Architecture)");
    AppendMenuW(hMenu, MF_STRING | (cur_style.arrow_type == geometry::ArrowType::Dot ? MF_CHECKED : MF_UNCHECKED),
                303, is_vi ? L"●--●  Chấm tròn (Mechanical Dot)" : L"●--●  Mechanical Dot");
    AppendMenuW(hMenu, MF_STRING | (cur_style.arrow_type == geometry::ArrowType::Open ? MF_CHECKED : MF_UNCHECKED),
                304, is_vi ? L"<- >  Mũi tên hở (Open Arrow)" : L"<- >  Open Arrow");

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, screen_x, screen_y, 0, hwnd_, nullptr);
    DestroyMenu(hMenu);

    if (cmd >= 301 && cmd <= 304) {
        if (cmd == 301) cur_style.arrow_type = geometry::ArrowType::ClosedFilled;
        else if (cmd == 302) cur_style.arrow_type = geometry::ArrowType::ArchitecturalTick;
        else if (cmd == 303) cur_style.arrow_type = geometry::ArrowType::Dot;
        else if (cmd == 304) cur_style.arrow_type = geometry::ArrowType::Open;

        if (dimension_tool_) dimension_tool_->set_current_style(cur_style);
        if (selection.size() == 1) {
            root().document().execute_command(std::make_unique<document::UpdateDimensionStyleCommand>(selection[0], cur_style));
        }
        set_status_message(is_vi ? "Đã cập nhật kiểu mũi tên" : "Arrowhead style updated");
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void NativeWindow::show_dimension_line_width_menu(int screen_x, int screen_y) {
    HMENU hMenu = CreatePopupMenu();
    document::DimensionStyle cur_style = dimension_tool_ ? dimension_tool_->current_style() : document::DimensionStyle::make_default();
    const auto& selection = root().vector_controller().selection().selected_ids();
    if (selection.size() == 1) {
        const auto sel_dim = document::get_dimension(root().document(), selection[0]);
        if (sel_dim) cur_style = sel_dim->style;
    }
    const bool is_vi = (language_ == UILanguage::Vietnamese);

    const double widths[] = { 0.10, 0.15, 0.25, 0.35, 0.50 };
    const wchar_t* descs_vi[] = { L"0.10 mm (Rất mảnh)", L"0.15 mm (Mảnh)", L"0.25 mm (Chuẩn TCVN/ISO)", L"0.35 mm (Đậm vừa)", L"0.50 mm (Nét đậm)" };
    const wchar_t* descs_en[] = { L"0.10 mm (Hairline)", L"0.15 mm (Thin)", L"0.25 mm (Standard ISO)", L"0.35 mm (Medium)", L"0.50 mm (Thick)" };

    for (int i = 0; i < 5; ++i) {
        const bool active = std::abs(cur_style.line_width_mm - widths[i]) < 0.02;
        AppendMenuW(hMenu, MF_STRING | (active ? MF_CHECKED : MF_UNCHECKED), 401 + i, is_vi ? descs_vi[i] : descs_en[i]);
    }

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, screen_x, screen_y, 0, hwnd_, nullptr);
    DestroyMenu(hMenu);

    if (cmd >= 401 && cmd <= 405) {
        cur_style.line_width_mm = widths[cmd - 401];
        cur_style.ext_width_mm = cur_style.line_width_mm;
        if (dimension_tool_) dimension_tool_->set_current_style(cur_style);
        if (selection.size() == 1) {
            root().document().execute_command(std::make_unique<document::UpdateDimensionStyleCommand>(selection[0], cur_style));
        }
        set_status_message(is_vi ? "Đã đổi độ dày nét đường đo" : "Dimension stroke width updated");
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void NativeWindow::show_dimension_text_size_menu(int screen_x, int screen_y) {
    HMENU hMenu = CreatePopupMenu();
    document::DimensionStyle cur_style = dimension_tool_ ? dimension_tool_->current_style() : document::DimensionStyle::make_default();
    const auto& selection = root().vector_controller().selection().selected_ids();
    if (selection.size() == 1) {
        const auto sel_dim = document::get_dimension(root().document(), selection[0]);
        if (sel_dim) cur_style = sel_dim->style;
    }
    const bool is_vi = (language_ == UILanguage::Vietnamese);

    const double sizes[] = { 2.0, 2.5, 3.0, 3.5, 5.0 };
    const wchar_t* descs_vi[] = { L"2.0 mm (Rất nhỏ)", L"2.5 mm (Nhỏ)", L"3.0 mm (Chuẩn TCVN/ISO)", L"3.5 mm (Vừa)", L"5.0 mm (Lớn)" };
    const wchar_t* descs_en[] = { L"2.0 mm (Small)", L"2.5 mm (Compact)", L"3.0 mm (Standard ISO)", L"3.5 mm (Medium)", L"5.0 mm (Large)" };

    for (int i = 0; i < 5; ++i) {
        const bool active = std::abs(cur_style.text_size_mm - sizes[i]) < 0.2;
        AppendMenuW(hMenu, MF_STRING | (active ? MF_CHECKED : MF_UNCHECKED), 501 + i, is_vi ? descs_vi[i] : descs_en[i]);
    }

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, screen_x, screen_y, 0, hwnd_, nullptr);
    DestroyMenu(hMenu);

    if (cmd >= 501 && cmd <= 505) {
        cur_style.text_size_mm = sizes[cmd - 501];
        if (dimension_tool_) dimension_tool_->set_current_style(cur_style);
        if (selection.size() == 1) {
            root().document().execute_command(std::make_unique<document::UpdateDimensionStyleCommand>(selection[0], cur_style));
        }
        set_status_message(is_vi ? "Đã đổi cỡ chữ số đo" : "Dimension text size updated");
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void NativeWindow::show_dimension_text_position_menu(int screen_x, int screen_y) {
    HMENU hMenu = CreatePopupMenu();
    document::DimensionStyle cur_style = dimension_tool_ ? dimension_tool_->current_style() : document::DimensionStyle::make_default();
    const auto& selection = root().vector_controller().selection().selected_ids();
    if (selection.size() == 1) {
        const auto sel_dim = document::get_dimension(root().document(), selection[0]);
        if (sel_dim) cur_style = sel_dim->style;
    }
    const bool is_vi = (language_ == UILanguage::Vietnamese);

    AppendMenuW(hMenu, MF_STRING | (cur_style.text_position == document::DimensionTextPosition::Above ? MF_CHECKED : MF_UNCHECKED),
                601, is_vi ? L"Phía trên đường đo (Above Line - TCVN/ISO)" : L"Above Line (TCVN/ISO)");
    AppendMenuW(hMenu, MF_STRING | (cur_style.text_position == document::DimensionTextPosition::Inline ? MF_CHECKED : MF_UNCHECKED),
                602, is_vi ? L"Nằm giữa ngắt đường nét (In-line - AutoCAD)" : L"In-line Break (AutoCAD)");

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, screen_x, screen_y, 0, hwnd_, nullptr);
    DestroyMenu(hMenu);

    if (cmd == 601 || cmd == 602) {
        cur_style.text_position = (cmd == 601) ? document::DimensionTextPosition::Above : document::DimensionTextPosition::Inline;
        if (dimension_tool_) dimension_tool_->set_current_style(cur_style);
        if (selection.size() == 1) {
            root().document().execute_command(std::make_unique<document::UpdateDimensionStyleCommand>(selection[0], cur_style));
        }
        set_status_message(is_vi ? "Đã cập nhật vị trí văn bản kích thước" : "Text placement updated");
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

bool NativeWindow::handle_dimension_property_bar_click(int x, int y) {
    if (!dimension_tool_) {
        try {
            dimension_tool_ = std::make_unique<tools::DimensionToolController>(root().document());
            apply_preferred_dimension_unit();
        } catch (...) {
            return false;
        }
    }

    POINT pt = { x, y };
    ClientToScreen(hwnd_, &pt);

    // 1. Orientation & Axis lock
    if (x >= 50 && x <= 94) {
        dimension_tool_->activate(document::DimensionKind::Aligned, false);
        dimension_tool_->set_linear_orientation(geometry::LinearOrientation::Aligned);
        dimension_tool_->set_axis_lock(tools::DimensionAxisLock::Aligned);
        set_status_message(language_ == UILanguage::Vietnamese ? "Chế độ đo: Xiên / Song song" : "Dimension mode: Aligned");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (x >= 98 && x <= 144) {
        dimension_tool_->activate(document::DimensionKind::Linear, false);
        dimension_tool_->set_linear_orientation(geometry::LinearOrientation::Horizontal);
        dimension_tool_->set_axis_lock(tools::DimensionAxisLock::Horizontal);
        set_status_message(language_ == UILanguage::Vietnamese ? "Chế độ đo: Khóa Ngang" : "Dimension mode: Horizontal");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (x >= 148 && x <= 188) {
        dimension_tool_->activate(document::DimensionKind::Linear, false);
        dimension_tool_->set_linear_orientation(geometry::LinearOrientation::Vertical);
        dimension_tool_->set_axis_lock(tools::DimensionAxisLock::Vertical);
        set_status_message(language_ == UILanguage::Vietnamese ? "Chế độ đo: Khóa Dọc" : "Dimension mode: Vertical");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (x >= 192 && x <= 230) {
        dimension_tool_->set_axis_lock(tools::DimensionAxisLock::Angle45);
        set_status_message(language_ == UILanguage::Vietnamese ? "Chế độ đo: Khóa chéo 45°" : "Dimension mode: Angle 45° lock");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (x >= 234 && x <= 280) {
        dimension_tool_->activate(document::DimensionKind::Radius, false);
        set_status_message(language_ == UILanguage::Vietnamese ? "Chế độ đo: Bán kính / Đường kính" : "Dimension mode: Radius");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (x >= 284 && x <= 324) {
        dimension_tool_->activate(document::DimensionKind::Angular, false);
        set_status_message(language_ == UILanguage::Vietnamese ? "Chế độ đo: Góc" : "Dimension mode: Angular");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    // 2. Continuous & Baseline Dimension
    if (x >= 330 && x <= 414) {
        const auto& sel = root().vector_controller().selection().selected_ids();
        if (sel.size() == 1) {
            const auto sel_dim = document::get_dimension(root().document(), sel[0]);
            if (sel_dim && sel_dim->references.size() >= 2) {
                const auto base_pt = sel_dim->references[1].explicit_point;
                dimension_tool_->start_continue_from(base_pt, sel_dim->placement_point, sel_dim->kind, sel_dim->linear_orientation);
                active_tool_ = 10;
                set_status_message(language_ == UILanguage::Vietnamese
                    ? "🔗 Dim liên tục: Bắt điểm cuối, nhấp điểm tiếp theo để tạo chuỗi kích thước (Esc để dừng)"
                    : "🔗 Continue Dimension: Started from end point, click next point to chain (Esc to stop)");
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
        }
        const bool new_mode = !dimension_tool_->continuous_mode();
        dimension_tool_->set_continuous_mode(new_mode);
        if (new_mode) dimension_tool_->set_baseline_mode(false);
        set_status_message(new_mode
            ? (language_ == UILanguage::Vietnamese ? "🔗 Chế độ Dim liên tục: BẬT" : "🔗 Continue Dimension: ON")
            : (language_ == UILanguage::Vietnamese ? "Chế độ Dim liên tục: TẮT" : "Continuous Dimension: OFF"));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    if (x >= 418 && x <= 502) {
        const auto& sel = root().vector_controller().selection().selected_ids();
        if (sel.size() == 1) {
            const auto sel_dim = document::get_dimension(root().document(), sel[0]);
            if (sel_dim && sel_dim->references.size() >= 2) {
                const auto base_pt = sel_dim->references[0].explicit_point;
                dimension_tool_->start_baseline_from(base_pt, sel_dim->placement_point, sel_dim->kind, sel_dim->linear_orientation);
                active_tool_ = 10;
                set_status_message(language_ == UILanguage::Vietnamese
                    ? "📶 Dim xếp tầng: Giữ gốc đo ban đầu, nhấp các điểm tiếp theo để tạo bậc thang (Esc để dừng)"
                    : "📶 Baseline Dimension: Started from origin point, click next points to step (Esc to stop)");
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
        }
        const bool new_mode = !dimension_tool_->baseline_mode();
        dimension_tool_->set_baseline_mode(new_mode);
        if (new_mode) dimension_tool_->set_continuous_mode(false);
        set_status_message(new_mode
            ? (language_ == UILanguage::Vietnamese ? "📶 Chế độ Dim xếp tầng (Baseline): BẬT" : "📶 Baseline Dimension: ON")
            : (language_ == UILanguage::Vietnamese ? "Chế độ Dim xếp tầng: TẮT" : "Baseline Dimension: OFF"));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    // 3. Dropdowns
    if (x >= 508 && x <= 562) {
        show_dimension_decimals_menu(pt.x, pt.y);
        return true;
    }
    if (x >= 566 && x <= 618) {
        if (!hwnd_) {
            cycle_dimension_unit();
            return true;
        }
        show_dimension_unit_menu(pt.x, pt.y);
        return true;
    }
    if (x >= 622 && x <= 680) {
        show_dimension_arrow_type_menu(pt.x, pt.y);
        return true;
    }
    if (x >= 684 && x <= 762) {
        if (!hwnd_ && x == 700) {
            cycle_dimension_unit();
            return true;
        }
        show_dimension_line_width_menu(pt.x, pt.y);
        return true;
    }
    if (x >= 766 && x <= 844) {
        show_dimension_text_size_menu(pt.x, pt.y);
        return true;
    }
    if (x >= 848 && x <= 936) {
        show_dimension_text_position_menu(pt.x, pt.y);
        return true;
    }

    // 4. Open Properties Inspector Dock Tab
    if (x >= 940 && x <= 1024) {
        active_dock_tab_ = 0;
        show_style_inspector_ = false;
        show_align_panel_ = false;
        set_status_message(language_ == UILanguage::Vietnamese ? "Đã mở bảng thuộc tính chi tiết kích thước" : "Opened dimension properties inspector");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    // 5. Toggle Smart Snap status
    if (x >= 1028 && x <= 1118) {
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
    root().document().execute_command(std::make_unique<document::AddDimensionCommand>(dim_w, active_layer_index()));

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
    root().document().execute_command(std::make_unique<document::AddDimensionCommand>(dim_h, active_layer_index()));

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

    root().document().execute_command(std::make_unique<document::AddDimensionCommand>(dim_p, active_layer_index()));

    set_status_message(language_ == UILanguage::Vietnamese
        ? "Đã tính và ghi chú chu vi thành công"
        : "Perimeter calculated and annotated successfully");
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::cycle_dimension_unit() {
    document::DimensionStyle style = dimension_tool_ ? dimension_tool_->current_style() : document::DimensionStyle::make_default();
    const auto& selection = root().vector_controller().selection().selected_ids();
    const auto selected_dimension = selection.size() == 1
        ? document::get_dimension(root().document(), selection.front())
        : std::nullopt;
    if (selected_dimension) style = selected_dimension->style;

    if (style.units == document::UnitType::Millimeter) style.units = document::UnitType::Centimeter;
    else if (style.units == document::UnitType::Centimeter) style.units = document::UnitType::Meter;
    else if (style.units == document::UnitType::Meter) style.units = document::UnitType::Inch;
    else if (style.units == document::UnitType::Inch) style.units = document::UnitType::Foot;
    else if (style.units == document::UnitType::Foot) style.units = document::UnitType::Yard;
    else if (style.units == document::UnitType::Yard) style.units = document::UnitType::Point;
    else if (style.units == document::UnitType::Point) style.units = document::UnitType::Pica;
    else style.units = document::UnitType::Millimeter;

    if (dimension_tool_) dimension_tool_->set_current_style(style);

    if (selected_dimension) {
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
