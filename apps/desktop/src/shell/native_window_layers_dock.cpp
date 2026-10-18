#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/shell/native_window_layers_dock.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace pikdraw::desktop {

#ifdef _WIN32

namespace {

static void draw_eye(HDC dc, int cx, int cy, bool visible, COLORREF col) {
    HPEN pen = CreatePen(PS_SOLID, 1, col);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));

    // Eye outline (almond shape via Arc or Ellipse)
    Arc(dc, cx - 6, cy - 4, cx + 7, cy + 8, cx + 6, cy, cx - 6, cy);
    Arc(dc, cx - 6, cy - 7, cx + 7, cy + 5, cx - 6, cy, cx + 6, cy);

    if (visible) {
        HBRUSH pupil_br = CreateSolidBrush(col);
        SelectObject(dc, pupil_br);
        Ellipse(dc, cx - 2, cy - 2, cx + 3, cy + 3);
        DeleteObject(pupil_br);
    } else {
        // Strike-through line for hidden
        MoveToEx(dc, cx - 5, cy + 4, nullptr);
        LineTo(dc, cx + 5, cy - 4);
    }

    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

static void draw_lock(HDC dc, int cx, int cy, bool locked, COLORREF col) {
    HPEN pen = CreatePen(PS_SOLID, 1, col);
    HBRUSH brush = CreateSolidBrush(locked ? col : RGB(0, 0, 0));
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, locked ? brush : GetStockObject(NULL_BRUSH));

    // Shackle
    Arc(dc, cx - 3, cy - 6, cx + 4, cy + 1, cx + 3, cy - 2, cx - 3, cy - 2);
    // Body
    RoundRect(dc, cx - 4, cy - 2, cx + 5, cy + 5, 2, 2);

    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}

static void draw_triangle(HDC dc, int cx, int cy, bool expanded, COLORREF col) {
    HBRUSH brush = CreateSolidBrush(col);
    HPEN pen = CreatePen(PS_SOLID, 1, col);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, brush);

    POINT pts[3];
    if (expanded) {
        // Pointing down: ▾
        pts[0] = {cx - 3, cy - 2};
        pts[1] = {cx + 3, cy - 2};
        pts[2] = {cx, cy + 2};
    } else {
        // Pointing right: ▸
        pts[0] = {cx - 2, cy - 3};
        pts[1] = {cx + 2, cy};
        pts[2] = {cx - 2, cy + 3};
    }
    Polygon(dc, pts, 3);

    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
    DeleteObject(brush);
}

static void draw_type_glyph(HDC dc, int type, int cx, int cy, COLORREF col) {
    HPEN pen = CreatePen(PS_SOLID, 1, col);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));

    switch (type) {
        case 0: // Path (Bezier curve icon)
            MoveToEx(dc, cx - 4, cy + 3, nullptr);
            LineTo(dc, cx + 4, cy - 3);
            Ellipse(dc, cx - 5, cy + 2, cx - 2, cy + 5);
            Ellipse(dc, cx + 2, cy - 4, cx + 5, cy - 1);
            break;
        case 1: // Text ('T' icon)
            MoveToEx(dc, cx - 4, cy - 4, nullptr);
            LineTo(dc, cx + 4, cy - 4);
            MoveToEx(dc, cx, cy - 4, nullptr);
            LineTo(dc, cx, cy + 4);
            break;
        case 2: // Image (Mountain / Frame)
            Rectangle(dc, cx - 4, cy - 4, cx + 5, cy + 5);
            MoveToEx(dc, cx - 3, cy + 2, nullptr);
            LineTo(dc, cx - 1, cy - 1);
            LineTo(dc, cx + 3, cy + 2);
            break;
        case 3: // Group (Folder)
            MoveToEx(dc, cx - 5, cy - 3, nullptr);
            LineTo(dc, cx - 1, cy - 3);
            LineTo(dc, cx + 1, cy - 1);
            LineTo(dc, cx + 5, cy - 1);
            LineTo(dc, cx + 5, cy + 4);
            LineTo(dc, cx - 5, cy + 4);
            LineTo(dc, cx - 5, cy - 3);
            break;
    }

    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

} // namespace

void NativeWindow::paint_layers_dock(HDC mem_dc, int insp_x, int content_y, int w, int h, int status_h) {
    paint_layers_panel_dock(*this, mem_dc, insp_x, content_y, w, h, status_h);
}

void paint_layers_panel_dock(NativeWindow& win, HDC mem_dc, int insp_x, int content_y, int w, int h, int status_h) {
    const bool is_dark = (win.theme() == ThemeMode::CyberDark);
    const bool is_vi = (win.language() == UILanguage::Vietnamese);

    const COLORREF col_header = is_dark ? RGB(12, 19, 29) : RGB(255, 255, 255);
    const COLORREF col_border = is_dark ? RGB(29, 38, 54) : RGB(203, 213, 225);
    const COLORREF col_teal = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    const COLORREF col_text_white = is_dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    const COLORREF col_text_muted = is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    const COLORREF col_active_tab = is_dark ? RGB(22, 37, 54) : RGB(236, 242, 248);
    const COLORREF col_btn_bg = is_dark ? RGB(20, 31, 46) : RGB(248, 250, 252);
    const COLORREF col_amber = is_dark ? RGB(245, 158, 11) : RGB(217, 119, 6);

    auto snapshot = win.root().document().current_snapshot();
    const auto& selection = win.root().vector_controller().selection().selected_ids();
    auto& state = win.layers_dock_state();

    // 1. Header Title
    RECT rc_head = { insp_x + 14, content_y + 4, w - 14, content_y + 24 };
    SetTextColor(mem_dc, col_teal);
    DrawTextW(mem_dc, is_vi ? L"LỚP & ĐỐI TƯỢNG" : L"LAYERS & OBJECTS", -1,
              &rc_head, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

    // 2. Filter Buttons Bar (y: content_y + 26 .. content_y + 48)
    const int filter_bar_y = content_y + 26;
    const int filter_btn_w = (w - insp_x - 24) / 5;
    const wchar_t* filter_labels_en[] = {L"All", L"Path", L"Text", L"Img", L"Grp"};
    const wchar_t* filter_labels_vi[] = {L"Tất cả", L"Nét", L"Chữ", L"Ảnh", L"Nhóm"};

    for (int i = 0; i < 5; ++i) {
        const int bx = insp_x + 12 + i * filter_btn_w;
        const bool active = (static_cast<int>(state.filter) == i);
        RECT rc_btn = { bx, filter_bar_y, bx + filter_btn_w - 2, filter_bar_y + 20 };

        HBRUSH br = CreateSolidBrush(active ? col_active_tab : col_btn_bg);
        HPEN pn = CreatePen(PS_SOLID, 1, active ? col_teal : col_border);
        HGDIOBJ op = SelectObject(mem_dc, pn);
        HGDIOBJ ob = SelectObject(mem_dc, br);
        RoundRect(mem_dc, rc_btn.left, rc_btn.top, rc_btn.right, rc_btn.bottom, 4, 4);
        SelectObject(mem_dc, ob);
        SelectObject(mem_dc, op);
        DeleteObject(pn);
        DeleteObject(br);

        SetTextColor(mem_dc, active ? col_teal : col_text_muted);
        const wchar_t* lbl = is_vi ? filter_labels_vi[i] : filter_labels_en[i];
        DrawTextW(mem_dc, lbl, -1, &rc_btn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // 3. Footer Action Bar (y: h - status_h - 32 .. h - status_h - 4)
    const int footer_y = h - status_h - 30;
    const int footer_btn_w = (w - insp_x - 24) / 5;
    const wchar_t* footer_labels_en[] = {L"+ Layer", L"Group", L"Up ▲", L"Dn ▼", L"Del 🗑"};
    const wchar_t* footer_labels_vi[] = {L"+ Lớp", L"Nhóm", L"Lên ▲", L"Xuống ▼", L"Xóa 🗑"};

    for (int i = 0; i < 5; ++i) {
        const int bx = insp_x + 12 + i * footer_btn_w;
        RECT rc_fb = { bx, footer_y, bx + footer_btn_w - 2, footer_y + 24 };

        bool enabled = (i == 0); // Add layer always enabled
        if (i == 1 && selection.size() >= 2) enabled = true; // Group
        if ((i == 2 || i == 3) && !selection.empty()) enabled = true; // Up/Down
        if (i == 4 && !selection.empty()) enabled = true; // Delete

        HBRUSH br = CreateSolidBrush(enabled ? col_btn_bg : col_header);
        HPEN pn = CreatePen(PS_SOLID, 1, enabled ? col_border : col_border);
        HGDIOBJ op = SelectObject(mem_dc, pn);
        HGDIOBJ ob = SelectObject(mem_dc, br);
        RoundRect(mem_dc, rc_fb.left, rc_fb.top, rc_fb.right, rc_fb.bottom, 4, 4);
        SelectObject(mem_dc, ob);
        SelectObject(mem_dc, op);
        DeleteObject(pn);
        DeleteObject(br);

        SetTextColor(mem_dc, enabled ? col_text_white : col_text_muted);
        const wchar_t* lbl = is_vi ? footer_labels_vi[i] : footer_labels_en[i];
        DrawTextW(mem_dc, lbl, -1, &rc_fb, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // 4. Hierarchical Virtual Tree
    const int tree_top = filter_bar_y + 26;
    const int tree_bottom = footer_y - 6;
    const int row_h = LayersDockMetrics::row_height;

    // Clip rect to prevent drawing outside tree viewport
    HRGN clip_rgn = CreateRectRgn(insp_x + 6, tree_top, w - 6, tree_bottom);
    SelectClipRgn(mem_dc, clip_rgn);

    int current_y = tree_top - state.scroll_offset_y;
    const auto& layers = snapshot->page().layers;
    const auto& objects_map = snapshot->objects();

    for (size_t layer_idx = 0; layer_idx < layers.size(); ++layer_idx) {
        const auto& layer = layers[layer_idx];
        const bool is_layer_collapsed = state.collapsed_layers.count(layer_idx) > 0;

        // Layer Row
        if (current_y + row_h >= tree_top && current_y <= tree_bottom) {
            RECT row_rc = { insp_x + 8, current_y, w - 8, current_y + row_h };

            HBRUSH row_br = CreateSolidBrush(col_btn_bg);
            HPEN row_pn = CreatePen(PS_SOLID, 1, col_border);
            HGDIOBJ op = SelectObject(mem_dc, row_pn);
            HGDIOBJ ob = SelectObject(mem_dc, row_br);
            RoundRect(mem_dc, row_rc.left, row_rc.top, row_rc.right, row_rc.bottom, 3, 3);
            SelectObject(mem_dc, ob);
            SelectObject(mem_dc, op);
            DeleteObject(row_pn);
            DeleteObject(row_br);

            // Expand/collapse arrow
            draw_triangle(mem_dc, row_rc.left + 8, current_y + row_h / 2, !is_layer_collapsed, col_teal);

            // Eye icon
            draw_eye(mem_dc, row_rc.left + 22, current_y + row_h / 2, layer.visible,
                     layer.visible ? col_teal : col_text_muted);

            // Lock icon
            draw_lock(mem_dc, row_rc.left + 38, current_y + row_h / 2, layer.locked,
                      layer.locked ? col_amber : col_text_muted);

            // Layer Name
            std::wostringstream l_name;
            l_name << win.utf8_to_wide(layer.name) << L" (" << layer.object_ids.size() << L")";
            RECT name_rc = { row_rc.left + 52, current_y, row_rc.right - 20, current_y + row_h };
            SetTextColor(mem_dc, layer.visible ? col_text_white : col_text_muted);
            DrawTextW(mem_dc, l_name.str().c_str(), -1, &name_rc,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        current_y += row_h + 2;

        if (is_layer_collapsed) continue;

        // Children of layer (drawn in reverse stacking order: topmost object first)
        for (int obj_i = static_cast<int>(layer.object_ids.size()) - 1; obj_i >= 0; --obj_i) {
            const auto obj_id = layer.object_ids[static_cast<size_t>(obj_i)];
            const auto obj_opt = objects_map.get(obj_id);
            if (!obj_opt) continue;

            const auto& obj_var = *obj_opt;
            const bool is_group = std::holds_alternative<document::GroupObject>(obj_var);
            const bool is_text = std::holds_alternative<document::TextObject>(obj_var);
            const bool is_image = std::holds_alternative<document::ImageObject>(obj_var);
            const int glyph_type = is_group ? 3 : is_text ? 1 : is_image ? 2 : 0;

            // Apply filter
            if (state.filter == LayerFilterType::Path && (is_text || is_image || is_group)) continue;
            if (state.filter == LayerFilterType::Text && !is_text) continue;
            if (state.filter == LayerFilterType::Image && !is_image) continue;
            if (state.filter == LayerFilterType::Group && !is_group) continue;

            const bool obj_visible = std::visit([](const auto& o) { return o.visible; }, obj_var);
            const bool obj_locked = std::visit([](const auto& o) { return o.locked; }, obj_var);
            const auto obj_name = std::visit([](const auto& o) { return o.name; }, obj_var);
            const bool is_selected = std::find(selection.begin(), selection.end(), obj_id) != selection.end();

            if (current_y + row_h >= tree_top && current_y <= tree_bottom) {
                RECT row_rc = { insp_x + 18, current_y, w - 8, current_y + row_h };

                if (is_selected) {
                    HBRUSH sel_br = CreateSolidBrush(col_active_tab);
                    HPEN sel_pn = CreatePen(PS_SOLID, 1, col_teal);
                    HGDIOBJ op = SelectObject(mem_dc, sel_pn);
                    HGDIOBJ ob = SelectObject(mem_dc, sel_br);
                    RoundRect(mem_dc, row_rc.left, row_rc.top, row_rc.right, row_rc.bottom, 3, 3);
                    SelectObject(mem_dc, ob);
                    SelectObject(mem_dc, op);
                    DeleteObject(sel_pn);
                    DeleteObject(sel_br);
                }

                // Eye
                draw_eye(mem_dc, row_rc.left + 10, current_y + row_h / 2, obj_visible,
                         obj_visible ? col_teal : col_text_muted);

                // Lock
                draw_lock(mem_dc, row_rc.left + 26, current_y + row_h / 2, obj_locked,
                          obj_locked ? col_amber : col_text_muted);

                // Type glyph
                draw_type_glyph(mem_dc, glyph_type, row_rc.left + 42, current_y + row_h / 2,
                                is_selected ? col_teal : col_text_muted);

                // Name + (#ID)
                std::wostringstream o_name;
                o_name << win.utf8_to_wide(obj_name) << L" (#" << obj_id << L")";
                RECT name_rc = { row_rc.left + 54, current_y, row_rc.right - 22, current_y + row_h };
                SetTextColor(mem_dc, is_selected ? col_teal : obj_visible ? col_text_white : col_text_muted);
                DrawTextW(mem_dc, o_name.str().c_str(), -1, &name_rc,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

                // Selection Proxy Dot/Square at right edge
                if (is_selected) {
                    HBRUSH dot_br = CreateSolidBrush(col_teal);
                    RECT dot_rc = { row_rc.right - 14, current_y + 8, row_rc.right - 6, current_y + 16 };
                    FillRect(mem_dc, &dot_rc, dot_br);
                    DeleteObject(dot_br);
                }
            }
            current_y += row_h + 1;
        }
    }

    state.total_content_height = std::max(0, current_y + state.scroll_offset_y - tree_top);

    // Remove clipping
    SelectClipRgn(mem_dc, nullptr);
    DeleteObject(clip_rgn);

    if (snapshot->object_count() == 0) {
        RECT rc_empty = { insp_x + 14, tree_top + 20, w - 14, tree_top + 60 };
        SetTextColor(mem_dc, col_text_muted);
        DrawTextW(mem_dc, is_vi ? L"Tài liệu chưa có đối tượng nào." : L"The document contains no objects.",
                  -1, &rc_empty, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

#endif

} // namespace pikdraw::desktop
