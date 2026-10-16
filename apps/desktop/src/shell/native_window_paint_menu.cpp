#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_file_menu.hpp"
#include "native_window_page_presets.hpp"
#include <cmath>

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
        if (is_file_menu_separator(entry.action)) {
            HPEN separator = CreatePen(PS_SOLID, 1, border);
            HGDIOBJ previous = SelectObject(dc, separator);
            MoveToEx(dc, x + 12, row_y + kFileMenuSeparatorHeight / 2, nullptr);
            LineTo(dc, x + width - 12, row_y + kFileMenuSeparatorHeight / 2);
            SelectObject(dc, previous);
            DeleteObject(separator);
            row_y += kFileMenuSeparatorHeight;
            continue;
        }

        RECT label_rect{x + 14, row_y, x + width - 102, row_y + kFileMenuRowHeight};
        RECT shortcut_rect{x + width - 100, row_y, x + width - 12, row_y + kFileMenuRowHeight};
        bool is_accent = (entry.action <= FileMenuAction::ExportSvg ||
                          entry.action == FileMenuAction::ToggleGuidelines);
        SetTextColor(dc, is_accent ? accent : text);

        std::wstring display_str;
        if (entry.action == FileMenuAction::ToggleGuidelines) {
            if (show_guidelines_) {
                display_str = is_vi ? L"✓ Hiện đường gióng" : L"✓ Show Guidelines";
            } else {
                display_str = is_vi ? L"   Hiện đường gióng" : L"   Show Guidelines";
            }
        } else {
            display_str = is_vi ? entry.label_vi : entry.label_en;
        }

        DrawTextW(dc, display_str.c_str(), -1, &label_rect,
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

void NativeWindow::paint_page_preset_dropdown(HDC dc, int x, int y, int width) {
    const bool is_dark = (theme_ == ThemeMode::CyberDark);
    const COLORREF background = is_dark ? RGB(12, 19, 29) : RGB(255, 255, 255);
    const COLORREF border = is_dark ? RGB(29, 38, 54) : RGB(203, 213, 225);
    const COLORREF text_pri = is_dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    const COLORREF text_mut = is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    const COLORREF accent = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    const COLORREF active_bg = is_dark ? RGB(22, 45, 62) : RGB(236, 253, 245);

    const int height = page_preset_dropdown_height();

    // Drop shadow
    HBRUSH shadow = CreateSolidBrush(is_dark ? RGB(2, 4, 8) : RGB(200, 208, 216));
    RECT shadow_rect{x + 4, y + 4, x + width + 4, y + height + 4};
    FillRect(dc, &shadow_rect, shadow);
    DeleteObject(shadow);

    // Dropdown container
    HBRUSH menu_brush = CreateSolidBrush(background);
    HPEN menu_pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ old_pen = SelectObject(dc, menu_pen);
    HGDIOBJ old_brush = SelectObject(dc, menu_brush);
    RoundRect(dc, x, y, x + width, y + height, 8, 8);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(menu_pen);
    DeleteObject(menu_brush);

    HFONT font_name = CreateFontW(13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT font_dims = CreateFontW(11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ old_font = SelectObject(dc, font_name);

    const auto snapshot = root().document().current_snapshot();
    const double pw = snapshot->page().width_mm;
    const double ph = snapshot->page().height_mm;

    int row_y = y + kPagePresetPadding;
    for (size_t i = 0; i < kPagePresetEntries.size(); ++i) {
        const auto& entry = kPagePresetEntries[i];
        bool is_match = (std::abs(pw - entry.width_mm) < 1.0 && std::abs(ph - entry.height_mm) < 1.0) ||
                        (std::abs(pw - entry.height_mm) < 1.0 && std::abs(ph - entry.width_mm) < 1.0);

        RECT item_rect{x + 4, row_y, x + width - 4, row_y + kPagePresetRowHeight};
        if (is_match) {
            HBRUSH act_br = CreateSolidBrush(active_bg);
            HPEN act_pen = CreatePen(PS_SOLID, 1, accent);
            HGDIOBJ old_p = SelectObject(dc, act_pen);
            HGDIOBJ old_b = SelectObject(dc, act_br);
            RoundRect(dc, item_rect.left, item_rect.top, item_rect.right, item_rect.bottom, 4, 4);
            SelectObject(dc, old_p);
            SelectObject(dc, old_b);
            DeleteObject(act_pen);
            DeleteObject(act_br);
        }

        // Draw Name (left)
        SelectObject(dc, font_name);
        SetTextColor(dc, is_match ? accent : text_pri);
        RECT name_rect{item_rect.left + 8, item_rect.top, item_rect.left + 105, item_rect.bottom};
        std::wstring name_str = is_match ? (std::wstring(L"✓ ") + entry.name) : entry.name;
        DrawTextW(dc, name_str.c_str(), -1, &name_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Draw Dimensions (right)
        SelectObject(dc, font_dims);
        SetTextColor(dc, is_match ? accent : text_mut);
        RECT dim_rect{item_rect.left + 105, item_rect.top, item_rect.right - 8, item_rect.bottom};
        DrawTextW(dc, entry.dim_label, -1, &dim_rect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        row_y += kPagePresetRowHeight;
    }

    SelectObject(dc, old_font);
    DeleteObject(font_name);
    DeleteObject(font_dims);
}

#endif

} // namespace pikdraw::desktop
