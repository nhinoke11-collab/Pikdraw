#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_file_menu.hpp"

namespace pikdraw::desktop {

#ifdef _WIN32

void NativeWindow::paint_file_menu(HDC dc, int x, int y, int width, int height) {
    const bool is_dark = theme_ == ThemeMode::CyberDark;
    const bool is_vi = language_ == UILanguage::Vietnamese;
    const COLORREF background = is_dark ? RGB(12, 19, 29) : RGB(255, 255, 255);
    const COLORREF border = is_dark ? RGB(29, 38, 54) : RGB(203, 213, 225);
    const COLORREF text = is_dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    const COLORREF muted = is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    const COLORREF accent = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);

    HBRUSH shadow = CreateSolidBrush(is_dark ? RGB(2, 4, 8) : RGB(200, 208, 216));
    RECT shadow_rect{x + 4, y + 4, x + width + 4, y + height + 4};
    FillRect(dc, &shadow_rect, shadow);
    DeleteObject(shadow);

    HBRUSH menu_brush = CreateSolidBrush(background);
    HPEN menu_pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ old_pen = SelectObject(dc, menu_pen);
    HGDIOBJ old_brush = SelectObject(dc, menu_brush);
    RoundRect(dc, x, y, x + width, y + height, 8, 8);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(menu_pen);
    DeleteObject(menu_brush);

    HFONT font = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT shortcut_font = CreateFontW(11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ old_font = SelectObject(dc, font);

    int row_y = y + kFileMenuPadding;
    for (const auto& entry : kFileMenuEntries) {
        if (entry.action == FileMenuAction::Separator) {
            HPEN separator = CreatePen(PS_SOLID, 1, border);
            HGDIOBJ previous = SelectObject(dc, separator);
            MoveToEx(dc, x + 12, row_y + kFileMenuSeparatorHeight / 2, nullptr);
            LineTo(dc, x + width - 12, row_y + kFileMenuSeparatorHeight / 2);
            SelectObject(dc, previous);
            DeleteObject(separator);
            row_y += kFileMenuSeparatorHeight;
            continue;
        }

        const int row_index = static_cast<int>(entry.action);
        RECT label_rect{x + 14, row_y, x + width - 102, row_y + kFileMenuRowHeight};
        RECT shortcut_rect{x + width - 100, row_y, x + width - 12, row_y + kFileMenuRowHeight};
        SetTextColor(dc, row_index <= static_cast<int>(FileMenuAction::ExportSvg)
                            ? accent : text);
        DrawTextW(dc, is_vi ? entry.label_vi : entry.label_en, -1, &label_rect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, shortcut_font);
        SetTextColor(dc, muted);
        DrawTextW(dc, entry.shortcut, -1, &shortcut_rect,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, font);
        row_y += kFileMenuRowHeight;
    }

    SelectObject(dc, old_font);
    DeleteObject(font);
    DeleteObject(shortcut_font);
}

#endif

} // namespace pikdraw::desktop
