#include "pikdraw/desktop/shell/native_window.hpp"
#include "../automation/native_window_automation.hpp"
#include <sstream>
#include <iomanip>

namespace pikdraw::desktop {

#ifdef _WIN32

void NativeWindow::paint_new_document_modal(HDC mem_dc, int w, int h) {
    bool is_vi = (language_ == UILanguage::Vietnamese);
    bool is_dark = (theme_ == ThemeMode::CyberDark);

    // 1. Semi-transparent backdrop
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

    // 2. Modal Card Geometry
    int modal_w = 680, modal_h = 510;
    int modal_x = (w - modal_w) / 2, modal_y = (h - modal_h) / 2;

    COLORREF m_card_bg = is_dark ? RGB(15, 23, 34) : RGB(255, 255, 255);
    COLORREF m_header_bg = is_dark ? RGB(19, 29, 43) : RGB(248, 250, 252);
    COLORREF m_border = is_dark ? RGB(36, 48, 68) : RGB(203, 213, 225);
    COLORREF m_teal = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    COLORREF m_text_pri = is_dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    COLORREF m_text_mut = is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    COLORREF m_card_item = is_dark ? RGB(20, 32, 48) : RGB(241, 245, 249);
    COLORREF m_card_active = is_dark ? RGB(22, 45, 62) : RGB(236, 253, 245);

    auto make_font = [](int sz, int weight, const wchar_t* name) {
        return CreateFontW(sz, 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, name);
    };
    HFONT font_title = make_font(15, FW_BOLD, L"Segoe UI");
    HFONT font_bold = make_font(13, FW_BOLD, L"Segoe UI");
    HFONT font_norm = make_font(12, FW_NORMAL, L"Segoe UI");
    HFONT font_mono = make_font(13, FW_BOLD, L"JetBrains Mono");

    // Shadow
    HBRUSH br_shad = CreateSolidBrush(RGB(1, 3, 6));
    RECT rc_shad = { modal_x + 6, modal_y + 6, modal_x + modal_w + 6, modal_y + modal_h + 6 };
    FillRect(mem_dc, &rc_shad, br_shad);
    DeleteObject(br_shad);

    // Main Card
    HBRUSH br_card = CreateSolidBrush(m_card_bg);
    HPEN pen_bdr = CreatePen(PS_SOLID, 1, m_border);
    SelectObject(mem_dc, pen_bdr);
    SelectObject(mem_dc, br_card);
    RoundRect(mem_dc, modal_x, modal_y, modal_x + modal_w, modal_y + modal_h, 14, 14);
    DeleteObject(pen_bdr);
    DeleteObject(br_card);

    // Header
    HBRUSH br_head = CreateSolidBrush(m_header_bg);
    RECT rc_head = { modal_x, modal_y, modal_x + modal_w, modal_y + 46 };
    FillRect(mem_dc, &rc_head, br_head);
    DeleteObject(br_head);

    SelectObject(mem_dc, font_title);
    SetTextColor(mem_dc, m_teal);
    RECT rc_htitle = { modal_x + 20, modal_y + 11, modal_x + 500, modal_y + 35 };
    DrawTextW(mem_dc, is_vi ? L"📄 Tạo tài liệu mới (New Document)" : L"📄 New Document", -1, &rc_htitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    RECT rc_close = { modal_x + modal_w - 38, modal_y + 9, modal_x + modal_w - 12, modal_y + 35 };
    SetTextColor(mem_dc, m_text_mut);
    SelectObject(mem_dc, font_bold);
    DrawTextW(mem_dc, L"✕", -1, &rc_close, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    HPEN pen_div = CreatePen(PS_SOLID, 1, m_border);
    HGDIOBJ old_pen = SelectObject(mem_dc, pen_div);
    MoveToEx(mem_dc, modal_x, modal_y + 46, NULL);
    LineTo(mem_dc, modal_x + modal_w, modal_y + 46);

    int cx = modal_x + 24;
    int cy = modal_y + 58;

    // Presets Section
    SelectObject(mem_dc, font_bold);
    SetTextColor(mem_dc, m_text_pri);
    RECT rc_pre_title = { cx, cy, cx + 400, cy + 18 };
    DrawTextW(mem_dc, is_vi ? L"KHỔ GIẤY TIÊU CHUẨN (PRESETS):" : L"STANDARD PRESETS:", -1, &rc_pre_title, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

    struct PresetInfo { const wchar_t* name; const wchar_t* dims; double w; double h; };
    PresetInfo presets[] = {
        { L"A4", L"210 × 297 mm", 210.0, 297.0 },
        { L"A3", L"297 × 420 mm", 297.0, 420.0 },
        { L"A2", L"420 × 594 mm", 420.0, 594.0 },
        { L"Pano 2×1m", L"2000 × 1000 mm", 2000.0, 1000.0 },
        { L"Banner 3×1m", L"3000 × 1000 mm", 3000.0, 1000.0 },
        { L"Vuông 1080", L"1080 × 1080 mm", 1080.0, 1080.0 },
    };

    for (int i = 0; i < 6; ++i) {
        int px = cx + (i % 3) * 212;
        int py = cy + 24 + (i / 3) * 48;
        RECT rc_p = { px, py, px + 204, py + 42 };

        bool match = (std::abs(new_doc_w_ - presets[i].w) < 0.1 && std::abs(new_doc_h_ - presets[i].h) < 0.1) ||
                     (std::abs(new_doc_w_ - presets[i].h) < 0.1 && std::abs(new_doc_h_ - presets[i].w) < 0.1);

        HBRUSH br_p = CreateSolidBrush(match ? m_card_active : m_card_item);
        HPEN pen_p = CreatePen(PS_SOLID, 1, match ? m_teal : m_border);
        SelectObject(mem_dc, pen_p);
        SelectObject(mem_dc, br_p);
        RoundRect(mem_dc, rc_p.left, rc_p.top, rc_p.right, rc_p.bottom, 6, 6);
        DeleteObject(pen_p);
        DeleteObject(br_p);

        SelectObject(mem_dc, font_bold);
        SetTextColor(mem_dc, match ? m_teal : m_text_pri);
        RECT rc_pn = { rc_p.left + 8, rc_p.top + 3, rc_p.right - 8, rc_p.top + 20 };
        DrawTextW(mem_dc, presets[i].name, -1, &rc_pn, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_norm);
        SetTextColor(mem_dc, m_text_mut);
        RECT rc_pd = { rc_p.left + 8, rc_p.top + 21, rc_p.right - 8, rc_p.bottom - 3 };
        DrawTextW(mem_dc, presets[i].dims, -1, &rc_pd, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    }

    // Custom Dimensions Form
    int fy_form = cy + 132;
    MoveToEx(mem_dc, modal_x + 20, fy_form, NULL);
    LineTo(mem_dc, modal_x + modal_w - 20, fy_form);
    fy_form += 10;

    // Row: Width & Height
    SelectObject(mem_dc, font_bold);
    SetTextColor(mem_dc, m_text_pri);
    RECT rc_w_lbl = { cx, fy_form, cx + 180, fy_form + 18 };
    DrawTextW(mem_dc, is_vi ? L"Chiều rộng (Width):" : L"Width:", -1, &rc_w_lbl, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

    RECT rc_h_lbl = { cx + 320, fy_form, cx + 500, fy_form + 18 };
    DrawTextW(mem_dc, is_vi ? L"Chiều cao (Height):" : L"Height:", -1, &rc_h_lbl, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

    // Width input card (Direct keyboard numeric input)
    RECT rc_w_box = { cx, fy_form + 22, cx + 290, fy_form + 54 };
    bool is_w_focused = (active_num_field_ == NumericFieldId::ModalWidth);
    HBRUSH br_w_box = CreateSolidBrush(is_w_focused ? m_card_active : m_card_item);
    HPEN pen_w_box = CreatePen(PS_SOLID, is_w_focused ? 2 : 1, is_w_focused ? m_teal : m_border);
    SelectObject(mem_dc, pen_w_box); SelectObject(mem_dc, br_w_box);
    RoundRect(mem_dc, rc_w_box.left, rc_w_box.top, rc_w_box.right, rc_w_box.bottom, 6, 6);
    DeleteObject(pen_w_box); DeleteObject(br_w_box);

    SelectObject(mem_dc, font_mono);
    SetTextColor(mem_dc, is_w_focused ? m_teal : m_text_pri);
    std::wstring w_val;
    if (is_w_focused) {
        w_val = utf8_to_wide(num_input_buffer_) + L"|";
    } else {
        std::ostringstream sw;
        sw << std::fixed << std::setprecision(1) << new_doc_w_;
        std::string s = sw.str();
        if (s.size() > 2 && s.substr(s.size() - 2) == ".0") s.erase(s.size() - 2);
        w_val = utf8_to_wide(s) + L" mm";
    }
    RECT rc_w_txt = { rc_w_box.left + 12, rc_w_box.top, rc_w_box.right - 12, rc_w_box.bottom };
    DrawTextW(mem_dc, w_val.c_str(), -1, &rc_w_txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Height input card (Direct keyboard numeric input)
    RECT rc_h_box = { cx + 320, fy_form + 22, cx + 610, fy_form + 54 };
    bool is_h_focused = (active_num_field_ == NumericFieldId::ModalHeight);
    HBRUSH br_h_box = CreateSolidBrush(is_h_focused ? m_card_active : m_card_item);
    HPEN pen_h_box = CreatePen(PS_SOLID, is_h_focused ? 2 : 1, is_h_focused ? m_teal : m_border);
    SelectObject(mem_dc, pen_h_box); SelectObject(mem_dc, br_h_box);
    RoundRect(mem_dc, rc_h_box.left, rc_h_box.top, rc_h_box.right, rc_h_box.bottom, 6, 6);
    DeleteObject(pen_h_box); DeleteObject(br_h_box);

    SelectObject(mem_dc, font_mono);
    SetTextColor(mem_dc, is_h_focused ? m_teal : m_text_pri);
    std::wstring h_val;
    if (is_h_focused) {
        h_val = utf8_to_wide(num_input_buffer_) + L"|";
    } else {
        std::ostringstream sh;
        sh << std::fixed << std::setprecision(1) << new_doc_h_;
        std::string s = sh.str();
        if (s.size() > 2 && s.substr(s.size() - 2) == ".0") s.erase(s.size() - 2);
        h_val = utf8_to_wide(s) + L" mm";
    }
    RECT rc_h_txt = { rc_h_box.left + 12, rc_h_box.top, rc_h_box.right - 12, rc_h_box.bottom };
    DrawTextW(mem_dc, h_val.c_str(), -1, &rc_h_txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Orientation: Dọc / Ngang
    int oy = fy_form + 64;
    SelectObject(mem_dc, font_bold);
    SetTextColor(mem_dc, m_text_pri);
    RECT rc_ori_lbl = { cx, oy, cx + 200, oy + 18 };
    DrawTextW(mem_dc, is_vi ? L"Hướng trang (Orientation):" : L"Orientation:", -1, &rc_ori_lbl, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

    RECT rc_ori_port = { cx, oy + 22, cx + 140, oy + 54 };
    bool is_port = !new_doc_landscape_;
    HBRUSH br_port = CreateSolidBrush(is_port ? m_card_active : m_card_item);
    HPEN pen_port = CreatePen(PS_SOLID, 1, is_port ? m_teal : m_border);
    SelectObject(mem_dc, pen_port); SelectObject(mem_dc, br_port);
    RoundRect(mem_dc, rc_ori_port.left, rc_ori_port.top, rc_ori_port.right, rc_ori_port.bottom, 6, 6);
    DeleteObject(pen_port); DeleteObject(br_port);
    SetTextColor(mem_dc, is_port ? m_teal : m_text_pri);
    DrawTextW(mem_dc, is_vi ? L"▯ Dọc (Portrait)" : L"▯ Portrait", -1, &rc_ori_port, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    RECT rc_ori_land = { cx + 150, oy + 22, cx + 300, oy + 54 };
    bool is_land = new_doc_landscape_;
    HBRUSH br_land = CreateSolidBrush(is_land ? m_card_active : m_card_item);
    HPEN pen_land = CreatePen(PS_SOLID, 1, is_land ? m_teal : m_border);
    SelectObject(mem_dc, pen_land); SelectObject(mem_dc, br_land);
    RoundRect(mem_dc, rc_ori_land.left, rc_ori_land.top, rc_ori_land.right, rc_ori_land.bottom, 6, 6);
    DeleteObject(pen_land); DeleteObject(br_land);
    SetTextColor(mem_dc, is_land ? m_teal : m_text_pri);
    DrawTextW(mem_dc, is_vi ? L"▭ Ngang (Landscape)" : L"▭ Landscape", -1, &rc_ori_land, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Color Mode (CMYK / RGB)
    RECT rc_col_lbl = { cx + 320, oy, cx + 500, oy + 18 };
    SetTextColor(mem_dc, m_text_pri);
    DrawTextW(mem_dc, is_vi ? L"Hệ màu (Color Mode):" : L"Color Mode:", -1, &rc_col_lbl, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

    bool is_cmyk = (new_doc_color_mode_ == 0);
    RECT rc_col_cmyk = { cx + 320, oy + 22, cx + 460, oy + 54 };
    HBRUSH br_cmyk = CreateSolidBrush(is_cmyk ? m_card_active : m_card_item);
    HPEN pen_cmyk = CreatePen(PS_SOLID, 1, is_cmyk ? m_teal : m_border);
    SelectObject(mem_dc, pen_cmyk); SelectObject(mem_dc, br_cmyk);
    RoundRect(mem_dc, rc_col_cmyk.left, rc_col_cmyk.top, rc_col_cmyk.right, rc_col_cmyk.bottom, 6, 6);
    DeleteObject(pen_cmyk); DeleteObject(br_cmyk);
    SetTextColor(mem_dc, is_cmyk ? m_teal : m_text_pri);
    DrawTextW(mem_dc, is_vi ? L"● CMYK (In ấn)" : L"● CMYK (Print)", -1, &rc_col_cmyk, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    RECT rc_col_rgb = { cx + 470, oy + 22, cx + 610, oy + 54 };
    bool is_rgb = !is_cmyk;
    HBRUSH br_rgb = CreateSolidBrush(is_rgb ? m_card_active : m_card_item);
    HPEN pen_rgb = CreatePen(PS_SOLID, 1, is_rgb ? m_teal : m_border);
    SelectObject(mem_dc, pen_rgb); SelectObject(mem_dc, br_rgb);
    RoundRect(mem_dc, rc_col_rgb.left, rc_col_rgb.top, rc_col_rgb.right, rc_col_rgb.bottom, 6, 6);
    DeleteObject(pen_rgb); DeleteObject(br_rgb);
    SetTextColor(mem_dc, is_rgb ? m_teal : m_text_pri);
    DrawTextW(mem_dc, is_vi ? L"○ RGB (Màn hình)" : L"○ RGB (Screen)", -1, &rc_col_rgb, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Resolution: 300 / 150 / 72 DPI
    int ry = oy + 64;
    SelectObject(mem_dc, font_bold);
    SetTextColor(mem_dc, m_text_pri);
    RECT rc_dpi_lbl = { cx, ry, cx + 300, ry + 18 };
    DrawTextW(mem_dc, is_vi ? L"Độ phân giải hiệu ứng (Raster DPI):" : L"Raster DPI:", -1, &rc_dpi_lbl, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

    int dpis[] = { 300, 150, 72 };
    const wchar_t* dpi_names_vi[] = { L"300 DPI (Cao / In ấn)", L"150 DPI (Trung bình / Pano)", L"72 DPI (Màn hình)" };
    const wchar_t* dpi_names_en[] = { L"300 DPI (High / Print)", L"150 DPI (Medium)", L"72 DPI (Screen)" };
    for (int i = 0; i < 3; ++i) {
        int dx = cx + i * 205;
        RECT rc_d = { dx, ry + 22, dx + 195, ry + 52 };
        bool is_d_act = (new_doc_dpi_ == dpis[i]);
        HBRUSH br_d = CreateSolidBrush(is_d_act ? m_card_active : m_card_item);
        HPEN pen_d = CreatePen(PS_SOLID, 1, is_d_act ? m_teal : m_border);
        SelectObject(mem_dc, pen_d); SelectObject(mem_dc, br_d);
        RoundRect(mem_dc, rc_d.left, rc_d.top, rc_d.right, rc_d.bottom, 6, 6);
        DeleteObject(pen_d); DeleteObject(br_d);
        SetTextColor(mem_dc, is_d_act ? m_teal : m_text_pri);
        DrawTextW(mem_dc, is_vi ? dpi_names_vi[i] : dpi_names_en[i], -1, &rc_d, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // Modal Footer
    int fy_btn = modal_y + modal_h - 48;
    HBRUSH br_foot_new = CreateSolidBrush(m_header_bg);
    RECT rc_foot_new = { modal_x, fy_btn, modal_x + modal_w, modal_y + modal_h };
    FillRect(mem_dc, &rc_foot_new, br_foot_new);
    DeleteObject(br_foot_new);

    MoveToEx(mem_dc, modal_x, fy_btn, NULL); LineTo(mem_dc, modal_x + modal_w, fy_btn);

    // Cancel Button
    RECT rc_btn_cancel = { modal_x + modal_w - 280, fy_btn + 8, modal_x + modal_w - 150, fy_btn + 40 };
    HBRUSH br_c = CreateSolidBrush(is_dark ? RGB(29, 38, 54) : RGB(226, 232, 240));
    SelectObject(mem_dc, br_c);
    RoundRect(mem_dc, rc_btn_cancel.left, rc_btn_cancel.top, rc_btn_cancel.right, rc_btn_cancel.bottom, 6, 6);
    DeleteObject(br_c);
    SetTextColor(mem_dc, m_text_pri);
    SelectObject(mem_dc, font_bold);
    DrawTextW(mem_dc, is_vi ? L"Hủy bỏ (Esc)" : L"Cancel", -1, &rc_btn_cancel, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Confirm Create Button
    RECT rc_btn_ok = { modal_x + modal_w - 140, fy_btn + 8, modal_x + modal_w - 15, fy_btn + 40 };
    HPEN pen_ok = CreatePen(PS_SOLID, 1, m_teal);
    SelectObject(mem_dc, pen_ok);
    HBRUSH br_ok = CreateSolidBrush(m_teal);
    SelectObject(mem_dc, br_ok);
    RoundRect(mem_dc, rc_btn_ok.left, rc_btn_ok.top, rc_btn_ok.right, rc_btn_ok.bottom, 6, 6);
    DeleteObject(br_ok);
    DeleteObject(pen_ok);
    SetTextColor(mem_dc, is_dark ? RGB(7, 11, 16) : RGB(255, 255, 255));
    DrawTextW(mem_dc, is_vi ? L"Tạo tài liệu ➔" : L"Create ➔", -1, &rc_btn_ok, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(mem_dc, old_pen);
    DeleteObject(pen_div);
    for (auto* f : { font_title, font_bold, font_norm, font_mono }) DeleteObject(f);
}

#endif

} // namespace pikdraw::desktop
