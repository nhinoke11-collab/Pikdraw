#include "pikdraw/desktop/shell/native_window.hpp"
#include <sstream>
#include <iomanip>

namespace pikdraw::desktop {

#ifdef _WIN32

void NativeWindow::paint_settings_modal(HDC mem_dc, int w, int h) {
    bool is_vi = (language_ == UILanguage::Vietnamese);
    bool is_dark = (theme_ == ThemeMode::CyberDark);

    // 1. Modal backdrop dimming (semi-transparent feel)
    HDC dim_dc = CreateCompatibleDC(mem_dc);
    HBITMAP dim_bmp = CreateCompatibleBitmap(mem_dc, w, h);
    HGDIOBJ old_dim_bmp = SelectObject(dim_dc, dim_bmp);
    HBRUSH br_dim = CreateSolidBrush(RGB(2, 4, 8));
    RECT rc_dim = { 0, 0, w, h };
    FillRect(dim_dc, &rc_dim, br_dim);
    DeleteObject(br_dim);

    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 185, 0 };
    AlphaBlend(mem_dc, 0, 0, w, h, dim_dc, 0, 0, w, h, bf);
    SelectObject(dim_dc, old_dim_bmp);
    DeleteObject(dim_bmp);
    DeleteDC(dim_dc);

    // 2. Modal Card Geometry
    int modal_w = 720;
    int modal_h = 500;
    int modal_x = (w - modal_w) / 2;
    int modal_y = (h - modal_h) / 2;

    COLORREF m_card_bg = is_dark ? RGB(15, 23, 34) : RGB(255, 255, 255);
    COLORREF m_header_bg = is_dark ? RGB(19, 29, 43) : RGB(248, 250, 252);
    COLORREF m_sidebar_bg = is_dark ? RGB(12, 20, 31) : RGB(241, 245, 249);
    COLORREF m_border = is_dark ? RGB(36, 48, 68) : RGB(203, 213, 225);
    COLORREF m_teal = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    COLORREF m_text_pri = is_dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    COLORREF m_text_mut = is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    COLORREF m_tab_act = is_dark ? RGB(22, 37, 54) : RGB(255, 255, 255);

    // Fonts
    HFONT font_vn_bold = CreateFontW(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Be Vietnam Pro");
    HFONT font_vn_norm = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Be Vietnam Pro");
    HFONT font_sec = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Google Sans");
    HFONT font_sec_bold = CreateFontW(12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Google Sans");
    HFONT font_mono = CreateFontW(11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"JetBrains Mono");

    // Drop shadow
    HBRUSH br_shad = CreateSolidBrush(RGB(1, 3, 6));
    RECT rc_shad = { modal_x + 6, modal_y + 6, modal_x + modal_w + 6, modal_y + modal_h + 6 };
    FillRect(mem_dc, &rc_shad, br_shad);
    DeleteObject(br_shad);

    // Main Card background & border
    HBRUSH br_card = CreateSolidBrush(m_card_bg);
    HPEN pen_bdr = CreatePen(PS_SOLID, 1, m_border);
    SelectObject(mem_dc, pen_bdr);
    SelectObject(mem_dc, br_card);
    RoundRect(mem_dc, modal_x, modal_y, modal_x + modal_w, modal_y + modal_h, 14, 14);
    DeleteObject(pen_bdr);
    DeleteObject(br_card);

    // 3. Modal Header (h = 46px)
    HBRUSH br_head = CreateSolidBrush(m_header_bg);
    RECT rc_head = { modal_x, modal_y, modal_x + modal_w, modal_y + 46 };
    FillRect(mem_dc, &rc_head, br_head);
    DeleteObject(br_head);

    // Icon pill
    HBRUSH br_icon = CreateSolidBrush(is_dark ? RGB(14, 38, 48) : RGB(220, 252, 231));
    RECT rc_icon = { modal_x + 16, modal_y + 11, modal_x + 40, modal_y + 35 };
    FillRect(mem_dc, &rc_icon, br_icon);
    DeleteObject(br_icon);
    SelectObject(mem_dc, font_vn_bold);
    SetTextColor(mem_dc, m_teal);
    DrawTextW(mem_dc, L"⚙", -1, &rc_icon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Title
    SetTextColor(mem_dc, m_text_pri);
    RECT rc_htitle = { modal_x + 48, modal_y + 11, modal_x + 480, modal_y + 35 };
    const wchar_t* title_str = is_vi ? L"Cài đặt hệ thống (PikDraw Preferences 2026)" : L"System Preferences (PikDraw Settings 2026)";
    DrawTextW(mem_dc, title_str, -1, &rc_htitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Close button [X]
    RECT rc_close = { modal_x + modal_w - 38, modal_y + 9, modal_x + modal_w - 12, modal_y + 35 };
    SetTextColor(mem_dc, m_text_mut);
    DrawTextW(mem_dc, L"✕", -1, &rc_close, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Divider line below header
    HPEN pen_div = CreatePen(PS_SOLID, 1, m_border);
    HGDIOBJ old_pen = SelectObject(mem_dc, pen_div);
    MoveToEx(mem_dc, modal_x, modal_y + 46, NULL);
    LineTo(mem_dc, modal_x + modal_w, modal_y + 46);

    // 4. Left Sidebar Navigation (w = 190px)
    HBRUSH br_side = CreateSolidBrush(m_sidebar_bg);
    RECT rc_side = { modal_x, modal_y + 47, modal_x + 190, modal_y + modal_h - 46 };
    FillRect(mem_dc, &rc_side, br_side);
    DeleteObject(br_side);

    MoveToEx(mem_dc, modal_x + 190, modal_y + 47, NULL);
    LineTo(mem_dc, modal_x + 190, modal_y + modal_h - 46);

    const wchar_t* vi_tab_names[] = {
        L"  Giao diện (Theme)", L"  Đơn vị & Lưới", L"  Quản lý màu & ICC",
        L"  Thi công & CNC", L"  Hiệu năng & Hệ thống"
    };
    const wchar_t* en_tab_names[] = {
        L"  Theme & Appearance", L"  Units & Workspace", L"  Color & ICC Profiles",
        L"  Production & CNC", L"  Hardware & System"
    };

    for (int i = 0; i < 5; ++i) {
        int ty = modal_y + 56 + i * 42;
        RECT rc_tab = { modal_x + 10, ty, modal_x + 180, ty + 36 };
        if (settings_active_tab_ == i) {
            HBRUSH br_tab_bg = CreateSolidBrush(m_tab_act);
            FillRect(mem_dc, &rc_tab, br_tab_bg);
            DeleteObject(br_tab_bg);
            HPEN pen_act = CreatePen(PS_SOLID, 1, m_teal);
            HGDIOBJ op_t = SelectObject(mem_dc, pen_act);
            SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            RoundRect(mem_dc, rc_tab.left, rc_tab.top, rc_tab.right, rc_tab.bottom, 6, 6);
            SelectObject(mem_dc, op_t);
            DeleteObject(pen_act);
            SetTextColor(mem_dc, m_teal);
            SelectObject(mem_dc, font_vn_bold);
        } else {
            SetTextColor(mem_dc, m_text_mut);
            SelectObject(mem_dc, font_vn_norm);
        }
        DrawTextW(mem_dc, is_vi ? vi_tab_names[i] : en_tab_names[i], -1, &rc_tab, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // 5. Right Content Area (w = 530px, from modal_x + 205 to modal_x + 705)
    int cx = modal_x + 205;
    int cy = modal_y + 60;

    if (settings_active_tab_ == 0) {
        // Tab 0: Giao diện (Theme & Appearance)
        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_s1_t = { cx, cy, cx + 490, cy + 20 };
        DrawTextW(mem_dc, is_vi ? L"Chế độ hiển thị giao diện (Theme Mode)" : L"UI Theme Mode (Light / Dark)", -1, &rc_s1_t, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_sec);
        SetTextColor(mem_dc, m_text_mut);
        RECT rc_s1_sub = { cx, cy + 22, cx + 490, cy + 38 };
        DrawTextW(mem_dc, is_vi ? L"Tối ưu độ tập trung và hiển thị màu sắc theo môi trường làm việc." : L"Optimized for visual precision and workstation lighting.", -1, &rc_s1_sub, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        // 3 Theme Visual Cards (Dark, Light, System)
        int card_w = 155;
        int card_h = 76;
        int card_y = cy + 44;

        for (int i = 0; i < 3; ++i) {
            int cur_cx = cx + i * 165;
            RECT rc_c = { cur_cx, card_y, cur_cx + card_w, card_y + card_h };
            bool is_active_card = (settings_theme_choice_ == i);

            HBRUSH br_c = CreateSolidBrush(is_active_card ? (is_dark ? RGB(18, 38, 48) : RGB(236, 253, 245)) : (is_dark ? RGB(10, 16, 24) : RGB(248, 250, 252)));
            FillRect(mem_dc, &rc_c, br_c);
            DeleteObject(br_c);

            HPEN pen_c = CreatePen(PS_SOLID, is_active_card ? 2 : 1, is_active_card ? m_teal : m_border);
            HGDIOBJ op_c = SelectObject(mem_dc, pen_c);
            SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            RoundRect(mem_dc, rc_c.left, rc_c.top, rc_c.right, rc_c.bottom, 8, 8);
            SelectObject(mem_dc, op_c);
            DeleteObject(pen_c);

            // Mini UI preview box
            HBRUSH br_mini = CreateSolidBrush(i == 0 ? RGB(7, 12, 18) : (i == 1 ? RGB(241, 245, 249) : RGB(20, 28, 40)));
            RECT rc_mini = { cur_cx + 8, card_y + 8, cur_cx + card_w - 8, card_y + 36 };
            FillRect(mem_dc, &rc_mini, br_mini);
            DeleteObject(br_mini);

            // Dot on mini preview
            SetPixel(mem_dc, cur_cx + 14, card_y + 14, i == 1 ? RGB(0, 135, 106) : RGB(0, 242, 195));

            // Card text
            SelectObject(mem_dc, font_vn_bold);
            SetTextColor(mem_dc, is_active_card ? m_teal : m_text_pri);
            RECT rc_c_title = { cur_cx + 8, card_y + 40, cur_cx + card_w - 8, card_y + 56 };
            const wchar_t* c_titles_vi[] = { L"● Chế độ Tối", L"● Chế độ Sáng", L"● Theo hệ thống" };
            const wchar_t* c_titles_en[] = { L"● Dark Mode", L"● Light Mode", L"● System (OS)" };
            DrawTextW(mem_dc, is_vi ? c_titles_vi[i] : c_titles_en[i], -1, &rc_c_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(mem_dc, font_sec);
            SetTextColor(mem_dc, m_text_mut);
            RECT rc_c_sub = { cur_cx + 8, card_y + 56, cur_cx + card_w - 8, card_y + 72 };
            const wchar_t* c_subs[] = { L"Obsidian Slate", L"Silver Slate Tech", L"Windows Auto" };
            DrawTextW(mem_dc, c_subs[i], -1, &rc_c_sub, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        // Section 2: Accent color
        int sec2_y = card_y + card_h + 16;
        MoveToEx(mem_dc, cx, sec2_y, NULL);
        LineTo(mem_dc, cx + 490, sec2_y);

        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_s2_t = { cx, sec2_y + 10, cx + 490, sec2_y + 30 };
        DrawTextW(mem_dc, is_vi ? L"Màu điểm nhấn (Accent Color)" : L"Tech Accent Color", -1, &rc_s2_t, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        COLORREF acc_colors[] = { RGB(0, 242, 195), RGB(14, 165, 233), RGB(168, 85, 247), RGB(245, 158, 11) };
        const wchar_t* acc_names[] = { L"Cyber Mint", L"Sky Blue", L"Purple Tech", L"Amber CAD" };
        for (int i = 0; i < 4; ++i) {
            int ax = cx + i * 115;
            int ay = sec2_y + 36;
            bool is_acc_act = (settings_accent_choice_ == i);

            HBRUSH br_a = CreateSolidBrush(acc_colors[i]);
            RECT rc_a_c = { ax, ay, ax + 18, ay + 18 };
            FillRect(mem_dc, &rc_a_c, br_a);
            DeleteObject(br_a);

            if (is_acc_act) {
                HPEN pen_ring = CreatePen(PS_SOLID, 2, m_teal);
                HGDIOBJ op_r = SelectObject(mem_dc, pen_ring);
                SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
                Rectangle(mem_dc, ax - 3, ay - 3, ax + 21, ay + 21);
                SelectObject(mem_dc, op_r);
                DeleteObject(pen_ring);
            }

            SelectObject(mem_dc, is_acc_act ? font_sec_bold : font_sec);
            SetTextColor(mem_dc, is_acc_act ? m_text_pri : m_text_mut);
            RECT rc_a_t = { ax + 24, ay, ax + 105, ay + 18 };
            DrawTextW(mem_dc, acc_names[i], -1, &rc_a_t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        // Section 3: Style 2026 tags
        int sec3_y = sec2_y + 68;
        MoveToEx(mem_dc, cx, sec3_y, NULL);
        LineTo(mem_dc, cx + 490, sec3_y);

        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_s3_t = { cx, sec3_y + 10, cx + 490, sec3_y + 30 };
        DrawTextW(mem_dc, is_vi ? L"Đặc tính công nghệ (Style 2026)" : L"Production Specifications", -1, &rc_s3_t, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_sec);
        SetTextColor(mem_dc, m_text_mut);
        RECT rc_s3_body = { cx, sec3_y + 34, cx + 490, sec3_y + 110 };
        const wchar_t* tags_text = is_vi ?
            L"✓ Bo nhẹ công nghệ (Micro-Radius 6px) trên toàn bộ dock và nút bấm\n"
            L"✓ Bảo toàn màu sắc Artwork khi đổi theme: Khóa chuẩn ICC FOGRA39\n"
            L"✓ Tự động lưu cấu hình màn hình làm việc (Workspace Persistence)" :
            L"✓ Micro-Radius (6px) rounded styling across all docks and toolbars\n"
            L"✓ Artwork Color Integrity on theme change: Locked to ICC FOGRA39\n"
            L"✓ Automatic workspace persistence across application sessions";
        DrawTextW(mem_dc, tags_text, -1, &rc_s3_body, DT_LEFT | DT_NOPREFIX);

    } else if (settings_active_tab_ == 1) {
        // Tab 1: Đơn vị & Lưới
        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_t1_t = { cx, cy, cx + 490, cy + 20 };
        DrawTextW(mem_dc, is_vi ? L"Đơn vị đo lường mặc định (Default Units)" : L"Measurement Units", -1, &rc_t1_t, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        const wchar_t* units[] = { L"mm (Milimét)", L"cm (Xentimét)", L"inch (Inches)", L"pt (Points)" };
        for (int i = 0; i < 4; ++i) {
            int ux = cx + i * 115;
            int uy = cy + 28;
            bool is_u_act = (settings_unit_choice_ == i);
            RECT rc_u = { ux, uy, ux + 105, uy + 28 };

            HBRUSH br_u = CreateSolidBrush(is_u_act ? (is_dark ? RGB(18, 38, 48) : RGB(236, 253, 245)) : (is_dark ? RGB(10, 16, 24) : RGB(248, 250, 252)));
            FillRect(mem_dc, &rc_u, br_u);
            DeleteObject(br_u);

            HPEN pen_u = CreatePen(PS_SOLID, 1, is_u_act ? m_teal : m_border);
            HGDIOBJ op_u = SelectObject(mem_dc, pen_u);
            SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            RoundRect(mem_dc, rc_u.left, rc_u.top, rc_u.right, rc_u.bottom, 6, 6);
            SelectObject(mem_dc, op_u);
            DeleteObject(pen_u);

            SelectObject(mem_dc, is_u_act ? font_vn_bold : font_vn_norm);
            SetTextColor(mem_dc, is_u_act ? m_teal : m_text_mut);
            DrawTextW(mem_dc, units[i], -1, &rc_u, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_t1_s2 = { cx, cy + 76, cx + 490, cy + 96 };
        DrawTextW(mem_dc, is_vi ? L"Hệ tọa độ gốc (Origin Point)" : L"Coordinate Origin", -1, &rc_t1_s2, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_sec);
        SetTextColor(mem_dc, m_text_mut);
        RECT rc_t1_s2_b = { cx, cy + 102, cx + 490, cy + 150 };
        const wchar_t* orig_text = is_vi ?
            L"(●) Góc trên-trái (Top-Left 0,0) - Chuẩn Vector Editor / CAD Thi Công\n(○) Tâm bản vẽ (Center 0,0) - Chuẩn Motion Graphics" :
            L"(●) Top-Left (0,0) - Standard Vector Editor / CAD Production\n(○) Center (0,0) - Motion Graphics & 3D Standard";
        DrawTextW(mem_dc, orig_text, -1, &rc_t1_s2_b, DT_LEFT | DT_NOPREFIX);

        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_teal);
        RECT rc_t1_s3 = { cx, cy + 160, cx + 490, cy + 180 };
        DrawTextW(mem_dc, is_vi ? L"Lưới thiết kế & Bắt điểm (Grid & Snapping)" : L"Grid & Magnetic Snapping", -1, &rc_t1_s3, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_sec);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_t1_s3_b = { cx, cy + 186, cx + 490, cy + 280 };
        const wchar_t* snap_text = is_vi ?
            L"☑ Bắt điểm Snap to Grid (Bước nhảy mặc định: 5.0 mm)\n"
            L"☑ Bắt điểm đối tượng thông minh (Smart Magnetic Guides)\n"
            L"☑ Khóa góc quay 45°/90° khi nhấn giữ phím Shift\n"
            L"☑ Hiển thị thước đo mm (Rulers) dọc theo mép bản vẽ" :
            L"☑ Snap to Grid enabled (Default grid step: 5.0 mm)\n"
            L"☑ Smart Magnetic Guides (Auto-align to object bounds)\n"
            L"☑ Constrain angle to 45°/90° when holding Shift\n"
            L"☑ Show mm rulers along top and left canvas edges";
        DrawTextW(mem_dc, snap_text, -1, &rc_t1_s3_b, DT_LEFT | DT_NOPREFIX);

    } else if (settings_active_tab_ == 2) {
        // Tab 2: Quản lý màu & ICC
        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_c_t = { cx, cy, cx + 490, cy + 20 };
        DrawTextW(mem_dc, is_vi ? L"Không gian màu làm việc (Working Color Spaces)" : L"Working Color Spaces & ICC", -1, &rc_c_t, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_sec);
        SetTextColor(mem_dc, m_text_mut);
        RECT rc_c_b = { cx, cy + 26, cx + 490, cy + 140 };
        const wchar_t* icc_text = is_vi ?
            L"• Không gian RGB: sRGB IEC61966-2.1 (Mặc định hiển thị màn hình & Web)\n"
            L"• Không gian CMYK: ISO Coated v2 (ECI) / FOGRA39 (Chuẩn in ấn Quốc Tế)\n"
            L"• Bảng màu biển hiệu: Bảng màu thực tế tấm Alcorest & Decal 3M\n"
            L"• Phương thức chuyển màu (Intent): Perceptual (Cảm nhận thị giác)" :
            L"• RGB Space: sRGB IEC61966-2.1 (Standard Monitor & Web Display)\n"
            L"• CMYK Space: ISO Coated v2 (ECI) / FOGRA39 (Standard Offset Print)\n"
            L"• Signage Palette: Real Alcorest Alu & 3M Decal Swatches\n"
            L"• Rendering Intent: Perceptual (Optimal for graphics and signage)";
        DrawTextW(mem_dc, icc_text, -1, &rc_c_b, DT_LEFT | DT_NOPREFIX);

        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_teal);
        RECT rc_c_s2 = { cx, cy + 150, cx + 490, cy + 170 };
        DrawTextW(mem_dc, is_vi ? L"Chính sách quản lý màu sắc" : L"Color Management Policies", -1, &rc_c_s2, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_sec);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_c_s2_b = { cx, cy + 176, cx + 490, cy + 260 };
        const wchar_t* pol_text = is_vi ?
            L"☑ Giữ nguyên cấu hình màu đính kèm trong tệp (Preserve Embedded Profiles)\n"
            L"☑ Cảnh báo màu vượt ngưỡng (Out-of-Gamut Warning) khi in ấn CMYK\n"
            L"☑ Tự động bù dải đen sâu (Black Point Compensation BPC)" :
            L"☑ Preserve embedded ICC profiles from source files\n"
            L"☑ Out-of-Gamut visual warning for unprintable CMYK colors\n"
            L"☑ Black Point Compensation (BPC) enabled by default";
        DrawTextW(mem_dc, pol_text, -1, &rc_c_s2_b, DT_LEFT | DT_NOPREFIX);

    } else if (settings_active_tab_ == 3) {
        // Tab 3: Thi công & CNC
        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_cnc_t = { cx, cy, cx + 490, cy + 20 };
        DrawTextW(mem_dc, is_vi ? L"Thông số thi công & Máy cắt CNC" : L"CAM & CNC Production Specs", -1, &rc_cnc_t, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_sec);
        SetTextColor(mem_dc, m_text_mut);
        RECT rc_cnc_b = { cx, cy + 26, cx + 490, cy + 150 };
        const wchar_t* cnc_text = is_vi ?
            L"• Đường kính dao cắt mặc định (Kerf): 3.175 mm (Dao xoắn 1 me Alu/Mica)\n"
            L"• Chế độ bù dao: Bên ngoài đường biên đối tượng (Outside Tool Offset)\n"
            L"• Tràn lề thi công (Bleed margin): 3.0 mm\n"
            L"• Khổ bàn cắt chuẩn: 1,220 x 2,440 mm (Khổ tấm Alcorest tiêu chuẩn)\n"
            L"• Định dạng xuất máy: Chuẩn G-Code / DXF R12 / HPGL Plotter" :
            L"• Default Tool Kerf: 3.175 mm (Single flute spiral bit for Alu/Acrylic)\n"
            L"• Tool Offset: Outside vector contour\n"
            L"• Bleed Margin: 3.0 mm for printing and routing\n"
            L"• Bed Size: 1,220 x 2,440 mm (Standard Alcorest sheet dimensions)\n"
            L"• Machine Export: G-Code / DXF R12 / HPGL Plotter";
        DrawTextW(mem_dc, cnc_text, -1, &rc_cnc_b, DT_LEFT | DT_NOPREFIX);

    } else {
        // Tab 4: Hiệu năng & Hệ thống
        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_sys_t = { cx, cy, cx + 490, cy + 20 };
        DrawTextW(mem_dc, is_vi ? L"Bộ tăng tốc phần cứng & GPU" : L"Hardware Acceleration & GPU", -1, &rc_sys_t, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_sec);
        SetTextColor(mem_dc, m_text_mut);
        RECT rc_sys_b = { cx, cy + 26, cx + 490, cy + 150 };
        const wchar_t* sys_text = is_vi ?
            L"• Renderer Backend: DirectX / Skia GPU Rasterizer (Probed: Windows D3D11)\n"
            L"• Bộ nhớ đệm đồ họa (RAM Cache): 512 MB (Tự động dọn rác bộ nhớ)\n"
            L"• Tần số làm tươi: 120 FPS / Tự động đồng bộ V-Sync màn hình\n"
            L"• Tự động sao lưu dự phòng: Mỗi 5 phút vào %LOCALAPPDATA%/PikDraw/\n"
            L"• Giới hạn lịch sử thao tác: 50 bước Undo/Redo" :
            L"• Renderer Backend: DirectX / Skia GPU Rasterizer (Probed: Windows D3D11)\n"
            L"• Raster Cache RAM Budget: 512 MB with automatic garbage collection\n"
            L"• Refresh Rate: 120 FPS / V-Sync hardware synchronized\n"
            L"• Autosave Interval: Every 5 minutes to %LOCALAPPDATA%/PikDraw/\n"
            L"• Undo/Redo Depth: 50 atomic command revisions";
        DrawTextW(mem_dc, sys_text, -1, &rc_sys_b, DT_LEFT | DT_NOPREFIX);
    }

    // 6. Modal Footer (h = 46px)
    int fy = modal_y + modal_h - 46;
    HBRUSH br_foot = CreateSolidBrush(m_header_bg);
    RECT rc_foot = { modal_x, fy, modal_x + modal_w, modal_y + modal_h };
    FillRect(mem_dc, &rc_foot, br_foot);
    DeleteObject(br_foot);

    MoveToEx(mem_dc, modal_x, fy, NULL);
    LineTo(mem_dc, modal_x + modal_w, fy);

    // Left: Reset defaults
    SelectObject(mem_dc, font_sec);
    SetTextColor(mem_dc, m_text_mut);
    RECT rc_f_rst = { modal_x + 18, fy + 12, modal_x + 160, fy + 36 };
    DrawTextW(mem_dc, is_vi ? L"Khôi phục mặc định" : L"Reset Defaults", -1, &rc_f_rst, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Right: Close button
    RECT rc_btn_cls = { modal_x + modal_w - 170, fy + 8, modal_x + modal_w - 95, fy + 38 };
    HBRUSH br_b_cls = CreateSolidBrush(is_dark ? RGB(29, 38, 54) : RGB(226, 232, 240));
    FillRect(mem_dc, &rc_btn_cls, br_b_cls);
    DeleteObject(br_b_cls);
    SelectObject(mem_dc, font_vn_bold);
    SetTextColor(mem_dc, m_text_pri);
    DrawTextW(mem_dc, is_vi ? L"Đóng" : L"Close", -1, &rc_btn_cls, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Right: Apply button
    RECT rc_btn_app = { modal_x + modal_w - 85, fy + 8, modal_x + modal_w - 15, fy + 38 };
    HBRUSH br_b_app = CreateSolidBrush(m_teal);
    FillRect(mem_dc, &rc_btn_app, br_b_app);
    DeleteObject(br_b_app);
    SetTextColor(mem_dc, is_dark ? RGB(7, 11, 16) : RGB(255, 255, 255));
    DrawTextW(mem_dc, is_vi ? L"Áp dụng" : L"Apply", -1, &rc_btn_app, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(mem_dc, old_pen);
    DeleteObject(pen_div);

    DeleteObject(font_vn_bold);
    DeleteObject(font_vn_norm);
    DeleteObject(font_sec);
    DeleteObject(font_sec_bold);
    DeleteObject(font_mono);
}

#endif

} // namespace pikdraw::desktop
