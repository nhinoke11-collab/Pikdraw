#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_eyedropper.hpp"
#include "native_window_length_input.hpp"
#include "native_window_page_navigation.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace pikdraw::desktop {

#ifdef _WIN32

void NativeWindow::paint_document_tab_bar(HDC mem_dc, int w, int topbar_h, int tabbar_h) {
    const bool is_dark = (theme_ == ThemeMode::CyberDark);
    const COLORREF col_tab_bar_bg = is_dark ? RGB(10, 16, 25) : RGB(241, 245, 249);
    const COLORREF col_tab_active = is_dark ? RGB(18, 28, 42) : RGB(255, 255, 255);
    const COLORREF col_tab_inactive = is_dark ? RGB(12, 19, 29) : RGB(226, 232, 240);
    const COLORREF col_border = is_dark ? RGB(29, 38, 54) : RGB(203, 213, 225);
    const COLORREF col_teal = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    const COLORREF col_text_mut = is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139);

    RECT rc_bar{0, topbar_h, w, topbar_h + tabbar_h};
    HBRUSH br_bar = CreateSolidBrush(col_tab_bar_bg);
    FillRect(mem_dc, &rc_bar, br_bar);
    DeleteObject(br_bar);

    HPEN pen_bdr = CreatePen(PS_SOLID, 1, col_border);
    HGDIOBJ old_pen = SelectObject(mem_dc, pen_bdr);
    MoveToEx(mem_dc, 0, topbar_h + tabbar_h, nullptr);
    LineTo(mem_dc, w, topbar_h + tabbar_h);

    int cur_x = 10;
    constexpr int tab_w = 210;
    ensure_ui_fonts();
    HFONT font_tab = font_tab_;
    HFONT font_tab_bold = font_tab_bold_;
    HGDIOBJ old_font = SelectObject(mem_dc, font_tab);

    for (size_t i = 0; i < tabs_.size(); ++i) {
        const auto& tab = tabs_[i];
        const bool is_act = (i == active_tab_index_);
        RECT rc_tab{cur_x, topbar_h + 2, cur_x + tab_w, topbar_h + tabbar_h};

        HBRUSH br_t = CreateSolidBrush(is_act ? col_tab_active : col_tab_inactive);
        HPEN pen_t = CreatePen(PS_SOLID, 1, is_act ? col_teal : col_border);
        const HGDIOBJ old_tab_pen = SelectObject(mem_dc, pen_t);
        const HGDIOBJ old_tab_brush = SelectObject(mem_dc, br_t);
        RoundRect(mem_dc, rc_tab.left, rc_tab.top, rc_tab.right, rc_tab.bottom + 2, 4, 4);
        SelectObject(mem_dc, old_tab_pen);
        SelectObject(mem_dc, old_tab_brush);
        DeleteObject(pen_t);
        DeleteObject(br_t);

        if (is_act) {
            HPEN pen_top = CreatePen(PS_SOLID, 2, col_teal);
            const HGDIOBJ old_top_pen = SelectObject(mem_dc, pen_top);
            MoveToEx(mem_dc, rc_tab.left + 2, rc_tab.top, nullptr);
            LineTo(mem_dc, rc_tab.right - 2, rc_tab.top);
            SelectObject(mem_dc, old_top_pen);
            DeleteObject(pen_top);
        }

        SelectObject(mem_dc, is_act ? font_tab_bold : font_tab);
        SetTextColor(mem_dc, is_act ? col_teal : col_text_mut);
        std::wstring wtitle = L"📄 " + utf8_to_wide(tab.title);
        const auto snapshot = tab.root->document().current_snapshot();
        const std::wstring wmode = snapshot->color_mode() == color::DocumentColorMode::Cmyk ? L"CMYK" : L"RGB";
        if (wtitle.find(wmode) == std::wstring::npos)
            wtitle += L" (" + wmode + L")";
        RECT rc_txt{rc_tab.left + 8, rc_tab.top, rc_tab.right - 24, rc_tab.bottom};
        DrawTextW(mem_dc, wtitle.c_str(), -1, &rc_txt,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        RECT rc_cls{rc_tab.right - 22, rc_tab.top + 2, rc_tab.right - 4, rc_tab.bottom - 2};
        SetTextColor(mem_dc, col_text_mut);
        DrawTextW(mem_dc, L"✕", -1, &rc_cls,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        cur_x += tab_w + 4;
    }

    RECT rc_new_tab{cur_x, topbar_h + 3, cur_x + 26, topbar_h + tabbar_h - 2};
    HBRUSH br_nt = CreateSolidBrush(col_tab_inactive);
    const HGDIOBJ old_new_tab_brush = SelectObject(mem_dc, br_nt);
    RoundRect(mem_dc, rc_new_tab.left, rc_new_tab.top, rc_new_tab.right,
              rc_new_tab.bottom, 4, 4);
    SelectObject(mem_dc, old_new_tab_brush);
    DeleteObject(br_nt);
    SelectObject(mem_dc, font_tab_bold);
    SetTextColor(mem_dc, col_teal);
    DrawTextW(mem_dc, L"+", -1, &rc_new_tab,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(mem_dc, old_font);
    SelectObject(mem_dc, old_pen);
    DeleteObject(pen_bdr);
}

void NativeWindow::paint_status_bar(HDC mem_dc, int w, int h, int status_h) {
    const bool is_dark = theme_ == ThemeMode::CyberDark;
    const bool is_vi = language_ == UILanguage::Vietnamese;
    const COLORREF col_statusbar = is_dark ? RGB(7, 11, 16) : RGB(226, 232, 240);
    const COLORREF col_border = is_dark ? RGB(29, 38, 54) : RGB(203, 213, 225);
    const COLORREF col_teal = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    const COLORREF col_text_white = is_dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    const COLORREF col_text_muted = is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    const COLORREF col_btn_bg = is_dark ? RGB(18, 28, 42) : RGB(241, 245, 249);
    const COLORREF col_btn_bdr = is_dark ? RGB(29, 38, 54) : RGB(203, 213, 225);
    const HGDIOBJ old_font = GetCurrentObject(mem_dc, OBJ_FONT);
    const HGDIOBJ old_pen = GetCurrentObject(mem_dc, OBJ_PEN);
    const HGDIOBJ old_brush = GetCurrentObject(mem_dc, OBJ_BRUSH);
    HFONT font_be_vietnam = font_be_vietnam_;
    HFONT font_mono = font_mono_;

    HBRUSH br_status = CreateSolidBrush(col_statusbar);
    RECT rc_status{0, h - status_h, w, h};
    FillRect(mem_dc, &rc_status, br_status);
    DeleteObject(br_status);

    HPEN pen_st_bdr = CreatePen(PS_SOLID, 1, col_border);
    const HGDIOBJ old_status_pen = SelectObject(mem_dc, pen_st_bdr);
    MoveToEx(mem_dc, 0, h - status_h, nullptr);
    LineTo(mem_dc, w, h - status_h);
    const int status_top = h - page_navigation::kStatusBarHeight;
    MoveToEx(mem_dc, 0, status_top, nullptr);
    LineTo(mem_dc, w, status_top);
    SelectObject(mem_dc, old_status_pen);
    DeleteObject(pen_st_bdr);

    const auto snapshot = root().document().current_snapshot();
    if (snapshot && !snapshot->pages().empty()) {
        const bool artboard_mode = artboard_overview_enabled();
        const auto& boards = snapshot->active_page().artboards;
        const size_t item_count = artboard_mode ? boards.size() : snapshot->pages().size();
        size_t active_index = 0;
        if (artboard_mode) {
            const auto active_it = std::find_if(boards.begin(), boards.end(), [&](const auto& board) {
                return board.id == active_tab().active_artboard_id;
            });
            if (active_it != boards.end())
                active_index = static_cast<size_t>(std::distance(boards.begin(), active_it));
        } else {
            const auto active_it = std::find_if(snapshot->pages().begin(), snapshot->pages().end(),
                [&](const auto& page) { return page.id == snapshot->active_page_id(); });
            if (active_it != snapshot->pages().end())
                active_index = static_cast<size_t>(std::distance(snapshot->pages().begin(), active_it));
        }
        const auto layout = page_navigation::make_layout(w, item_count, active_index);
        const int row_top = h - status_h;
        const auto draw_page_button = [&](page_navigation::Span span, const wchar_t* label,
                                          bool enabled) {
            RECT rc{span.left, row_top + 3, span.right, status_top - 3};
            HBRUSH button_brush = CreateSolidBrush(enabled ? col_btn_bg : col_statusbar);
            HPEN button_pen = CreatePen(PS_SOLID, 1, enabled ? col_btn_bdr : col_border);
            const auto prior_pen = SelectObject(mem_dc, button_pen);
            const auto prior_brush = SelectObject(mem_dc, button_brush);
            RoundRect(mem_dc, rc.left, rc.top, rc.right, rc.bottom, 4, 4);
            SelectObject(mem_dc, prior_pen);
            SelectObject(mem_dc, prior_brush);
            DeleteObject(button_pen);
            DeleteObject(button_brush);
            SetTextColor(mem_dc, enabled ? col_text_white : col_text_muted);
            DrawTextW(mem_dc, label, -1, &rc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        };
        draw_page_button(layout.first, L"|<", active_index > 0);
        draw_page_button(layout.previous, L"<", active_index > 0);
        draw_page_button(layout.next, L">", active_index + 1 < item_count);
        draw_page_button(layout.last, L">|", active_index + 1 < item_count);
        draw_page_button(layout.add, L"+", true);
        draw_page_button(layout.remove, L"-", item_count > 1);

        SelectObject(mem_dc, font_mono);
        const std::wstring page_count = (artboard_mode ? L"A " : L"") +
            std::to_wstring(active_index + 1) + L" / " + std::to_wstring(item_count);
        RECT count_rc{layout.counter.left, row_top, layout.counter.right, status_top};
        SetTextColor(mem_dc, col_text_white);
        DrawTextW(mem_dc, page_count.c_str(), -1, &count_rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_be_vietnam);
        for (size_t i = layout.first_visible;
             i < layout.first_visible + layout.visible_count; ++i) {
            const auto span = layout.tab(i);
            const bool active = i == active_index;
            RECT rc{span.left + 2, row_top + 3, span.right - 2, status_top - 3};
            HBRUSH tab_brush = CreateSolidBrush(active ? col_btn_bg : col_statusbar);
            HPEN tab_pen = CreatePen(PS_SOLID, 1, active ? col_teal : col_border);
            const auto prior_pen = SelectObject(mem_dc, tab_pen);
            const auto prior_brush = SelectObject(mem_dc, tab_brush);
            RoundRect(mem_dc, rc.left, rc.top, rc.right, rc.bottom, 4, 4);
            SelectObject(mem_dc, prior_pen);
            SelectObject(mem_dc, prior_brush);
            DeleteObject(tab_pen);
            DeleteObject(tab_brush);
            const std::wstring name = utf8_to_wide(artboard_mode
                ? boards[i].name : snapshot->pages()[i].name);
            SetTextColor(mem_dc, active ? col_teal : col_text_muted);
            DrawTextW(mem_dc, name.c_str(), -1, &rc,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
    }

    SelectObject(mem_dc, font_be_vietnam);
    const int color_badge_left = std::max(16, w - 385);
    const int color_badge_right = std::max(color_badge_left + 96, w - 245);
    HBRUSH br_cbadge = CreateSolidBrush(col_btn_bg);
    HPEN p_cb = CreatePen(PS_SOLID, 1, col_btn_bdr);
    const HGDIOBJ old_cb_pen = SelectObject(mem_dc, p_cb);
    const HGDIOBJ old_cb_brush = SelectObject(mem_dc, br_cbadge);
    RoundRect(mem_dc, color_badge_left, status_top + 3, color_badge_right, h - 3, 5, 5);
    SelectObject(mem_dc, old_cb_pen);
    SelectObject(mem_dc, old_cb_brush);
    DeleteObject(p_cb);
    DeleteObject(br_cbadge);

    const auto snapshot_cur = root().document().current_snapshot();
    std::wstring color_badge = (snapshot_cur && snapshot_cur->color_mode() == color::DocumentColorMode::Cmyk ? L"CMYK" : L"RGB");
    if (proof_colors_enabled_) color_badge += L" • Proof";
    if (gamut_warning_enabled_) color_badge += L" • Gamut";
    if (overprint_preview_enabled_) color_badge += L" • OP";

    SetTextColor(mem_dc, (proof_colors_enabled_ || gamut_warning_enabled_ || overprint_preview_enabled_) ? RGB(255, 170, 0) : col_teal);
    RECT rc_cbadge_txt{color_badge_left, status_top + 3, color_badge_right, h - 3};
    DrawTextW(mem_dc, color_badge.c_str(), -1, &rc_cbadge_txt,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    int msg_right = color_badge_left - 12;
    if (hover_color_sample_.valid && w >= 650) {
        const int inspect_box_w = 175;
        const int inspect_box_r = color_badge_left - 8;
        const int inspect_box_l = inspect_box_r - inspect_box_w;
        msg_right = inspect_box_l - 8;

        RECT rc_ibox{inspect_box_l, status_top + 3, inspect_box_r, h - 3};
        HBRUSH br_ibox = CreateSolidBrush(col_btn_bg);
        HPEN p_ibox = CreatePen(PS_SOLID, 1, col_btn_bdr);
        const HGDIOBJ ob = SelectObject(mem_dc, br_ibox);
        const HGDIOBJ op = SelectObject(mem_dc, p_ibox);
        RoundRect(mem_dc, rc_ibox.left, rc_ibox.top, rc_ibox.right, rc_ibox.bottom, 5, 5);
        SelectObject(mem_dc, ob);
        SelectObject(mem_dc, op);
        DeleteObject(p_ibox);
        DeleteObject(br_ibox);

        RECT rc_iswatch{inspect_box_l + 4, status_top + 5, inspect_box_l + 20, h - 5};
        COLORREF sample_col = RGB(hover_color_sample_.r, hover_color_sample_.g, hover_color_sample_.b);
        HBRUSH br_isw = CreateSolidBrush(sample_col);
        HPEN p_isw = CreatePen(PS_SOLID, 1, col_border);
        const HGDIOBJ ob2 = SelectObject(mem_dc, br_isw);
        const HGDIOBJ op2 = SelectObject(mem_dc, p_isw);
        RoundRect(mem_dc, rc_iswatch.left, rc_iswatch.top, rc_iswatch.right, rc_iswatch.bottom, 3, 3);
        SelectObject(mem_dc, ob2);
        SelectObject(mem_dc, op2);
        DeleteObject(p_isw);
        DeleteObject(br_isw);

        std::ostringstream read_ss;
        read_ss << eyedropper_detail::format_sample_color(hover_color_sample_);
        if (hover_color_sample_.is_bitmap_pixel) {
            read_ss << " (" << hover_color_sample_.pixel_x << "," << hover_color_sample_.pixel_y << "px)";
        }
        SelectObject(mem_dc, font_mono);
        SetTextColor(mem_dc, col_teal);
        RECT rc_itxt{inspect_box_l + 24, status_top + 3, inspect_box_r - 4, h - 3};
        const std::wstring witxt = utf8_to_wide(read_ss.str());
        DrawTextW(mem_dc, witxt.c_str(), -1, &rc_itxt,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    SelectObject(mem_dc, font_be_vietnam);
    SetTextColor(mem_dc, col_text_white);
    const std::wstring wmsg = utf8_to_wide(status_msg_);
    RECT rc_st_msg{16, status_top, std::max(16, msg_right), h};
    DrawTextW(mem_dc, wmsg.c_str(), -1, &rc_st_msg,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(mem_dc, font_mono);
    std::wstring wzoom = std::to_wstring(zoom_level_) + L"%";
    RECT rc_zm_fit{w - 210, status_top + 3, w - 145, h - 3};
    RECT rc_zm_btn_minus{w - 120, status_top + 3, w - 95, h - 3};
    RECT rc_zm_txt{w - 90, status_top, w - 50, h};
    RECT rc_zm_btn_plus{w - 45, status_top + 3, w - 20, h - 3};

    HBRUSH br_zm_btn = CreateSolidBrush(col_btn_bg);
    HPEN p_zm = CreatePen(PS_SOLID, 1, col_btn_bdr);
    SelectObject(mem_dc, p_zm);
    SelectObject(mem_dc, br_zm_btn);
    RoundRect(mem_dc, rc_zm_fit.left, rc_zm_fit.top, rc_zm_fit.right,
              rc_zm_fit.bottom, 5, 5);
    RoundRect(mem_dc, rc_zm_btn_minus.left, rc_zm_btn_minus.top,
              rc_zm_btn_minus.right, rc_zm_btn_minus.bottom, 5, 5);
    RoundRect(mem_dc, rc_zm_btn_plus.left, rc_zm_btn_plus.top,
              rc_zm_btn_plus.right, rc_zm_btn_plus.bottom, 5, 5);
    SelectObject(mem_dc, old_pen);
    SelectObject(mem_dc, old_brush);
    DeleteObject(p_zm);
    DeleteObject(br_zm_btn);

    DrawTextW(mem_dc, L"-", -1, &rc_zm_btn_minus,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(mem_dc, is_vi ? L"Vừa trang" : L"Fit page", -1, &rc_zm_fit,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(mem_dc, wzoom.c_str(), -1, &rc_zm_txt,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(mem_dc, L"+", -1, &rc_zm_btn_plus,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(mem_dc, old_font);
}

#endif

} // namespace pikdraw::desktop
