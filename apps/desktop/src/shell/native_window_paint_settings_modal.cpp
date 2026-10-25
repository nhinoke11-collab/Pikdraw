#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/shell/native_window_shortcuts_settings.hpp"
#include "../automation/native_window_automation.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include "native_window_snap_status.hpp"
#include <sstream>
#include <iomanip>

namespace pikdraw::desktop {

#ifdef _WIN32

void NativeWindow::paint_settings_modal(HDC mem_dc, int w, int h) {
    bool is_vi = (language_ == UILanguage::Vietnamese);
    bool is_dark = (theme_ == ThemeMode::CyberDark);

    // 1. Modal backdrop dimming (semi-transparent)
    HDC dim_dc = CreateCompatibleDC(mem_dc);
    HBITMAP dim_bmp = CreateCompatibleBitmap(mem_dc, dip(w), dip(h));
    HGDIOBJ old_dim_bmp = SelectObject(dim_dc, dim_bmp);
    configure_ui_dc(dim_dc);
    HBRUSH br_dim = CreateSolidBrush(RGB(2, 4, 8));
    RECT rc_dim = { 0, 0, w, h };
    FillRect(dim_dc, &rc_dim, br_dim);
    DeleteObject(br_dim);

    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 185, 0 };
    AlphaBlend(mem_dc, 0, 0, w, h, dim_dc, 0, 0, w, h, bf);
    SelectObject(dim_dc, old_dim_bmp);
    DeleteObject(dim_bmp);
    DeleteDC(dim_dc);

    // 2. Modal Card Geometry (CorelDRAW Options Style)
    int modal_w = 720, modal_h = 500;
    int modal_x = (w - modal_w) / 2, modal_y = (h - modal_h) / 2;

    COLORREF m_card_bg = is_dark ? RGB(15, 23, 34) : RGB(255, 255, 255);
    COLORREF m_header_bg = is_dark ? RGB(19, 29, 43) : RGB(248, 250, 252);
    COLORREF m_sidebar_bg = is_dark ? RGB(12, 20, 31) : RGB(241, 245, 249);
    COLORREF m_border = is_dark ? RGB(36, 48, 68) : RGB(203, 213, 225);
    COLORREF m_teal = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    COLORREF m_text_pri = is_dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    COLORREF m_text_mut = is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    COLORREF m_tab_act = is_dark ? RGB(22, 37, 54) : RGB(255, 255, 255);

    auto make_font = [](int sz, int weight, const wchar_t* name) {
        return CreateFontW(sz, 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, name);
    };
    HFONT font_vn_bold = make_font(14, FW_BOLD, L"Segoe UI"), font_vn_norm = make_font(13, FW_NORMAL, L"Segoe UI");
    HFONT font_sec = make_font(12, FW_NORMAL, L"Segoe UI"), font_sec_bold = make_font(12, FW_BOLD, L"Segoe UI");
    HFONT font_mono = make_font(11, FW_NORMAL, L"JetBrains Mono");

    HBRUSH br_shad = CreateSolidBrush(RGB(1, 3, 6));
    RECT rc_shad = { modal_x + 6, modal_y + 6, modal_x + modal_w + 6, modal_y + modal_h + 6 };
    FillRect(mem_dc, &rc_shad, br_shad); DeleteObject(br_shad);

    HBRUSH br_card = CreateSolidBrush(m_card_bg);
    HPEN pen_bdr = CreatePen(PS_SOLID, 1, m_border);
    SelectObject(mem_dc, pen_bdr); SelectObject(mem_dc, br_card);
    RoundRect(mem_dc, modal_x, modal_y, modal_x + modal_w, modal_y + modal_h, 14, 14);
    DeleteObject(pen_bdr); DeleteObject(br_card);

    // 3. Modal Header (h = 46px)
    HBRUSH br_head = CreateSolidBrush(m_header_bg);
    RECT rc_head = { modal_x, modal_y, modal_x + modal_w, modal_y + 46 };
    FillRect(mem_dc, &rc_head, br_head);
    DeleteObject(br_head);

    HBRUSH br_icon = CreateSolidBrush(is_dark ? RGB(14, 38, 48) : RGB(220, 252, 231));
    RECT rc_icon = { modal_x + 16, modal_y + 11, modal_x + 40, modal_y + 35 };
    FillRect(mem_dc, &rc_icon, br_icon);
    DeleteObject(br_icon);
    SelectObject(mem_dc, font_vn_bold);
    SetTextColor(mem_dc, m_teal);
    DrawTextW(mem_dc, L"⚙", -1, &rc_icon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SetTextColor(mem_dc, m_text_pri);
    RECT rc_htitle = { modal_x + 48, modal_y + 11, modal_x + 550, modal_y + 35 };
    const wchar_t* title_str = is_vi ? L"Cài đặt & Tùy chọn hệ thống (PikDraw Options & Preferences)" : L"Options & Preferences (CorelDRAW / CAD Standard)";
    DrawTextW(mem_dc, title_str, -1, &rc_htitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    RECT rc_close = { modal_x + modal_w - 38, modal_y + 9, modal_x + modal_w - 12, modal_y + 35 };
    SetTextColor(mem_dc, m_text_mut);
    DrawTextW(mem_dc, L"✕", -1, &rc_close, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    HPEN pen_div = CreatePen(PS_SOLID, 1, m_border);
    HGDIOBJ old_pen = SelectObject(mem_dc, pen_div);
    MoveToEx(mem_dc, modal_x, modal_y + 46, NULL);
    LineTo(mem_dc, modal_x + modal_w, modal_y + 46);

    // 4. Left Sidebar Navigation (existing categories plus AI Connections)
    HBRUSH br_side = CreateSolidBrush(m_sidebar_bg);
    RECT rc_side = { modal_x, modal_y + 47, modal_x + 190, modal_y + modal_h - 46 };
    FillRect(mem_dc, &rc_side, br_side);
    DeleteObject(br_side);

    MoveToEx(mem_dc, modal_x + 190, modal_y + 47, NULL);
    LineTo(mem_dc, modal_x + 190, modal_y + modal_h - 46);

    const wchar_t* vi_tab_names[] = {
        L"  Chung & Ngôn ngữ", L"  Giao diện (Theme)", L"  Đơn vị & Bắt điểm",
        L"  Quản lý màu & ICC", L"  Phím tắt & Thao tác", L"  Thi công & CNC",
        L"  Kết nối AI"
    };
    const wchar_t* en_tab_names[] = {
        L"  General & Language", L"  Display & Theme", L"  Units & Snapping",
        L"  Color & ICC Profiles", L"  Shortcuts & Hotkeys", L"  Production & CAM",
        L"  AI Connections"
    };

    for (int i = 0; i < 7; ++i) {
        int ty = modal_y + 54 + i * 36;
        RECT rc_tab = { modal_x + 10, ty, modal_x + 180, ty + 32 };
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

    int cx = modal_x + 205, cy = modal_y + 58;

    if (settings_active_tab_ == 0) {
        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_g1_t = { cx, cy, cx + 490, cy + 20 };
        DrawTextW(mem_dc, is_vi ? L"Ngôn ngữ hiển thị (UI Language) ────────────────────────" : L"UI Language ──────────────────────────────────────────", -1, &rc_g1_t, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        for (int i = 0; i < 2; ++i) {
            int lx = cx + i * 150;
            RECT rc_l = { lx, cy + 26, lx + 140, cy + 56 };
            bool is_cur = (i == 0 && language_ == UILanguage::Vietnamese) || (i == 1 && language_ == UILanguage::English);
            HBRUSH br_l = CreateSolidBrush(is_cur ? (is_dark ? RGB(18, 38, 48) : RGB(236, 253, 245)) : (is_dark ? RGB(10, 16, 24) : RGB(248, 250, 252)));
            FillRect(mem_dc, &rc_l, br_l);
            DeleteObject(br_l);
            HPEN pen_l = CreatePen(PS_SOLID, 1, is_cur ? m_teal : m_border);
            HGDIOBJ op_l = SelectObject(mem_dc, pen_l);
            SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            RoundRect(mem_dc, rc_l.left, rc_l.top, rc_l.right, rc_l.bottom, 6, 6);
            SelectObject(mem_dc, op_l);
            DeleteObject(pen_l);
            SelectObject(mem_dc, is_cur ? font_vn_bold : font_vn_norm);
            SetTextColor(mem_dc, is_cur ? m_teal : m_text_mut);
            const wchar_t* l_names[] = { L"● Tiếng Việt (VN)", L"● English (US)" };
            DrawTextW(mem_dc, l_names[i], -1, &rc_l, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        // Section 2: Chế độ hiển thị (Theme Mode)
        int g2_y = cy + 70;
        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_g2_t = { cx, g2_y, cx + 490, g2_y + 20 };
        DrawTextW(mem_dc, is_vi ? L"Chế độ hiển thị (Theme Mode) ─────────────────────────" : L"Theme Appearance ────────────────────────────────────", -1, &rc_g2_t, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        for (int i = 0; i < 2; ++i) {
            int tx = cx + i * 150;
            RECT rc_t = { tx, g2_y + 26, tx + 140, g2_y + 56 };
            bool is_cur = (i == 0 && theme_ == ThemeMode::CyberDark) || (i == 1 && theme_ == ThemeMode::Light);
            HBRUSH br_t = CreateSolidBrush(is_cur ? (is_dark ? RGB(18, 38, 48) : RGB(236, 253, 245)) : (is_dark ? RGB(10, 16, 24) : RGB(248, 250, 252)));
            FillRect(mem_dc, &rc_t, br_t);
            DeleteObject(br_t);
            HPEN pen_t = CreatePen(PS_SOLID, 1, is_cur ? m_teal : m_border);
            HGDIOBJ op_t = SelectObject(mem_dc, pen_t);
            SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            RoundRect(mem_dc, rc_t.left, rc_t.top, rc_t.right, rc_t.bottom, 6, 6);
            SelectObject(mem_dc, op_t);
            DeleteObject(pen_t);
            SelectObject(mem_dc, is_cur ? font_vn_bold : font_vn_norm);
            SetTextColor(mem_dc, is_cur ? m_teal : m_text_mut);
            const wchar_t* thm_opts_vi[] = { L"🌙 Chế độ Tối", L"☀ Chế độ Sáng" };
            const wchar_t* thm_opts_en[] = { L"🌙 Dark Mode", L"☀ Light Mode" };
            DrawTextW(mem_dc, is_vi ? thm_opts_vi[i] : thm_opts_en[i], -1, &rc_t, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        // Section 3: Getting Started (CorelDRAW Style)
        int g3_y = g2_y + 70;
        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_g3_t = { cx, g3_y, cx + 490, g3_y + 20 };
        DrawTextW(mem_dc, is_vi ? L"Khởi động ứng dụng (Getting started) ─────────────────" : L"Getting started ─────────────────────────────────────", -1, &rc_g3_t, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_sec);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_g3_b = { cx, g3_y + 26, cx + 490, g3_y + 80 };
        const wchar_t* start_txt = is_vi ?
            L"☑ Hiện hộp thoại tạo mới tài liệu khi khởi động (Show New Document dialog)\n"
            L"☑ Mở màn hình chào mừng (Welcome Screen & Recent Projects)\n"
            L"Khổ giấy mặc định khởi tạo: Biển hiệu chuẩn (1850 x 420 mm)" :
            L"☑ Show New Document dialog box on startup\n"
            L"☑ Open Welcome Screen & Recent Projects on launch\n"
            L"Default canvas format: Standard Signage (1850 x 420 mm)";
        DrawTextW(mem_dc, start_txt, -1, &rc_g3_b, DT_LEFT | DT_NOPREFIX);

        // Section 4: Undo Levels (CorelDRAW Style)
        int g4_y = g3_y + 85;
        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_g4_t = { cx, g4_y, cx + 490, g4_y + 20 };
        DrawTextW(mem_dc, is_vi ? L"Lịch sử hoàn tác & Sao lưu (Undo levels & Save) ──────" : L"Undo levels & Autosave ──────────────────────────────", -1, &rc_g4_t, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_sec);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_g4_b = { cx, g4_y + 26, cx + 490, g4_y + 80 };
        const wchar_t* undo_txt = is_vi ?
            L"Số bước hoàn tác tiêu chuẩn (Regular Undo levels): 50 bước\n"
            L"☑ Tự động lưu bản nháp dự phòng (Autosave): Mỗi 5 phút\n"
            L"☑ Tự động giải phóng RAM Cache khi tài liệu không hoạt động" :
            L"Regular Undo levels: 50 atomic revisions\n"
            L"☑ Autosave backup copies every 5 minutes\n"
            L"☑ Automatic memory garbage collection on idle";
        DrawTextW(mem_dc, undo_txt, -1, &rc_g4_b, DT_LEFT | DT_NOPREFIX);

    } else if (settings_active_tab_ == 1) {
        // Tab 1: Giao diện & Hiển thị (Display & Theme)
        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_s1_t = { cx, cy, cx + 490, cy + 20 };
        DrawTextW(mem_dc, is_vi ? L"Chế độ hiển thị giao diện (Theme Mode)" : L"UI Theme Mode (Light / Dark)", -1, &rc_s1_t, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        int card_w = 155, card_h = 76, card_y = cy + 30;
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

            HBRUSH br_mini = CreateSolidBrush(i == 0 ? RGB(7, 12, 18) : (i == 1 ? RGB(241, 245, 249) : RGB(20, 28, 40)));
            RECT rc_mini = { cur_cx + 8, card_y + 8, cur_cx + card_w - 8, card_y + 36 };
            FillRect(mem_dc, &rc_mini, br_mini);
            DeleteObject(br_mini);
            SetPixel(mem_dc, cur_cx + 14, card_y + 14, i == 1 ? RGB(0, 135, 106) : RGB(0, 242, 195));

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

        int sec2_y = card_y + card_h + 16;
        MoveToEx(mem_dc, cx, sec2_y, NULL); LineTo(mem_dc, cx + 490, sec2_y);

        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_s2_t = { cx, sec2_y + 10, cx + 490, sec2_y + 30 };
        DrawTextW(mem_dc, is_vi ? L"Màu điểm nhấn (Accent Color)" : L"Tech Accent Color", -1, &rc_s2_t, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        COLORREF acc_colors[] = { RGB(0, 242, 195), RGB(14, 165, 233), RGB(168, 85, 247), RGB(245, 158, 11) };
        const wchar_t* acc_names[] = { L"Cyber Mint", L"Sky Blue", L"Purple Tech", L"Amber CAD" };
        for (int i = 0; i < 4; ++i) {
            int ax = cx + i * 115, ay = sec2_y + 36;
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

        int sec3_y = sec2_y + 68;
        MoveToEx(mem_dc, cx, sec3_y, NULL); LineTo(mem_dc, cx + 490, sec3_y);
        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_s3_t = { cx, sec3_y + 10, cx + 490, sec3_y + 30 };
        DrawTextW(mem_dc, is_vi ? L"Đặc tính công nghệ (Style 2026)" : L"Production Specifications", -1, &rc_s3_t, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_sec);
        SetTextColor(mem_dc, m_text_mut);
        RECT rc_s3_b = { cx, sec3_y + 34, cx + 490, sec3_y + 110 };
        const wchar_t* tags_text = is_vi ?
            L"✓ Bo nhẹ công nghệ (Micro-Radius 6px) trên toàn bộ dock và nút bấm\n"
            L"✓ Đổi theme giữ nguyên các kênh màu nguồn\n"
            L"✓ Profile preview ICC được lưu riêng trên máy" :
            L"✓ Micro-Radius (6px) rounded styling across all docks and toolbars\n"
            L"✓ Theme changes preserve source color channels\n"
            L"✓ ICC preview profiles are saved on this machine";
        DrawTextW(mem_dc, tags_text, -1, &rc_s3_b, DT_LEFT | DT_NOPREFIX);

        const int workspace_y = cy + 306;
        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT workspace_title{cx, workspace_y, cx + 490, workspace_y + 20};
        DrawTextW(mem_dc, is_vi ? L"Cách hiển thị trang và Artboard" : L"Page and Artboard workspace view",
                  -1, &workspace_title, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        const std::array<std::pair<const wchar_t*, bool>, 2> workspace_modes{{
            {is_vi ? L"Trang · tập trung" : L"Page · focused", !artboard_overview_enabled()},
            {is_vi ? L"Artboard · tổng quan" : L"Artboard · overview", artboard_overview_enabled()}
        }};
        for (size_t index = 0; index < workspace_modes.size(); ++index) {
            const int left = cx + static_cast<int>(index) * 250;
            RECT card{left, workspace_y + 24, left + 238, workspace_y + 60};
            const bool selected = workspace_modes[index].second;
            HBRUSH card_brush = CreateSolidBrush(selected
                ? (is_dark ? RGB(18, 38, 48) : RGB(236, 253, 245))
                : (is_dark ? RGB(10, 16, 24) : RGB(248, 250, 252)));
            FillRect(mem_dc, &card, card_brush);
            DeleteObject(card_brush);
            HPEN card_pen = CreatePen(PS_SOLID, selected ? 2 : 1, selected ? m_teal : m_border);
            const auto prior_pen = SelectObject(mem_dc, card_pen);
            SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            RoundRect(mem_dc, card.left, card.top, card.right, card.bottom, 6, 6);
            SelectObject(mem_dc, prior_pen);
            DeleteObject(card_pen);
            SelectObject(mem_dc, selected ? font_sec_bold : font_sec);
            SetTextColor(mem_dc, selected ? m_teal : m_text_pri);
            DrawTextW(mem_dc, workspace_modes[index].first, -1, &card,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        SelectObject(mem_dc, font_sec);
        SetTextColor(mem_dc, m_text_mut);
        RECT workspace_hint{cx, workspace_y + 62, cx + 490, workspace_y + 84};
        DrawTextW(mem_dc, is_vi
            ? L"Artboard trong cùng trang dùng chung artwork; đổi chế độ xem không tạo Undo. Mặc định áp dụng cho tài liệu mới."
            : L"Artboards on one page share artwork; changing view adds no Undo. The choice becomes the default for new documents.",
            -1, &workspace_hint, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    } else if (settings_active_tab_ == 2) {
        // Tab 2: Đơn vị & Bắt điểm (Units & Snapping)
        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_t1_t = { cx, cy, cx + 490, cy + 20 };
        DrawTextW(mem_dc, is_vi ? L"Đơn vị trang và mặc định cho bản vẽ mới" : L"Page unit and default for new documents", -1, &rc_t1_t, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        const wchar_t* units[] = { L"mm", L"cm", L"in", L"pt", L"m", L"ft", L"yd", L"pc" };
        for (int i = 0; i < 8; ++i) {
            int ux = cx + (i % 4) * 120, uy = cy + 28 + (i / 4) * 34;
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

        SelectObject(mem_dc, font_sec);
        SetTextColor(mem_dc, m_text_mut);
        RECT rc_unit_hint = { cx, cy + 91, cx + 490, cy + 106 };
        DrawTextW(mem_dc, is_vi
            ? L"Đổi đơn vị trang hiện tại và mặc định cho bản vẽ mới; hậu tố mm, cm, m, in, ft, yd, pt, pc luôn được ưu tiên."
            : L"Changes the current page unit and new-document default; explicit mm, cm, m, in, ft, yd, pt or pc suffixes override it.",
            -1, &rc_unit_hint, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_t1_s2 = { cx, cy + 108, cx + 490, cy + 128 };
        DrawTextW(mem_dc, is_vi ? L"Hệ tọa độ gốc (Origin Point)" : L"Coordinate Origin", -1, &rc_t1_s2, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_sec);
        SetTextColor(mem_dc, m_text_mut);
        RECT rc_t1_s2_b = { cx, cy + 134, cx + 490, cy + 182 };
        const wchar_t* orig_text = is_vi ?
            L"(●) Góc trên-trái (Top-Left 0,0) - Chuẩn Vector Editor / CAD Thi Công\n(○) Tâm bản vẽ (Center 0,0) - Chuẩn Motion Graphics" :
            L"(●) Top-Left (0,0) - Standard Vector Editor / CAD Production\n(○) Center (0,0) - Motion Graphics & 3D Standard";
        DrawTextW(mem_dc, orig_text, -1, &rc_t1_s2_b, DT_LEFT | DT_NOPREFIX);

        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_teal);
        RECT rc_t1_s3 = { cx, cy + 192, cx + 490, cy + 212 };
        DrawTextW(mem_dc, is_vi ? L"Lưới thiết kế & Bắt điểm (Snapping & Grid)" : L"Snapping & Guides", -1, &rc_t1_s3, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_sec);
        SetTextColor(mem_dc, m_text_pri);
        const auto snap = drawing_tool_ ? drawing_tool_->grid_snap_status()
                                        : tools::DrawingToolGridSnapStatus{};
        const auto grid_status = format_snap_grid_status(
            snap.enabled, snap.spacing_mm, display_length_unit_, is_vi);
        RECT rc_grid_status = { cx, cy + 218, cx + 490, cy + 237 };
        DrawTextW(mem_dc, grid_status.c_str(), -1, &rc_grid_status,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        RECT rc_grid_button = { cx, cy + 239, cx + 490, cy + 266 };
        HBRUSH grid_button_brush = CreateSolidBrush(is_dark
            ? RGB(10, 16, 24) : RGB(248, 250, 252));
        FillRect(mem_dc, &rc_grid_button, grid_button_brush);
        DeleteObject(grid_button_brush);
        HPEN grid_button_pen = CreatePen(PS_SOLID, 1, m_border);
        HGDIOBJ old_grid_button_pen = SelectObject(mem_dc, grid_button_pen);
        SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
        RoundRect(mem_dc, rc_grid_button.left, rc_grid_button.top,
                  rc_grid_button.right, rc_grid_button.bottom, 6, 6);
        SelectObject(mem_dc, old_grid_button_pen);
        DeleteObject(grid_button_pen);
        const std::string spacing_value = active_num_field_ == NumericFieldId::GridSpacing
            ? num_input_buffer_
            : format_mm_display_utf8(snap.spacing_mm, display_length_unit_, 6);
        const auto spacing_wide = NativeWindow::utf8_to_wide(spacing_value);
        const std::wstring grid_button_label = (is_vi
            ? L"Đổi khoảng lưới · " : L"Edit grid interval · ") + spacing_wide;
        SelectObject(mem_dc, font_sec_bold);
        SetTextColor(mem_dc, active_num_field_ == NumericFieldId::GridSpacing
            ? m_teal : m_text_pri);
        DrawTextW(mem_dc, grid_button_label.c_str(), -1, &rc_grid_button,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        const auto snap_config = drawing_tool_ ? drawing_tool_->snap_config_snapshot()
            : vector::precision::snap::SnapConfigSnapshot{};
        const std::array<std::pair<const wchar_t*, bool>, 6> snap_rows{{
            {is_vi ? L"Bật bắt điểm (Snap On/Off)" : L"Snap On/Off",
             snap_config.master_enabled},
            {is_vi ? L"Bắt điểm/biên đối tượng" : L"Snap to object points and bounds",
             snap_config.groups[static_cast<size_t>(vector::precision::snap::SnapGroup::ObjectPoints)]},
            {is_vi ? L"Smart Guides · căn mép và tâm khi kéo" : L"Smart Guides · align edges and centers while moving",
             snap_config.smart_guides_enabled},
            {is_vi ? L"Khoảng cách bằng nhau khi kéo" : L"Equal spacing while moving",
             snap_config.smart_spacing_enabled},
            {is_vi ? L"Bắt vào mép/tâm trang" : L"Snap to page/artboard edges and center",
             snap_config.options[static_cast<size_t>(vector::precision::snap::SnapOption::PageEdge)]},
            {is_vi ? L"Bắt vào đường gióng đang hiện" : L"Snap to visible guidelines",
             snap_config.options[static_cast<size_t>(vector::precision::snap::SnapOption::Guide)]}
        }};
        SelectObject(mem_dc, font_sec);
        for (size_t row = 0; row < snap_rows.size(); ++row) {
            const int row_y = modal_y + 331 + static_cast<int>(row) * 21;
            RECT check{cx + 1, row_y + 3, cx + 15, row_y + 17};
            HBRUSH check_fill = CreateSolidBrush(snap_rows[row].second
                ? (is_dark ? RGB(10, 73, 68) : RGB(220, 252, 231))
                : (is_dark ? RGB(10, 16, 24) : RGB(248, 250, 252)));
            FillRect(mem_dc, &check, check_fill);
            DeleteObject(check_fill);
            HPEN check_pen = CreatePen(PS_SOLID, 1, snap_rows[row].second ? m_teal : m_border);
            HGDIOBJ old_check_pen = SelectObject(mem_dc, check_pen);
            SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            Rectangle(mem_dc, check.left, check.top, check.right, check.bottom);
            SelectObject(mem_dc, old_check_pen);
            DeleteObject(check_pen);
            RECT label{cx + 23, row_y, cx + 490, row_y + 20};
            SetTextColor(mem_dc, snap_rows[row].second ? m_text_pri : m_text_mut);
            DrawTextW(mem_dc, snap_rows[row].first, -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            if (snap_rows[row].second) {
                SetTextColor(mem_dc, m_teal);
                DrawTextW(mem_dc, L"✓", -1, &check,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
        }

    } else if (settings_active_tab_ == 3) {
        // Tab 3: color preview and explicit, non-profile-bound TAC preflight.
        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_c_t = { cx, cy, cx + 315, cy + 22 };
        DrawTextW(mem_dc, is_vi ? L"Màu xem trước & profile ICC" : L"Color preview & ICC profiles", -1, &rc_c_t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT manage{cx + 328, cy - 2, cx + 490, cy + 24};
        HBRUSH manage_brush = CreateSolidBrush(m_header_bg);
        FillRect(mem_dc, &manage, manage_brush);
        DeleteObject(manage_brush);
        HPEN manage_pen = CreatePen(PS_SOLID, 1, m_border);
        HGDIOBJ old_manage_pen = SelectObject(mem_dc, manage_pen);
        SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
        RoundRect(mem_dc, manage.left, manage.top, manage.right, manage.bottom, 6, 6);
        SelectObject(mem_dc, old_manage_pen);
        DeleteObject(manage_pen);
        SelectObject(mem_dc, font_sec_bold);
        SetTextColor(mem_dc, m_teal);
        DrawTextW(mem_dc, is_vi ? L"Quản lý profile…" : L"Manage profiles…", -1,
                  &manage, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_sec);
        SetTextColor(mem_dc, m_text_mut);
        RECT rc_c_b = { cx, cy + 28, cx + 490, cy + 98 };
        const wchar_t* icc_text = is_vi ?
            L"Preview dùng profile RGB/CMYK đã nạp và Windows Color Management.\n"
            L"Không có profile CMYK thì preview là gần đúng.\n"
            L"Đây là thiết lập xem trên máy, không gán profile vào tài liệu." :
            L"Preview uses the loaded RGB/CMYK profile through Windows Color Management.\n"
            L"Without a CMYK profile, preview is approximate.\n"
            L"These are local view settings; they do not assign a document profile.";
        DrawTextW(mem_dc, icc_text, -1, &rc_c_b, DT_LEFT | DT_NOPREFIX);

        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_teal);
        RECT rc_c_s2 = { cx, cy + 102, cx + 490, cy + 122 };
        DrawTextW(mem_dc, is_vi ? L"Giới hạn hiện có" : L"Current color limits", -1, &rc_c_s2, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_sec);
        SetTextColor(mem_dc, m_text_pri);
        RECT rc_c_s2_b = { cx, cy + 128, cx + 490, cy + 182 };
        const wchar_t* pol_text = is_vi ?
            L"Profile preview không đổi số màu nguồn.\n"
            L"Gamut warning hiện là phép so sánh gần đúng, không phải Delta E.\n"
            L"Overprint theo kênh mực và PDF/X chưa khả dụng." :
            L"Profile preview does not change source color values.\n"
            L"Gamut warning is approximate, not Delta E.\n"
            L"Ink separation overprint and PDF/X are unavailable.";
        DrawTextW(mem_dc, pol_text, -1, &rc_c_s2_b, DT_LEFT | DT_NOPREFIX);

        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT tac_heading{cx, cy + 188, cx + 490, cy + 208};
        DrawTextW(mem_dc, is_vi ? L"Kiểm tra tổng lượng mực (TAC)" : L"Total area coverage (TAC)", -1,
                  &tac_heading, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        const auto& current_selection = root().vector_controller().selection().selected_ids();
        const bool selection_available = !current_selection.empty();
        const auto draw_button = [&](RECT rect, const wchar_t* label, bool active, bool enabled, bool focused) {
            HBRUSH brush = CreateSolidBrush(!enabled
                ? (is_dark ? RGB(24, 30, 39) : RGB(226, 232, 240))
                : active ? (is_dark ? RGB(18, 38, 48) : RGB(236, 253, 245)) : m_header_bg);
            FillRect(mem_dc, &rect, brush);
            DeleteObject(brush);
            HPEN pen = CreatePen(PS_SOLID, (active || focused) && enabled ? 2 : 1,
                (active || focused) && enabled ? m_teal : m_border);
            HGDIOBJ old_pen = SelectObject(mem_dc, pen);
            SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            RoundRect(mem_dc, rect.left, rect.top, rect.right, rect.bottom, 6, 6);
            SelectObject(mem_dc, old_pen);
            DeleteObject(pen);
            SelectObject(mem_dc, font_sec_bold);
            SetTextColor(mem_dc, enabled ? ((active || focused) ? m_teal : m_text_pri) : m_text_mut);
            DrawTextW(mem_dc, label, -1, &rect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        };
        const int row_y = cy + 216;
        const wchar_t* scope_labels_vi[] = { L"Tài liệu", L"Trang", L"Vùng chọn" };
        const wchar_t* scope_labels_en[] = { L"Document", L"Page", L"Selection" };
        for (int index = 0; index < 3; ++index) {
            RECT scope_rect{cx + index * 86, row_y, cx + index * 86 + 82, row_y + 28};
            draw_button(scope_rect, is_vi ? scope_labels_vi[index] : scope_labels_en[index],
                settings_tac_scope_choice_ == index,
                index != 2 || selection_available,
                settings_tac_focus_index_ == index);
        }

        RECT limit_rect{cx + 262, row_y, cx + 338, row_y + 28};
        HBRUSH limit_brush = CreateSolidBrush(is_dark ? RGB(8, 14, 21) : RGB(255, 255, 255));
        FillRect(mem_dc, &limit_rect, limit_brush);
        DeleteObject(limit_brush);
        const bool limit_focused = active_num_field_ == NumericFieldId::TacLimitPercent ||
            settings_tac_focus_index_ == 3;
        HPEN limit_pen = CreatePen(PS_SOLID, limit_focused ? 2 : 1,
            limit_focused ? m_teal : m_border);
        HGDIOBJ old_limit_pen = SelectObject(mem_dc, limit_pen);
        SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
        RoundRect(mem_dc, limit_rect.left, limit_rect.top, limit_rect.right, limit_rect.bottom, 6, 6);
        SelectObject(mem_dc, old_limit_pen);
        DeleteObject(limit_pen);
        SelectObject(mem_dc, font_sec_bold);
        SetTextColor(mem_dc, m_text_pri);
        std::wstring limit_text;
        if (active_num_field_ == NumericFieldId::TacLimitPercent && !num_input_buffer_.empty()) {
            limit_text.assign(num_input_buffer_.begin(), num_input_buffer_.end());
        } else {
            limit_text = std::to_wstring(static_cast<int>(settings_tac_limit_percent_));
        }
        DrawTextW(mem_dc, limit_text.c_str(), -1, &limit_rect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT percent_rect{cx + 338, row_y, cx + 354, row_y + 28};
        DrawTextW(mem_dc, L"%", -1, &percent_rect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        const auto checked_snapshot = root().document().current_snapshot();
        const bool report_current = settings_tac_summary_ && checked_snapshot &&
            checked_snapshot->epoch() == settings_tac_report_epoch_ &&
            checked_snapshot->revision() == settings_tac_report_revision_ &&
            checked_snapshot->document_id() == settings_tac_report_document_id_ &&
            checked_snapshot->active_page_id() == settings_tac_report_active_page_ &&
            settings_tac_scope_choice_ == settings_tac_report_scope_ &&
            settings_tac_limit_percent_ == settings_tac_report_limit_percent_ &&
            (settings_tac_scope_choice_ != 2 || current_selection == settings_tac_report_selection_);
        RECT check_rect{cx + 358, row_y, cx + 424, row_y + 28};
        draw_button(check_rect, is_vi ? L"Kiểm tra" : L"Check", false, true,
            settings_tac_focus_index_ == 4);
        RECT fix_rect{cx + 430, row_y, cx + 490, row_y + 28};
        const bool can_fix = report_current &&
            settings_tac_summary_->process_colors_over_limit > 0;
        draw_button(fix_rect, is_vi ? L"Sửa" : L"Fix", false, can_fix,
            settings_tac_focus_index_ == 5);

        SelectObject(mem_dc, font_sec);
        const auto draw_report_line = [&](int top, const std::wstring& text, COLORREF color) {
            RECT line{cx, top, cx + 490, top + 18};
            SetTextColor(mem_dc, color);
            DrawTextW(mem_dc, text.c_str(), -1, &line,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        };
        draw_report_line(cy + 250,
            is_vi ? L"Ngưỡng thủ công · chưa gắn profile/output condition ICC" :
                    L"Manual limit · not bound to an ICC output condition",
            m_text_mut);
        if (!settings_tac_error_.empty()) {
            draw_report_line(cy + 270,
                NativeWindow::utf8_to_wide(settings_tac_error_),
                is_dark ? RGB(251, 191, 36) : RGB(180, 83, 9));
        } else if (settings_tac_summary_) {
            const auto& report = *settings_tac_summary_;
            std::wostringstream values;
            values << (report_current ? L"" : (is_vi ? L"Kết quả cũ · " : L"Stale report · "))
                   << (is_vi ? L"Đã quét " : L"Scanned ")
                   << report.process_colors_examined
                   << (is_vi ? L" màu CMYK · vượt " : L" CMYK paints · over ")
                   << report.process_colors_over_limit
                   << (is_vi ? L" · trước tối đa " : L" · max before ")
                   << std::fixed << std::setprecision(1) << report.maximum_before_percent << L"%";
            draw_report_line(cy + 270, values.str(), m_text_pri);
            std::wostringstream after;
            after << (is_vi ? L"Sau sửa tối đa " : L"After fix max ")
                  << std::fixed << std::setprecision(1) << report.maximum_after_percent << L"%"
                  << (is_vi ? L" · đã đổi " : L" · changed ") << report.process_colors_changed;
            draw_report_line(cy + 290, after.str(), m_text_pri);
            std::wostringstream unassessed;
            unassessed << (is_vi ? L"Chưa đánh giá: spot " : L"Not assessed: spot ")
                       << report.spot_colors_not_assessed
                       << (is_vi ? L" · raster " : L" · raster ")
                       << report.raster_objects_not_assessed;
            draw_report_line(cy + 310, unassessed.str(), m_text_mut);
            if (!report.fix_method.empty())
                draw_report_line(cy + 330,
                    NativeWindow::utf8_to_wide(report.fix_method), m_text_mut);
        } else {
            draw_report_line(cy + 270,
                is_vi ? L"Check chỉ đọc · Fix là một bước Undo và có thể đổi ngoại hình" :
                        L"Check is read-only · Fix is one undo step and may change appearance",
                m_text_pri);
            draw_report_line(cy + 290,
                is_vi ? L"Chỉ kiểm tra màu process CMYK của vector; raster/spot báo riêng" :
                        L"Scans vector process CMYK; raster and spot colors are reported separately",
                m_text_mut);
        }

    } else if (settings_active_tab_ == 4) {
        paint_shortcuts_settings_tab(*this, mem_dc, cx, cy, is_vi, is_dark,
            m_text_pri, m_text_mut, m_teal, m_border,
            font_vn_bold, font_vn_norm, font_sec, font_mono);
    } else if (settings_active_tab_ == 5) {
        // Tab 5: Thi công & CNC
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

        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_teal);
        RECT rc_sys_t = { cx, cy + 160, cx + 490, cy + 180 };
        DrawTextW(mem_dc, is_vi ? L"Bộ tăng tốc phần cứng & GPU" : L"Hardware Acceleration & GPU", -1, &rc_sys_t, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_sec);
        SetTextColor(mem_dc, m_text_mut);
        RECT rc_sys_b = { cx, cy + 186, cx + 490, cy + 300 };
        const wchar_t* sys_text = is_vi ?
            L"• Renderer Backend: DirectX / Skia GPU Rasterizer (Probed: Windows D3D11)\n"
            L"• Cache ảnh GPU dùng LRU; ở ngưỡng 95% RAM, canvas chuyển sang dựng CPU từ pixel nguồn\n"
            L"• Tần số làm tươi: 120 FPS / Tự động đồng bộ V-Sync màn hình\n"
            L"• Bản phục hồi tự động: Mỗi 60 giây vào %LOCALAPPDATA%/PikDraw/Recovery\n"
            L"• Giới hạn lịch sử thao tác: 50 bước Undo/Redo" :
            L"• Renderer Backend: DirectX / Skia GPU Rasterizer (Probed: Windows D3D11)\n"
            L"• GPU image cache uses LRU; at 95% RAM the canvas renders from source pixels on CPU\n"
            L"• Refresh Rate: 120 FPS / V-Sync hardware synchronized\n"
            L"• Auto-recovery: Every 60 seconds to %LOCALAPPDATA%/PikDraw/Recovery\n"
            L"• Undo/Redo Depth: 50 atomic command revisions";
        DrawTextW(mem_dc, sys_text, -1, &rc_sys_b, DT_LEFT | DT_NOPREFIX);

    } else if (settings_active_tab_ == 6) {
        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT title{cx, cy, cx + 490, cy + 22};
        DrawTextW(mem_dc, is_vi ? L"Kết nối AI qua MCP" : L"AI Connections through MCP", -1,
                  &title, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        auto draw_access_card = [&](int top, bool checked, bool enabled, int focus,
                                    const wchar_t* label, const wchar_t* detail) {
            RECT card{cx, top, cx + 490, top + 60};
            HBRUSH card_brush = CreateSolidBrush(enabled ? m_header_bg : m_sidebar_bg);
            FillRect(mem_dc, &card, card_brush);
            DeleteObject(card_brush);
            HPEN card_pen = CreatePen(PS_SOLID, settings_ai_focus_index_ == focus ? 2 : 1,
                settings_ai_focus_index_ == focus ? m_teal : m_border);
            HGDIOBJ previous_pen = SelectObject(mem_dc, card_pen);
            HGDIOBJ previous_brush = SelectObject(mem_dc, GetStockObject(HOLLOW_BRUSH));
            RoundRect(mem_dc, card.left, card.top, card.right, card.bottom, 7, 7);
            SelectObject(mem_dc, previous_brush);
            SelectObject(mem_dc, previous_pen);
            DeleteObject(card_pen);

            RECT box{cx + 14, top + 17, cx + 36, top + 39};
            HPEN box_pen = CreatePen(PS_SOLID, 1, enabled ? m_teal : m_text_mut);
            previous_pen = SelectObject(mem_dc, box_pen);
            previous_brush = SelectObject(mem_dc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(mem_dc, box.left, box.top, box.right, box.bottom);
            SelectObject(mem_dc, previous_brush);
            SelectObject(mem_dc, previous_pen);
            DeleteObject(box_pen);
            if (checked) {
                SetTextColor(mem_dc, enabled ? m_teal : m_text_mut);
                RECT mark{box.left, box.top - 1, box.right, box.bottom + 1};
                DrawTextW(mem_dc, L"✓", -1, &mark, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            SelectObject(mem_dc, font_vn_bold);
            SetTextColor(mem_dc, enabled ? m_text_pri : m_text_mut);
            RECT label_rect{cx + 48, top + 8, cx + 475, top + 30};
            DrawTextW(mem_dc, label, -1, &label_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(mem_dc, font_sec);
            SetTextColor(mem_dc, m_text_mut);
            RECT detail_rect{cx + 48, top + 30, cx + 475, top + 54};
            DrawTextW(mem_dc, detail, -1, &detail_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        };

        draw_access_card(cy + 30, settings_mcp_enabled_choice_, true, 0,
            is_vi ? L"Cho phép MCP" : L"Allow MCP access",
            is_vi ? L"Cho agent đọc tài liệu, truy vấn đối tượng, font và phép đo chữ."
                  : L"Allow agents to inspect documents, objects, fonts and text metrics.");
        draw_access_card(cy + 102, settings_mcp_edit_choice_, settings_mcp_enabled_choice_, 1,
            is_vi ? L"Cho phép sửa tài liệu và tệp native" : L"Allow document editing and native files",
            is_vi ? L"Bao gồm tạo tài liệu, sửa batch, đọc và lưu tệp .pr được cấp quyền."
                  : L"Includes document creation, batch edits, and granted .pr file access.");

        SelectObject(mem_dc, font_vn_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT status_title{cx, cy + 176, cx + 490, cy + 196};
        DrawTextW(mem_dc, is_vi ? L"Trạng thái endpoint" : L"Endpoint status", -1,
                  &status_title, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(mem_dc, font_sec_bold);
        SetTextColor(mem_dc, mcp_listener_active() ? m_teal : m_text_mut);
        RECT status_rect{cx, cy + 200, cx + 490, cy + 220};
        const wchar_t* endpoint_status = mcp_listener_active()
            ? (mcp_edit_access_active()
                ? (is_vi ? L"Đang lắng nghe · Có quyền chỉnh sửa" : L"Listening · Editing enabled")
                : (is_vi ? L"Đang lắng nghe · Chỉ đọc" : L"Listening · Read only"))
            : (mcp_error_message().empty()
                ? (is_vi ? L"Đã tắt" : L"Off")
                : (is_vi ? L"Không thể khởi động MCP" : L"Could not start MCP"));
        DrawTextW(mem_dc, endpoint_status, -1, &status_rect, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        const bool access_pending = settings_mcp_enabled_choice_ != mcp_listener_active() ||
            settings_mcp_edit_choice_ != mcp_edit_access_active();
        SelectObject(mem_dc, font_sec_bold);
        SetTextColor(mem_dc, access_pending ? m_teal : m_text_mut);
        RECT pending_rect{cx, cy + 222, cx + 490, cy + 242};
        DrawTextW(mem_dc, access_pending
            ? (is_vi ? L"Có thay đổi chưa áp dụng" : L"Changes pending")
            : (is_vi ? L"Quyền đang áp dụng" : L"Applied access"), -1,
            &pending_rect, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        const std::string endpoint_value = mcp_listener_active() && mcp_runtime_
            ? mcp_runtime_->http.endpoint() : std::string{};
        const std::string token_value = mcp_listener_active() && mcp_runtime_
            ? mcp_runtime_->http.access_token() : std::string{};
        const auto endpoint_wide = NativeWindow::utf8_to_wide(endpoint_value.empty()
            ? (is_vi ? "Áp dụng để tạo endpoint" : "Apply to create the endpoint") : endpoint_value);
        const auto token_wide = NativeWindow::utf8_to_wide(token_value.empty()
            ? (is_vi ? "Áp dụng để tạo access token" : "Apply to create an access token") : token_value);
        SelectObject(mem_dc, font_sec_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT endpoint_label{cx, cy + 246, cx + 78, cy + 265};
        DrawTextW(mem_dc, is_vi ? L"MCP URL" : L"MCP URL", -1, &endpoint_label,
                  DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(mem_dc, font_mono);
        SetTextColor(mem_dc, m_teal);
        RECT endpoint_rect{cx + 78, cy + 246, cx + 405, cy + 265};
        DrawTextW(mem_dc, endpoint_wide.c_str(), -1, &endpoint_rect,
                  DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(mem_dc, font_sec_bold);
        SetTextColor(mem_dc, m_text_pri);
        RECT token_label{cx, cy + 267, cx + 78, cy + 286};
        DrawTextW(mem_dc, is_vi ? L"Access key" : L"Access key", -1, &token_label,
                  DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(mem_dc, font_mono);
        SetTextColor(mem_dc, mcp_listener_active() ? m_text_pri : m_text_mut);
        RECT token_rect{cx + 78, cy + 267, cx + 405, cy + 286};
        DrawTextW(mem_dc, token_wide.c_str(), -1, &token_rect,
                  DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        auto draw_copy_button = [&](RECT rect, int focus, const wchar_t* label) {
            const bool enabled = mcp_listener_active();
            HBRUSH button_brush = CreateSolidBrush(enabled ? m_header_bg : m_sidebar_bg);
            HPEN button_pen = CreatePen(PS_SOLID, 1,
                settings_ai_focus_index_ == focus ? m_teal : m_border);
            HGDIOBJ old_brush = SelectObject(mem_dc, button_brush);
            HGDIOBJ old_pen = SelectObject(mem_dc, button_pen);
            RoundRect(mem_dc, rect.left, rect.top, rect.right, rect.bottom, 5, 5);
            SelectObject(mem_dc, old_brush);
            SelectObject(mem_dc, old_pen);
            DeleteObject(button_brush);
            DeleteObject(button_pen);
            SelectObject(mem_dc, font_sec_bold);
            SetTextColor(mem_dc, enabled ? m_text_pri : m_text_mut);
            DrawTextW(mem_dc, label, -1, &rect,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            if (settings_ai_focus_index_ == focus) {
                HPEN focus_pen = CreatePen(PS_SOLID, 2, m_teal);
                HGDIOBJ previous_pen = SelectObject(mem_dc, focus_pen);
                HGDIOBJ previous_brush = SelectObject(mem_dc, GetStockObject(HOLLOW_BRUSH));
                RoundRect(mem_dc, rect.left - 2, rect.top - 2,
                    rect.right + 2, rect.bottom + 2, 6, 6);
                SelectObject(mem_dc, previous_brush);
                SelectObject(mem_dc, previous_pen);
                DeleteObject(focus_pen);
            }
        };
        draw_copy_button({cx + 412, cy + 245, cx + 490, cy + 266}, 2,
            is_vi ? L"Chép URL" : L"Copy URL");
        draw_copy_button({cx + 412, cy + 267, cx + 490, cy + 288}, 3,
            is_vi ? L"Chép khóa" : L"Copy key");

        SelectObject(mem_dc, font_sec);
        SetTextColor(mem_dc, m_text_mut);
        RECT explanation{cx, cy + 290, cx + 490, cy + 324};
        if (!mcp_error_message().empty()) {
            const auto error_wide = NativeWindow::utf8_to_wide(mcp_error_message());
            DrawTextW(mem_dc, error_wide.c_str(), -1, &explanation,
                      DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        } else {
            DrawTextW(mem_dc, is_vi
                ? L"MCP chạy ngay trong PikDraw, chỉ bind 127.0.0.1 và yêu cầu access key. Origin ngoài loopback bị từ chối."
                : L"MCP runs inside PikDraw, binds only to 127.0.0.1 and requires the access key. Non-loopback Origin is rejected.",
                -1, &explanation, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        }
        RECT file_scope{cx, cy + 326, cx + 490, cy + 356};
        DrawTextW(mem_dc, is_vi
            ? L"Tệp native chỉ nằm trong %LOCALAPPDATA%\\PikDraw\\automation\\files. Quyền chỉ có hiệu lực trong phiên app này."
            : L"Native files are limited to %LOCALAPPDATA%\\PikDraw\\automation\\files. Access lasts for this app session.",
            -1, &file_scope, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        RECT help_text{cx, cy + 360, cx + 490, cy + 402};
        DrawTextW(mem_dc, is_vi
            ? L"Cấu hình client MCP Streamable HTTP bằng URL và header Authorization: Bearer <access key> ở trên. Thay đổi chỉ có hiệu lực sau khi nhấn Áp dụng."
            : L"Configure the MCP client for Streamable HTTP with the URL above and Authorization: Bearer <access key>. Changes take effect after Apply.",
            -1, &help_text, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    }

    // 6. Modal Footer (Matching CorelDRAW [ ? ] Help and [ OK ] [ Cancel ])
    int fy = modal_y + modal_h - 46;
    HBRUSH br_foot = CreateSolidBrush(m_header_bg);
    RECT rc_foot = { modal_x, fy, modal_x + modal_w, modal_y + modal_h };
    FillRect(mem_dc, &rc_foot, br_foot);
    DeleteObject(br_foot);

    MoveToEx(mem_dc, modal_x, fy, NULL); LineTo(mem_dc, modal_x + modal_w, fy);

    // Left: [ ? ] Help & Reset defaults
    HPEN pen_b_def = CreatePen(PS_SOLID, 1, m_border);
    HGDIOBJ old_pen_modal = SelectObject(mem_dc, pen_b_def);
    HBRUSH br_q = CreateSolidBrush(is_dark ? RGB(29, 38, 54) : RGB(226, 232, 240));
    SelectObject(mem_dc, br_q);
    RoundRect(mem_dc, modal_x + 16, fy + 8, modal_x + 44, fy + 38, 6, 6);
    DeleteObject(br_q);
    RECT rc_q = { modal_x + 16, fy + 8, modal_x + 44, fy + 38 };
    SelectObject(mem_dc, font_vn_bold);
    SetTextColor(mem_dc, m_text_pri);
    DrawTextW(mem_dc, L"?", -1, &rc_q, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(mem_dc, font_sec);
    SetTextColor(mem_dc, m_text_mut);
    RECT rc_f_rst = { modal_x + 54, fy + 12, modal_x + 180, fy + 36 };
    DrawTextW(mem_dc, is_vi ? L"Khôi phục mặc định" : L"Reset Defaults", -1, &rc_f_rst, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (settings_active_tab_ == 6 && settings_ai_focus_index_ == 4) {
        HPEN focus_pen = CreatePen(PS_SOLID, 2, m_teal);
        HGDIOBJ previous_pen = SelectObject(mem_dc, focus_pen);
        HGDIOBJ previous_brush = SelectObject(mem_dc, GetStockObject(HOLLOW_BRUSH));
        RoundRect(mem_dc, rc_f_rst.left - 3, rc_f_rst.top - 3,
                  rc_f_rst.right + 3, rc_f_rst.bottom + 3, 6, 6);
        SelectObject(mem_dc, previous_brush);
        SelectObject(mem_dc, previous_pen);
        DeleteObject(focus_pen);
    }

    // Right: Cancel button
    RECT rc_btn_cls = { modal_x + modal_w - 170, fy + 8, modal_x + modal_w - 95, fy + 38 };
    HBRUSH br_b_cls = CreateSolidBrush(is_dark ? RGB(29, 38, 54) : RGB(226, 232, 240));
    SelectObject(mem_dc, br_b_cls);
    RoundRect(mem_dc, rc_btn_cls.left, rc_btn_cls.top, rc_btn_cls.right, rc_btn_cls.bottom, 6, 6);
    DeleteObject(br_b_cls);
    SelectObject(mem_dc, font_vn_bold);
    SetTextColor(mem_dc, m_text_pri);
    DrawTextW(mem_dc, is_vi ? L"Hủy (Cancel)" : L"Cancel", -1, &rc_btn_cls, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (settings_active_tab_ == 6 && settings_ai_focus_index_ == 5) {
        HPEN focus_pen = CreatePen(PS_SOLID, 2, m_teal);
        HGDIOBJ previous_pen = SelectObject(mem_dc, focus_pen);
        HGDIOBJ previous_brush = SelectObject(mem_dc, GetStockObject(HOLLOW_BRUSH));
        RoundRect(mem_dc, rc_btn_cls.left - 2, rc_btn_cls.top - 2,
                  rc_btn_cls.right + 2, rc_btn_cls.bottom + 2, 8, 8);
        SelectObject(mem_dc, previous_brush);
        SelectObject(mem_dc, previous_pen);
        DeleteObject(focus_pen);
    }

    // Right: OK button
    RECT rc_btn_app = { modal_x + modal_w - 85, fy + 8, modal_x + modal_w - 15, fy + 38 };
    HPEN pen_ok = CreatePen(PS_SOLID, 1, m_teal);
    SelectObject(mem_dc, pen_ok);
    HBRUSH br_b_app = CreateSolidBrush(m_teal);
    SelectObject(mem_dc, br_b_app);
    RoundRect(mem_dc, rc_btn_app.left, rc_btn_app.top, rc_btn_app.right, rc_btn_app.bottom, 6, 6);
    DeleteObject(br_b_app);
    SelectObject(mem_dc, old_pen_modal);
    DeleteObject(pen_ok);
    DeleteObject(pen_b_def);
    SetTextColor(mem_dc, is_dark ? RGB(7, 11, 16) : RGB(255, 255, 255));
    DrawTextW(mem_dc, settings_active_tab_ == 6
        ? (is_vi ? L"Áp dụng" : L"Apply") : L"OK", -1, &rc_btn_app,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (settings_active_tab_ == 6 && settings_ai_focus_index_ == 6) {
        HPEN focus_pen = CreatePen(PS_SOLID, 2, m_text_pri);
        HGDIOBJ previous_pen = SelectObject(mem_dc, focus_pen);
        HGDIOBJ previous_brush = SelectObject(mem_dc, GetStockObject(HOLLOW_BRUSH));
        RoundRect(mem_dc, rc_btn_app.left - 2, rc_btn_app.top - 2,
                  rc_btn_app.right + 2, rc_btn_app.bottom + 2, 8, 8);
        SelectObject(mem_dc, previous_brush);
        SelectObject(mem_dc, previous_pen);
        DeleteObject(focus_pen);
    }

    SelectObject(mem_dc, old_pen);
    DeleteObject(pen_div);
    for (auto* f : { font_vn_bold, font_vn_norm, font_sec, font_sec_bold, font_mono }) DeleteObject(f);
}

#endif

} // namespace pikdraw::desktop
