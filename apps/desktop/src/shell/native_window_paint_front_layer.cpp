#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/shell/native_font_picker.hpp"

#include "native_window_page_presets.hpp"
#include "native_window_page_navigation.hpp"
#include "native_window_menus.hpp"

namespace pikdraw::desktop {

#ifdef _WIN32

void NativeWindow::paint_topmost_ui_layer(HDC dc, int width, int height) {
    const int saved_dc = SaveDC(dc);
    configure_ui_dc(dc);
    ensure_ui_fonts();

    const int topbar_height = 38;
    const int tabbar_height = 28;
    const int ribbon_height = 36;
    SelectObject(dc, font_be_vietnam_);

    if (show_settings_modal_)
        paint_settings_modal(dc, width, height);
    if (show_new_doc_modal_)
        paint_new_document_modal(dc, width, height);
    if (show_recovery_modal_)
        paint_recovery_modal(dc, width, height);

    if (open_menu_id_ >= 0) {
        const RECT anchor = get_menu_bar_item_rect(open_menu_id_, topbar_height);
        paint_dropdown_menu(dc, open_menu_id_, anchor.left, topbar_height);
    }
    if (show_snap_to_popup_)
        paint_snap_to_popup(dc, width, topbar_height);
    if (show_page_preset_dropdown_)
        paint_page_preset_dropdown(dc, 52,
            topbar_height + tabbar_height + ribbon_height, kPagePresetDropdownWidth);

    if (active_tool_ == 15)
        paint_eyedropper_cursor_overlay(dc, width, height);

    if (tooltip_.visible && !tooltip_.text.empty()) {
        SelectObject(dc, font_secondary_ ? font_secondary_ : font_be_vietnam_);
        RECT measured{0, 0, 0, 0};
        DrawTextW(dc, tooltip_.text.c_str(), -1, &measured,
                  DT_CALCRECT | DT_NOPREFIX);
        const int padding_x = 10;
        const int padding_y = 6;
        const int tip_width = measured.right - measured.left + padding_x * 2;
        const int tip_height = measured.bottom - measured.top + padding_y * 2;
        int tip_x = tooltip_.x + 12;
        int tip_y = tooltip_.y + 18;
        if (tip_x + tip_width > width - 8)
            tip_x = width - tip_width - 8;
        if (tip_x < 8)
            tip_x = 8;
        if (tip_y + tip_height > height - 28)
            tip_y = tooltip_.y - tip_height - 6;
        if (tip_y < 8)
            tip_y = 8;

        RECT shadow{tip_x + 2, tip_y + 2,
                    tip_x + tip_width + 2, tip_y + tip_height + 2};
        const HBRUSH shadow_brush = CreateSolidBrush(RGB(15, 23, 42));
        const HPEN shadow_pen = CreatePen(PS_SOLID, 1, RGB(15, 23, 42));
        const HGDIOBJ old_shadow_pen = SelectObject(dc, shadow_pen);
        const HGDIOBJ old_shadow_brush = SelectObject(dc, shadow_brush);
        RoundRect(dc, shadow.left, shadow.top, shadow.right, shadow.bottom, 6, 6);
        SelectObject(dc, old_shadow_pen);
        SelectObject(dc, old_shadow_brush);
        DeleteObject(shadow_pen);
        DeleteObject(shadow_brush);

        const HBRUSH tip_brush = CreateSolidBrush(RGB(24, 24, 27));
        const HPEN tip_pen = CreatePen(PS_SOLID, 1, RGB(0, 242, 195));
        const HGDIOBJ old_tip_pen = SelectObject(dc, tip_pen);
        const HGDIOBJ old_tip_brush = SelectObject(dc, tip_brush);
        RoundRect(dc, tip_x, tip_y, tip_x + tip_width, tip_y + tip_height, 6, 6);
        SelectObject(dc, old_tip_pen);
        SelectObject(dc, old_tip_brush);
        DeleteObject(tip_pen);
        DeleteObject(tip_brush);

        RECT text_rect{tip_x + padding_x, tip_y + padding_y,
                       tip_x + tip_width - padding_x,
                       tip_y + tip_height - padding_y};
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        DrawTextW(dc, tooltip_.text.c_str(), -1, &text_rect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    if (font_picker_ && font_picker_->is_open())
        font_picker_->paint(dc);
    RestoreDC(dc, saved_dc);
}

#endif

}  // namespace pikdraw::desktop
