#pragma once

#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/presenters/object_style.hpp"
#include "native_style_dialog.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>

namespace pikdraw::desktop {

struct DropShadowPreset {
    const wchar_t* name_vi;
    const wchar_t* name_en;
    const char* label_vi;
    const char* label_en;
    double dx_mm;
    double dy_mm;
    double sigma_mm;
    double opacity;
};

inline constexpr DropShadowPreset kDropShadowPresets[] = {
    {L"Dưới-Phải", L"Bottom-Right", "Dưới-Phải", "Bottom-Right", 2.0, -2.0, 2.0, 0.50},
    {L"Dưới-Trái", L"Bottom-Left", "Dưới-Trái", "Bottom-Left", -2.0, -2.0, 2.0, 0.50},
    {L"Trên-Phải", L"Top-Right", "Trên-Phải", "Top-Right", 2.0, 2.0, 2.0, 0.50},
    {L"Trên-Trái", L"Top-Left", "Trên-Trái", "Top-Left", -2.0, 2.0, 2.0, 0.50},
    {L"Tỏa bóng", L"Soft Glow", "Tỏa bóng", "Soft Glow", 0.0, 0.0, 4.0, 0.60},
    {L"Đổ đậm", L"Hard Cast", "Đổ đậm", "Hard Cast", 3.0, -3.0, 1.0, 0.75},
};

inline const wchar_t* drop_shadow_blend_mode_name(document::BlendMode mode, bool is_vi) {
    switch (mode) {
    case document::BlendMode::Normal: return is_vi ? L"Bình thường" : L"Normal";
    case document::BlendMode::Multiply: return is_vi ? L"Nhân" : L"Multiply";
    case document::BlendMode::Screen: return is_vi ? L"Màn hình" : L"Screen";
    case document::BlendMode::Overlay: return is_vi ? L"Phủ" : L"Overlay";
    case document::BlendMode::Darken: return is_vi ? L"Làm tối" : L"Darken";
    case document::BlendMode::Lighten: return is_vi ? L"Làm sáng" : L"Lighten";
    default: return is_vi ? L"Khác" : L"Other";
    }
}

struct DropShadowRibbon {
    static bool get_shadow(NativeWindow& win, size_t& effect_index, document::DropShadowEffect& shadow) {
        const auto snapshot = win.root().document().current_snapshot();
        if (!snapshot) return false;
        const auto ids = win.root().vector_controller().selection().selected_ids();
        if (ids.size() != 1) return false;
        const auto summary = selection_appearance(*snapshot, ids);
        if (summary.can_edit_effect(win.property_effect_index_)) {
            if (const auto* s = std::get_if<document::DropShadowEffect>(
                    &summary.effects[win.property_effect_index_].parameters)) {
                effect_index = win.property_effect_index_;
                shadow = *s;
                const auto draft = win.workflow().effect_draft();
                if (draft && draft->editing_existing) {
                    if (const auto* ds = std::get_if<document::DropShadowEffect>(&draft->parameters))
                        shadow = *ds;
                }
                return true;
            }
        }
        for (size_t i = 0; i < summary.effects.size(); ++i) {
            if (const auto* s = std::get_if<document::DropShadowEffect>(&summary.effects[i].parameters)) {
                effect_index = i;
                win.property_effect_index_ = i;
                shadow = *s;
                const auto draft = win.workflow().effect_draft();
                if (draft && draft->editing_existing) {
                    if (const auto* ds = std::get_if<document::DropShadowEffect>(&draft->parameters))
                        shadow = *ds;
                }
                return true;
            }
        }
        return false;
    }

    template <typename DrawRibBtn, typename DrawNumInput>
    static void paint(NativeWindow& win, HDC mem_dc, int w,
                      int topbar_h, int tabbar_h, int content_top, int inspect_w,
                      COLORREF col_teal, COLORREF col_text_white, COLORREF col_text_muted,
                      COLORREF col_border, bool is_dark, bool is_vi,
                      DrawRibBtn&& draw_rib_btn, DrawNumInput&& draw_num_input) {
        (void)w; (void)inspect_w; (void)col_text_muted; (void)col_border; (void)is_dark;
        size_t effect_index = 0;
        document::DropShadowEffect shadow;
        if (!get_shadow(win, effect_index, shadow)) return;

        // 1. Label
        SelectObject(mem_dc, win.font_be_vietnam_bold_);
        SetTextColor(mem_dc, col_teal);
        RECT rc_lbl = {10, topbar_h + tabbar_h, 75, content_top};
        DrawTextW(mem_dc, is_vi ? L"◐ Bóng:" : L"◐ Shadow:", -1, &rc_lbl,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // 2. Preset Button
        int cur_preset = -1;
        for (int i = 0; i < 6; ++i) {
            if (std::abs(shadow.dx_mm - kDropShadowPresets[i].dx_mm) < 0.15 &&
                std::abs(shadow.dy_mm - kDropShadowPresets[i].dy_mm) < 0.15) {
                cur_preset = i;
                break;
            }
        }
        std::wstring preset_txt = (cur_preset >= 0)
            ? (std::wstring(is_vi ? kDropShadowPresets[cur_preset].name_vi : kDropShadowPresets[cur_preset].name_en) + L" ▾")
            : (is_vi ? L"Mẫu ▾" : L"Preset ▾");
        draw_rib_btn(80, 155, preset_txt.c_str(), cur_preset >= 0, cur_preset >= 0 ? col_teal : col_text_white);

        // 3. Offset X & Y, Feather, Opacity
        std::ostringstream sx, sy, ssigma, sopacity;
        sx << std::fixed << std::setprecision(1) << shadow.dx_mm;
        sy << std::fixed << std::setprecision(1) << shadow.dy_mm;
        ssigma << std::fixed << std::setprecision(1) << shadow.sigma_x_mm;
        sopacity << std::fixed << std::setprecision(0) << (shadow.opacity * 100.0);

        draw_num_input(160, 245, L"X:", sx.str(), NumericFieldId::EffectOffsetX, false);
        draw_num_input(250, 335, L"Y:", sy.str(), NumericFieldId::EffectOffsetY, false);
        draw_num_input(340, 435, is_vi ? L"Nhòe:" : L"Feather:", ssigma.str(), NumericFieldId::EffectSigmaX, false);
        draw_num_input(440, 530, is_vi ? L"Mờ:" : L"Opacity:", sopacity.str() + "%", NumericFieldId::EffectOpacity, false);

        // 4. Color Swatch
        const auto rgb = color::to_preview_rgba(shadow.color);
        COLORREF col_swatch = RGB(
            std::clamp(static_cast<int>(std::round(rgb.r * 255.0)), 0, 255),
            std::clamp(static_cast<int>(std::round(rgb.g * 255.0)), 0, 255),
            std::clamp(static_cast<int>(std::round(rgb.b * 255.0)), 0, 255));
        draw_rib_btn(535, 615, is_vi ? L"■ Màu" : L"■ Color", false, col_swatch);

        // 5. Blend Mode
        const wchar_t* mode_name = drop_shadow_blend_mode_name(shadow.blend_mode, is_vi);
        std::wstring mode_display = std::wstring(mode_name) + L" ▾";
        draw_rib_btn(620, 720, mode_display.c_str(), shadow.blend_mode != document::BlendMode::Normal,
                     shadow.blend_mode != document::BlendMode::Normal ? col_teal : col_text_white);

        // 6. Clear Shadow
        draw_rib_btn(725, 795, is_vi ? L"✕ Xóa" : L"✕ Clear", false, RGB(239, 68, 68));

        // 7. Toggle Object
        draw_rib_btn(800, 885, is_vi ? L"⛶ Vật thể" : L"⛶ Object", false, col_teal);
    }

    static bool handle_click(NativeWindow& win, int x, int y, int w, int inspect_w) {
        (void)w; (void)inspect_w;
        if (y < 66 || y > 102) return false;
        size_t effect_index = 0;
        document::DropShadowEffect shadow;
        if (!get_shadow(win, effect_index, shadow)) return false;

        win.numeric_effect_index_ = effect_index;
        const bool is_vi = win.language_ == UILanguage::Vietnamese;

        // Preset button [80, 155]
        if (x >= 80 && x <= 155) {
            win.commit_numeric_field();
            int cur = -1;
            for (int i = 0; i < 6; ++i) {
                if (std::abs(shadow.dx_mm - kDropShadowPresets[i].dx_mm) < 0.15 &&
                    std::abs(shadow.dy_mm - kDropShadowPresets[i].dy_mm) < 0.15) {
                    cur = i;
                    break;
                }
            }

            if (!win.hwnd_) {
                const int next = (cur + 1) % 6;
                const auto& p = kDropShadowPresets[next];
                if (win.workflow().begin_selected_effect_edit_draft(effect_index)) {
                    auto updated = shadow;
                    updated.dx_mm = p.dx_mm;
                    updated.dy_mm = p.dy_mm;
                    updated.sigma_x_mm = p.sigma_mm;
                    updated.sigma_y_mm = p.sigma_mm;
                    updated.opacity = p.opacity;
                    if (win.workflow().set_effect_draft_parameters(updated) &&
                        win.workflow().apply_effect_draft()) {
                        win.status_msg_ = is_vi
                            ? std::string("Đã áp dụng mẫu bóng: ") + p.label_vi + " · Ctrl+Z hoàn tác"
                            : std::string("Applied shadow preset: ") + p.label_en + " · Ctrl+Z to undo";
                    }
                }
                return true;
            }

            HMENU menu = CreatePopupMenu();
            if (menu) {
                for (int i = 0; i < 6; ++i) {
                    const wchar_t* name = is_vi ? kDropShadowPresets[i].name_vi : kDropShadowPresets[i].name_en;
                    UINT flags = MF_STRING;
                    if (cur == i) flags |= MF_CHECKED;
                    AppendMenuW(menu, flags, 1000 + i, name);
                }
                POINT pt{win.dip(80), win.dip(102)};
                ClientToScreen(win.hwnd_, &pt);
                const int chosen = TrackPopupMenu(
                    menu,
                    TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
                    pt.x, pt.y, 0, win.hwnd_, nullptr);
                DestroyMenu(menu);

                if (chosen >= 1000 && chosen < 1006) {
                    const int selected_idx = chosen - 1000;
                    const auto& p = kDropShadowPresets[selected_idx];
                    if (win.workflow().begin_selected_effect_edit_draft(effect_index)) {
                        auto updated = shadow;
                        updated.dx_mm = p.dx_mm;
                        updated.dy_mm = p.dy_mm;
                        updated.sigma_x_mm = p.sigma_mm;
                        updated.sigma_y_mm = p.sigma_mm;
                        updated.opacity = p.opacity;
                        if (win.workflow().set_effect_draft_parameters(updated) &&
                            win.workflow().apply_effect_draft()) {
                            win.status_msg_ = is_vi
                                ? std::string("Đã áp dụng mẫu bóng: ") + p.label_vi + " · Ctrl+Z hoàn tác"
                                : std::string("Applied shadow preset: ") + p.label_en + " · Ctrl+Z to undo";
                        }
                    }
                    if (win.hwnd_) InvalidateRect(win.hwnd_, nullptr, FALSE);
                }
            }
            return true;
        }

        // Offset X [160, 245]
        if (x >= 160 && x <= 245) {
            win.start_numeric_input(NumericFieldId::EffectOffsetX);
            return true;
        }

        // Offset Y [250, 335]
        if (x >= 250 && x <= 335) {
            win.start_numeric_input(NumericFieldId::EffectOffsetY);
            return true;
        }

        // Feather / Nhòe [340, 435]
        if (x >= 340 && x <= 435) {
            win.start_numeric_input(NumericFieldId::EffectSigmaX);
            return true;
        }

        // Opacity [440, 530]
        if (x >= 440 && x <= 530) {
            win.start_numeric_input(NumericFieldId::EffectOpacity);
            return true;
        }

        // Color Swatch [535, 615]
        if (x >= 535 && x <= 615) {
            win.commit_numeric_field();
            const auto snapshot = win.root().document().current_snapshot();
            if (snapshot) {
                if (const auto edited = edit_source_color(
                        win.hwnd_, shadow.color, 0.0, false,
                        is_vi, ColorEditContext::Generic,
                        false, false, snapshot->color_mode())) {
                    if (win.workflow().set_selected_effect_color(effect_index, edited->color)) {
                        win.status_msg_ = is_vi
                            ? "Đã cập nhật màu bóng đổ · Ctrl+Z hoàn tác"
                            : "Updated shadow color · Ctrl+Z to undo";
                    }
                }
            }
            if (win.hwnd_) InvalidateRect(win.hwnd_, nullptr, FALSE);
            return true;
        }

        // Blend Mode [620, 720]
        if (x >= 620 && x <= 720) {
            win.commit_numeric_field();
            constexpr document::BlendMode kModes[] = {
                document::BlendMode::Normal,
                document::BlendMode::Multiply,
                document::BlendMode::Screen,
                document::BlendMode::Overlay,
                document::BlendMode::Darken,
                document::BlendMode::Lighten,
            };
            int cur_mode_idx = 0;
            for (int i = 0; i < 6; ++i) {
                if (shadow.blend_mode == kModes[i]) {
                    cur_mode_idx = i;
                    break;
                }
            }

            if (!win.hwnd_) {
                const auto next_mode = kModes[(cur_mode_idx + 1) % 6];
                if (win.workflow().set_selected_effect_blend_mode(effect_index, next_mode)) {
                    win.status_msg_ = is_vi
                        ? "Đã đổi chế độ hòa trộn bóng: " + std::string(next_mode == document::BlendMode::Multiply ? "Nhân" : "Hòa trộn") + " · Ctrl+Z hoàn tác"
                        : "Changed shadow blend mode · Ctrl+Z to undo";
                }
                return true;
            }

            HMENU menu = CreatePopupMenu();
            if (menu) {
                for (int i = 0; i < 6; ++i) {
                    const wchar_t* name = drop_shadow_blend_mode_name(kModes[i], is_vi);
                    UINT flags = MF_STRING;
                    if (cur_mode_idx == i) flags |= MF_CHECKED;
                    if (!win.workflow().can_set_selected_effect_blend_mode(effect_index, kModes[i])) {
                        flags |= MF_GRAYED | MF_DISABLED;
                    }
                    AppendMenuW(menu, flags, 2000 + i, name);
                }

                POINT pt{win.dip(620), win.dip(102)};
                ClientToScreen(win.hwnd_, &pt);
                const int chosen = TrackPopupMenu(
                    menu,
                    TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
                    pt.x, pt.y, 0, win.hwnd_, nullptr);
                DestroyMenu(menu);

                if (chosen >= 2000 && chosen < 2006) {
                    const int selected_mode_idx = chosen - 2000;
                    const auto new_mode = kModes[selected_mode_idx];
                    if (win.workflow().set_selected_effect_blend_mode(effect_index, new_mode)) {
                        const wchar_t* name = drop_shadow_blend_mode_name(new_mode, is_vi);
                        win.status_msg_ = is_vi
                            ? "Đã đổi chế độ hòa trộn bóng: " + NativeWindow::wide_to_utf8(name) + " · Ctrl+Z hoàn tác"
                            : "Changed shadow blend mode: " + NativeWindow::wide_to_utf8(name) + " · Ctrl+Z to undo";
                    }
                    if (win.hwnd_) InvalidateRect(win.hwnd_, nullptr, FALSE);
                }
            }
            return true;
        }

        // Clear Shadow [725, 795]
        if (x >= 725 && x <= 795) {
            win.commit_numeric_field();
            win.ribbon_drop_shadow_mode_ = false;
            if (win.workflow().remove_selected_effect(effect_index)) {
                win.status_msg_ = is_vi
                    ? "Đã xóa bóng đổ khỏi đối tượng · Ctrl+Z hoàn tác"
                    : "Removed drop shadow from object · Ctrl+Z to undo";
            }
            if (win.hwnd_) InvalidateRect(win.hwnd_, nullptr, FALSE);
            return true;
        }

        // Toggle Object [800, 885]
        if (x >= 800 && x <= 885) {
            win.commit_numeric_field();
            win.ribbon_drop_shadow_mode_ = false;
            win.status_msg_ = is_vi
                ? "Đang hiển thị thanh thuộc tính Vật thể"
                : "Showing Object properties";
            if (win.hwnd_) InvalidateRect(win.hwnd_, nullptr, FALSE);
            return true;
        }

        win.commit_numeric_field();
        return true;
    }
};

} // namespace pikdraw::desktop

#endif
