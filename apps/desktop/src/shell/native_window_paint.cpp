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
    HFONT font_be_vietnam = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Be Vietnam Pro");
    HFONT font_be_vietnam_bold = CreateFontW(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Be Vietnam Pro");
    HFONT font_title_bold = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Be Vietnam Pro");
    HFONT font_secondary = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Google Sans");
    HFONT font_secondary_bold = CreateFontW(13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Google Sans");
    HFONT font_mono = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"JetBrains Mono");

    HGDIOBJ old_font = SelectObject(mem_dc, font_be_vietnam);

    // Cyber Dark 2026 Palette
    COLORREF col_canvas = RGB(7, 12, 18);
    COLORREF col_header = RGB(12, 19, 29);
    COLORREF col_ribbon = RGB(14, 22, 34);
    COLORREF col_statusbar = RGB(7, 11, 16);
    COLORREF col_border = RGB(29, 38, 54);
    COLORREF col_teal = RGB(0, 242, 195);
    COLORREF col_blue = RGB(14, 165, 233);
    COLORREF col_text_white = RGB(241, 245, 249);
    COLORREF col_text_muted = RGB(148, 163, 184);
    COLORREF col_input_bg = RGB(8, 13, 20);
    COLORREF col_active_tab = RGB(22, 37, 54);

    bool is_vi = (language_ == UILanguage::Vietnamese);

    // 1. Canvas Background with dotted grid
    HBRUSH br_canvas = CreateSolidBrush(col_canvas);
    RECT rc_full = { 0, 0, w, h };
    FillRect(mem_dc, &rc_full, br_canvas);
    DeleteObject(br_canvas);

    for (int gx = 60; gx < w - 280; gx += 24) {
        for (int gy = 80; gy < h - 30; gy += 24) {
            SetPixel(mem_dc, gx, gy, RGB(24, 35, 52));
        }
    }

    SetBkMode(mem_dc, TRANSPARENT);

    int topbar_h = 38;
    int ribbon_h = 34;
    int status_h = 26;
    int tool_w = 52;
    int inspect_w = 280;

    // 2. Central Signboard Artboard (Centered on canvas)
    int canvas_w = w - tool_w - inspect_w;
    int art_w = 660;
    int art_h = 440;
    int art_x = tool_w + (canvas_w - art_w) / 2;
    int art_y = topbar_h + ribbon_h + 35;

    // Drop shadow
    HBRUSH br_shadow = CreateSolidBrush(RGB(2, 4, 8));
    RECT rc_shadow = { art_x + 8, art_y + 8, art_x + art_w + 8, art_y + art_h + 8 };
    FillRect(mem_dc, &rc_shadow, br_shadow);
    DeleteObject(br_shadow);

    // Acrylic signboard base (Dark Slate Navy #0f172a)
    HBRUSH br_sign_base = CreateSolidBrush(RGB(15, 23, 42));
    HPEN pen_metal = CreatePen(PS_SOLID, 2, RGB(51, 65, 85));
    SelectObject(mem_dc, pen_metal);
    SelectObject(mem_dc, br_sign_base);
    RoundRect(mem_dc, art_x, art_y, art_x + art_w, art_y + art_h, 12, 12);
    DeleteObject(pen_metal);
    DeleteObject(br_sign_base);

    // Interactive Vector Signboard Object (drag and move)
    int obj_x = art_x + 30 + static_cast<int>(shape_x_ * 0.15);
    int obj_y = art_y + 40 + static_cast<int>(shape_y_ * 0.15);
    int obj_w = 460;
    int obj_h = 160;

    // Neon plate fill & glowing border
    HBRUSH br_neon_bg = CreateSolidBrush(RGB(12, 32, 44));
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
    DrawTextW(mem_dc, L"AN PHÁT SIGNAGE 2026", -1, &rc_title, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Secondary Text (Google Sans)
    SelectObject(mem_dc, font_secondary_bold);
    SetTextColor(mem_dc, col_text_muted);
    RECT rc_sub = { obj_x + 10, obj_y + 80, obj_x + obj_w - 10, obj_y + 115 };
    DrawTextW(mem_dc, L"CÔNG TY TNHH QUẢNG CÁO & THI CÔNG NỘI THẤT", -1, &rc_sub, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Selection box with handles and dimension callout
    HPEN pen_sel = CreatePen(PS_DOT, 1, col_blue);
    SelectObject(mem_dc, pen_sel);
    SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
    Rectangle(mem_dc, obj_x - 4, obj_y - 4, obj_x + obj_w + 4, obj_y + obj_h + 4);
    DeleteObject(pen_sel);

    // 8 resize handles
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

    // Dimension callout chip
    SelectObject(mem_dc, font_mono);
    HBRUSH br_chip = CreateSolidBrush(RGB(18, 28, 42));
    RECT rc_chip = { obj_x + obj_w / 2 - 70, obj_y + obj_h + 8, obj_x + obj_w / 2 + 70, obj_y + obj_h + 28 };
    FillRect(mem_dc, &rc_chip, br_chip);
    DeleteObject(br_chip);
    SetTextColor(mem_dc, col_teal);
    DrawTextW(mem_dc, L"1,850.0 x 420.0 mm", -1, &rc_chip, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 3. Topbar
    HBRUSH br_topbar = CreateSolidBrush(col_header);
    RECT rc_topbar = { 0, 0, w, topbar_h };
    FillRect(mem_dc, &rc_topbar, br_topbar);
    DeleteObject(br_topbar);

    // Logo PK gradient badge
    HBRUSH br_logo = CreateSolidBrush(col_teal);
    RECT rc_logo = { 10, 6, 36, 32 };
    FillRect(mem_dc, &rc_logo, br_logo);
    DeleteObject(br_logo);
    SelectObject(mem_dc, font_be_vietnam_bold);
    SetTextColor(mem_dc, RGB(7, 11, 16));
    DrawTextW(mem_dc, L"PK", -1, &rc_logo, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // App title & Document name with pulse dot
    SetTextColor(mem_dc, col_text_white);
    RECT rc_app_title = { 46, 6, 250, 32 };
    DrawTextW(mem_dc, L"PikDraw PRO 2026 / Bien_Hieu.pik ●", -1, &rc_app_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Quick Menus
    SelectObject(mem_dc, font_be_vietnam);
    SetTextColor(mem_dc, col_text_muted);
    RECT rc_menus = { 260, 6, 510, 32 };
    const wchar_t* m_txt = is_vi ? L"Tệp    Sửa    Đối tượng    Thi công    Plugin" : L"File    Edit    Object    Production    Plugin";
    DrawTextW(mem_dc, m_txt, -1, &rc_menus, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Workspace Pills (520 to 790)
    int pill_x = 520;
    const wchar_t* ws_names_vi[] = { L"Vector", L"In ấn", L"Thi công", L"3D" };
    const wchar_t* ws_names_en[] = { L"Vector", L"Print", L"Production", L"3D" };
    int ws_widths[] = { 65, 65, 75, 50 };
    int cur_px = pill_x;
    for (int i = 0; i < 4; ++i) {
        RECT rc_p = { cur_px, 6, cur_px + ws_widths[i], 32 };
        if (active_workspace_ == i) {
            HBRUSH br_w_act = CreateSolidBrush(col_active_tab);
            FillRect(mem_dc, &rc_p, br_w_act);
            DeleteObject(br_w_act);
            SetTextColor(mem_dc, col_teal);
            SelectObject(mem_dc, font_be_vietnam_bold);
        } else {
            SetTextColor(mem_dc, col_text_muted);
            SelectObject(mem_dc, font_be_vietnam);
        }
        DrawTextW(mem_dc, is_vi ? ws_names_vi[i] : ws_names_en[i], -1, &rc_p, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        cur_px += ws_widths[i] + 5;
    }

    // Language Toggle [ VI | EN ]
    int lang_x = w - 175;
    HBRUSH br_lang = CreateSolidBrush(RGB(18, 28, 42));
    RECT rc_lang = { lang_x, 6, lang_x + 65, 32 };
    FillRect(mem_dc, &rc_lang, br_lang);
    DeleteObject(br_lang);
    RECT rc_vi = { lang_x, 6, lang_x + 32, 32 };
    RECT rc_en = { lang_x + 33, 6, lang_x + 65, 32 };
    SelectObject(mem_dc, font_be_vietnam_bold);
    SetTextColor(mem_dc, is_vi ? col_teal : col_text_muted);
    DrawTextW(mem_dc, L"VI", -1, &rc_vi, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(mem_dc, !is_vi ? col_teal : col_text_muted);
    DrawTextW(mem_dc, L"EN", -1, &rc_en, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Export Button
    HBRUSH br_export = CreateSolidBrush(col_teal);
    RECT rc_export = { w - 95, 6, w - 12, 32 };
    FillRect(mem_dc, &rc_export, br_export);
    DeleteObject(br_export);
    SetTextColor(mem_dc, RGB(7, 11, 16));
    SelectObject(mem_dc, font_be_vietnam_bold);
    DrawTextW(mem_dc, is_vi ? L"Xuất file" : L"Export", -1, &rc_export, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 4. Context Tool Ribbon
    HBRUSH br_ribbon = CreateSolidBrush(col_ribbon);
    RECT rc_ribbon = { 0, topbar_h, w, topbar_h + ribbon_h };
    FillRect(mem_dc, &rc_ribbon, br_ribbon);
    DeleteObject(br_ribbon);

    // Indicator chip
    RECT rc_rib_ind = { 16, topbar_h, 240, topbar_h + ribbon_h };
    SetTextColor(mem_dc, col_teal);
    SelectObject(mem_dc, font_be_vietnam_bold);
    DrawTextW(mem_dc, is_vi ? L"■ Nhóm chữ Neon Signboard (#402)" : L"■ Neon Signboard Group (#402)", -1, &rc_rib_ind, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Geometry chips
    SelectObject(mem_dc, font_mono);
    SetTextColor(mem_dc, col_text_white);
    std::wostringstream wgeom;
    wgeom << L"X: " << std::fixed << std::setprecision(1) << shape_x_ << L" mm   "
          << L"Y: " << std::fixed << std::setprecision(1) << shape_y_ << L" mm   "
          << L"W: " << std::fixed << std::setprecision(1) << shape_w_ << L" mm   "
          << L"H: " << std::fixed << std::setprecision(1) << shape_h_ << L" mm   "
          << L"Góc: 0°   |   Tô: #00f2c3   Viền: #0ea5e9 (2 mm)";
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
            FillRect(mem_dc, &rc_tb, br_t_act);
            DeleteObject(br_t_act);
            HPEN pen_t_act = CreatePen(PS_SOLID, 1, col_teal);
            HGDIOBJ op = SelectObject(mem_dc, pen_t_act);
            SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            RoundRect(mem_dc, rc_tb.left, rc_tb.top, rc_tb.right, rc_tb.bottom, 6, 6);
            SelectObject(mem_dc, op);
            DeleteObject(pen_t_act);
            SetTextColor(mem_dc, col_teal);
            SelectObject(mem_dc, font_be_vietnam_bold);
        } else {
            SetTextColor(mem_dc, col_text_muted);
            SelectObject(mem_dc, font_be_vietnam);
        }
        DrawTextW(mem_dc, tool_labels[i], -1, &rc_tb, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // Dual Color Box at bottom of Tool Strip
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

    // Dock Tabs: Thuộc tính | Lớp (Layers) | Lịch sử
    int tab_w = inspect_w / 3;
    const wchar_t* tab_names_vi[] = { L"Thuộc tính", L"Lớp (Layers)", L"Lịch sử" };
    const wchar_t* tab_names_en[] = { L"Inspector", L"Layers", L"History" };
    for (int i = 0; i < 3; ++i) {
        RECT rc_tab = { insp_x + i * tab_w, topbar_h + ribbon_h, insp_x + (i + 1) * tab_w, topbar_h + ribbon_h + 28 };
        if (active_dock_tab_ == i) {
            HBRUSH br_tab = CreateSolidBrush(col_active_tab);
            FillRect(mem_dc, &rc_tab, br_tab);
            DeleteObject(br_tab);
            SetTextColor(mem_dc, col_teal);
            SelectObject(mem_dc, font_be_vietnam_bold);
        } else {
            SetTextColor(mem_dc, col_text_muted);
            SelectObject(mem_dc, font_secondary);
        }
        DrawTextW(mem_dc, is_vi ? tab_names_vi[i] : tab_names_en[i], -1, &rc_tab, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // Content of Active Tab
    int content_y = topbar_h + ribbon_h + 36;
    if (active_dock_tab_ == 0) { // Inspector
        SelectObject(mem_dc, font_be_vietnam_bold);
        SetTextColor(mem_dc, col_teal);
        RECT rc_sec1 = { insp_x + 14, content_y, w - 14, content_y + 20 };
        DrawTextW(mem_dc, is_vi ? L"THUỘC TÍNH HÌNH HỌC (TRANSFORM)" : L"TRANSFORM MATRIX", -1, &rc_sec1, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_secondary);
        SetTextColor(mem_dc, col_text_white);
        std::wostringstream winsp;
        winsp << (is_vi ? L"Đối tượng: Bảng hiệu Neon (#402)\n" : L"Object: Neon Signboard (#402)\n")
              << L"Vị trí X: " << std::fixed << std::setprecision(1) << shape_x_ << L" mm\n"
              << L"Vị trí Y: " << std::fixed << std::setprecision(1) << shape_y_ << L" mm\n"
              << L"Chiều rộng: " << std::fixed << std::setprecision(1) << shape_w_ << L" mm\n"
              << L"Chiều cao: " << std::fixed << std::setprecision(1) << shape_h_ << L" mm\n"
              << L"Góc quay: 0.00°";
        std::wstring insp_text = winsp.str();
        RECT rc_sec1_body = { insp_x + 14, content_y + 25, w - 14, content_y + 130 };
        DrawTextW(mem_dc, insp_text.c_str(), -1, &rc_sec1_body, DT_LEFT | DT_NOPREFIX);

        SelectObject(mem_dc, font_be_vietnam_bold);
        SetTextColor(mem_dc, col_teal);
        RECT rc_sec2 = { insp_x + 14, content_y + 140, w - 14, content_y + 160 };
        DrawTextW(mem_dc, is_vi ? L"MÀU SẮC & ĐƯỜNG VIỀN" : L"APPEARANCE & STROKE", -1, &rc_sec2, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_secondary);
        SetTextColor(mem_dc, col_text_white);
        RECT rc_sec2_body = { insp_x + 14, content_y + 165, w - 14, content_y + 240 };
        const wchar_t* app_text = is_vi ?
            L"Màu tô: #00f2c3 (Cyber Teal)\nĐộ mờ đục: 100%\nViền viền: #0ea5e9 (Sky Blue)\nĐộ dày viền: 2.0 mm" :
            L"Fill: #00f2c3 (Cyber Teal)\nOpacity: 100%\nStroke: #0ea5e9 (Sky Blue)\nStroke Width: 2.0 mm";
        DrawTextW(mem_dc, app_text, -1, &rc_sec2_body, DT_LEFT | DT_NOPREFIX);
    } else if (active_dock_tab_ == 1) { // Layers
        SelectObject(mem_dc, font_be_vietnam_bold);
        SetTextColor(mem_dc, col_teal);
        RECT rc_l_head = { insp_x + 14, content_y, w - 14, content_y + 20 };
        DrawTextW(mem_dc, is_vi ? L"CẤU TRÚC PHÂN CẤP LỚP" : L"LAYER HIERARCHY", -1, &rc_l_head, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_secondary);
        SetTextColor(mem_dc, col_text_white);
        RECT rc_l_body = { insp_x + 14, content_y + 25, w - 14, content_y + 200 };
        const wchar_t* layer_tree = is_vi ?
            L"▼ Bảng hiệu chính (#402)\n   [T] AN PHÁT SIGNAGE 2026\n   [T] CÔNG TY TNHH QUẢNG CÁO...\n   [R] Khung Mica Neon #00f2c3\n\n▼ Lớp nền (Background)\n   [R] Tấm ốp Alumini ngoài trời" :
            L"▼ Main Signboard (#402)\n   [T] AN PHAT SIGNAGE 2026\n   [T] CTY TNHH QUANG CAO...\n   [R] Neon Mica Frame #00f2c3\n\n▼ Background Layer\n   [R] Outdoor Alumini Base";
        DrawTextW(mem_dc, layer_tree, -1, &rc_l_body, DT_LEFT | DT_NOPREFIX);
    } else { // History
        SelectObject(mem_dc, font_be_vietnam_bold);
        SetTextColor(mem_dc, col_teal);
        RECT rc_h_head = { insp_x + 14, content_y, w - 14, content_y + 20 };
        DrawTextW(mem_dc, is_vi ? L"LỊCH SỬ THAO TÁC (UNDO)" : L"COMMAND HISTORY", -1, &rc_h_head, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_secondary);
        SetTextColor(mem_dc, col_text_white);
        RECT rc_h_body = { insp_x + 14, content_y + 25, w - 14, content_y + 200 };
        const wchar_t* hist_text = is_vi ?
            L"● Rev 14: Di chuyển đối tượng chữ\n● Rev 13: Đổi màu tô sang #00f2c3\n● Rev 12: Thêm tiêu đề Be Vietnam Pro\n● Rev 11: Tạo khung mica bo góc\n● Rev 10: Khởi tạo tài liệu khổ A4" :
            L"● Rev 14: Move text object\n● Rev 13: Change fill to #00f2c3\n● Rev 12: Add Be Vietnam Pro title\n● Rev 11: Create rounded frame\n● Rev 10: Initialize A4 document";
        DrawTextW(mem_dc, hist_text, -1, &rc_h_body, DT_LEFT | DT_NOPREFIX);
    }

    // 7. Bottom Status Bar
    HBRUSH br_status = CreateSolidBrush(col_statusbar);
    RECT rc_status = { 0, h - status_h, w, h };
    FillRect(mem_dc, &rc_status, br_status);
    DeleteObject(br_status);

    // Live coordinates
    SelectObject(mem_dc, font_mono);
    SetTextColor(mem_dc, col_text_muted);
    std::wostringstream wcoord;
    wcoord << L"X: " << std::fixed << std::setprecision(1) << cursor_mm_x_ << L" mm   "
           << L"Y: " << std::fixed << std::setprecision(1) << cursor_mm_y_ << L" mm";
    std::wstring coord_str = wcoord.str();
    RECT rc_st_coord = { 16, h - status_h, 240, h };
    DrawTextW(mem_dc, coord_str.c_str(), -1, &rc_st_coord, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Live interactive status message (Proper UTF-8 to Wide decoding)
    SelectObject(mem_dc, font_be_vietnam);
    SetTextColor(mem_dc, col_teal);
    std::wstring wmsg = utf8_to_wide(status_msg_);
    RECT rc_st_msg = { 250, h - status_h, w - 200, h };
    DrawTextW(mem_dc, wmsg.c_str(), -1, &rc_st_msg, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Zoom buttons [-] 100% [+]
    SelectObject(mem_dc, font_mono);
    SetTextColor(mem_dc, col_text_white);
    std::wstring wzoom = std::to_wstring(zoom_level_) + L"%";
    RECT rc_zm_btn_minus = { w - 120, h - status_h + 3, w - 95, h - 3 };
    RECT rc_zm_txt = { w - 90, h - status_h, w - 50, h };
    RECT rc_zm_btn_plus = { w - 45, h - status_h + 3, w - 20, h - 3 };

    HBRUSH br_zm_btn = CreateSolidBrush(RGB(18, 28, 42));
    FillRect(mem_dc, &rc_zm_btn_minus, br_zm_btn);
    FillRect(mem_dc, &rc_zm_btn_plus, br_zm_btn);
    DeleteObject(br_zm_btn);

    DrawTextW(mem_dc, L"-", -1, &rc_zm_btn_minus, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(mem_dc, wzoom.c_str(), -1, &rc_zm_txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(mem_dc, L"+", -1, &rc_zm_btn_plus, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Hairline Borders
    HPEN pen_bdr = CreatePen(PS_SOLID, 1, col_border);
    HGDIOBJ old_bdr = SelectObject(mem_dc, pen_bdr);

    MoveToEx(mem_dc, 0, topbar_h, NULL);
    LineTo(mem_dc, w, topbar_h);

    MoveToEx(mem_dc, 0, topbar_h + ribbon_h, NULL);
    LineTo(mem_dc, w, topbar_h + ribbon_h);

    MoveToEx(mem_dc, tool_w, topbar_h + ribbon_h, NULL);
    LineTo(mem_dc, tool_w, h - status_h);

    MoveToEx(mem_dc, insp_x, topbar_h + ribbon_h, NULL);
    LineTo(mem_dc, insp_x, h - status_h);

    MoveToEx(mem_dc, 0, h - status_h, NULL);
    LineTo(mem_dc, w, h - status_h);

    SelectObject(mem_dc, old_bdr);
    DeleteObject(pen_bdr);

    BitBlt(hdc, 0, 0, w, h, mem_dc, 0, 0, SRCCOPY);

    SelectObject(mem_dc, old_font);
    DeleteObject(font_be_vietnam);
    DeleteObject(font_be_vietnam_bold);
    DeleteObject(font_title_bold);
    DeleteObject(font_secondary);
    DeleteObject(font_secondary_bold);
    DeleteObject(font_mono);

    SelectObject(mem_dc, old_bmp);
    DeleteObject(mem_bmp);
    DeleteDC(mem_dc);
}

void NativeWindow::capture_screenshot_bmp(const std::string& path) {
    int w = width_;
    int h = height_;

    HDC screen_dc = GetDC(NULL);
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    HBITMAP mem_bmp = CreateCompatibleBitmap(screen_dc, w, h);
    HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);

    paint(mem_dc, w, h);

    BITMAPINFOHEADER bi = { sizeof(BITMAPINFOHEADER) };
    bi.biWidth = w;
    bi.biHeight = h;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;
    int row_stride = ((w * 3 + 3) / 4) * 4;
    int data_size = row_stride * h;
    bi.biSizeImage = data_size;

    std::vector<uint8_t> pixel_data(data_size);
    GetDIBits(mem_dc, mem_bmp, 0, h, pixel_data.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    BITMAPFILEHEADER bfh = { 0 };
    bfh.bfType = 0x4D42;
    bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + data_size;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    std::ofstream out(path, std::ios::binary);
    if (out.is_open()) {
        out.write(reinterpret_cast<const char*>(&bfh), sizeof(bfh));
        out.write(reinterpret_cast<const char*>(&bi), sizeof(bi));
        out.write(reinterpret_cast<const char*>(pixel_data.data()), data_size);
    }

    SelectObject(mem_dc, old_bmp);
    DeleteObject(mem_bmp);
    DeleteDC(mem_dc);
    ReleaseDC(NULL, screen_dc);
}

#endif

} // namespace pikdraw::desktop
