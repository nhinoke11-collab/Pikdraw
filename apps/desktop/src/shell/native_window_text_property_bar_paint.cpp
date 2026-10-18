#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_text_properties.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace pikdraw::desktop {

void NativeWindow::paint_text_property_bar(HDC mem_dc, int w, int content_top, int topbar_h, int tabbar_h) {
    ensure_ui_fonts();
    const bool is_dark = (theme_ == ThemeMode::CyberDark);
    const bool is_vi = (language_ == UILanguage::Vietnamese);

    const COLORREF col_btn_bg = is_dark ? RGB(19, 28, 43) : RGB(241, 245, 249);
    const COLORREF col_btn_bdr = is_dark ? RGB(37, 50, 72) : RGB(203, 213, 225);
    const COLORREF col_teal = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    const COLORREF col_text_white = is_dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    const COLORREF col_text_muted = is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    const COLORREF col_active_tab = is_dark ? RGB(22, 37, 54) : RGB(236, 242, 248);
    const int inspect_w = (active_dock_tab_ >= 0) ? 280 : 0;

    const auto snapshot = root().document().current_snapshot();
    const auto& selection = root().vector_controller().selection().selected_ids();

    auto draw_rib_btn = [&](int left, int right, const wchar_t* label, bool active, COLORREF text_col) {
        RECT rect{left, topbar_h + tabbar_h + 4, right, content_top - 4};
        HBRUSH brush = CreateSolidBrush(active ? col_active_tab : col_btn_bg);
        HPEN pen = CreatePen(PS_SOLID, 1, active ? col_teal : col_btn_bdr);
        HGDIOBJ old_p = SelectObject(mem_dc, pen);
        HGDIOBJ old_b = SelectObject(mem_dc, brush);
        RoundRect(mem_dc, rect.left, rect.top, rect.right, rect.bottom, 4, 4);
        SelectObject(mem_dc, old_p);
        SelectObject(mem_dc, old_b);
        DeleteObject(pen);
        DeleteObject(brush);
        SetTextColor(mem_dc, text_col);
        SelectObject(mem_dc, active ? font_be_vietnam_bold_ : font_be_vietnam_);
        DrawTextW(mem_dc, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    };

    auto draw_num_input = [&](int left, int right, const wchar_t* prefix,
                              const std::string& current_val, NumericFieldId field_id) {
        const bool is_focused = (active_num_field_ == field_id);
        RECT rect{left, topbar_h + tabbar_h + 4, right, content_top - 4};
        HBRUSH brush = CreateSolidBrush(is_focused ? col_active_tab : col_btn_bg);
        HPEN pen = CreatePen(PS_SOLID, is_focused ? 2 : 1, is_focused ? col_teal : col_btn_bdr);
        HGDIOBJ old_p = SelectObject(mem_dc, pen);
        HGDIOBJ old_b = SelectObject(mem_dc, brush);
        RoundRect(mem_dc, rect.left, rect.top, rect.right, rect.bottom, 4, 4);
        SelectObject(mem_dc, old_p);
        SelectObject(mem_dc, old_b);
        DeleteObject(pen);
        DeleteObject(brush);

        std::wstring display_text;
        if (prefix && *prefix) {
            display_text += prefix;
            display_text += L" ";
        }
        if (is_focused) {
            display_text += utf8_to_wide(num_input_buffer_);
            display_text += L"|";
        } else {
            display_text += utf8_to_wide(current_val);
        }

        SetTextColor(mem_dc, is_focused ? col_teal : col_text_white);
        SelectObject(mem_dc, font_mono_);
        RECT rc_txt = { rect.left + 4, rect.top, rect.right - 4, rect.bottom };
        DrawTextW(mem_dc, display_text.c_str(), -1, &rc_txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    };

    // Label: "Chữ:" / "Text:"
    SelectObject(mem_dc, font_be_vietnam_bold_);
    SetTextColor(mem_dc, col_teal);
    RECT rc_t_lbl = { 10, topbar_h + tabbar_h, 48, content_top };
    DrawTextW(mem_dc, is_vi ? L"Chữ:" : L"Text:", -1, &rc_t_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 1. Font Family Selector Button
    const auto text_view = text_properties_view(*this);
    const auto family = text_view.context
        ? (text_view.mixed_font ? std::string{"Mixed fonts"} : text_view.family)
        : text_font_family_at(font_family_index_);
    const auto actual_font = utf8_to_wide(family) + L" ▾";
    draw_rib_btn(52, 192, actual_font.c_str(), false, col_text_white);

    // 2. Font Size pt Controls
    RECT rc_sz_lbl = { 198, topbar_h + tabbar_h, 218, content_top };
    SetTextColor(mem_dc, col_text_muted);
    DrawTextW(mem_dc, L"Pt:", -1, &rc_sz_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    draw_rib_btn(220, 240, L"-", false, col_teal);
    std::wstring sz_str = text_view.context
        ? (text_view.mixed_font ? L"Mixed" : std::to_wstring(static_cast<int>(std::round(text_view.size_pt))) + L" pt")
        : std::to_wstring(static_cast<int>(std::round(font_size_pt_))) + L" pt";
    draw_num_input(242, 292, L"", NativeWindow::wide_to_utf8(sz_str), text_numeric_field(TextPropertyField::FontSize));
    draw_rib_btn(294, 314, L"+", false, col_teal);

    // 3. Styles: B, I, U
    draw_rib_btn(322, 350, L"B", font_bold_, font_bold_ ? col_teal : col_text_white);
    draw_rib_btn(354, 382, L"I", font_italic_, font_italic_ ? col_teal : col_text_white);
    draw_rib_btn(386, 414, L"U", font_underline_, font_underline_ ? col_teal : col_text_white);

    // 4. Alignment: Left, Center, Right
    draw_rib_btn(422, 462, L"|◀ L", font_alignment_ == typography::TextAlignment::Left,
                 font_alignment_ == typography::TextAlignment::Left ? col_teal : col_text_white);
    draw_rib_btn(466, 506, L"▶|◀ C", font_alignment_ == typography::TextAlignment::Center,
                 font_alignment_ == typography::TextAlignment::Center ? col_teal : col_text_white);
    draw_rib_btn(510, 550, L"▶| R", font_alignment_ == typography::TextAlignment::Right,
                 font_alignment_ == typography::TextAlignment::Right ? col_teal : col_text_white);

    // 5. Convert to Curves & Table (Maintained for full test compatibility)
    draw_rib_btn(556, 616, L"⤹ Q", false, col_teal);
    draw_rib_btn(622, 692, L"▦ Table", false, col_teal);

    // 6. Extended Typography Controls (CorelDRAW / Word Reference)
    draw_rib_btn(698, 728, L"S", font_strikethrough_, font_strikethrough_ ? col_teal : col_text_white);
    draw_rib_btn(732, 762, L"•=", false, col_text_white);
    draw_rib_btn(766, 796, L"1.", false, col_text_white);
    draw_rib_btn(800, 826, L"¶", show_non_printing_characters_,
                 show_non_printing_characters_ ? col_teal : col_text_white);
    draw_rib_btn(830, 864, L"A⇄¶", false, col_teal);

    // 7. Delete Button (if selection active)
    if (!selection.empty() && 868 + 46 <= w - inspect_w) {
        draw_rib_btn(868, 914, is_vi ? L"✕ Xóa" : L"✕ Del", false, RGB(239, 68, 68));
    }

    // 8. Opacity Field (if 1 object selected)
    if (selection.size() == 1 && workflow().can_set_selected_object_opacity() &&
        920 + 120 <= w - inspect_w) {
        const auto appearance = selection_appearance(*snapshot, selection);
        std::ostringstream val_ss;
        val_ss << std::fixed << std::setprecision(1) << appearance.object_opacity * 100.0;
        std::string op_text = val_ss.str();
        if (op_text.size() > 2 && op_text.substr(op_text.size() - 2) == ".0")
            op_text.erase(op_text.size() - 2);
        op_text += "%";

        draw_num_input(920, 1040, is_vi ? L"Độ mờ:" : L"Opacity:",
                       op_text, NumericFieldId::ObjectOpacity);
    }
}

} // namespace pikdraw::desktop
#endif
