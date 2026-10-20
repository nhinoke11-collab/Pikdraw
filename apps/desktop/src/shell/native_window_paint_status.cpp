#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/rendering/d2d_backend.hpp"

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
    SelectObject(mem_dc, old_status_pen);
    DeleteObject(pen_st_bdr);

    SelectObject(mem_dc, font_mono);
    SetTextColor(mem_dc, col_text_muted);
    std::wostringstream wcoord;
    wcoord << L"X: " << std::fixed << std::setprecision(1) << cursor_mm_x_ << L" mm   "
           << L"Y: " << std::fixed << std::setprecision(1) << cursor_mm_y_ << L" mm";
    const std::wstring coord_str = wcoord.str();
    RECT rc_st_coord{16, h - status_h, 210, h};
    DrawTextW(mem_dc, coord_str.c_str(), -1, &rc_st_coord,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(mem_dc, font_be_vietnam);
    HBRUSH br_tbadge = CreateSolidBrush(col_btn_bg);
    HPEN p_tb = CreatePen(PS_SOLID, 1, col_btn_bdr);
    const HGDIOBJ old_tb_pen = SelectObject(mem_dc, p_tb);
    const HGDIOBJ old_tb_brush = SelectObject(mem_dc, br_tbadge);
    RoundRect(mem_dc, 215, h - status_h + 3, 385, h - 3, 5, 5);
    SelectObject(mem_dc, old_tb_pen);
    SelectObject(mem_dc, old_tb_brush);
    DeleteObject(p_tb);
    DeleteObject(br_tbadge);
    SetTextColor(mem_dc, col_teal);
    const wchar_t* thm_badge = is_dark ? L"Theme: Dark" : L"Theme: Light";
    RECT rc_tbadge{215, h - status_h + 3, 385, h - 3};
    DrawTextW(mem_dc, thm_badge, -1, &rc_tbadge,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    HBRUSH br_gbadge = CreateSolidBrush(col_btn_bg);
    HPEN p_gb = CreatePen(PS_SOLID, 1, col_btn_bdr);
    const HGDIOBJ old_gb_pen = SelectObject(mem_dc, p_gb);
    const HGDIOBJ old_gb_brush = SelectObject(mem_dc, br_gbadge);
    RoundRect(mem_dc, 390, h - status_h + 3, 535, h - 3, 5, 5);
    SelectObject(mem_dc, old_gb_pen);
    SelectObject(mem_dc, old_gb_brush);
    DeleteObject(p_gb);
    DeleteObject(br_gbadge);
    SetTextColor(mem_dc, col_teal);
    const auto& backend = root().renderer().backend();
    const auto* d2d_backend = dynamic_cast<const rendering::D2DRenderBackend*>(&backend);
    const wchar_t* gpu_badge = L"Backend: CPU Fallback";
    if (d2d_backend) {
        if (d2d_backend->device_status() != rendering::DeviceLossStatus::Healthy)
            gpu_badge = L"Direct2D: unavailable";
        else if (d2d_backend->is_hardware_accelerated())
            gpu_badge = L"GPU: Direct2D 1.1";
        else
            gpu_badge = L"Direct2D: WARP";
    }
    RECT rc_gbadge{390, h - status_h + 3, 535, h - 3};
    DrawTextW(mem_dc, gpu_badge, -1, &rc_gbadge,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    HBRUSH br_cbadge = CreateSolidBrush(col_btn_bg);
    HPEN p_cb = CreatePen(PS_SOLID, 1, col_btn_bdr);
    const HGDIOBJ old_cb_pen = SelectObject(mem_dc, p_cb);
    const HGDIOBJ old_cb_brush = SelectObject(mem_dc, br_cbadge);
    RoundRect(mem_dc, 540, h - status_h + 3, 685, h - 3, 5, 5);
    SelectObject(mem_dc, old_cb_pen);
    SelectObject(mem_dc, old_cb_brush);
    DeleteObject(p_cb);
    DeleteObject(br_cbadge);

    const auto snapshot_cur = root().document().current_snapshot();
    std::wstring color_badge = (snapshot_cur && snapshot_cur->color_mode() == color::DocumentColorMode::Cmyk ? L"CMYK" : L"RGB");
    if (proof_colors_enabled_) color_badge += L" • Proof";
    if (gamut_warning_enabled_) color_badge += L" • !Gamut";
    if (overprint_preview_enabled_) color_badge += L" • OP";

    SetTextColor(mem_dc, (proof_colors_enabled_ || gamut_warning_enabled_ || overprint_preview_enabled_) ? RGB(255, 170, 0) : col_teal);
    RECT rc_cbadge_txt{540, h - status_h + 3, 685, h - 3};
    DrawTextW(mem_dc, color_badge.c_str(), -1, &rc_cbadge_txt,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SetTextColor(mem_dc, col_text_white);
    const std::wstring wmsg = utf8_to_wide(status_msg_);
    RECT rc_st_msg{695, h - status_h, w - 230, h};
    DrawTextW(mem_dc, wmsg.c_str(), -1, &rc_st_msg,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(mem_dc, font_mono);
    std::wstring wzoom = std::to_wstring(zoom_level_) + L"%";
    RECT rc_zm_fit{w - 210, h - status_h + 3, w - 145, h - 3};
    RECT rc_zm_btn_minus{w - 120, h - status_h + 3, w - 95, h - 3};
    RECT rc_zm_txt{w - 90, h - status_h, w - 50, h};
    RECT rc_zm_btn_plus{w - 45, h - status_h + 3, w - 20, h - 3};

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
