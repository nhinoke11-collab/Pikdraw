#include "pikdraw/desktop/shell/native_window.hpp"

namespace pikdraw::desktop {

#ifdef _WIN32

void NativeWindow::paint_file_menu(HDC mem_dc, int x, int y, int w, int h) {
    bool is_dark = (theme_ == ThemeMode::CyberDark);
    bool is_vi = (language_ == UILanguage::Vietnamese);

    COLORREF col_header = is_dark ? RGB(12, 19, 29) : RGB(255, 255, 255);
    COLORREF col_border = is_dark ? RGB(29, 38, 54) : RGB(203, 213, 225);
    COLORREF col_teal = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    COLORREF col_text_white = is_dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    COLORREF col_text_muted = is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139);

    // Drop shadow
    HBRUSH br_shadow = CreateSolidBrush(is_dark ? RGB(2, 4, 8) : RGB(200, 208, 216));
    RECT rc_shadow = { x + 4, y + 4, x + w + 4, y + h + 4 };
    FillRect(mem_dc, &rc_shadow, br_shadow);
    DeleteObject(br_shadow);

    // Menu background container (bo nhẹ 6px)
    HBRUSH br_menu = CreateSolidBrush(col_header);
    HPEN pen_menu = CreatePen(PS_SOLID, 1, col_border);
    SelectObject(mem_dc, pen_menu);
    SelectObject(mem_dc, br_menu);
    RoundRect(mem_dc, x, y, x + w, y + h, 6, 6);
    DeleteObject(pen_menu);
    DeleteObject(br_menu);

    auto make_font = [](int sz, int weight, const wchar_t* name) {
        return CreateFontW(sz, 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, name);
    };
    HFONT font_item = make_font(13, FW_NORMAL, L"Be Vietnam Pro");
    HFONT font_mono = make_font(11, FW_NORMAL, L"JetBrains Mono");
    HGDIOBJ old_font = SelectObject(mem_dc, font_item);

    struct MenuItem {
        const wchar_t* icon;
        const wchar_t* text_vi;
        const wchar_t* text_en;
        const wchar_t* sc;
        bool is_sep;
        bool is_accent;
    };

    MenuItem items[] = {
        { L"📄", L"Tạo mới (New Document)", L"New Document", L"Ctrl+N", false, false },
        { L"📁", L"Tạo từ mẫu (New from Template...)", L"New from Template...", L"", false, false },
        { L"📂", L"Mở bản vẽ (Open...)", L"Open...", L"Ctrl+O", false, false },
        { L"☁", L"Mở từ đám mây (Open from Cloud)", L"Open from Cloud", L"", false, false },
        { L"🕒", L"Mở tệp gần đây (Open Recent)", L"Open Recent ▶", L"", false, false },
        { L"", L"", L"", L"", true, false },
        { L"❌", L"Đóng (Close)", L"Close", L"Ctrl+W", false, false },
        { L"🚫", L"Đóng tất cả (Close All)", L"Close All", L"", false, false },
        { L"", L"", L"", L"", true, false },
        { L"💾", L"Lưu bản vẽ (Save)", L"Save", L"Ctrl+S", false, true },
        { L"📑", L"Lưu bản sao (Save As...)", L"Save As...", L"Ctrl+Shift+S", false, false },
        { L"☁", L"Lưu lên đám mây (Save to Cloud)", L"Save to Cloud", L"", false, false },
        { L"📐", L"Lưu thành mẫu chuẩn (Save as Template)", L"Save as Template", L"", false, false },
        { L"↩", L"Khôi phục gốc (Revert)", L"Revert", L"", false, false },
        { L"", L"", L"", L"", true, false },
        { L"📷", L"Nhập từ máy quét (Acquire Image)", L"Acquire Image ▶", L"", false, false },
        { L"📥", L"Nhập tệp đồ họa (Import...)", L"Import...", L"Ctrl+I", false, true },
        { L"📤", L"Xuất file đồ họa (Export...)", L"Export...", L"Ctrl+E", false, true },
        { L"⚡", L"Xuất cho máy cắt CNC / Laser", L"Export for CNC/Cutters", L"", false, false },
        { L"📦", L"Quản lý xuất đa định dạng (Export for Office)", L"Export for Office", L"", false, false },
        { L"✂", L"Gửi trực tiếp máy cắt Decal (Send to Cutter)", L"Send to Cutter", L"", false, false },
        { L"📑", L"Xuất tài liệu in ấn PDF CMYK chuẩn xưởng", L"Publish to PDF...", L"", false, true },
        { L"", L"", L"", L"", true, false },
        { L"🖨", L"Trộn dữ liệu in tem nhãn (Print Merge)", L"Print Merge ▶", L"", false, false },
        { L"🖨", L"In ấn bản vẽ (Print...)", L"Print...", L"Ctrl+P", false, false },
        { L"🔍", L"Xem trước trang in (Print Preview)", L"Print Preview", L"", false, false },
        { L"", L"", L"", L"", true, false },
        { L"ℹ", L"Thuộc tính tài liệu (Document Properties)", L"Document Properties", L"Alt+Enter", false, false },
        { L"", L"", L"", L"", true, false },
        { L"🚪", L"Thoát PikDraw (Exit)", L"Exit", L"Alt+F4", false, false }
    };

    int cur_y = y + 4;
    for (const auto& item : items) {
        if (item.is_sep) {
            HPEN p_sep = CreatePen(PS_SOLID, 1, col_border);
            SelectObject(mem_dc, p_sep);
            MoveToEx(mem_dc, x + 8, cur_y + 3, NULL);
            LineTo(mem_dc, x + w - 8, cur_y + 3);
            DeleteObject(p_sep);
            cur_y += 7;
            continue;
        }

        SelectObject(mem_dc, font_item);
        if (item.is_accent) {
            SetTextColor(mem_dc, col_teal);
        } else {
            SetTextColor(mem_dc, col_text_white);
        }

        RECT rc_ic = { x + 8, cur_y, x + 30, cur_y + 22 };
        DrawTextW(mem_dc, item.icon, -1, &rc_ic, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        RECT rc_tx = { x + 34, cur_y, x + 175, cur_y + 22 };
        DrawTextW(mem_dc, is_vi ? item.text_vi : item.text_en, -1, &rc_tx, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        if (wcsstr(item.text_en, L"▶") != nullptr) {
            SetTextColor(mem_dc, col_text_muted);
            RECT rc_ar = { x + w - 20, cur_y, x + w - 8, cur_y + 22 };
            DrawTextW(mem_dc, L"▶", -1, &rc_ar, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        } else if (item.sc[0] != L'\0') {
            SelectObject(mem_dc, font_mono);
            SetTextColor(mem_dc, col_text_muted);
            RECT rc_sc = { x + 175, cur_y, x + w - 10, cur_y + 22 };
            DrawTextW(mem_dc, item.sc, -1, &rc_sc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        cur_y += 22;
    }

    SelectObject(mem_dc, old_font);
    DeleteObject(font_item);
    DeleteObject(font_mono);
}

#endif

} // namespace pikdraw::desktop
