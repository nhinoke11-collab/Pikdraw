#include "pikdraw/desktop/shell/native_window.hpp"

namespace pikdraw::desktop {

#ifdef _WIN32

void NativeWindow::paint_home_screen(HDC mem_dc, int w, int h) {
    bool is_dark = (theme_ == ThemeMode::CyberDark);
    bool is_vi = (language_ == UILanguage::Vietnamese);

    COLORREF col_canvas = is_dark ? RGB(7, 12, 18) : RGB(241, 245, 249);
    COLORREF col_header = is_dark ? RGB(12, 19, 29) : RGB(255, 255, 255);
    COLORREF col_border = is_dark ? RGB(29, 38, 54) : RGB(203, 213, 225);
    COLORREF col_grid = is_dark ? RGB(24, 35, 52) : RGB(218, 225, 233);
    COLORREF col_teal = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    COLORREF col_text_white = is_dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    COLORREF col_text_muted = is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    COLORREF col_active_tab = is_dark ? RGB(22, 37, 54) : RGB(236, 242, 248);

    auto make_font = [](int sz, int weight, const wchar_t* name) {
        return CreateFontW(sz, 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, name);
    };
    HFONT font_h1 = make_font(24, FW_BOLD, L"Segoe UI");
    HFONT font_h2 = make_font(14, FW_BOLD, L"Segoe UI");
    HFONT font_body = make_font(13, FW_NORMAL, L"Segoe UI");
    HFONT font_sub = make_font(11, FW_NORMAL, L"Segoe UI");
    HFONT font_mono = make_font(11, FW_NORMAL, L"JetBrains Mono");

    // Background Canvas
    HBRUSH br_bg = CreateSolidBrush(col_canvas);
    RECT rc_bg = { 0, 38, w, h - 26 };
    FillRect(mem_dc, &rc_bg, br_bg);
    DeleteObject(br_bg);

    for (int gx = 40; gx < w - 40; gx += 24) {
        for (int gy = 50; gy < h - 40; gy += 24) {
            SetPixel(mem_dc, gx, gy, col_grid);
        }
    }

    // Header Area: Logo PK badge (bo nhẹ 8px)
    HPEN pen_bdr = CreatePen(PS_SOLID, 1, col_border);
    HBRUSH br_logo = CreateSolidBrush(col_teal);
    SelectObject(mem_dc, pen_bdr);
    SelectObject(mem_dc, br_logo);
    RoundRect(mem_dc, 180, 52, 222, 94, 8, 8);
    DeleteObject(br_logo);

    SelectObject(mem_dc, font_h2);
    SetTextColor(mem_dc, is_dark ? RGB(7, 11, 16) : RGB(255, 255, 255));
    RECT rc_logo_txt = { 180, 52, 222, 94 };
    DrawTextW(mem_dc, L"PK", -1, &rc_logo_txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Title & Subtitle
    SelectObject(mem_dc, font_h1);
    SetTextColor(mem_dc, col_text_white);
    RECT rc_title = { 235, 50, w - 260, 80 };
    DrawTextW(mem_dc, L"PikDraw PRO 2026", -1, &rc_title, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(mem_dc, font_body);
    SetTextColor(mem_dc, col_text_muted);
    RECT rc_sub = { 235, 78, w - 260, 102 };
    const wchar_t* sub_txt = is_vi ?
        L"Không gian thiết kế vector & sản xuất quảng cáo chuyên nghiệp (Chuẩn thi công xưởng Việt Nam)" :
        L"Professional Vector Design & Signage Manufacturing Suite (CorelDRAW Workflow Compatible)";
    DrawTextW(mem_dc, sub_txt, -1, &rc_sub, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

    // Button [ 🎨 Vào bản vẽ (Canvas) ] (bo nhẹ 6px)
    HBRUSH br_btn_canvas = CreateSolidBrush(col_teal);
    SelectObject(mem_dc, br_btn_canvas);
    RoundRect(mem_dc, w - 240, 55, w - 40, 88, 6, 6);
    DeleteObject(br_btn_canvas);

    SelectObject(mem_dc, font_h2);
    SetTextColor(mem_dc, is_dark ? RGB(7, 11, 16) : RGB(255, 255, 255));
    RECT rc_bc_txt = { w - 240, 55, w - 40, 88 };
    DrawTextW(mem_dc, is_vi ? L"🎨 Vào bản vẽ (Canvas)" : L"🎨 Return to Canvas", -1, &rc_bc_txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Home Navigation Tabs (y: 110 to 142)
    const wchar_t* tab_names_vi[] = { L"✨ Bắt đầu nhanh", L"⏱ Tệp gần đây (5)", L"📐 Mẫu thi công", L"⌨ Phím tắt Corel" };
    const wchar_t* tab_names_en[] = { L"✨ Get Started", L"⏱ Recent Files (5)", L"📐 Templates", L"⌨ Corel Shortcuts" };
    int tab_ws[] = { 140, 150, 180, 180 };
    int cur_tx = 180;
    for (int i = 0; i < 4; ++i) {
        RECT rc_tab = { cur_tx, 110, cur_tx + tab_ws[i], 142 };
        if (active_home_tab_ == i) {
            HBRUSH br_act = CreateSolidBrush(col_active_tab);
            HPEN p_act = CreatePen(PS_SOLID, 1, col_teal);
            SelectObject(mem_dc, p_act);
            SelectObject(mem_dc, br_act);
            RoundRect(mem_dc, rc_tab.left, rc_tab.top, rc_tab.right, rc_tab.bottom, 6, 6);
            DeleteObject(p_act);
            DeleteObject(br_act);
            SetTextColor(mem_dc, col_teal);
            SelectObject(mem_dc, font_h2);
        } else {
            HBRUSH br_inact = CreateSolidBrush(col_header);
            HPEN p_inact = CreatePen(PS_SOLID, 1, col_border);
            SelectObject(mem_dc, p_inact);
            SelectObject(mem_dc, br_inact);
            RoundRect(mem_dc, rc_tab.left, rc_tab.top, rc_tab.right, rc_tab.bottom, 6, 6);
            DeleteObject(p_inact);
            DeleteObject(br_inact);
            SetTextColor(mem_dc, col_text_muted);
            SelectObject(mem_dc, font_body);
        }
        DrawTextW(mem_dc, is_vi ? tab_names_vi[i] : tab_names_en[i], -1, &rc_tab, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        cur_tx += tab_ws[i] + 6;
    }

    // Tab 0 Content: Báº¯t Ä‘áº§u nhanh
    if (active_home_tab_ == 0) {
        struct QuickCard {
            const wchar_t* icon;
            const wchar_t* title;
            const wchar_t* desc;
        };
        const QuickCard qcards[] = {
            { L"[ + ]", L"Tạo bản vẽ mới (Ctrl+N)", L"Khởi tạo tài liệu vector kích thước tự do theo đơn vị mm chuẩn thi công" },
            { L"[ 📐 ]", L"Tạo từ mẫu thi công", L"Kho mẫu chuẩn: Biển alu, hộp đèn, standee cuốn, bạt in hiflex 3M" },
            { L"[ 📂 ]", L"Mở tệp đã có (Ctrl+O)", L"Định dạng có adapter: .pr, .svg, .dxf, PNG/JPEG/TIFF" },
            { L"[ ☁ ]", L"Thư viện đám mây Cloud", L"Đồng bộ thiết kế, bản vẽ duyệt khách hàng và file cắt CNC từ xưởng" }
        };
        for (int i = 0; i < 4; ++i) {
            int cx = 180 + i * 260;
            RECT rc_c = { cx, 160, cx + 245, 280 };
            HBRUSH br_c = CreateSolidBrush(col_header);
            HPEN p_c = CreatePen(PS_SOLID, 1, col_border);
            SelectObject(mem_dc, p_c);
            SelectObject(mem_dc, br_c);
            RoundRect(mem_dc, rc_c.left, rc_c.top, rc_c.right, rc_c.bottom, 8, 8); // bo nhẹ 8px
            DeleteObject(p_c);
            DeleteObject(br_c);

            // Icon
            SelectObject(mem_dc, font_h2);
            SetTextColor(mem_dc, col_teal);
            RECT rc_ci = { cx + 16, 175, cx + 80, 205 };
            DrawTextW(mem_dc, qcards[i].icon, -1, &rc_ci, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Title
            SelectObject(mem_dc, font_h2);
            SetTextColor(mem_dc, col_text_white);
            RECT rc_ct = { cx + 16, 210, cx + 230, 232 };
            DrawTextW(mem_dc, qcards[i].title, -1, &rc_ct, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

            // Subtitle
            SelectObject(mem_dc, font_sub);
            SetTextColor(mem_dc, col_text_muted);
            RECT rc_cd = { cx + 16, 235, cx + 230, 275 };
            DrawTextW(mem_dc, qcards[i].desc, -1, &rc_cd, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        }

        // Section Recent preview
        SelectObject(mem_dc, font_h2);
        SetTextColor(mem_dc, col_teal);
        RECT rc_sec = { 180, 310, 600, 335 };
        DrawTextW(mem_dc, is_vi ? L"CÁC BẢN VẼ GẦN ĐÂY NHẤT" : L"RECENT VECTOR DRAWINGS", -1, &rc_sec, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        const wchar_t* r_names[] = { L"Bien_Hieu_An_Phat.pik", L"Hop_Den_Hiflex_Techcom.pik", L"Chu_Noi_Inox_Vang_Guong.pik" };
        const wchar_t* r_dims[] = { L"1,850.0 × 420.0 mm (Biển alu mica LED)", L"3,000.0 × 1,200.0 mm (Bạt hiflex 3M)", L"800.0 × 350.0 mm (Cắt laser inox CNC)" };
        for (int i = 0; i < 3; ++i) {
            int rx = 180 + i * 350;
            RECT rc_rc = { rx, 345, rx + 330, 430 };
            HBRUSH br_rc = CreateSolidBrush(col_header);
            HPEN p_rc = CreatePen(PS_SOLID, 1, col_border);
            SelectObject(mem_dc, p_rc);
            SelectObject(mem_dc, br_rc);
            RoundRect(mem_dc, rc_rc.left, rc_rc.top, rc_rc.right, rc_rc.bottom, 8, 8);
            DeleteObject(p_rc);
            DeleteObject(br_rc);

            SelectObject(mem_dc, font_h2);
            SetTextColor(mem_dc, col_text_white);
            RECT rc_rn = { rx + 16, 360, rx + 315, 385 };
            DrawTextW(mem_dc, r_names[i], -1, &rc_rn, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(mem_dc, font_mono);
            SetTextColor(mem_dc, col_teal);
            RECT rc_rd = { rx + 16, 390, rx + 315, 415 };
            DrawTextW(mem_dc, r_dims[i], -1, &rc_rd, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        }
    } else if (active_home_tab_ == 1) {
        // Tab 1: Tá»‡p gáº§n Ä‘Ã¢y (5 rows, bo nhẹ 6px)
        const wchar_t* r_files[] = {
            L"Bien_Hieu_An_Phat.pik",
            L"Hop_Den_Hiflex_Techcom.pik",
            L"Chu_Noi_Inox_Vang_Guong.pik",
            L"Standee_Cuon_Su_Kien_2026.pik",
            L"Tem_Nhan_Decal_Cuon.pik"
        };
        const wchar_t* r_dims[] = {
            L"1,850.0 × 420.0 mm",
            L"3,000.0 × 1,200.0 mm",
            L"800.0 × 350.0 mm",
            L"800.0 × 2,000.0 mm",
            L"500.0 × 500.0 mm"
        };
        const wchar_t* r_types[] = {
            L"Biển alu chữ nổi mica đèn LED",
            L"Bạt xuyên sáng hiflex 3M in UV",
            L"File cắt laser CNC kim loại tấm",
            L"Bạt PP cán màng mờ chân cuốn",
            L"Decal bế demi chống nước ngoài trời"
        };
        const wchar_t* r_times[] = {
            L"5 phút trước",
            L"2 giờ trước",
            L"Hôm qua",
            L"3 ngày trước",
            L"Tuần trước"
        };
        for (int i = 0; i < 5; ++i) {
            int ry = 160 + i * 55;
            RECT rc_row = { 180, ry, w - 180, ry + 46 };
            HBRUSH br_row = CreateSolidBrush(col_header);
            HPEN p_row = CreatePen(PS_SOLID, 1, col_border);
            SelectObject(mem_dc, p_row);
            SelectObject(mem_dc, br_row);
            RoundRect(mem_dc, rc_row.left, rc_row.top, rc_row.right, rc_row.bottom, 6, 6); // bo nhẹ 6px
            DeleteObject(p_row);
            DeleteObject(br_row);

            // Doc icon
            SelectObject(mem_dc, font_mono);
            SetTextColor(mem_dc, col_teal);
            RECT rc_ico = { 195, ry, 235, ry + 46 };
            DrawTextW(mem_dc, L"[PIK]", -1, &rc_ico, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Name
            SelectObject(mem_dc, font_h2);
            SetTextColor(mem_dc, col_text_white);
            RECT rc_fn = { 245, ry, 530, ry + 46 };
            DrawTextW(mem_dc, r_files[i], -1, &rc_fn, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Dims
            SelectObject(mem_dc, font_mono);
            SetTextColor(mem_dc, col_teal);
            RECT rc_dim = { 540, ry, 720, ry + 46 };
            DrawTextW(mem_dc, r_dims[i], -1, &rc_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Category chip
            SelectObject(mem_dc, font_body);
            SetTextColor(mem_dc, col_text_muted);
            RECT rc_tp = { 730, ry, 1050, ry + 46 };
            DrawTextW(mem_dc, r_types[i], -1, &rc_tp, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Time
            SelectObject(mem_dc, font_sub);
            SetTextColor(mem_dc, col_text_muted);
            RECT rc_tm = { w - 320, ry, w - 195, ry + 46 };
            DrawTextW(mem_dc, r_times[i], -1, &rc_tm, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    } else if (active_home_tab_ == 2) {
        // Tab 2: Máº«u thi cÃ´ng & in áº¥n chuáº©n
        struct TplCard {
            const wchar_t* name;
            const wchar_t* dim;
            const wchar_t* desc;
            const wchar_t* tag;
        };
        const TplCard tpls[] = {
            { L"Biển hiệu ngoài trời", L"6,000 × 3,000 mm", L"Khung sắt hộp mạ kẽm, bạt Hiflex / Alu ngoài trời", L"Khổ lớn" },
            { L"Tấm Alu chuẩn thi công", L"1,220 × 2,440 mm", L"Khổ alu chuẩn quy cách nhà máy cắt phay CNC", L"Alu CNC" },
            { L"Hộp đèn mica hút nổi", L"600 × 600 mm", L"Mặt mica đúc nổi 3D, đèn LED module hắt sáng", L"Hộp đèn" },
            { L"Standee cuốn sự kiện", L"800 × 2,000 mm", L"Bạt PP cán màng mờ, chân nhôm cuốn tiêu chuẩn", L"Standee" },
            { L"Card visit xưởng in", L"90 × 54 mm", L"Khổ ghép bài in offset 4 màu, cán màng mờ 2 mặt", L"In offset" },
            { L"Decal bế tem nhãn", L"1,000 × 1,000 mm", L"Khổ bế tem decal nhựa kỹ thuật số chống nước", L"Bế Decal" }
        };
        for (int i = 0; i < 6; ++i) {
            int row = i / 3;
            int col = i % 3;
            int cx = 180 + col * 360;
            int cy = 160 + row * 120;
            RECT rc_tc = { cx, cy, cx + 340, cy + 105 };
            HBRUSH br_tc = CreateSolidBrush(col_header);
            HPEN p_tc = CreatePen(PS_SOLID, 1, col_border);
            SelectObject(mem_dc, p_tc);
            SelectObject(mem_dc, br_tc);
            RoundRect(mem_dc, rc_tc.left, rc_tc.top, rc_tc.right, rc_tc.bottom, 8, 8); // bo nhẹ 8px
            DeleteObject(p_tc);
            DeleteObject(br_tc);

            // Name
            SelectObject(mem_dc, font_h2);
            SetTextColor(mem_dc, col_text_white);
            RECT rc_tn = { cx + 16, cy + 12, cx + 250, cy + 34 };
            DrawTextW(mem_dc, tpls[i].name, -1, &rc_tn, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

            // Tag badge
            RECT rc_tag = { cx + 255, cy + 12, cx + 325, cy + 32 };
            HBRUSH br_tag = CreateSolidBrush(col_active_tab);
            SelectObject(mem_dc, br_tag);
            RoundRect(mem_dc, rc_tag.left, rc_tag.top, rc_tag.right, rc_tag.bottom, 4, 4);
            DeleteObject(br_tag);
            SelectObject(mem_dc, font_sub);
            SetTextColor(mem_dc, col_teal);
            DrawTextW(mem_dc, tpls[i].tag, -1, &rc_tag, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Dim
            SelectObject(mem_dc, font_mono);
            SetTextColor(mem_dc, col_teal);
            RECT rc_td = { cx + 16, cy + 38, cx + 320, cy + 58 };
            DrawTextW(mem_dc, tpls[i].dim, -1, &rc_td, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

            // Desc
            SelectObject(mem_dc, font_sub);
            SetTextColor(mem_dc, col_text_muted);
            RECT rc_desc = { cx + 16, cy + 62, cx + 324, cy + 98 };
            DrawTextW(mem_dc, tpls[i].desc, -1, &rc_desc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        }
    } else {
        // Tab 3: PhÃ­m táº¯t CorelDRAW chuyá»ƒn tiáº¿p
        struct ScDef {
            const wchar_t* sc;
            const wchar_t* action;
            const wchar_t* note;
        };
        const ScDef scs[] = {
            { L"F4",         L"Zoom toàn bộ bản vẽ (Fit All Objects)",         L"Hiển thị tất cả đối tượng trên canvas" },
            { L"P",          L"Căn giữa vào tâm trang in (Center to Page)",    L"Đưa các đối tượng được chọn vào trung tâm" },
            { L"Ctrl + Q",   L"Chuyển đổi thành đường cong (Convert to Curves)",L"Rã phông chữ và hình học thành Bézier" },
            { L"Ctrl + K",   L"Rã khối đối tượng (Break Apart)",               L"Tách rời các đường cong tổ hợp" },
            { L"Shift + F2", L"Phóng to vùng chọn (Zoom to Selection)",        L"Lấy nét tối đa vào chi tiết đang chọn" },
            { L"F10",        L"Chỉnh sửa điểm Node (Node Edit Tool)",          L"Thao tác trực tiếp các tiếp tuyến Bézier" },
            { L"Ctrl + G",   L"Nhóm đối tượng (Group Objects)",                L"Gộp nhiều phần tử thành một nhóm" },
            { L"Ctrl + D",   L"Nhân đôi đối tượng (Duplicate)",                L"Tạo bản sao dịch chuyển theo bước offset" }
        };

        int sy = 160;
        RECT rc_card = { 180, sy, w - 180, sy + 320 };
        HBRUSH br_c = CreateSolidBrush(col_header);
        HPEN p_c = CreatePen(PS_SOLID, 1, col_border);
        SelectObject(mem_dc, p_c);
        SelectObject(mem_dc, br_c);
        RoundRect(mem_dc, rc_card.left, rc_card.top, rc_card.right, rc_card.bottom, 8, 8); // bo nhẹ 8px
        DeleteObject(p_c);
        DeleteObject(br_c);

        for (int i = 0; i < 8; ++i) {
            int row_y = sy + 10 + i * 38;
            // Shortcut key chip
            RECT rc_k = { 200, row_y, 310, row_y + 28 };
            HBRUSH br_k = CreateSolidBrush(col_active_tab);
            HPEN p_k = CreatePen(PS_SOLID, 1, col_teal);
            SelectObject(mem_dc, p_k);
            SelectObject(mem_dc, br_k);
            RoundRect(mem_dc, rc_k.left, rc_k.top, rc_k.right, rc_k.bottom, 5, 5); // bo nhẹ 5px
            DeleteObject(p_k);
            DeleteObject(br_k);

            SelectObject(mem_dc, font_mono);
            SetTextColor(mem_dc, col_teal);
            DrawTextW(mem_dc, scs[i].sc, -1, &rc_k, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Action
            SelectObject(mem_dc, font_h2);
            SetTextColor(mem_dc, col_text_white);
            RECT rc_act = { 325, row_y, 750, row_y + 28 };
            DrawTextW(mem_dc, scs[i].action, -1, &rc_act, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Note
            SelectObject(mem_dc, font_sub);
            SetTextColor(mem_dc, col_text_muted);
            RECT rc_nt = { 760, row_y, w - 200, row_y + 28 };
            DrawTextW(mem_dc, scs[i].note, -1, &rc_nt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    DeleteObject(font_h1);
    DeleteObject(font_h2);
    DeleteObject(font_body);
    DeleteObject(font_sub);
    DeleteObject(font_mono);
    DeleteObject(pen_bdr);
}

#endif

} // namespace pikdraw::desktop
