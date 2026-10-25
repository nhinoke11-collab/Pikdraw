#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_file_menu.hpp"
#include "native_window_menus.hpp"
#include "native_window_page_presets.hpp"
#include "native_window_length_input.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>

namespace pikdraw::desktop {

#ifdef _WIN32

namespace {
const wchar_t* menu_label(const MenuBarEntry& entry, bool vi) {
    return vi ? entry.label_vi : entry.label_en;
}

int menu_item_natural_width(const MenuBarEntry& entry, bool vi) {
    const int characters = static_cast<int>(std::wcslen(menu_label(entry, vi)));
    return std::clamp(characters * 6 + 16, 32, 92);
}
} // namespace

RECT NativeWindow::get_menu_bar_item_rect(int menu_index, int topbar_h) const noexcept {
    if (menu_index < 0 || menu_index >= static_cast<int>(kMenuBarEntries.size()))
        return RECT{};
    const bool vi = language_ == UILanguage::Vietnamese;
    int natural_total = 0;
    for (const auto& entry : kMenuBarEntries)
        natural_total += menu_item_natural_width(entry, vi);
    const int available = std::max(1, width_ - 260);
    const double factor = std::min(1.0, static_cast<double>(available) / natural_total);
    int left = 148;
    for (int index = 0; index < menu_index; ++index)
        left += std::max(24, static_cast<int>(std::lround(
            menu_item_natural_width(kMenuBarEntries[static_cast<size_t>(index)], vi) * factor)));
    const int item_width = std::max(24, static_cast<int>(std::lround(
        menu_item_natural_width(kMenuBarEntries[static_cast<size_t>(menu_index)], vi) * factor)));
    return RECT{left, 5, left + item_width, topbar_h - 4};
}

int NativeWindow::get_menu_bar_item_at(int mouse_x, int mouse_y, int topbar_h) const noexcept {
    for (int index = 0; index < static_cast<int>(kMenuBarEntries.size()); ++index) {
        const RECT rect = get_menu_bar_item_rect(index, topbar_h);
        if (mouse_x >= rect.left && mouse_x < rect.right &&
            mouse_y >= rect.top && mouse_y < rect.bottom)
            return index;
    }
    return -1;
}

void NativeWindow::paint_main_menu_bar(HDC dc, int width, int topbar_h) {
    const bool vi = language_ == UILanguage::Vietnamese;
    const bool dark = theme_ == ThemeMode::CyberDark;
    HFONT font = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    const auto old_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    for (int index = 0; index < static_cast<int>(kMenuBarEntries.size()); ++index) {
        const RECT rect = get_menu_bar_item_rect(index, topbar_h);
        if (rect.left >= width) break;
        const bool active = index == open_menu_id_;
        const bool hovered = index == hovered_menu_bar_item_;
        if (active || hovered) {
            RECT active_rect = rect;
            InflateRect(&active_rect, -2, -2);
            const COLORREF background = dark ? RGB(22, 45, 62) : RGB(236, 253, 245);
            const COLORREF accent = dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
            const auto brush = CreateSolidBrush(background);
            const auto pen = CreatePen(PS_SOLID, 1, active ? accent : background);
            const auto old_pen = SelectObject(dc, pen);
            const auto old_brush = SelectObject(dc, brush);
            RoundRect(dc, active_rect.left, active_rect.top, active_rect.right,
                      active_rect.bottom, 5, 5);
            SelectObject(dc, old_brush);
            SelectObject(dc, old_pen);
            DeleteObject(pen);
            DeleteObject(brush);
            SetTextColor(dc, accent);
        } else {
            SetTextColor(dc, dark ? RGB(226, 232, 240) : RGB(30, 41, 59));
        }
        RECT label_rect = rect;
        InflateRect(&label_rect, -3, 0);
        DrawTextW(dc, menu_label(kMenuBarEntries[static_cast<size_t>(index)], vi), -1,
                  &label_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    RECT snap_button{width - 104, 5, width - 8, topbar_h - 4};
    const int snap_split = width - 36;
    const COLORREF snap_accent = dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    const COLORREF snap_background = show_snap_to_popup_
        ? (dark ? RGB(22, 45, 62) : RGB(236, 253, 245))
        : (dark ? RGB(18, 28, 42) : RGB(241, 245, 249));
    const auto snap_brush = CreateSolidBrush(snap_background);
    const auto snap_pen = CreatePen(PS_SOLID, 1, show_snap_to_popup_ ? snap_accent
        : (dark ? RGB(45, 58, 73) : RGB(203, 213, 225)));
    const auto old_snap_pen = SelectObject(dc, snap_pen);
    const auto old_snap_brush = SelectObject(dc, snap_brush);
    RoundRect(dc, snap_button.left, snap_button.top, snap_button.right, snap_button.bottom, 5, 5);
    SelectObject(dc, old_snap_brush); SelectObject(dc, old_snap_pen);
    DeleteObject(snap_brush); DeleteObject(snap_pen);
    const auto snap_config = drawing_tool_ ? drawing_tool_->snap_config_snapshot()
        : vector::precision::snap::SnapConfigSnapshot{};
    SetTextColor(dc, snap_config.master_enabled ? snap_accent : (dark ? RGB(148, 163, 184) : RGB(100, 116, 139)));
    RECT snap_text{snap_button.left, snap_button.top, snap_split, snap_button.bottom};
    DrawTextW(dc, L"Snap", -1, &snap_text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    const auto split_pen = CreatePen(PS_SOLID, 1, dark ? RGB(45, 58, 73) : RGB(203, 213, 225));
    const auto old_split_pen = SelectObject(dc, split_pen);
    MoveToEx(dc, snap_split, snap_button.top + 4, nullptr);
    LineTo(dc, snap_split, snap_button.bottom - 4);
    SelectObject(dc, old_split_pen);
    DeleteObject(split_pen);
    RECT arrow_text{snap_split, snap_button.top, snap_button.right, snap_button.bottom};
    DrawTextW(dc, L"▾", -1, &arrow_text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old_font);
    DeleteObject(font);
}

void NativeWindow::paint_dropdown_menu(HDC dc, int menu_id, int x, int y) {
    if (menu_id < 0 || menu_id >= static_cast<int>(MenuId::Count)) return;
    const auto items = get_menu_items(static_cast<MenuId>(menu_id));
    if (items.empty()) return;
    constexpr int width = kMenuPopupWidth;
    const int height = get_menu_height(static_cast<MenuId>(menu_id));
    x = std::clamp(x, 4, std::max(4, width_ - width - 4));
    y = std::clamp(y, 4, std::max(4, height_ - height - 4));
    const bool dark = theme_ == ThemeMode::CyberDark;
    const bool vi = language_ == UILanguage::Vietnamese;
    const COLORREF background = dark ? RGB(12, 19, 29) : RGB(255, 255, 255);
    const COLORREF border = dark ? RGB(45, 58, 73) : RGB(203, 213, 225);
    const COLORREF normal = dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    const COLORREF muted = dark ? RGB(125, 139, 155) : RGB(148, 163, 184);
    const COLORREF accent = dark ? RGB(0, 242, 195) : RGB(0, 135, 106);

    const auto shadow = CreateSolidBrush(dark ? RGB(2, 4, 8) : RGB(200, 208, 216));
    RECT shadow_rect{x + 4, y + 4, x + width + 4, y + height + 4};
    FillRect(dc, &shadow_rect, shadow);
    DeleteObject(shadow);
    const auto brush = CreateSolidBrush(background);
    const auto pen = CreatePen(PS_SOLID, 1, border);
    const auto old_pen = SelectObject(dc, pen);
    const auto old_brush = SelectObject(dc, brush);
    RoundRect(dc, x, y, x + width, y + height, 8, 8);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
    DeleteObject(brush);

    HFONT font = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT heading_font = CreateFontW(12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    const auto previous_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    int row_y = y + kMenuPadding;
    for (const auto& item : items) {
        const int row_height = item.is_separator ? kMenuSeparatorHeight : kMenuRowHeight;
        if (item.is_separator) {
            const auto separator = CreatePen(PS_SOLID, 1, border);
            const auto previous = SelectObject(dc, separator);
            MoveToEx(dc, x + 12, row_y + row_height / 2, nullptr);
            LineTo(dc, x + width - 12, row_y + row_height / 2);
            SelectObject(dc, previous);
            DeleteObject(separator);
        } else {
            const bool hovered = static_cast<int>(item.action) == hovered_menu_item_ ||
                (item.has_submenu && static_cast<int>(item.action) == open_submenu_action_);
            const bool enabled = !item.is_disabled && is_menu_action_enabled(item.action);
            const COLORREF label_color = item.is_heading ? muted : (!enabled ? muted : (hovered ? accent : normal));
            if (hovered && enabled) {
                RECT row{x + 4, row_y, x + width - 4, row_y + row_height};
                const auto row_brush = CreateSolidBrush(dark ? RGB(22, 45, 62) : RGB(236, 253, 245));
                FillRect(dc, &row, row_brush);
                DeleteObject(row_brush);
            }
            RECT label_rect{x + (item.is_checkable ? 34 : 14), row_y, x + width - 110, row_y + row_height};
            RECT shortcut_rect{x + width - 106, row_y,
                x + width - (item.has_submenu ? 34 : 14), row_y + row_height};
            if (item.is_heading) SelectObject(dc, heading_font);
            SetTextColor(dc, label_color);
            DrawTextW(dc, vi ? item.label_vi : item.label_en, -1, &label_rect,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            if (item.shortcut && *item.shortcut) {
                SetTextColor(dc, muted);
                DrawTextW(dc, item.shortcut, -1, &shortcut_rect,
                          DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            if (item.is_checkable) {
                RECT check_rect{x + 12, row_y, x + 30, row_y + row_height};
                SetTextColor(dc, enabled ? accent : muted);
                DrawTextW(dc, is_menu_action_checked(item.action) ? L"✓" : L"□", -1, &check_rect,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            if (item.has_submenu) {
                RECT arrow_rect{x + width - 30, row_y, x + width - 10, row_y + row_height};
                SetTextColor(dc, enabled ? (hovered ? accent : muted) : muted);
                DrawTextW(dc, L"›", -1, &arrow_rect,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            if (item.is_heading) SelectObject(dc, font);
        }
        row_y += row_height;
    }
    if (menu_id == static_cast<int>(MenuId::Text)) paint_text_case_submenu(dc);
    SelectObject(dc, previous_font);
    DeleteObject(heading_font);
    DeleteObject(font);
}

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
        const auto dim_label = format_length_pair_mm(
            entry.width_mm, entry.height_mm, display_length_unit_, 4);
        DrawTextW(dc, dim_label.c_str(), -1, &dim_rect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        row_y += kPagePresetRowHeight;
    }

    SelectObject(dc, old_font);
    DeleteObject(font_name);
    DeleteObject(font_dims);
}

#endif

} // namespace pikdraw::desktop
