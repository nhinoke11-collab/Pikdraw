#include "pikdraw/desktop/shell/native_window.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <sstream>

namespace pikdraw::desktop {

#ifdef _WIN32


void NativeWindow::paint(HDC hdc, int w, int h) {
    HDC mem_dc = CreateCompatibleDC(hdc);
    HBITMAP mem_bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);

    // Primary font: Be Vietnam Pro; Secondary: Google Sans; Numeric: JetBrains Mono
    auto make_font = [](int sz, int weight, const wchar_t* name) {
        return CreateFontW(sz, 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, name);
    };
    HFONT font_be_vietnam = make_font(14, FW_NORMAL, L"Be Vietnam Pro");
    HFONT font_be_vietnam_bold = make_font(14, FW_BOLD, L"Be Vietnam Pro");
    HFONT font_title_bold = make_font(22, FW_BOLD, L"Be Vietnam Pro");
    HFONT font_secondary = make_font(13, FW_NORMAL, L"Google Sans");
    HFONT font_secondary_bold = make_font(13, FW_BOLD, L"Google Sans");
    HFONT font_mono = make_font(12, FW_NORMAL, L"JetBrains Mono");

    HGDIOBJ old_font = SelectObject(mem_dc, font_be_vietnam);

    bool is_dark = (theme_ == ThemeMode::CyberDark);
    bool is_vi = (language_ == UILanguage::Vietnamese);

    // 2026 Dual Theme Palette: Dark (Obsidian Teal) / Light (Silver Tech)
    COLORREF col_canvas = is_dark ? RGB(7, 12, 18) : RGB(241, 245, 249);
    COLORREF col_header = is_dark ? RGB(12, 19, 29) : RGB(255, 255, 255);
    COLORREF col_ribbon = is_dark ? RGB(14, 22, 34) : RGB(248, 250, 252);
    COLORREF col_statusbar = is_dark ? RGB(7, 11, 16) : RGB(226, 232, 240);
    COLORREF col_border = is_dark ? RGB(29, 38, 54) : RGB(203, 213, 225);
    COLORREF col_grid = is_dark ? RGB(24, 35, 52) : RGB(218, 225, 233);
    COLORREF col_teal = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    COLORREF col_blue = is_dark ? RGB(14, 165, 233) : RGB(2, 132, 199);
    COLORREF col_text_white = is_dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    COLORREF col_text_muted = is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    COLORREF col_input_bg = is_dark ? RGB(8, 13, 20) : RGB(255, 255, 255);
    COLORREF col_active_tab = is_dark ? RGB(22, 37, 54) : RGB(236, 242, 248);
    COLORREF col_btn_bg = is_dark ? RGB(18, 28, 42) : RGB(241, 245, 249);
    COLORREF col_btn_bdr = is_dark ? RGB(29, 38, 54) : RGB(203, 213, 225);

    int topbar_h = 38, ribbon_h = 34, status_h = 26, tool_w = 52, inspect_w = 280;

    SetBkMode(mem_dc, TRANSPARENT);

    if (show_home_screen_) {
        // Render Home Screen (Trang Home)
        paint_home_screen(mem_dc, w, h);
    } else {
        // 1. Canvas Background with dotted grid
        HBRUSH br_canvas = CreateSolidBrush(col_canvas);
        RECT rc_full = { 0, 0, w, h };
        FillRect(mem_dc, &rc_full, br_canvas);
        DeleteObject(br_canvas);

        for (int gx = 60; gx < w - 280; gx += 24) {
            for (int gy = 80; gy < h - 30; gy += 24) {
                SetPixel(mem_dc, gx, gy, col_grid);
            }
        }

        // 2. Central Signboard Artboard (Centered on canvas)
        int canvas_w = w - tool_w - inspect_w;
        int art_w = 660, art_h = 440;
        int art_x = tool_w + (canvas_w - art_w) / 2;
        int art_y = topbar_h + ribbon_h + 35;

        // Drop shadow
        HBRUSH br_shadow = CreateSolidBrush(is_dark ? RGB(2, 4, 8) : RGB(210, 218, 226));
        RECT rc_shadow = { art_x + 8, art_y + 8, art_x + art_w + 8, art_y + art_h + 8 };
        FillRect(mem_dc, &rc_shadow, br_shadow);
        DeleteObject(br_shadow);

        // Acrylic signboard base (bo nháº¹ 12px)
        HBRUSH br_sign_base = CreateSolidBrush(is_dark ? RGB(15, 23, 42) : RGB(255, 255, 255));
        HPEN pen_metal = CreatePen(PS_SOLID, 2, is_dark ? RGB(51, 65, 85) : RGB(203, 213, 225));
        SelectObject(mem_dc, pen_metal);
        SelectObject(mem_dc, br_sign_base);
        RoundRect(mem_dc, art_x, art_y, art_x + art_w, art_y + art_h, 12, 12);
        DeleteObject(pen_metal);
        DeleteObject(br_sign_base);

        // Interactive Vector Signboard Object (drag and move)
        int obj_x = art_x + 30 + static_cast<int>(shape_x_ * 0.15);
        int obj_y = art_y + 40 + static_cast<int>(shape_y_ * 0.15);
        int obj_w = 460, obj_h = 160;

        // Neon plate fill & glowing border (bo nháº¹ 10px)
        HBRUSH br_neon_bg = CreateSolidBrush(is_dark ? RGB(12, 32, 44) : RGB(240, 253, 250));
        HPEN pen_neon = CreatePen(PS_SOLID, 2, col_teal);
        SelectObject(mem_dc, pen_neon);
        SelectObject(mem_dc, br_neon_bg);
        RoundRect(mem_dc, obj_x, obj_y, obj_x + obj_w, obj_y + obj_h, 10, 10);
        DeleteObject(pen_neon);
        DeleteObject(br_neon_bg);

        // Primary Text (Be Vietnam Pro)
        SelectObject(mem_dc, font_title_bold);
        SetTextColor(mem_dc, col_teal);
        RECT rc_title = { obj_x + 10, obj_y + 30, obj_x + obj_w - 10, obj_y + 68 };
        DrawTextW(mem_dc, L"AN PHÃT SIGNAGE 2026", -1, &rc_title, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Secondary Text (Google Sans)
        SelectObject(mem_dc, font_secondary_bold);
        SetTextColor(mem_dc, col_text_muted);
        RECT rc_sub = { obj_x + 10, obj_y + 80, obj_x + obj_w - 10, obj_y + 115 };
        DrawTextW(mem_dc, L"CÃ”NG TY TNHH QUáº¢NG CÃO & THI CÃ”NG Ná»˜I THáº¤T", -1, &rc_sub, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Selection box with handles and dimension callout
        HPEN pen_sel = CreatePen(PS_DOT, 1, col_blue);
        SelectObject(mem_dc, pen_sel);
        SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
        Rectangle(mem_dc, obj_x - 4, obj_y - 4, obj_x + obj_w + 4, obj_y + obj_h + 4);
        DeleteObject(pen_sel);

        HBRUSH br_h = CreateSolidBrush(RGB(255, 255, 255));
        SelectObject(mem_dc, br_h);
        int handles[8][2] = {
            {obj_x - 6, obj_y - 6}, {obj_x + obj_w / 2 - 3, obj_y - 6}, {obj_x + obj_w, obj_y - 6},
            {obj_x - 6, obj_y + obj_h / 2 - 3}, {obj_x + obj_w, obj_y + obj_h / 2 - 3},
            {obj_x - 6, obj_y + obj_h}, {obj_x + obj_w / 2 - 3, obj_y + obj_h}, {obj_x + obj_w, obj_y + obj_h}
        };
        for (int i = 0; i < 8; ++i) {
            Rectangle(mem_dc, handles[i][0], handles[i][1], handles[i][0] + 6, handles[i][1] + 6);
        }
        DeleteObject(br_h);

        // Dimension callout chip (bo nháº¹ 6px)
        SelectObject(mem_dc, font_mono);
        HBRUSH br_chip = CreateSolidBrush(is_dark ? RGB(18, 28, 42) : RGB(226, 232, 240));
        HPEN pen_chip = CreatePen(PS_SOLID, 1, col_border);
        SelectObject(mem_dc, pen_chip);
        SelectObject(mem_dc, br_chip);
        RoundRect(mem_dc, obj_x + obj_w / 2 - 75, obj_y + obj_h + 8, obj_x + obj_w / 2 + 75, obj_y + obj_h + 30, 6, 6);
        DeleteObject(pen_chip);
        DeleteObject(br_chip);
        SetTextColor(mem_dc, col_teal);
        RECT rc_chip = { obj_x + obj_w / 2 - 75, obj_y + obj_h + 8, obj_x + obj_w / 2 + 75, obj_y + obj_h + 30 };
        DrawTextW(mem_dc, L"1,850.0 x 420.0 mm", -1, &rc_chip, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // 4. Context Tool Ribbon
        HBRUSH br_ribbon = CreateSolidBrush(col_ribbon);
        RECT rc_ribbon = { 0, topbar_h, w, topbar_h + ribbon_h };
        FillRect(mem_dc, &rc_ribbon, br_ribbon);
        DeleteObject(br_ribbon);

        RECT rc_rib_ind = { 16, topbar_h, 240, topbar_h + ribbon_h };
        SetTextColor(mem_dc, col_teal);
        SelectObject(mem_dc, font_be_vietnam_bold);
        DrawTextW(mem_dc, is_vi ? L"â–  NhÃ³m chá»¯ Neon Signboard (#402)" : L"â–  Neon Signboard Group (#402)", -1, &rc_rib_ind, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_mono);
        SetTextColor(mem_dc, col_text_white);
        std::wostringstream wgeom;
        wgeom << L"X: " << std::fixed << std::setprecision(1) << shape_x_ << L" mm   "
              << L"Y: " << std::fixed << std::setprecision(1) << shape_y_ << L" mm   "
              << L"W: " << std::fixed << std::setprecision(1) << shape_w_ << L" mm   "
              << L"H: " << std::fixed << std::setprecision(1) << shape_h_ << L" mm   "
              << L"GÃ³c: 0Â°   |   TÃ´: #00f2c3   Viá»n: #0ea5e9 (2 mm)";
        std::wstring geom_str = wgeom.str();
        RECT rc_rib_geom = { 250, topbar_h, w - 16, topbar_h + ribbon_h };
        DrawTextW(mem_dc, geom_str.c_str(), -1, &rc_rib_geom, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // 5. Left Tool Strip (V, A, P, R, E, T, H, Z)
        HBRUSH br_tools = CreateSolidBrush(col_header);
        RECT rc_tools = { 0, topbar_h + ribbon_h, tool_w, h - status_h };
        FillRect(mem_dc, &rc_tools, br_tools);
        DeleteObject(br_tools);

        const wchar_t* tool_labels[] = { L"V", L"A", L"P", L"R", L"E", L"T", L"H", L"Z" };
        for (int i = 0; i < 8; ++i) {
            int ty = topbar_h + ribbon_h + 10 + i * 42;
            RECT rc_tb = { 6, ty, tool_w - 6, ty + 36 };
            if (active_tool_ == i) {
                HBRUSH br_t_act = CreateSolidBrush(col_active_tab);
                HPEN pen_t_act = CreatePen(PS_SOLID, 1, col_teal);
                SelectObject(mem_dc, pen_t_act);
                SelectObject(mem_dc, br_t_act);
                RoundRect(mem_dc, rc_tb.left, rc_tb.top, rc_tb.right, rc_tb.bottom, 6, 6); // bo nháº¹ 6px
                DeleteObject(pen_t_act);
                DeleteObject(br_t_act);
                SetTextColor(mem_dc, col_teal);
                SelectObject(mem_dc, font_be_vietnam_bold);
            } else {
                HBRUSH br_t_inact = CreateSolidBrush(col_header);
                HPEN pen_t_inact = CreatePen(PS_SOLID, 1, col_border);
                SelectObject(mem_dc, pen_t_inact);
                SelectObject(mem_dc, br_t_inact);
                RoundRect(mem_dc, rc_tb.left, rc_tb.top, rc_tb.right, rc_tb.bottom, 6, 6); // bo nháº¹ 6px
                DeleteObject(pen_t_inact);
                DeleteObject(br_t_inact);
                SetTextColor(mem_dc, col_text_muted);
                SelectObject(mem_dc, font_be_vietnam);
            }
            DrawTextW(mem_dc, tool_labels[i], -1, &rc_tb, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        // Dual Color Box
        int sw_y = h - status_h - 60;
        HBRUSH br_sw_bg = CreateSolidBrush(col_input_bg);
        RECT rc_sw_box = { 8, sw_y, tool_w - 8, sw_y + 44 };
        FillRect(mem_dc, &rc_sw_box, br_sw_bg);
        DeleteObject(br_sw_bg);
        HBRUSH br_fill_sw = CreateSolidBrush(col_teal);
        RECT rc_fill_sq = { 12, sw_y + 4, 30, sw_y + 22 };
        FillRect(mem_dc, &rc_fill_sq, br_fill_sw);
        DeleteObject(br_fill_sw);
        HBRUSH br_strk_sw = CreateSolidBrush(col_blue);
        RECT rc_strk_sq = { 22, sw_y + 16, 40, sw_y + 34 };
        FillRect(mem_dc, &rc_strk_sq, br_strk_sw);
        DeleteObject(br_strk_sw);

        // 6. Right Inspector & Layers Dock
        int insp_x = w - inspect_w;
        HBRUSH br_dock = CreateSolidBrush(col_header);
        RECT rc_dock = { insp_x, topbar_h + ribbon_h, w, h - status_h };
        FillRect(mem_dc, &rc_dock, br_dock);
        DeleteObject(br_dock);

        int tab_w = inspect_w / 3;
        const wchar_t* tab_names_vi[] = { L"Thuá»™c tÃ­nh", L"Lá»›p (Layers)", L"Lá»‹ch sá»­" };
        const wchar_t* tab_names_en[] = { L"Inspector", L"Layers", L"History" };
        for (int i = 0; i < 3; ++i) {
            RECT rc_tab = { insp_x + i * tab_w, topbar_h + ribbon_h, insp_x + (i + 1) * tab_w, topbar_h + ribbon_h + 28 };
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

        int content_y = topbar_h + ribbon_h + 36;
        if (active_dock_tab_ == 0) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT rc_sec1 = { insp_x + 14, content_y, w - 14, content_y + 20 };
            DrawTextW(mem_dc, is_vi ? L"THUá»˜C TÃNH HÃŒNH Há»ŒC (TRANSFORM)" : L"TRANSFORM MATRIX", -1, &rc_sec1, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(mem_dc, font_secondary);
            SetTextColor(mem_dc, col_text_white);
            std::wostringstream winsp;
            winsp << (is_vi ? L"Äá»‘i tÆ°á»£ng: Báº£ng hiá»‡u Neon (#402)\n" : L"Object: Neon Signboard (#402)\n")
                  << L"Vá»‹ trÃ­ X: " << std::fixed << std::setprecision(1) << shape_x_ << L" mm\n"
                  << L"Vá»‹ trÃ­ Y: " << std::fixed << std::setprecision(1) << shape_y_ << L" mm\n"
                  << L"Chiá»u rá»™ng: " << std::fixed << std::setprecision(1) << shape_w_ << L" mm\n"
                  << L"Chiá»u cao: " << std::fixed << std::setprecision(1) << shape_h_ << L" mm\n"
                  << L"GÃ³c quay: 0.00Â°";
            std::wstring insp_text = winsp.str();
            RECT rc_sec1_body = { insp_x + 14, content_y + 25, w - 14, content_y + 130 };
            DrawTextW(mem_dc, insp_text.c_str(), -1, &rc_sec1_body, DT_LEFT | DT_NOPREFIX);

            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT rc_sec2 = { insp_x + 14, content_y + 140, w - 14, content_y + 160 };
            DrawTextW(mem_dc, is_vi ? L"MÃ€U Sáº®C & ÄÆ¯á»œNG VIá»€N" : L"APPEARANCE & STROKE", -1, &rc_sec2, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(mem_dc, font_secondary);
            SetTextColor(mem_dc, col_text_white);
            RECT rc_sec2_body = { insp_x + 14, content_y + 165, w - 14, content_y + 240 };
            const wchar_t* app_text = is_vi ?
                L"MÃ u tÃ´: #00f2c3 (Cyber Teal)\nÄá»™ má» Ä‘á»¥c: 100%\nViá»n viá»n: #0ea5e9 (Sky Blue)\nÄá»™ dÃ y viá»n: 2.0 mm" :
                L"Fill: #00f2c3 (Cyber Teal)\nOpacity: 100%\nStroke: #0ea5e9 (Sky Blue)\nStroke Width: 2.0 mm";
            DrawTextW(mem_dc, app_text, -1, &rc_sec2_body, DT_LEFT | DT_NOPREFIX);
        } else if (active_dock_tab_ == 1) {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT rc_l_head = { insp_x + 14, content_y, w - 14, content_y + 20 };
            DrawTextW(mem_dc, is_vi ? L"Cáº¤U TRÃšC PHÃ‚N Cáº¤P Lá»šP" : L"LAYER HIERARCHY", -1, &rc_l_head, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(mem_dc, font_secondary);
            SetTextColor(mem_dc, col_text_white);
            RECT rc_l_body = { insp_x + 14, content_y + 25, w - 14, content_y + 200 };
            const wchar_t* layer_tree = is_vi ?
                L"â–¼ Báº£ng hiá»‡u chÃ­nh (#402)\n   [T] AN PHÃT SIGNAGE 2026\n   [T] CÃ”NG TY TNHH QUáº¢NG CÃO...\n   [R] Khung Mica Neon #00f2c3\n\nâ–¼ Lá»›p ná»n (Background)\n   [R] Táº¥m á»‘p Alumini ngoÃ i trá»i" :
                L"â–¼ Main Signboard (#402)\n   [T] AN PHAT SIGNAGE 2026\n   [T] CTY TNHH QUANG CAO...\n   [R] Neon Mica Frame #00f2c3\n\nâ–¼ Background Layer\n   [R] Outdoor Alumini Base";
            DrawTextW(mem_dc, layer_tree, -1, &rc_l_body, DT_LEFT | DT_NOPREFIX);
        } else {
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT rc_h_head = { insp_x + 14, content_y, w - 14, content_y + 20 };
            DrawTextW(mem_dc, is_vi ? L"Lá»ŠCH Sá»¬ THAO TÃC (UNDO)" : L"COMMAND HISTORY", -1, &rc_h_head, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(mem_dc, font_secondary);
            SetTextColor(mem_dc, col_text_white);
            RECT rc_h_body = { insp_x + 14, content_y + 25, w - 14, content_y + 200 };
            const wchar_t* hist_text = is_vi ?
                L"â— Rev 15: Chuyá»ƒn Ä‘á»•i Theme SÃ¡ng/Tá»‘i\nâ— Rev 14: Di chuyá»ƒn Ä‘á»‘i tÆ°á»£ng chá»¯\nâ— Rev 13: Äá»•i mÃ u tÃ´ sang #00f2c3\nâ— Rev 12: ThÃªm tiÃªu Ä‘á» Be Vietnam Pro\nâ— Rev 11: Táº¡o khung mica bo gÃ³c" :
                L"â— Rev 15: Toggle Light/Dark Theme\nâ— Rev 14: Move text object\nâ— Rev 13: Change fill to #00f2c3\nâ— Rev 12: Add Be Vietnam Pro title\nâ— Rev 11: Create rounded frame";
            DrawTextW(mem_dc, hist_text, -1, &rc_h_body, DT_LEFT | DT_NOPREFIX);
        }

        // Hairline borders between layout blocks
        HPEN pen_bdr = CreatePen(PS_SOLID, 1, col_border);
        HGDIOBJ old_bdr = SelectObject(mem_dc, pen_bdr);

        MoveToEx(mem_dc, 0, topbar_h + ribbon_h, NULL); LineTo(mem_dc, w, topbar_h + ribbon_h);
        MoveToEx(mem_dc, tool_w, topbar_h + ribbon_h, NULL); LineTo(mem_dc, tool_w, h - status_h);
        MoveToEx(mem_dc, insp_x, topbar_h + ribbon_h, NULL); LineTo(mem_dc, insp_x, h - status_h);

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

    // Logo PK badge (bo nháº¹ 6px)
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

    // Topbar Home Button [ ðŸ  Trang chá»§ / ðŸŽ¨ Báº£n váº½ ] (bo nháº¹ 6px)
    RECT rc_home = { 148, 6, 232, 32 };
    HBRUSH br_home = CreateSolidBrush(show_home_screen_ ? col_teal : col_btn_bg);
    HPEN pen_home = CreatePen(PS_SOLID, 1, show_home_screen_ ? col_teal : col_btn_bdr);
    SelectObject(mem_dc, pen_home);
    SelectObject(mem_dc, br_home);
    RoundRect(mem_dc, rc_home.left, rc_home.top, rc_home.right, rc_home.bottom, 6, 6);
    DeleteObject(pen_home);
    DeleteObject(br_home);
    SelectObject(mem_dc, font_be_vietnam_bold);
    SetTextColor(mem_dc, show_home_screen_ ? RGB(7, 11, 16) : col_teal);
    const wchar_t* home_lbl = show_home_screen_ ? (is_vi ? L"ðŸŽ¨ Báº£n váº½" : L"ðŸŽ¨ Canvas") : (is_vi ? L"ðŸ  Trang chá»§" : L"ðŸ  Home");
    DrawTextW(mem_dc, home_lbl, -1, &rc_home, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Menu [ Tá»‡p â–¾ ] (bo nháº¹ 6px khi active)
    RECT rc_m_file = { 236, 6, 282, 32 };
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
    DrawTextW(mem_dc, is_vi ? L"Tá»‡p â–¾" : L"File â–¾", -1, &rc_m_file, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Quick Menus: Sá»­a, Äá»‘i tÆ°á»£ng, Thi cÃ´ng, Plugin
    SelectObject(mem_dc, font_be_vietnam);
    SetTextColor(mem_dc, col_text_muted);
    RECT rc_menus = { 288, 6, 515, 32 };
    const wchar_t* m_txt = is_vi ? L"Sá»­a    Äá»‘i tÆ°á»£ng    Thi cÃ´ng    Plugin" : L"Edit    Object    Production    Plugin";
    DrawTextW(mem_dc, m_txt, -1, &rc_menus, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Workspace Pills (520 to 790, bo nháº¹ 6px)
    int pill_x = 520;
    const wchar_t* ws_names_vi[] = { L"Vector", L"In áº¥n", L"Thi cÃ´ng", L"3D" };
    const wchar_t* ws_names_en[] = { L"Vector", L"Print", L"Production", L"3D" };
    int ws_widths[] = { 65, 65, 75, 50 };
    int cur_px = pill_x;
    for (int i = 0; i < 4; ++i) {
        RECT rc_p = { cur_px, 6, cur_px + ws_widths[i], 32 };
        if (active_workspace_ == i) {
            HBRUSH br_w_act = CreateSolidBrush(col_active_tab);
            HPEN p_w_act = CreatePen(PS_SOLID, 1, col_teal);
            SelectObject(mem_dc, p_w_act);
            SelectObject(mem_dc, br_w_act);
            RoundRect(mem_dc, rc_p.left, rc_p.top, rc_p.right, rc_p.bottom, 6, 6);
            DeleteObject(p_w_act);
            DeleteObject(br_w_act);
            SetTextColor(mem_dc, col_teal);
            SelectObject(mem_dc, font_be_vietnam_bold);
        } else {
            HBRUSH br_w_inact = CreateSolidBrush(col_btn_bg);
            HPEN p_w_inact = CreatePen(PS_SOLID, 1, col_btn_bdr);
            SelectObject(mem_dc, p_w_inact);
            SelectObject(mem_dc, br_w_inact);
            RoundRect(mem_dc, rc_p.left, rc_p.top, rc_p.right, rc_p.bottom, 6, 6);
            DeleteObject(p_w_inact);
            DeleteObject(br_w_inact);
            SetTextColor(mem_dc, col_text_muted);
            SelectObject(mem_dc, font_be_vietnam);
        }
        DrawTextW(mem_dc, is_vi ? ws_names_vi[i] : ws_names_en[i], -1, &rc_p, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        cur_px += ws_widths[i] + 5;
    }

    // Topbar Button 1: Settings [ âš™ CÃ i Ä‘áº·t (Ctrl+J) ] (w - 215 to w - 105, bo nháº¹ 6px)
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
    DrawTextW(mem_dc, is_vi ? L"âš™ CÃ i Ä‘áº·t (Ctrl+J)" : L"âš™ Settings (Ctrl+J)", -1, &rc_sett, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Topbar Button 4: Export Button [ Xuáº¥t file ] (w - 95 to w - 12, bo nháº¹ 6px)
    HBRUSH br_export = CreateSolidBrush(col_teal);
    HPEN p_export = CreatePen(PS_SOLID, 1, col_teal);
    SelectObject(mem_dc, p_export);
    SelectObject(mem_dc, br_export);
    RoundRect(mem_dc, w - 95, 6, w - 12, 32, 6, 6);
    DeleteObject(p_export);
    DeleteObject(br_export);
    SetTextColor(mem_dc, is_dark ? RGB(7, 11, 16) : RGB(255, 255, 255));
    RECT rc_export = { w - 95, 6, w - 12, 32 };
    DrawTextW(mem_dc, is_vi ? L"Xuáº¥t file" : L"Export", -1, &rc_export, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

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

    // Theme Mode Badge Chip (bo nháº¹ 5px)
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
    RECT rc_st_msg = { 410, h - status_h, w - 170, h };
    DrawTextW(mem_dc, wmsg.c_str(), -1, &rc_st_msg, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Zoom buttons [-] 100% [+] (bo nháº¹ 5px)
    SelectObject(mem_dc, font_mono);
    SetTextColor(mem_dc, col_text_white);
    std::wstring wzoom = std::to_wstring(zoom_level_) + L"%";
    RECT rc_zm_btn_minus = { w - 120, h - status_h + 3, w - 95, h - 3 };
    RECT rc_zm_txt = { w - 90, h - status_h, w - 50, h };
    RECT rc_zm_btn_plus = { w - 45, h - status_h + 3, w - 20, h - 3 };

    HBRUSH br_zm_btn = CreateSolidBrush(col_btn_bg);
    HPEN p_zm = CreatePen(PS_SOLID, 1, col_btn_bdr);
    SelectObject(mem_dc, p_zm);
    SelectObject(mem_dc, br_zm_btn);
    RoundRect(mem_dc, rc_zm_btn_minus.left, rc_zm_btn_minus.top, rc_zm_btn_minus.right, rc_zm_btn_minus.bottom, 5, 5);
    RoundRect(mem_dc, rc_zm_btn_plus.left, rc_zm_btn_plus.top, rc_zm_btn_plus.right, rc_zm_btn_plus.bottom, 5, 5);
    DeleteObject(p_zm);
    DeleteObject(br_zm_btn);

    DrawTextW(mem_dc, L"-", -1, &rc_zm_btn_minus, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(mem_dc, wzoom.c_str(), -1, &rc_zm_txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(mem_dc, L"+", -1, &rc_zm_btn_plus, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 8. Paint Settings Modal Dialog if open
    if (show_settings_modal_) {
        paint_settings_modal(mem_dc, w, h);
    }

    // 9. Paint File Menu Dropdown if open (CorelDRAW Reference)
    if (show_file_menu_) {
        paint_file_menu(mem_dc, 236, topbar_h, 270, 582);
    }

    BitBlt(hdc, 0, 0, w, h, mem_dc, 0, 0, SRCCOPY);

    SelectObject(mem_dc, old_font);
    for (auto* f : { font_be_vietnam, font_be_vietnam_bold, font_title_bold, font_secondary, font_secondary_bold, font_mono }) {
        DeleteObject(f);
    }
    SelectObject(mem_dc, old_bmp);
    DeleteObject(mem_bmp);
    DeleteDC(mem_dc);
}

#endif

} // namespace pikdraw::desktop
