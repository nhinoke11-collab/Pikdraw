#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_file_menu.hpp"
#include "native_window_page_presets.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <variant>

namespace pikdraw::desktop {

#ifdef _WIN32
namespace {

std::optional<geometry::Rect2D> selected_bounds(const document::ObjectVariant& object) {
    return std::visit([](const auto& value) -> std::optional<geometry::Rect2D> {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, document::GroupObject>)
            return std::nullopt;
        else
            return value.bounds();
    }, object);
}

void draw_tool_icon(HDC dc, int tool, int center_x, int center_y, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    if (tool == 0) {
        MoveToEx(dc, center_x - 6, center_y - 9, nullptr);
        LineTo(dc, center_x - 6, center_y + 8);
        LineTo(dc, center_x - 1, center_y + 3);
        LineTo(dc, center_x + 3, center_y + 10);
        LineTo(dc, center_x + 6, center_y + 8);
        LineTo(dc, center_x + 2, center_y + 1);
        LineTo(dc, center_x + 9, center_y + 1);
        LineTo(dc, center_x - 6, center_y - 9);
    } else if (tool == 1) {
        Rectangle(dc, center_x - 8, center_y - 6, center_x + 8, center_y + 6);
    } else if (tool == 2) {
        Ellipse(dc, center_x - 8, center_y - 6, center_x + 8, center_y + 6);
    } else if (tool == 3) {
        MoveToEx(dc, center_x - 6, center_y + 2, nullptr);
        LineTo(dc, center_x - 8, center_y - 1);
        LineTo(dc, center_x - 6, center_y - 3);
        LineTo(dc, center_x - 3, center_y);
        LineTo(dc, center_x - 3, center_y - 9);
        LineTo(dc, center_x - 1, center_y - 10);
        LineTo(dc, center_x + 1, center_y - 9);
        LineTo(dc, center_x + 1, center_y - 2);
        LineTo(dc, center_x + 3, center_y - 8);
        LineTo(dc, center_x + 5, center_y - 8);
        LineTo(dc, center_x + 6, center_y - 1);
        LineTo(dc, center_x + 8, center_y - 6);
        LineTo(dc, center_x + 10, center_y - 5);
        LineTo(dc, center_x + 9, center_y + 3);
        LineTo(dc, center_x + 6, center_y + 9);
        LineTo(dc, center_x - 4, center_y + 9);
        LineTo(dc, center_x - 7, center_y + 5);
        LineTo(dc, center_x - 6, center_y + 2);
    } else if (tool == 4) {
        MoveToEx(dc, center_x - 7, center_y + 8, nullptr);
        LineTo(dc, center_x, center_y - 8);
        LineTo(dc, center_x + 7, center_y + 8);
        MoveToEx(dc, center_x - 4, center_y + 2, nullptr);
        LineTo(dc, center_x + 4, center_y + 2);
    } else if (tool == 5) {
        // Knife / Scalpel icon: diagonal blade and handle cutting across
        MoveToEx(dc, center_x - 5, center_y + 7, nullptr);
        LineTo(dc, center_x - 2, center_y + 7);
        LineTo(dc, center_x + 6, center_y - 5);
        LineTo(dc, center_x + 3, center_y - 8);
        LineTo(dc, center_x - 3, center_y + 1);
        LineTo(dc, center_x - 5, center_y + 7);
        // Cut line indicator
        MoveToEx(dc, center_x - 8, center_y + 8, nullptr);
        LineTo(dc, center_x + 7, center_y - 7);
    }
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

} // namespace

void NativeWindow::paint_document_tab_bar(HDC mem_dc, int w, int topbar_h, int tabbar_h) {
    bool is_dark = (theme_ == ThemeMode::CyberDark);
    COLORREF col_tab_bar_bg = is_dark ? RGB(10, 16, 25) : RGB(241, 245, 249);
    COLORREF col_tab_active = is_dark ? RGB(18, 28, 42) : RGB(255, 255, 255);
    COLORREF col_tab_inactive = is_dark ? RGB(12, 19, 29) : RGB(226, 232, 240);
    COLORREF col_border = is_dark ? RGB(29, 38, 54) : RGB(203, 213, 225);
    COLORREF col_teal = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    COLORREF col_text_mut = is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139);

    RECT rc_bar = { 0, topbar_h, w, topbar_h + tabbar_h };
    HBRUSH br_bar = CreateSolidBrush(col_tab_bar_bg);
    FillRect(mem_dc, &rc_bar, br_bar);
    DeleteObject(br_bar);

    HPEN pen_bdr = CreatePen(PS_SOLID, 1, col_border);
    HGDIOBJ old_pen = SelectObject(mem_dc, pen_bdr);
    MoveToEx(mem_dc, 0, topbar_h + tabbar_h, NULL);
    LineTo(mem_dc, w, topbar_h + tabbar_h);

    int cur_x = 10;
    int tab_w = 210;
    ensure_ui_fonts();
    HFONT font_tab = font_tab_;
    HFONT font_tab_bold = font_tab_bold_;
    HGDIOBJ old_font = SelectObject(mem_dc, font_tab);

    for (size_t i = 0; i < tabs_.size(); ++i) {
        const auto& tab = tabs_[i];
        bool is_act = (i == active_tab_index_);
        RECT rc_tab = { cur_x, topbar_h + 2, cur_x + tab_w, topbar_h + tabbar_h };

        HBRUSH br_t = CreateSolidBrush(is_act ? col_tab_active : col_tab_inactive);
        HPEN pen_t = CreatePen(PS_SOLID, 1, is_act ? col_teal : col_border);
        SelectObject(mem_dc, pen_t);
        SelectObject(mem_dc, br_t);
        RoundRect(mem_dc, rc_tab.left, rc_tab.top, rc_tab.right, rc_tab.bottom + 2, 4, 4);
        DeleteObject(pen_t);
        DeleteObject(br_t);

        if (is_act) {
            HPEN pen_top = CreatePen(PS_SOLID, 2, col_teal);
            SelectObject(mem_dc, pen_top);
            MoveToEx(mem_dc, rc_tab.left + 2, rc_tab.top, NULL);
            LineTo(mem_dc, rc_tab.right - 2, rc_tab.top);
            DeleteObject(pen_top);
        }

        SelectObject(mem_dc, is_act ? font_tab_bold : font_tab);
        SetTextColor(mem_dc, is_act ? col_teal : col_text_mut);
        std::wstring wtitle = L"📄 " + utf8_to_wide(tab.title);
        std::wstring wmode = utf8_to_wide(tab.color_mode);
        if (wtitle.find(wmode) == std::wstring::npos) {
            wtitle += L" (" + wmode + L")";
        }
        RECT rc_txt = { rc_tab.left + 8, rc_tab.top, rc_tab.right - 24, rc_tab.bottom };
        DrawTextW(mem_dc, wtitle.c_str(), -1, &rc_txt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        RECT rc_cls = { rc_tab.right - 22, rc_tab.top + 2, rc_tab.right - 4, rc_tab.bottom - 2 };
        SetTextColor(mem_dc, col_text_mut);
        DrawTextW(mem_dc, L"✕", -1, &rc_cls, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        cur_x += tab_w + 4;
    }

    RECT rc_new_tab = { cur_x, topbar_h + 3, cur_x + 26, topbar_h + tabbar_h - 2 };
    HBRUSH br_nt = CreateSolidBrush(col_tab_inactive);
    SelectObject(mem_dc, br_nt);
    RoundRect(mem_dc, rc_new_tab.left, rc_new_tab.top, rc_new_tab.right, rc_new_tab.bottom, 4, 4);
    DeleteObject(br_nt);
    SelectObject(mem_dc, font_tab_bold);
    SetTextColor(mem_dc, col_teal);
    DrawTextW(mem_dc, L"+", -1, &rc_new_tab, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(mem_dc, old_font);
    SelectObject(mem_dc, old_pen);
    DeleteObject(pen_bdr);
}

void NativeWindow::paint(HDC hdc, int w, int h) {
    ensure_ui_fonts();

    if (!backbuffer_dc_) {
        backbuffer_dc_ = CreateCompatibleDC(hdc);
    }
    if (backbuffer_bmp_ && (w != backbuffer_w_ || h != backbuffer_h_)) {
        SelectObject(backbuffer_dc_, backbuffer_old_bmp_);
        DeleteObject(backbuffer_bmp_);
        backbuffer_bmp_ = nullptr;
        backbuffer_old_bmp_ = nullptr;
    }
    if (!backbuffer_bmp_) {
        backbuffer_bmp_ = CreateCompatibleBitmap(hdc, w, h);
        backbuffer_old_bmp_ = SelectObject(backbuffer_dc_, backbuffer_bmp_);
        backbuffer_w_ = w;
        backbuffer_h_ = h;
    }
    HDC mem_dc = backbuffer_dc_;

    HFONT font_be_vietnam = font_be_vietnam_;
    HFONT font_be_vietnam_bold = font_be_vietnam_bold_;
    HFONT font_secondary = font_secondary_;
    HFONT font_mono = font_mono_;

    HGDIOBJ old_font = SelectObject(mem_dc, font_be_vietnam);

    bool is_dark = (theme_ == ThemeMode::CyberDark);
    bool is_vi = (language_ == UILanguage::Vietnamese);

    // 2026 Dual Theme Palette: Dark (Obsidian Teal) / Light (Silver Tech)
    COLORREF col_canvas = is_dark ? RGB(7, 12, 18) : RGB(241, 245, 249);
    COLORREF col_header = is_dark ? RGB(12, 19, 29) : RGB(255, 255, 255);
    COLORREF col_ribbon = is_dark ? RGB(14, 22, 34) : RGB(248, 250, 252);
    COLORREF col_statusbar = is_dark ? RGB(7, 11, 16) : RGB(226, 232, 240);
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

        auto draw_num_input = [&](int left, int right, const wchar_t* prefix, const std::string& current_val, NumericFieldId field_id) {
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
                display_text += L" mm";
            }

            SetTextColor(mem_dc, is_focused ? col_teal : col_text_white);
            SelectObject(mem_dc, font_mono);
            RECT rc_txt = { rect.left + 4, rect.top, rect.right - 4, rect.bottom };
            DrawTextW(mem_dc, display_text.c_str(), -1, &rc_txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        };

        bool is_text_mode = (active_tool_ == 4 || is_inline_text_editing_ || is_text_object_selected());

        if (is_text_mode) {
            // TYPOGRAPHY & TEXT PROPERTY BAR (CorelDRAW v25 / MS Word)
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT rc_t_lbl = { 10, topbar_h + tabbar_h, 48, content_top };
            DrawTextW(mem_dc, is_vi ? L"Chữ:" : L"Text:", -1, &rc_t_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // 1. Font Family Selector Button
            const wchar_t* font_names_w[] = {
                L"Be Vietnam Pro ▾",
                L"Segoe UI ▾",
                L"Arial ▾",
                L"Times New Roman ▾",
                L"JetBrains Mono ▾"
            };
            int fam_idx = font_family_index_ % 5;
            draw_rib_btn(52, 192, font_names_w[fam_idx], false, col_text_white);

            // 2. Font Size pt Controls
            RECT rc_sz_lbl = { 198, topbar_h + tabbar_h, 218, content_top };
            SetTextColor(mem_dc, col_text_muted);
            DrawTextW(mem_dc, L"Pt:", -1, &rc_sz_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            draw_rib_btn(220, 240, L"-", false, col_teal);
            std::wstring sz_str = std::to_wstring(static_cast<int>(font_size_pt_)) + L" pt";
            RECT rc_sz_val = { 242, topbar_h + tabbar_h, 292, content_top };
            SetTextColor(mem_dc, col_text_white);
            DrawTextW(mem_dc, sz_str.c_str(), -1, &rc_sz_val, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            draw_rib_btn(294, 314, L"+", false, col_teal);

            // 3. Styles: B, I, U
            draw_rib_btn(322, 350, L"B", font_bold_, font_bold_ ? col_teal : col_text_white);
            draw_rib_btn(354, 382, L"I", font_italic_, font_italic_ ? col_teal : col_text_white);
            draw_rib_btn(386, 414, L"U", font_underline_, font_underline_ ? col_teal : col_text_white);

            // 4. Alignment: Left, Center, Right (Compact icon buttons)
            bool is_left = (font_alignment_ == typography::TextAlignment::Left);
            bool is_center = (font_alignment_ == typography::TextAlignment::Center);
            bool is_right = (font_alignment_ == typography::TextAlignment::Right);
            draw_rib_btn(422, 462, L"|◀ L", is_left, is_left ? col_teal : col_text_white);
            draw_rib_btn(466, 506, L"▶|◀ C", is_center, is_center ? col_teal : col_text_white);
            draw_rib_btn(510, 550, L"▶| R", is_right, is_right ? col_teal : col_text_white);

            // 5. Convert to Curves (CorelDRAW Ctrl+Q) - Compact icon
            draw_rib_btn(556, 616, L"⤹ Q", false, col_teal);

            // 6. Table Tool Button (CorelDRAW v25 Table) - Compact icon
            draw_rib_btn(622, 692, L"▦ Table", false, col_teal);

            // 7. Delete button if text is selected
            if (!selection.empty()) {
                draw_rib_btn(698, 748, is_vi ? L"✕ Xóa" : L"✕ Del", false, RGB(239, 68, 68));
            }

            // 8. Color Palette Swatches
            int color_start_x = (!selection.empty()) ? 754 : 698;
            draw_rib_btn(color_start_x, color_start_x + 36, L"■", false, RGB(0, 242, 195));
            draw_rib_btn(color_start_x + 40, color_start_x + 76, L"■", false, RGB(14, 165, 233));
            draw_rib_btn(color_start_x + 80, color_start_x + 116, L"■", false, RGB(234, 179, 8));
            draw_rib_btn(color_start_x + 120, color_start_x + 156, L"■", false, RGB(239, 68, 68));
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
            std::string pw_str = spw.str();
            std::string ph_str = sph.str();
            if (pw_str.size() > 2 && pw_str.substr(pw_str.size() - 2) == ".0") pw_str.erase(pw_str.size() - 2);
            if (ph_str.size() > 2 && ph_str.substr(ph_str.size() - 2) == ".0") ph_str.erase(ph_str.size() - 2);

            draw_num_input(175, 275, L"W:", pw_str, NumericFieldId::PageWidth);
            draw_num_input(283, 383, L"H:", ph_str, NumericFieldId::PageHeight);

            bool is_land = (pw > ph);
            draw_rib_btn(391, 455, is_vi ? L"▯ Dọc" : L"▯ Port", !is_land, !is_land ? col_teal : col_text_white);
            draw_rib_btn(461, 525, is_vi ? L"▭ Ngang" : L"▭ Land", is_land, is_land ? col_teal : col_text_white);
            draw_rib_btn(533, 665, L"CMYK · 300 DPI", false, col_teal);
        } else {
            // OBJECT TRANSFORM & EDIT MODE (Selection Ribbon - Direct Numeric Input)
            const auto obj_id = selection.front();
            auto bounds_opt = workflow().get_object_bounds(obj_id);
            double obj_x = bounds_opt ? bounds_opt->x : 0.0;
            double obj_y = bounds_opt ? bounds_opt->y : 0.0;
            double obj_w = bounds_opt ? bounds_opt->width : 0.0;
            double obj_h = bounds_opt ? bounds_opt->height : 0.0;

            std::ostringstream sx, sy, sw, sh;
            sx << std::fixed << std::setprecision(1) << obj_x;
            sy << std::fixed << std::setprecision(1) << obj_y;
            sw << std::fixed << std::setprecision(1) << obj_w;
            sh << std::fixed << std::setprecision(1) << obj_h;

            draw_num_input(10, 95, L"X:", sx.str(), NumericFieldId::ObjectX);
            draw_num_input(100, 185, L"Y:", sy.str(), NumericFieldId::ObjectY);
            draw_num_input(190, 285, L"W:", sw.str(), NumericFieldId::ObjectWidth);

            const wchar_t* lock_lbl = aspect_ratio_locked_ ?
                (is_vi ? L"🔒 Khóa" : L"🔒 Lock") :
                (is_vi ? L"🔓 Mở" : L"🔓 Free");
            draw_rib_btn(290, 360, lock_lbl, aspect_ratio_locked_, aspect_ratio_locked_ ? col_teal : col_text_muted);

            draw_num_input(365, 460, L"H:", sh.str(), NumericFieldId::ObjectHeight);

            draw_rib_btn(468, 518, is_vi ? L"✕ Xóa" : L"✕ Del", false, RGB(239, 68, 68));
            draw_rib_btn(524, 574, L"⧉ Dup", false, col_teal);

            if (is_in_isolation_mode()) {
                draw_rib_btn(580, 670, is_vi ? L"⬅ Thoát (Esc)" : L"⬅ Exit (Esc)", true, col_teal);
            } else {
                if (selection.size() >= 2) {
                    draw_rib_btn(580, 620, L"|◀ L", false, col_teal);
                    draw_rib_btn(624, 664, L"▶|◀ C", false, col_teal);
                    draw_rib_btn(668, 708, L"▶| R", false, col_teal);
                    draw_rib_btn(712, 752, L"▲ T", false, col_teal);
                    draw_rib_btn(756, 796, L"▼▲ E", false, col_teal);
                    draw_rib_btn(800, 840, L"▼ B", false, col_teal);
                    draw_rib_btn(844, 904, L"⌖ Page", false, col_teal);
                    draw_rib_btn(908, 968, is_vi ? L"⛶ Nhóm" : L"⛶ Group", false, col_teal);
                    draw_rib_btn(972, 1026, is_vi ? L"⧉ Hàn" : L"⧉ Weld", false, col_teal);
                    draw_rib_btn(1030, 1084, is_vi ? L"✂ Xén" : L"✂ Trim", false, col_teal);
                } else if (selection.size() == 1) {
                    draw_rib_btn(580, 640, L"⌖ Page", false, col_teal);
                    if (workflow().is_group(selection.front())) {
                        draw_rib_btn(646, 716, is_vi ? L"⛶ Rã" : L"⛶ Ungroup", false, col_teal);
                        draw_rib_btn(722, 792, is_vi ? L"🔍 Chui vào" : L"🔍 Edit", true, col_teal);
                    }
                }
            }

            int color_start_x = (selection.size() == 1 && workflow().is_group(selection.front())) ? 798
                              : (selection.size() == 1) ? 646
                              : (selection.size() >= 2) ? 1090
                              : is_in_isolation_mode() ? 676 : 580;
            draw_rib_btn(color_start_x, color_start_x + 36, L"■", false, RGB(0, 242, 195));
            draw_rib_btn(color_start_x + 40, color_start_x + 76, L"■", false, RGB(14, 165, 233));
            draw_rib_btn(color_start_x + 80, color_start_x + 116, L"■", false, RGB(234, 179, 8));
            draw_rib_btn(color_start_x + 120, color_start_x + 156, L"■", false, RGB(239, 68, 68));
        }

        // 5. Left Tool Strip: 6 working tools (Select, Rect, Ellipse, Hand, Text, Knife)
        HBRUSH br_tools = CreateSolidBrush(col_header);
        RECT rc_tools = { 0, content_top, tool_w, h - status_h };
        FillRect(mem_dc, &rc_tools, br_tools);
        DeleteObject(br_tools);

        for (int i = 0; i < 6; ++i) {
            int ty = content_top + 10 + i * 42;
            RECT rc_tb = { 6, ty, tool_w - 6, ty + 36 };
            if (active_tool_ == i) {
                HBRUSH br_t_act = CreateSolidBrush(col_active_tab);
                HPEN pen_t_act = CreatePen(PS_SOLID, 1, col_teal);
                SelectObject(mem_dc, pen_t_act);
                SelectObject(mem_dc, br_t_act);
                RoundRect(mem_dc, rc_tb.left, rc_tb.top, rc_tb.right, rc_tb.bottom, 6, 6);
                DeleteObject(pen_t_act);
                DeleteObject(br_t_act);
                draw_tool_icon(mem_dc, i, (rc_tb.left + rc_tb.right) / 2,
                               (rc_tb.top + rc_tb.bottom) / 2, col_teal);
            } else {
                HBRUSH br_t_inact = CreateSolidBrush(col_header);
                HPEN pen_t_inact = CreatePen(PS_SOLID, 1, col_border);
                SelectObject(mem_dc, pen_t_inact);
                SelectObject(mem_dc, br_t_inact);
                RoundRect(mem_dc, rc_tb.left, rc_tb.top, rc_tb.right, rc_tb.bottom, 6, 6);
                DeleteObject(pen_t_inact);
                DeleteObject(br_t_inact);
                draw_tool_icon(mem_dc, i, (rc_tb.left + rc_tb.right) / 2,
                               (rc_tb.top + rc_tb.bottom) / 2, col_text_muted);
            }
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

        const int content_y = content_top + 36;
        SelectObject(mem_dc, font_be_vietnam_bold);
        SetTextColor(mem_dc, col_teal);
        RECT rc_head = { insp_x + 14, content_y, w - 14, content_y + 22 };
        SelectObject(mem_dc, font_secondary);
        SetTextColor(mem_dc, col_text_white);

        std::wostringstream content;
        if (active_dock_tab_ == 0) {
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
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT tools_head = { insp_x + 14, content_y + 195, w - 14, content_y + 218 };
            DrawTextW(mem_dc, is_vi ? L"TẠO HÌNH KHỐI (SOLID TOOLS)" : L"SOLID & SHAPING TOOLS", -1,
                      &tools_head, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

            // Checkboxes: Keep Source / Keep Target
            RECT rc_chk1 = { insp_x + 14, content_y + 222, insp_x + 135, content_y + 244 };
            RECT rc_chk2 = { insp_x + 140, content_y + 222, w - 14, content_y + 244 };
            const wchar_t* chk1_txt = keep_source_object_ ?
                (is_vi ? L"☑ Giữ khuôn" : L"☑ Keep Source") :
                (is_vi ? L"☐ Giữ khuôn" : L"☐ Keep Source");
            const wchar_t* chk2_txt = keep_target_object_ ?
                (is_vi ? L"☑ Giữ hình cắt" : L"☑ Keep Target") :
                (is_vi ? L"☐ Giữ hình cắt" : L"☐ Keep Target");
            SelectObject(mem_dc, font_secondary);
            SetTextColor(mem_dc, keep_source_object_ ? col_teal : col_text_muted);
            DrawTextW(mem_dc, chk1_txt, -1, &rc_chk1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SetTextColor(mem_dc, keep_target_object_ ? col_teal : col_text_muted);
            DrawTextW(mem_dc, chk2_txt, -1, &rc_chk2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Grid of Solid buttons
            auto draw_shaping_btn = [&](int bx, int by, int bw, int bh, const wchar_t* blabel) {
                RECT brect = { bx, by, bx + bw, by + bh };
                HBRUSH br = CreateSolidBrush(col_btn_bg);
                HPEN pn = CreatePen(PS_SOLID, 1, col_btn_bdr);
                HGDIOBJ op = SelectObject(mem_dc, pn);
                HGDIOBJ ob = SelectObject(mem_dc, br);
                RoundRect(mem_dc, brect.left, brect.top, brect.right, brect.bottom, 4, 4);
                SelectObject(mem_dc, op);
                SelectObject(mem_dc, ob);
                DeleteObject(pn);
                DeleteObject(br);
                SetTextColor(mem_dc, col_text_white);
                DrawTextW(mem_dc, blabel, -1, &brect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            };

            int btn_w = (inspect_w - 38) / 3;
            int by1 = content_y + 248;
            int by2 = by1 + 30;
            draw_shaping_btn(insp_x + 14, by1, btn_w, 26, is_vi ? L"⧉ Hàn" : L"⧉ Weld");
            draw_shaping_btn(insp_x + 14 + btn_w + 5, by1, btn_w, 26, is_vi ? L"✂ Xén" : L"✂ Trim");
            draw_shaping_btn(insp_x + 14 + (btn_w + 5) * 2, by1, btn_w, 26, is_vi ? L"⨅ Giao" : L"⨅ Intersect");

            draw_shaping_btn(insp_x + 14, by2, btn_w, 26, is_vi ? L"⧇ Rỗng" : L"⧇ Exclude");
            draw_shaping_btn(insp_x + 14 + btn_w + 5, by2, btn_w, 26, is_vi ? L"◫ Trừ dưới" : L"◫ Back-Front");
            draw_shaping_btn(insp_x + 14 + (btn_w + 5) * 2, by2, btn_w, 26, is_vi ? L"🔲 Bao ngoài" : L"🔲 Boundary");

            // CNC Auto Weld Helper Button
            int by3 = by2 + 32;
            draw_shaping_btn(insp_x + 14, by3, inspect_w - 28, 28, is_vi ? L"⚡ Tự động hàn chữ/khối (CNC)" : L"⚡ CNC Auto-Weld Overlaps");

            // ----------------------------------------------------
            // Section: ALIGN & DISTRIBUTE (CorelDRAW & Illustrator)
            // ----------------------------------------------------
            int by_align_hdr = by3 + 36;
            RECT align_head = { insp_x + 14, by_align_hdr, w - 14, by_align_hdr + 22 };
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            DrawTextW(mem_dc, is_vi ? L"CĂN CHỈNH & PHÂN BỔ" : L"ALIGN & DISTRIBUTE", -1,
                      &align_head, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

            // Row 1: Horizontal Align (L, C, R)
            int by_a1 = by_align_hdr + 24;
            draw_shaping_btn(insp_x + 14, by_a1, btn_w, 26, L"|◀ L");
            draw_shaping_btn(insp_x + 14 + btn_w + 5, by_a1, btn_w, 26, L"▶|◀ C");
            draw_shaping_btn(insp_x + 14 + (btn_w + 5) * 2, by_a1, btn_w, 26, L"▶| R");

            // Row 2: Vertical Align (T, E, B)
            int by_a2 = by_a1 + 30;
            draw_shaping_btn(insp_x + 14, by_a2, btn_w, 26, L"▲ T");
            draw_shaping_btn(insp_x + 14 + btn_w + 5, by_a2, btn_w, 26, L"▼▲ E");
            draw_shaping_btn(insp_x + 14 + (btn_w + 5) * 2, by_a2, btn_w, 26, L"▼ B");

            // Row 3: Center to Page (P)
            int by_a3 = by_a2 + 30;
            draw_shaping_btn(insp_x + 14, by_a3, inspect_w - 28, 26, is_vi ? L"⌖ Căn giữa trang in (P)" : L"⌖ Center to Page (P)");

            // Row 4: Align Reference Selector
            int by_a4 = by_a3 + 32;
            SelectObject(mem_dc, font_secondary);
            SetTextColor(mem_dc, col_text_muted);
            RECT rc_ref_lbl = { insp_x + 14, by_a4, insp_x + 72, by_a4 + 22 };
            DrawTextW(mem_dc, is_vi ? L"Quy chiếu:" : L"Align to:", -1, &rc_ref_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            int ref_w = (inspect_w - 85) / 3;
            auto draw_ref_pill = [&](int rx, const wchar_t* rtxt, bool active) {
                RECT rrect = { rx, by_a4 - 2, rx + ref_w, by_a4 + 22 };
                HBRUSH rbr = CreateSolidBrush(active ? col_active_tab : col_btn_bg);
                HPEN rpn = CreatePen(PS_SOLID, 1, active ? col_teal : col_btn_bdr);
                HGDIOBJ rop = SelectObject(mem_dc, rpn);
                HGDIOBJ rob = SelectObject(mem_dc, rbr);
                RoundRect(mem_dc, rrect.left, rrect.top, rrect.right, rrect.bottom, 4, 4);
                SelectObject(mem_dc, rop);
                SelectObject(mem_dc, rob);
                DeleteObject(rpn);
                DeleteObject(rbr);
                SetTextColor(mem_dc, active ? col_teal : col_text_white);
                DrawTextW(mem_dc, rtxt, -1, &rrect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            };
            draw_ref_pill(insp_x + 72, is_vi ? L"Vùng chọn" : L"Selection", align_reference_ == geometry::AlignReference::Selection);
            draw_ref_pill(insp_x + 72 + ref_w + 3, is_vi ? L"Neo (Key)" : L"Key Object", align_reference_ == geometry::AlignReference::KeyObject);
            draw_ref_pill(insp_x + 72 + (ref_w + 3) * 2, is_vi ? L"Trang giấy" : L"Page", align_reference_ == geometry::AlignReference::Page);

            // Row 5: Distribute Spacing with Specified Gap mm
            int by_a5 = by_a4 + 30;
            SelectObject(mem_dc, font_secondary);
            SetTextColor(mem_dc, col_text_muted);
            RECT rc_gap_lbl = { insp_x + 14, by_a5, insp_x + 72, by_a5 + 24 };
            DrawTextW(mem_dc, is_vi ? L"Khoảng hở:" : L"Spacing:", -1, &rc_gap_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            int gap_half = (inspect_w - 90) / 2;
            std::wstring gap_x_txt = (is_vi ? L"↔ Ngang " : L"↔ Horiz ") + std::to_wstring(static_cast<int>(distribute_spacing_mm_)) + L"mm";
            std::wstring gap_y_txt = (is_vi ? L"↕ Dọc " : L"↕ Vert ") + std::to_wstring(static_cast<int>(distribute_spacing_mm_)) + L"mm";
            draw_shaping_btn(insp_x + 72, by_a5 - 2, gap_half, 24, gap_x_txt.c_str());
            draw_shaping_btn(insp_x + 72 + gap_half + 4, by_a5 - 2, gap_half, 24, gap_y_txt.c_str());
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

    auto draw_action_button = [&](int left, int right, const wchar_t* label, bool active) {
        RECT rect{left + 1, 6, right - 1, 32};
        HBRUSH brush = CreateSolidBrush(active ? col_active_tab : col_btn_bg);
        HPEN pen = CreatePen(PS_SOLID, 1, active ? col_teal : col_btn_bdr);
        HGDIOBJ old_pen = SelectObject(mem_dc, pen);
        HGDIOBJ old_brush = SelectObject(mem_dc, brush);
        RoundRect(mem_dc, rect.left, rect.top, rect.right, rect.bottom, 6, 6);
        SelectObject(mem_dc, old_pen);
        SelectObject(mem_dc, old_brush);
        DeleteObject(pen);
        DeleteObject(brush);
        SetTextColor(mem_dc, active ? col_teal : col_text_white);
        SelectObject(mem_dc, active ? font_be_vietnam_bold : font_be_vietnam);
        DrawTextW(mem_dc, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    };
    draw_action_button(148, 198, is_vi ? L"Mới" : L"New", false);
    draw_action_button(198, 248, is_vi ? L"Mở" : L"Open", false);
    draw_action_button(248, 298, is_vi ? L"Lưu" : L"Save", false);
    draw_action_button(298, 350, L"Undo", false);
    draw_action_button(350, 402, L"Redo", false);

    // The File menu contains only implemented document commands.
    RECT rc_m_file = { 402, 6, 456, 32 };
    if (show_file_menu_) {
        HBRUSH br_mf = CreateSolidBrush(col_active_tab);
        HPEN pen_mf = CreatePen(PS_SOLID, 1, col_teal);
        SelectObject(mem_dc, pen_mf);
        SelectObject(mem_dc, br_mf);
        RoundRect(mem_dc, rc_m_file.left, rc_m_file.top, rc_m_file.right, rc_m_file.bottom, 6, 6);
        DeleteObject(pen_mf);
        DeleteObject(br_mf);
        SetTextColor(mem_dc, col_teal);
        SelectObject(mem_dc, font_be_vietnam_bold);
    } else {
        HBRUSH br_mf = CreateSolidBrush(col_header);
        HPEN pen_mf = CreatePen(PS_SOLID, 1, col_header);
        SelectObject(mem_dc, pen_mf);
        SelectObject(mem_dc, br_mf);
        RoundRect(mem_dc, rc_m_file.left, rc_m_file.top, rc_m_file.right, rc_m_file.bottom, 6, 6);
        DeleteObject(pen_mf);
        DeleteObject(br_mf);
        SetTextColor(mem_dc, col_text_white);
        SelectObject(mem_dc, font_be_vietnam);
    }
    DrawTextW(mem_dc, is_vi ? L"Tệp" : L"File", -1, &rc_m_file,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Other workspaces are not exposed until their workflows are implemented.
    draw_action_button(520, 620, L"Vector 2D", true);

    // Topbar Button 1: Settings [ ⚙ Cài đặt (Ctrl+J) ] (w - 215 to w - 105, bo nhẹ 6px)
    int sett_x = w - 215;
    HBRUSH br_sett = CreateSolidBrush(col_btn_bg);
    HPEN p_sett = CreatePen(PS_SOLID, 1, col_btn_bdr);
    SelectObject(mem_dc, p_sett);
    SelectObject(mem_dc, br_sett);
    RoundRect(mem_dc, sett_x, 6, sett_x + 110, 32, 6, 6);
    DeleteObject(p_sett);
    DeleteObject(br_sett);
    SelectObject(mem_dc, font_be_vietnam);
    SetTextColor(mem_dc, col_text_white);
    RECT rc_sett = { sett_x, 6, sett_x + 110, 32 };
    DrawTextW(mem_dc, is_vi ? L"⚙ Cài đặt (Ctrl+J)" : L"⚙ Settings (Ctrl+J)", -1, &rc_sett, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Topbar Button 4: Export Button [ Xuất file ] (w - 95 to w - 12, bo nhẹ 6px)
    HBRUSH br_export = CreateSolidBrush(col_teal);
    HPEN p_export = CreatePen(PS_SOLID, 1, col_teal);
    SelectObject(mem_dc, p_export);
    SelectObject(mem_dc, br_export);
    RoundRect(mem_dc, w - 95, 6, w - 12, 32, 6, 6);
    DeleteObject(p_export);
    DeleteObject(br_export);
    SetTextColor(mem_dc, is_dark ? RGB(7, 11, 16) : RGB(255, 255, 255));
    RECT rc_export = { w - 95, 6, w - 12, 32 };
    DrawTextW(mem_dc, is_vi ? L"Xuất file" : L"Export", -1, &rc_export, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 7. Bottom Status Bar (Always at bottom)
    HBRUSH br_status = CreateSolidBrush(col_statusbar);
    RECT rc_status = { 0, h - status_h, w, h };
    FillRect(mem_dc, &rc_status, br_status);
    DeleteObject(br_status);

    HPEN pen_st_bdr = CreatePen(PS_SOLID, 1, col_border);
    HGDIOBJ old_st_bdr = SelectObject(mem_dc, pen_st_bdr);
    MoveToEx(mem_dc, 0, h - status_h, NULL); LineTo(mem_dc, w, h - status_h);
    SelectObject(mem_dc, old_st_bdr);
    DeleteObject(pen_st_bdr);

    // Live coordinates
    SelectObject(mem_dc, font_mono);
    SetTextColor(mem_dc, col_text_muted);
    std::wostringstream wcoord;
    wcoord << L"X: " << std::fixed << std::setprecision(1) << cursor_mm_x_ << L" mm   "
           << L"Y: " << std::fixed << std::setprecision(1) << cursor_mm_y_ << L" mm";
    std::wstring coord_str = wcoord.str();
    RECT rc_st_coord = { 16, h - status_h, 210, h };
    DrawTextW(mem_dc, coord_str.c_str(), -1, &rc_st_coord, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Theme Mode Badge Chip (bo nhẹ 5px)
    SelectObject(mem_dc, font_be_vietnam);
    HBRUSH br_tbadge = CreateSolidBrush(col_btn_bg);
    HPEN p_tb = CreatePen(PS_SOLID, 1, col_btn_bdr);
    SelectObject(mem_dc, p_tb);
    SelectObject(mem_dc, br_tbadge);
    RoundRect(mem_dc, 215, h - status_h + 3, 400, h - 3, 5, 5);
    DeleteObject(p_tb);
    DeleteObject(br_tbadge);
    SetTextColor(mem_dc, col_teal);
    const wchar_t* thm_badge = is_dark ? L"Theme: Dark Mode (Obsidian)" : L"Theme: Light Mode (Silver)";
    RECT rc_tbadge = { 215, h - status_h + 3, 400, h - 3 };
    DrawTextW(mem_dc, thm_badge, -1, &rc_tbadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Live status message
    SetTextColor(mem_dc, col_text_white);
    std::wstring wmsg = utf8_to_wide(status_msg_);
    RECT rc_st_msg = { 410, h - status_h, w - 230, h };
    DrawTextW(mem_dc, wmsg.c_str(), -1, &rc_st_msg, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Zoom buttons [-] 100% [+] (bo nhẹ 5px)
    SelectObject(mem_dc, font_mono);
    SetTextColor(mem_dc, col_text_white);
    std::wstring wzoom = std::to_wstring(zoom_level_) + L"%";
    RECT rc_zm_fit = { w - 210, h - status_h + 3, w - 145, h - 3 };
    RECT rc_zm_btn_minus = { w - 120, h - status_h + 3, w - 95, h - 3 };
    RECT rc_zm_txt = { w - 90, h - status_h, w - 50, h };
    RECT rc_zm_btn_plus = { w - 45, h - status_h + 3, w - 20, h - 3 };

    HBRUSH br_zm_btn = CreateSolidBrush(col_btn_bg);
    HPEN p_zm = CreatePen(PS_SOLID, 1, col_btn_bdr);
    SelectObject(mem_dc, p_zm);
    SelectObject(mem_dc, br_zm_btn);
    RoundRect(mem_dc, rc_zm_fit.left, rc_zm_fit.top, rc_zm_fit.right, rc_zm_fit.bottom, 5, 5);
    RoundRect(mem_dc, rc_zm_btn_minus.left, rc_zm_btn_minus.top, rc_zm_btn_minus.right, rc_zm_btn_minus.bottom, 5, 5);
    RoundRect(mem_dc, rc_zm_btn_plus.left, rc_zm_btn_plus.top, rc_zm_btn_plus.right, rc_zm_btn_plus.bottom, 5, 5);
    DeleteObject(p_zm);
    DeleteObject(br_zm_btn);

    DrawTextW(mem_dc, L"-", -1, &rc_zm_btn_minus, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(mem_dc, is_vi ? L"Vừa trang" : L"Fit page", -1, &rc_zm_fit,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(mem_dc, wzoom.c_str(), -1, &rc_zm_txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(mem_dc, L"+", -1, &rc_zm_btn_plus, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 8. Paint Settings Modal Dialog if open
    if (show_settings_modal_) {
        paint_settings_modal(mem_dc, w, h);
    }

    // 8b. Paint New Document Modal Dialog if open
    if (show_new_doc_modal_) {
        paint_new_document_modal(mem_dc, w, h);
    }

    // 9. Paint File Menu Dropdown if open (CorelDRAW Reference)
    if (show_file_menu_) {
        paint_file_menu(mem_dc, 402, topbar_h, 270, file_menu_height());
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

        // Drop shadow
        RECT rc_shadow = { tip_x + 2, tip_y + 2, tip_x + tip_w + 2, tip_y + tip_h + 2 };
        HBRUSH br_shadow = CreateSolidBrush(RGB(15, 23, 42));
        HPEN pen_shadow = CreatePen(PS_SOLID, 1, RGB(15, 23, 42));
        HGDIOBJ old_psh = SelectObject(mem_dc, pen_shadow);
        HGDIOBJ old_bsh = SelectObject(mem_dc, br_shadow);
        RoundRect(mem_dc, rc_shadow.left, rc_shadow.top, rc_shadow.right, rc_shadow.bottom, 6, 6);
        SelectObject(mem_dc, old_psh);
        SelectObject(mem_dc, old_bsh);
        DeleteObject(pen_shadow);
        DeleteObject(br_shadow);

        // Tooltip container (Obsidian Dark with Cyber Mint glow border)
        COLORREF col_tip_bg = RGB(24, 24, 27);
        COLORREF col_tip_bdr = RGB(0, 242, 195);
        HBRUSH br_tip = CreateSolidBrush(col_tip_bg);
        HPEN pen_tip = CreatePen(PS_SOLID, 1, col_tip_bdr);
        HGDIOBJ old_pt = SelectObject(mem_dc, pen_tip);
        HGDIOBJ old_bt = SelectObject(mem_dc, br_tip);
        RoundRect(mem_dc, tip_x, tip_y, tip_x + tip_w, tip_y + tip_h, 6, 6);
        SelectObject(mem_dc, old_pt);
        SelectObject(mem_dc, old_bt);
        DeleteObject(pen_tip);
        DeleteObject(br_tip);

        // Text inside tooltip
        RECT rc_text = { tip_x + tip_pad_x, tip_y + tip_pad_y, tip_x + tip_w - tip_pad_x, tip_y + tip_h - tip_pad_y };
        SetBkMode(mem_dc, TRANSPARENT);
        SetTextColor(mem_dc, RGB(255, 255, 255));
        DrawTextW(mem_dc, tooltip_.text.c_str(), -1, &rc_text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    BitBlt(hdc, 0, 0, w, h, mem_dc, 0, 0, SRCCOPY);

    SelectObject(mem_dc, old_font);
}

#endif

} // namespace pikdraw::desktop
