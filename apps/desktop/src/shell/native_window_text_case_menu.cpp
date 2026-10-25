#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_menus.hpp"

#include <algorithm>
#include <optional>

#ifdef _WIN32
#include <windows.h>

namespace pikdraw::desktop {
namespace {
bool contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

} // namespace

void NativeWindow::clear_text_case_submenu_tooltip() noexcept {
    tooltip_.visible = false;
    tooltip_.text.clear();
}

RECT NativeWindow::text_case_menu_parent_rect() const noexcept {
    if (open_menu_id_ != static_cast<int>(MenuId::Text)) return RECT{};
    const RECT anchor = get_menu_bar_item_rect(open_menu_id_, 38);
    const int menu_height = get_menu_height(MenuId::Text);
    const int menu_x = std::clamp(static_cast<int>(anchor.left), 4,
                                  std::max(4, width_ - kMenuPopupWidth - 4));
    const int menu_y = std::clamp(38, 4, std::max(4, height_ - menu_height - 4));
    const int row_top = get_menu_item_top(MenuId::Text, MenuActionId::TextChangeCase);
    if (row_top < 0) return RECT{};
    const int top = menu_y + row_top;
    return RECT{menu_x, top, menu_x + kMenuPopupWidth, top + kMenuRowHeight};
}

RECT NativeWindow::text_case_submenu_rect() const noexcept {
    if (open_submenu_action_ != static_cast<int>(MenuActionId::TextChangeCase)) return RECT{};
    const RECT parent = text_case_menu_parent_rect();
    if (parent.right <= parent.left) return RECT{};
    const int submenu_height = get_menu_height(get_menu_submenu_items(MenuActionId::TextChangeCase));
    int left = parent.right - 1;
    if (left + kMenuPopupWidth > width_ - 4)
        left = parent.left - kMenuPopupWidth + 1;
    left = std::clamp(left, 4, std::max(4, width_ - kMenuPopupWidth - 4));
    const int top = std::clamp(static_cast<int>(parent.top), 4,
                               std::max(4, height_ - submenu_height - 4));
    return RECT{left, top, left + kMenuPopupWidth, top + submenu_height};
}

void NativeWindow::paint_text_case_submenu(HDC dc) {
    if (open_submenu_action_ != static_cast<int>(MenuActionId::TextChangeCase)) return;
    const auto items = get_menu_submenu_items(MenuActionId::TextChangeCase);
    if (items.empty()) return;
    const RECT bounds = text_case_submenu_rect();
    const bool dark = theme_ == ThemeMode::CyberDark;
    const bool vi = language_ == UILanguage::Vietnamese;
    const COLORREF background = dark ? RGB(12, 19, 29) : RGB(255, 255, 255);
    const COLORREF border = dark ? RGB(45, 58, 73) : RGB(203, 213, 225);
    const COLORREF normal = dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    const COLORREF muted = dark ? RGB(125, 139, 155) : RGB(148, 163, 184);
    const COLORREF accent = dark ? RGB(0, 242, 195) : RGB(0, 135, 106);

    const auto shadow = CreateSolidBrush(dark ? RGB(2, 4, 8) : RGB(200, 208, 216));
    RECT shadow_rect{bounds.left + 4, bounds.top + 4, bounds.right + 4, bounds.bottom + 4};
    FillRect(dc, &shadow_rect, shadow);
    DeleteObject(shadow);
    const auto brush = CreateSolidBrush(background);
    const auto pen = CreatePen(PS_SOLID, 1, border);
    const auto old_pen = SelectObject(dc, pen);
    const auto old_brush = SelectObject(dc, brush);
    RoundRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom, 8, 8);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
    DeleteObject(brush);

    HFONT font = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    const auto previous_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    int row_y = bounds.top + kMenuPadding;
    for (const auto& item : items) {
        const bool enabled = !item.is_disabled && is_menu_action_enabled(item.action);
        const bool hovered = static_cast<int>(item.action) == hovered_submenu_item_;
        if (hovered && enabled) {
            RECT row{bounds.left + 4, row_y, bounds.right - 4, row_y + kMenuRowHeight};
            const auto row_brush = CreateSolidBrush(dark ? RGB(22, 45, 62) : RGB(236, 253, 245));
            FillRect(dc, &row, row_brush);
            DeleteObject(row_brush);
        }
        RECT label_rect{bounds.left + 14, row_y, bounds.right - 14, row_y + kMenuRowHeight};
        SetTextColor(dc, enabled ? (hovered ? accent : normal) : muted);
        DrawTextW(dc, vi ? item.label_vi : item.label_en, -1, &label_rect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        row_y += kMenuRowHeight;
    }
    SelectObject(dc, previous_font);
    DeleteObject(font);
}

bool NativeWindow::handle_text_case_submenu_mouse_move(int x, int y) {
    if (open_menu_id_ != static_cast<int>(MenuId::Text)) return false;
    const auto parent = text_case_menu_parent_rect();
    const auto submenu = text_case_submenu_rect();
    const auto items = get_menu_submenu_items(MenuActionId::TextChangeCase);
    if (contains(parent, x, y)) {
        const bool changed = open_submenu_action_ != static_cast<int>(MenuActionId::TextChangeCase) ||
            hovered_menu_item_ != static_cast<int>(MenuActionId::TextChangeCase) || hovered_submenu_item_ != -1;
        open_submenu_action_ = static_cast<int>(MenuActionId::TextChangeCase);
        hovered_menu_item_ = static_cast<int>(MenuActionId::TextChangeCase);
        hovered_submenu_item_ = -1;
        clear_text_case_submenu_tooltip();
        if (changed && hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (contains(submenu, x, y)) {
        const auto* item = get_menu_item_at(items, y - submenu.top);
        const int hovered = item ? static_cast<int>(item->action) : -1;
        const bool changed = hovered_submenu_item_ != hovered ||
            hovered_menu_item_ != static_cast<int>(MenuActionId::TextChangeCase);
        hovered_menu_item_ = static_cast<int>(MenuActionId::TextChangeCase);
        hovered_submenu_item_ = hovered;
        if (item && !is_menu_action_enabled(item->action)) {
            tooltip_.visible = true;
            tooltip_.x = x;
            tooltip_.y = y;
            tooltip_.text = language_ == UILanguage::Vietnamese
                ? L"Chọn đối tượng Text chưa khóa; khi soạn, áp dụng lên vùng bôi đen hoặc toàn bộ chữ."
                : L"Select unlocked text objects; while editing, applies to the selection or all text.";
        } else {
            clear_text_case_submenu_tooltip();
        }
        if (changed && hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    const int row_top = get_menu_item_top(MenuId::Text, MenuActionId::TextChangeCase);
    const int root_top = parent.top - row_top;
    const int root_height = get_menu_height(MenuId::Text);
    const RECT root{parent.left, root_top, parent.right, root_top + root_height};
    if (contains(root, x, y)) {
        const auto* item = get_menu_item_at(MenuId::Text, y - root.top);
        if (item && item->has_submenu) return true;
    }
    if (open_submenu_action_ != -1 || hovered_submenu_item_ != -1) {
        open_submenu_action_ = -1;
        hovered_submenu_item_ = -1;
        clear_text_case_submenu_tooltip();
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    }
    return false;
}

bool NativeWindow::handle_text_case_submenu_mouse_down(int x, int y) {
    if (open_menu_id_ != static_cast<int>(MenuId::Text)) return false;
    if (contains(text_case_submenu_rect(), x, y)) {
        const auto* item = get_menu_item_at(
            get_menu_submenu_items(MenuActionId::TextChangeCase), y - text_case_submenu_rect().top);
        const auto action = item ? std::optional<MenuActionId>(item->action) : std::nullopt;
        close_menu();
        if (action && is_menu_action_enabled(*action)) execute_menu_action(static_cast<int>(*action));
        return true;
    }
    if (contains(text_case_menu_parent_rect(), x, y)) {
        open_submenu_action_ = static_cast<int>(MenuActionId::TextChangeCase);
        hovered_menu_item_ = static_cast<int>(MenuActionId::TextChangeCase);
        hovered_submenu_item_ = -1;
        clear_text_case_submenu_tooltip();
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    return false;
}

bool NativeWindow::handle_text_case_submenu_key(unsigned key) {
    if (open_menu_id_ != static_cast<int>(MenuId::Text)) return false;
    const auto items = get_menu_submenu_items(MenuActionId::TextChangeCase);
    if (open_submenu_action_ == static_cast<int>(MenuActionId::TextChangeCase)) {
        if (key == VK_ESCAPE || key == VK_LEFT) {
            open_submenu_action_ = -1;
            hovered_submenu_item_ = -1;
            hovered_menu_item_ = static_cast<int>(MenuActionId::TextChangeCase);
            clear_text_case_submenu_tooltip();
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == VK_UP || key == VK_DOWN) {
            const auto action = get_adjacent_menu_action(items, hovered_submenu_item_,
                key == VK_UP ? -1 : 1,
                [this](MenuActionId candidate) { return is_menu_action_enabled(candidate); });
            hovered_submenu_item_ = action ? static_cast<int>(*action) : -1;
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == VK_RETURN || key == VK_SPACE) {
            const auto action = hovered_submenu_item_ >= 0
                ? std::optional(static_cast<MenuActionId>(hovered_submenu_item_))
                : get_adjacent_menu_action(items, -1, 1,
                    [this](MenuActionId candidate) { return is_menu_action_enabled(candidate); });
            close_menu();
            if (action && is_menu_action_enabled(*action)) execute_menu_action(static_cast<int>(*action));
            return true;
        }
        if (key == VK_RIGHT) return true;
        return false;
    }

    if (hovered_menu_item_ == static_cast<int>(MenuActionId::TextChangeCase) &&
        (key == VK_RIGHT || key == VK_RETURN || key == VK_SPACE)) {
        open_submenu_action_ = static_cast<int>(MenuActionId::TextChangeCase);
        hovered_submenu_item_ = -1;
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    return false;
}

} // namespace pikdraw::desktop
#endif
