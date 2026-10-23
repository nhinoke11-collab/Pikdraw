#include "native_style_dialog.hpp"
#include "native_window_menus.hpp"
#include "native_window_gradient_properties_paint.hpp"
#include "native_window_gradient_ramp.hpp"
#include "native_window_length_input.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "../tools/gradient_tool_controller.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace pikdraw::desktop {
#ifdef _WIN32
namespace {
int effect_row_height(const document::EffectParameters &parameters) {
    return std::holds_alternative<document::DropShadowEffect>(parameters) ? 246 : 118;
}

bool effect_numeric_field(NativeWindow::NumericFieldId field) {
    return field == NativeWindow::NumericFieldId::BlurSigmaX ||
           field == NativeWindow::NumericFieldId::BlurSigmaY ||
           field == NativeWindow::NumericFieldId::ShadowFeatherX ||
           field == NativeWindow::NumericFieldId::ShadowFeatherY ||
           field == NativeWindow::NumericFieldId::ShadowOffsetX ||
           field == NativeWindow::NumericFieldId::ShadowOffsetY ||
           field == NativeWindow::NumericFieldId::ShadowOpacity;
}

} // namespace

void NativeWindow::open_style_editor(bool stroke) {
    const auto snapshot = root().document().current_snapshot();
    const auto &ids = root().vector_controller().selection().selected_ids();
    const auto summary = selection_style(*snapshot, ids, default_style_);
    if (stroke && !summary.all_paths)
        return;
    bool mixed_overprint = false, first_overprint = false, have_overprint = false;
    if (!ids.empty()) for (const auto id : ids) {
        const auto *object = snapshot->objects().lookup_ref(id);
        if (!object) continue;
        std::visit([&](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            const color::ColorValue *source = nullptr;
            if constexpr (std::is_same_v<T, document::PathObject>)
                source = stroke ? &value.stroke : &value.fill;
            else if constexpr (std::is_same_v<T, document::TextObject>)
                if (!stroke) source = &value.fill;
            if (!source) return;
            const bool enabled = stroke ? source->overprint().stroke : source->overprint().fill;
            if (!have_overprint) { first_overprint = enabled; have_overprint = true; }
            else mixed_overprint = mixed_overprint || first_overprint != enabled;
        }, *object);
    }
    if (const auto edited =
            edit_source_color(hwnd_, stroke ? summary.values.stroke : summary.values.fill,
                              summary.values.width_mm, stroke, language_ == UILanguage::Vietnamese,
                              stroke ? ColorEditContext::Stroke : ColorEditContext::Fill,
                              stroke ? summary.mixed_stroke : summary.mixed_fill,
                              mixed_overprint, snapshot->color_mode(),
                              theme_ == ThemeMode::CyberDark, effective_color_preview(),
                              display_length_unit_)) {
        vector::StylePatch patch;
        if (edited->color_changed) {
            if (stroke) patch.stroke = edited->color;
            else patch.fill = edited->color;
        }
        if (stroke && edited->width_changed) patch.width_mm = edited->width_mm;
        if (edited->overprint) {
            if (stroke) patch.stroke_overprint = edited->overprint;
            else patch.fill_overprint = edited->overprint;
        }
        if (patch.fill || patch.stroke || patch.width_mm || patch.fill_overprint || patch.stroke_overprint)
            (void)apply_style_patch(patch);
    }
}

void NativeWindow::choose_stroke_width(int x, int y) {
    constexpr double widths[] = {.1, .25, .5, 1, 2, 5, 10};
    const auto menu = CreatePopupMenu();
    const bool vi = language_ == UILanguage::Vietnamese;
    AppendMenuW(menu, MF_STRING, 90, vi ? L"Không viền (None)" : L"None");
    AppendMenuW(menu, MF_STRING, 99, vi ? L"Hairline (Nét siêu mảnh 1px)" : L"Hairline (1px)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (int i = 0; i < 7; ++i) {
        const auto text = format_mm_display(widths[i], display_length_unit_, 4);
        AppendMenuW(menu, MF_STRING, 100 + i, text.c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 200,
                vi ? L"Nhập kích thước…" : L"Custom width…");
    POINT point{dip(x), dip(y)};
    ClientToScreen(hwnd_, &point);
    const auto chosen =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (chosen == 90) {
        vector::StylePatch patch;
        patch.has_stroke = false;
        patch.width_mm = 0.0;
        (void)apply_style_patch(patch);
    } else if (chosen == 99) {
        vector::StylePatch patch;
        patch.has_stroke = true;
        patch.width_mm = 0.0;
        (void)apply_style_patch(patch);
    } else if (chosen >= 100 && chosen < 107) {
        vector::StylePatch patch;
        patch.has_stroke = true;
        patch.width_mm = widths[chosen - 100];
        (void)apply_style_patch(patch);
    } else if (chosen == 200) {
        open_style_editor(true);
    }
}

int NativeWindow::style_inspector_content_height() const {
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return kAppearanceEffectListTop;
    const auto summary = selection_appearance(
        *snapshot, root().vector_controller().selection().selected_ids());
    int height = kAppearanceEffectListTop;
    if (const auto draft = workflow().effect_draft()) {
        height += std::holds_alternative<document::DropShadowEffect>(draft->parameters) ? 166 : 108;
    } else if (summary.effect_stack_compatible) {
        for (const auto &effect : summary.effects)
            height = std::min(1'000'000, height + effect_row_height(effect.parameters) + 10);
    } else if (summary.selected_count != 0) {
        height += 56;
    }
    if (!workflow().has_effect_draft() && workflow().can_add_selected_effect()) {
        int add_top = kAppearanceEffectListTop;
        if (summary.effects.empty()) add_top += 24;
        else if (!summary.effect_stack_compatible) add_top += 56;
        else
            for (const auto &effect : summary.effects)
                add_top = std::min(1'000'000, add_top + effect_row_height(effect.parameters) + 10);
        height = std::max(height, add_top + 32);
    }
    return height;
}

void NativeWindow::scroll_style_inspector(int wheel_delta) {
    const int viewport_height = std::max(0, height_ - 26 - kStylePanelTop);
    const int max_scroll = std::max(0, style_inspector_content_height() - viewport_height);
    const int notches = wheel_delta / 120;
    if (notches == 0) return;
    const int next = std::clamp(style_inspector_scroll_ - notches * 56, 0, max_scroll);
    if (next == style_inspector_scroll_) return;
    style_inspector_scroll_ = next;
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Cuộn bảng Appearance để xem thuộc tính tiếp theo"
        : "Scroll Appearance Properties to view more settings";
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

bool NativeWindow::style_numeric_field_at(int x, int y) const {
    if (y >= 66 && y <= 102) {
        if (active_num_field_ == NumericFieldId::ShadowOffsetX) return x >= 160 && x <= 245;
        if (active_num_field_ == NumericFieldId::ShadowOffsetY) return x >= 250 && x <= 335;
        if (active_num_field_ == NumericFieldId::ShadowFeatherX || active_num_field_ == NumericFieldId::ShadowFeatherY) return x >= 340 && x <= 435;
        if (active_num_field_ == NumericFieldId::ShadowOpacity) return x >= 440 && x <= 530;
    }
    const int dx = x - (width_ - 280);
    const int logical_y = y - kStylePanelTop + style_inspector_scroll_;
    if (active_num_field_ == NumericFieldId::ObjectOpacity)
        return dx >= 12 && dx <= 268 && logical_y >= 298 && logical_y <= 328;
    if (active_num_field_ == NumericFieldId::FillOpacity)
        return dx >= 12 && dx < 134 && logical_y >= 270 && logical_y < 296;
    if (active_num_field_ == NumericFieldId::StrokeOpacity)
        return dx >= 142 && dx < 268 && logical_y >= 270 && logical_y < 296;
    if (!effect_numeric_field(active_num_field_)) return false;
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return false;
    const auto summary = selection_appearance(
        *snapshot, root().vector_controller().selection().selected_ids());
    if (!summary.can_edit_effect(numeric_effect_index_)) return false;
    int node_top = kAppearanceEffectListTop;
    for (size_t index = 0; index < numeric_effect_index_; ++index)
        node_top += effect_row_height(summary.effects[index].parameters) + 10;
    const bool blur = std::holds_alternative<document::GaussianBlurEffect>(
        summary.effects[numeric_effect_index_].parameters);
    const int x_column = dx >= 54 && dx < 134;
    const int y_column = dx >= 184 && dx < 264;
    if (!x_column && !y_column) return false;
    if (blur)
        return active_num_field_ == NumericFieldId::BlurSigmaX ||
               active_num_field_ == NumericFieldId::BlurSigmaY
            ? logical_y >= node_top + 49 && logical_y < node_top + 74 : false;
    if (active_num_field_ == NumericFieldId::ShadowOffsetX ||
        active_num_field_ == NumericFieldId::ShadowOffsetY)
        return logical_y >= node_top + 49 && logical_y < node_top + 74;
    if (active_num_field_ == NumericFieldId::ShadowFeatherX ||
        active_num_field_ == NumericFieldId::ShadowFeatherY)
        return logical_y >= node_top + 79 && logical_y < node_top + 104;
    if (active_num_field_ == NumericFieldId::ShadowOpacity)
        return x_column && logical_y >= node_top + 141 && logical_y < node_top + 166;
    return false;
}

bool NativeWindow::handle_style_click(int x, int y, bool right_click) {
    if (active_dock_tab_ != 0 || !show_style_inspector_ || x < width_ - 280 ||
        y < kStylePanelTop || y >= height_ - 26 ||
        (active_tool_ == 12 && contour_tool_ && contour_tool_->active()) ||
        (active_tool_ == 13 && gradient_tool_ && gradient_tool_->active()))
        return false;
    if (workflow().cancel_stale_effect_draft()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Bản nháp hiệu ứng đã hủy vì tài liệu hoặc vùng chọn thay đổi"
            : "Effect draft cancelled because the document or selection changed";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    const int dx = x - (width_ - 280);
    const int dy = y - kStylePanelTop + style_inspector_scroll_;
    const auto snapshot = root().document().current_snapshot();
    const auto &selected_ids = root().vector_controller().selection().selected_ids();
    const auto appearance = selection_appearance(*snapshot, selected_ids);
    const auto style = selection_style(*snapshot, selected_ids, default_style_);
    const auto draft = workflow().effect_draft();
    if (dy >= 424 && dy < 476 && dx >= 12 && dx < 264) {
        const int column = (dx - 12) / 28, row = (dy - 424) / 28;
        if ((dx - 12) % 28 < 24 && (dy - 424) % 28 < 24 && column < 9 && row < 2) {
            const bool target_stroke = right_click || palette_stroke_;
            const bool show_cmyk = palette_show_cmyk_.value_or(snapshot->color_mode() == color::DocumentColorMode::Cmyk);
            (void)apply_selection_color(
                target_stroke, editor_palette(show_cmyk)[static_cast<size_t>(row * 9 + column)]);
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (dy >= 356 && dy < 380) {
        if (dx >= 12 && dx < 134)
            palette_show_cmyk_ = true;
        else if (dx >= 142 && dx < 268)
            palette_show_cmyk_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (right_click)
        return true;
    if (dy >= 270 && dy < 296 && dx >= 12 && dx < 134) {
        start_numeric_input(NumericFieldId::FillOpacity);
    } else if (dy >= 270 && dy < 296 && dx >= 142 && dx < 268) {
        start_numeric_input(NumericFieldId::StrokeOpacity);
    } else if (dy >= 298 && dy < 328 && dx >= 12 && dx < 268) {
        if (workflow().can_set_selected_object_opacity()) {
            start_numeric_input(NumericFieldId::ObjectOpacity);
        } else {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? (appearance.selected_count == 0 ? "Chọn Path hoặc Text để sửa độ mờ"
                   : appearance.contains_locked ? "Không thể sửa Opacity: vùng chọn có đối tượng bị khóa"
                   : "Không thể sửa Opacity: chỉ hỗ trợ Path/Text trong toàn bộ vùng chọn")
                : (appearance.selected_count == 0 ? "Select a Path or Text object to edit opacity"
                   : appearance.contains_locked ? "Opacity is unavailable because the selection contains a locked object"
                   : "Opacity requires the entire selection to contain editable Path/Text objects");
        }
    } else if (dy >= 82 && dy < 110) {
        if (dx >= 12 && dx < 134)
            palette_stroke_ = false;
        if (dx >= 142 && dx < 268 && style.all_paths)
            palette_stroke_ = true;
    } else if (dy >= 120 && dy < 164 && dx >= 12 && dx < 268) {
        open_style_editor(palette_stroke_);
    } else if (dy >= 174 && dy < 202 && dx >= 12 && dx < 268 && style.all_paths) {
        if (palette_stroke_)
            (void)set_selection_stroke_enabled(style.mixed_stroke_enabled || !style.values.has_stroke);
        else
            (void)set_selection_fill_enabled(style.mixed_fill_enabled || !style.values.has_fill);
    } else if (dy >= 236 && dy < 266 && dx >= 12 && dx < 268 && style.all_paths) {
        choose_stroke_width(x, y);
    } else if (dy >= 386 && dy < 414) {
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đổi hệ màu tài liệu trong menu Edit"
                                                       : "Change document color mode in the Edit menu";
    } else if (dy >= 536 && dy < 568 && dx >= 12 && dx < 268) {
        show_color_profiles();
    } else if (dy >= kAppearanceBlendTop + 26 && dy < kAppearanceBlendTop + 80 &&
               dx >= 12 && dx < 268) {
        constexpr document::BlendMode modes[] = {
            document::BlendMode::Normal, document::BlendMode::Multiply,
            document::BlendMode::Screen, document::BlendMode::Overlay,
            document::BlendMode::Darken, document::BlendMode::Lighten};
        const int row = dy < kAppearanceBlendTop + 54 ? 0 : 1;
        const int column = std::clamp((dx - 12) / 88, 0, 2);
        const size_t index = static_cast<size_t>(row * 3 + column);
        if (workflow().can_edit_selected_appearance()) {
            if (workflow().set_selected_blend_mode(modes[index]))
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đã cập nhật Blend mode cho toàn vùng chọn · Ctrl+Z hoàn tác"
                    : "Updated Blend mode for the complete selection · Ctrl+Z undoes";
        } else {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Blend mode chỉ sửa được trên Path/Text không khóa"
                : "Blend mode requires an unlocked Path/Text selection";
        }
    } else if (draft) {
        const auto *blur = std::get_if<document::GaussianBlurEffect>(&draft->parameters);
        const auto *shadow = std::get_if<document::DropShadowEffect>(&draft->parameters);
        const int draft_top = kAppearanceEffectListTop;
        const auto numeric_field_at = [&](int row_y, bool left, bool right) {
            if (dy < row_y || dy >= row_y + 26) return DocumentWorkflow::EffectDraftField::None;
            if (left && dx >= 54 && dx < 134) return DocumentWorkflow::EffectDraftField::SigmaX;
            if (right && dx >= 184 && dx < 264) return DocumentWorkflow::EffectDraftField::SigmaY;
            return DocumentWorkflow::EffectDraftField::None;
        };
        auto field = DocumentWorkflow::EffectDraftField::None;
        if (blur) {
            field = numeric_field_at(draft_top + 40, true, true);
        } else if (shadow) {
            if (dy >= draft_top + 40 && dy < draft_top + 66) {
                if (dx >= 54 && dx < 134) field = DocumentWorkflow::EffectDraftField::OffsetX;
                else if (dx >= 184 && dx < 264) field = DocumentWorkflow::EffectDraftField::OffsetY;
            } else if (dy >= draft_top + 68 && dy < draft_top + 94) {
                if (dx >= 54 && dx < 134) field = DocumentWorkflow::EffectDraftField::SigmaX;
                else if (dx >= 184 && dx < 264) field = DocumentWorkflow::EffectDraftField::SigmaY;
            }
            else if (dy >= draft_top + 96 && dy < draft_top + 122 && dx >= 54 && dx < 134)
                field = DocumentWorkflow::EffectDraftField::Opacity;
        }
        const int buttons_y = draft_top + (blur ? 72 : 126);
        const bool cancel_button = dy >= buttons_y && dy < buttons_y + 26 && dx >= 194 && dx < 268;
        if (cancel_button) {
            workflow().cancel_effect_draft();
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đã hủy bản nháp hiệu ứng · tài liệu không đổi"
                : "Effect draft cancelled · document unchanged";
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (field != DocumentWorkflow::EffectDraftField::None) {
            if (workflow().effect_draft_numeric_input_active() &&
                !workflow().commit_effect_draft_numeric_input()) {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Thông số chưa hợp lệ · bản nháp chưa được áp dụng"
                    : "Invalid value · effect draft was not applied";
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
            if (workflow().start_effect_draft_numeric_input(field)) {
                SetFocus(hwnd_);
                const std::string unit(display_length_unit_suffix(display_length_unit_));
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Nhập chiều dài (mặc định " + unit + ") hoặc Opacity % · Enter xác nhận · Tab chuyển · Esc hủy"
                    : "Enter length (default " + unit + ") or Opacity % · Enter confirms · Tab moves · Esc cancels";
            }
        } else if (shadow && dy >= draft_top + 96 && dy < draft_top + 122 &&
                   dx >= 184 && dx < 268) {
            if (!workflow().effect_draft_numeric_input_active() ||
                workflow().commit_effect_draft_numeric_input()) {
                if (const auto edited = edit_source_color(
                        hwnd_, shadow->color, 0.0, false,
                        language_ == UILanguage::Vietnamese, ColorEditContext::Generic,
                        false, false, snapshot->color_mode(), theme_ == ThemeMode::CyberDark)) {
                    auto parameters = draft->parameters;
                    std::get<document::DropShadowEffect>(parameters).color = edited->color;
                    if (workflow().set_effect_draft_parameters(std::move(parameters)))
                        status_msg_ = language_ == UILanguage::Vietnamese
                            ? "Đã cập nhật màu bóng trong bản nháp"
                            : "Updated shadow color in the draft";
                }
            } else {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Thông số chưa hợp lệ · sửa giá trị hoặc hủy bản nháp"
                    : "Invalid value · correct it or cancel the draft";
            }
        } else if (dy >= buttons_y && dy < buttons_y + 26) {
            if (workflow().effect_draft_numeric_input_active() &&
                !workflow().commit_effect_draft_numeric_input()) {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Thông số chưa hợp lệ · sửa giá trị hoặc hủy bản nháp"
                    : "Invalid value · correct it or cancel the draft";
            } else if (dx >= 12 && dx < 108) {
                const bool enabled = !draft->preview_enabled;
                const bool updated = workflow().set_effect_draft_preview(enabled);
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? (updated ? (enabled ? "Đang xem trước hiệu ứng · tài liệu chưa đổi"
                                          : "Đã tắt xem trước hiệu ứng")
                               : "Preview vượt giới hạn dựng hình hoặc bản nháp đã cũ")
                    : (updated ? (enabled ? "Effect preview is active · document unchanged"
                                          : "Effect preview is off")
                    : "Could not prepare the effect preview; the draft remains uncommitted");
            } else if (dx >= 112 && dx < 190) {
                const bool applied = workflow().apply_effect_draft();
                const bool editing_existing = draft->editing_existing;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? (applied ? (editing_existing
                                    ? "Đã cập nhật hiệu ứng hiện có · Ctrl+Z hoàn tác"
                                    : "Đã thêm hiệu ứng bằng một bước Undo · Ctrl+Z hoàn tác")
                               : "Không thể Apply bản nháp; tài liệu chưa đổi")
                    : (applied ? (editing_existing
                                    ? "Updated existing effect · Ctrl+Z undoes"
                                    : "Applied effect in one undo step · Ctrl+Z undoes")
                               : "Could not apply the draft; document unchanged");
            }
        } else if (workflow().effect_draft_numeric_input_active()) {
            if (!workflow().commit_effect_draft_numeric_input()) {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Thông số chưa hợp lệ · bản nháp vẫn đang mở"
                    : "Invalid value · the draft remains open";
            }
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    } else if (dy >= kAppearanceEffectsTop + 50 && dy < kAppearanceEffectsTop + 78 &&
               dx >= 12 && dx < 268) {
        const auto action = dx < 140 ? MenuActionId::EffectsGaussianBlur
                                     : MenuActionId::EffectsDropShadow;
        if (!is_menu_action_enabled(action)) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Hiệu ứng đang có thể được chỉnh; vùng chọn phải tương thích và không khóa"
                : "The effect must be editable; the selection must be compatible and unlocked";
        } else {
            execute_menu_action(static_cast<int>(action));
        }
    } else if (appearance.effect_stack_compatible && !appearance.effects.empty()) {
        int node_top = kAppearanceEffectListTop;
        for (size_t index = 0; index < appearance.effects.size(); ++index) {
            const auto &effect = appearance.effects[index];
            const bool blur = std::holds_alternative<document::GaussianBlurEffect>(effect.parameters);
            const bool can_edit_node = appearance.can_edit_effect(index) &&
                workflow().can_edit_selected_effect(index);
            if (dy >= node_top + 22 && dy < node_top + 45) {
                bool applied = false;
                if (can_edit_node && dx >= 12 && dx < 96)
                    applied = workflow().set_selected_effect_enabled(index,
                        effect.mixed_enabled || !effect.enabled);
                else if (can_edit_node && dx >= 100 && dx < 146 && index > 0)
                    applied = workflow().move_selected_effect(index, -1);
                else if (can_edit_node && dx >= 150 && dx < 206 && index + 1 < appearance.effects.size())
                    applied = workflow().move_selected_effect(index, 1);
                else if (can_edit_node && dx >= 210 && dx < 268)
                    applied = workflow().remove_selected_effect(index);
                if (applied)
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Đã cập nhật stack Appearance · Ctrl+Z hoàn tác"
                        : "Updated Appearance stack · Ctrl+Z undoes";
                else if (!can_edit_node)
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Stack không đồng nhất hoặc vùng chọn khóa · thao tác bị vô hiệu"
                        : "Effect stacks differ or selection is locked · edit is disabled";
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
            const int first_row = node_top + 48;
            if (blur) {
                if (dy >= first_row + 1 && dy < first_row + 26 && dx >= 12 && dx < 268) {
                    if (dx < 140) start_effect_numeric_input(NumericFieldId::BlurSigmaX, index);
                    else start_effect_numeric_input(NumericFieldId::BlurSigmaY, index);
                    return true;
                }
            } else {
                if (dy >= first_row + 1 && dy < first_row + 26 && dx >= 12 && dx < 268) {
                    if (dx < 140) start_effect_numeric_input(NumericFieldId::ShadowOffsetX, index);
                    else start_effect_numeric_input(NumericFieldId::ShadowOffsetY, index);
                    return true;
                }
                if (dy >= first_row + 31 && dy < first_row + 56 && dx >= 12 && dx < 268) {
                    if (dx < 140) start_effect_numeric_input(NumericFieldId::ShadowFeatherX, index);
                    else start_effect_numeric_input(NumericFieldId::ShadowFeatherY, index);
                    return true;
                }
                if (dy >= first_row + 62 && dy < first_row + 88 && dx >= 96 && dx < 268) {
                    if (can_edit_node && !effect.mixed_color) {
                        const auto &shadow = std::get<document::DropShadowEffect>(effect.parameters);
                        if (const auto edited = edit_source_color(
                                hwnd_, shadow.color, 0.0, false,
                                language_ == UILanguage::Vietnamese, ColorEditContext::Generic,
                                false, false, snapshot->color_mode(), theme_ == ThemeMode::CyberDark,
                                effective_color_preview())) {
                            if (workflow().set_selected_effect_color(index, edited->color))
                                status_msg_ = language_ == UILanguage::Vietnamese
                                    ? "Đã cập nhật màu nguồn của bóng · Ctrl+Z hoàn tác"
                                    : "Updated the shadow source color · Ctrl+Z undoes";
                        }
                    } else {
                        status_msg_ = effect.mixed_color
                            ? (language_ == UILanguage::Vietnamese ? "Màu bóng đang khác nhau · chọn một đối tượng để sửa"
                                                                  : "Shadow colors are mixed · select one object to edit")
                            : (language_ == UILanguage::Vietnamese ? "Màu bóng không thể sửa cho vùng chọn này"
                                                                  : "Shadow color is unavailable for this selection");
                    }
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return true;
                }
                if (dy >= first_row + 92 && dy < first_row + 118 && dx >= 12 && dx < 140) {
                    start_effect_numeric_input(NumericFieldId::ShadowOpacity, index);
                    return true;
                }
                if (dy >= first_row + 144 && dy < first_row + 196 &&
                    dx >= 12 && dx < 276) {
                    const int column = (dx - 12) / 88;
                    const int row = (dy - first_row - 144) / 28;
                    const int column_offset = (dx - 12) % 88;
                    const int row_offset = (dy - first_row - 144) % 28;
                    if (column < 3 && row < 2 && column_offset < 80 &&
                        row_offset < 24) {
                        constexpr document::BlendMode shadow_modes[] = {
                            document::BlendMode::Normal, document::BlendMode::Multiply,
                            document::BlendMode::Screen, document::BlendMode::Overlay,
                            document::BlendMode::Darken, document::BlendMode::Lighten};
                        const auto mode = shadow_modes[row * 3 + column];
                        if (workflow().set_selected_effect_blend_mode(index, mode)) {
                            status_msg_ = language_ == UILanguage::Vietnamese
                                ? (mode == document::BlendMode::Normal
                                    ? "Đã cập nhật hòa trộn bóng · Ctrl+Z hoàn tác"
                                    : "Bóng hòa trộn với nền · Ctrl+Z hoàn tác")
                                : (mode == document::BlendMode::Normal
                                    ? "Updated shadow blend · Ctrl+Z undoes"
                                    : "Shadow blends with the backdrop · Ctrl+Z undoes");
                        } else if (mode != document::BlendMode::Normal) {
                            status_msg_ = language_ == UILanguage::Vietnamese
                                ? "Hòa bóng khác Normal yêu cầu Shadow là hiệu ứng bật cuối cùng"
                                : "A non-Normal shadow blend requires Shadow to be the last enabled effect";
                        }
                    }
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return true;
                }
            }
            node_top += effect_row_height(effect.parameters) + 10;
        }
    }
    if (!draft && workflow().can_add_selected_effect()) {
        int draft_add_top = kAppearanceEffectListTop;
        if (appearance.effects.empty()) draft_add_top += 24;
        else if (!appearance.effect_stack_compatible) draft_add_top += 56;
        else
            for (const auto &effect : appearance.effects)
                draft_add_top += effect_row_height(effect.parameters) + 10;
        if (dy >= draft_add_top && dy < draft_add_top + 26 && dx >= 12 && dx < 268) {
            const bool blur = dx < 140;
            const auto parameters = blur
                ? document::EffectParameters{document::GaussianBlurEffect{1.0, 1.0}}
                : document::EffectParameters{document::DropShadowEffect{}};
            if (workflow().can_add_selected_effect(parameters) &&
                workflow().begin_selected_effect_draft(parameters)) {
                const auto first_field = dx < 140
                    ? DocumentWorkflow::EffectDraftField::SigmaX
                    : DocumentWorkflow::EffectDraftField::OffsetX;
                (void)workflow().start_effect_draft_numeric_input(first_field);
                toolbox_keyboard_focus_ = false;
                if (hwnd_) SetFocus(hwnd_);
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Nhập số nếu cần · Preview để xem · Apply để lưu · Esc để hủy"
                    : "Type values if needed · Preview to inspect · Apply to keep · Esc to cancel";
            }
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}
bool NativeWindow::handle_gradient_property_click(int x, int y) {
    using Field = NumericFieldId;
    namespace Layout = GradientPropertiesLayout;
    if (active_tool_ != 13 || !gradient_tool_ || !gradient_tool_->active()) return false;
    auto& presenter = gradient_tool_->presenter();
    auto add_adjacent_stop = [&]() {
        const auto* paint = presenter.draft_paint();
        if (!paint || paint->stops.empty() || paint->stops.size() >= document::kMaxGradientStops)
            return false;
        const auto selected = std::find_if(paint->stops.begin(), paint->stops.end(),
            [&](const auto& stop) { return stop.id == presenter.selected_stop_id(); });
        if (selected == paint->stops.end()) return false;
        const size_t index = static_cast<size_t>(selected - paint->stops.begin());
        const double offset = index + 1 < paint->stops.size()
            ? (selected->offset + paint->stops[index + 1].offset) * 0.5
            : index > 0 ? (paint->stops[index - 1].offset + selected->offset) * 0.5
                        : selected->offset;
        std::vector<uint64_t> ids;
        for (const auto& stop : paint->stops) ids.push_back(stop.id);
        if (!presenter.add_stop(offset, selected->color, selected->opacity)) return false;
        const auto* updated = presenter.draft_paint();
        const auto inserted = std::find_if(updated->stops.begin(), updated->stops.end(),
            [&](const auto& stop) { return std::find(ids.begin(), ids.end(), stop.id) == ids.end(); });
        if (inserted != updated->stops.end()) (void)presenter.select_stop(inserted->id);
        return true;
    };
    auto commit_active = [&]() {
        if (active_num_field_ == Field::None) return true;
        commit_numeric_field();
        return active_num_field_ == Field::None;
    };

    const int inspector_left = width_ - 280;
    constexpr int properties_top = 38 + 28 + 36 + 36;
    if (active_dock_tab_ == 0 && x >= inspector_left && x < width_ &&
        y >= properties_top && y < height_ - 26) {
        const int left = inspector_left + Layout::content_left;
        const int right = std::min(width_ - 14, left + Layout::content_width);
        const int local_x = x - left;
        const int local_y = y - properties_top;
        auto hit = [&](int top, int height = 26) {
            return local_y >= top && local_y < top + height && x < right;
        };
        auto commit_or_stop = [&]() { return !commit_active(); };
        const auto* paint = presenter.draft_paint();
        const auto& summary = presenter.summary();
        const bool can_edit = summary.can_edit_gradient() && paint;
        const bool mixed_stops = summary.mixed_stops && summary.supported_count > 1;
        const size_t count = paint ? paint->stops.size() : 0;
        const int panel_width = right - left;
        const int kind_half = (panel_width - 6) / 2;
        const int spread_width = (panel_width - 12) / 3;
        const int stop_action_width = (panel_width - 18) / 4;

        const int action_half = (panel_width - 6) / 2;
        if (hit(Layout::actions_top, 30) && local_x < action_half) {
            if (commit_or_stop()) return true;
            if (can_edit && gradient_tool_->apply())
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đã áp dụng chuyển sắc · Ctrl+Z để hoàn tác"
                    : "Applied Gradient · Ctrl+Z undoes";
            else
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Không thể áp dụng · chọn Path có thể chỉnh sửa"
                    : "Could not apply · select an editable Path";
        } else if (hit(Layout::actions_top, 30) && local_x >= action_half + 6) {
            if (active_num_field_ != Field::None) cancel_numeric_field();
            gradient_tool_->cancel();
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã bỏ bản nháp Gradient" : "Discarded Gradient draft";
        } else if (hit(Layout::kind_top) && can_edit) {
            if (commit_or_stop()) return true;
            presenter.set_kind(local_x < kind_half ? document::GradientKind::Linear
                                                   : document::GradientKind::Radial);
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã cập nhật kiểu trong bản nháp" : "Updated kind in draft";
        } else if (hit(Layout::spread_top) && can_edit) {
            if (commit_or_stop()) return true;
            constexpr document::GradientSpread spreads[] = {
                document::GradientSpread::Pad, document::GradientSpread::Repeat,
                document::GradientSpread::Reflect};
            const int index = std::clamp(local_x / std::max(1, spread_width + 6), 0, 2);
            presenter.set_spread(spreads[index]);
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã cập nhật tràn viền trong bản nháp" : "Updated spread in draft";
        } else if (local_y >= Layout::ramp_top && local_y <= Layout::ramp_top + Layout::ramp_height && can_edit) {
            if (commit_or_stop()) return true;
            if (mixed_stops) {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Stop hỗn hợp bị khóa · chọn một Path để chỉnh dải màu"
                    : "Mixed stops are locked · select one Path to edit ramp";
            } else if (const auto* current = presenter.draft_paint()) {
                const RECT ramp_rect{Layout::content_left, Layout::ramp_top,
                                     Layout::content_left + Layout::content_width,
                                     Layout::ramp_top + Layout::ramp_height};
                const POINT pt{local_x + Layout::content_left, local_y};
                const auto hit_res = hit_test_gradient_ramp(pt, ramp_rect, *current);
                if (hit_res.kind == RampHitKind::StopPin) {
                    if (hit_res.index < current->stops.size()) {
                        (void)presenter.select_stop(current->stops[hit_res.index].id);
                        status_msg_ = language_ == UILanguage::Vietnamese
                            ? "Đã chọn Stop " + std::to_string(hit_res.index + 1)
                            : "Selected Stop " + std::to_string(hit_res.index + 1);
                    }
                } else if (hit_res.kind == RampHitKind::MidpointDiamond) {
                    if (hit_res.index < current->stops.size()) {
                        (void)presenter.select_stop(current->stops[hit_res.index].id);
                        start_numeric_input(Field::GradientStopMidpoint);
                    }
                } else if (hit_res.kind == RampHitKind::RampTrack) {
                    if (current->stops.size() < document::kMaxGradientStops) {
                        auto get_rgb = [](const color::ColorValue& cv) -> color::RGBColor {
                            return std::visit([](const auto& c) -> color::RGBColor {
                                using T = std::decay_t<decltype(c)>;
                                if constexpr (std::is_same_v<T, color::RGBColor>) return c;
                                return color::RGBColor{0.0, 0.0, 0.0, 1.0};
                            }, cv.payload());
                        };
                        color::ColorValue new_col{color::RGBColor{0.5, 0.5, 0.5, 1.0}};
                        const auto& stops = current->stops;
                        if (!stops.empty()) {
                            if (hit_res.offset <= stops.front().offset) {
                                new_col = stops.front().color;
                            } else if (hit_res.offset >= stops.back().offset) {
                                new_col = stops.back().color;
                            } else {
                                for (size_t s = 0; s + 1 < stops.size(); ++s) {
                                    if (hit_res.offset >= stops[s].offset && hit_res.offset <= stops[s + 1].offset) {
                                        const double s0 = stops[s].offset;
                                        const double s1 = stops[s + 1].offset;
                                        const double seg_len = s1 - s0;
                                        const double t = seg_len > 1e-6 ? (hit_res.offset - s0) / seg_len : 0.0;
                                        const auto c0 = get_rgb(stops[s].color);
                                        const auto c1 = get_rgb(stops[s + 1].color);
                                        new_col = color::ColorValue{color::RGBColor{
                                            c0.r + (c1.r - c0.r) * t,
                                            c0.g + (c1.g - c0.g) * t,
                                            c0.b + (c1.b - c0.b) * t,
                                            c0.a + (c1.a - c0.a) * t
                                        }};
                                        break;
                                    }
                                }
                            }
                        }
                        if (presenter.add_stop(hit_res.offset, new_col)) {
                            gradient_tool_->tool().set_draft_paint(*presenter.draft_paint());
                            status_msg_ = language_ == UILanguage::Vietnamese
                                ? "Đã thêm điểm dừng màu mới vào dải màu"
                                : "Added new color stop to ramp";
                        }
                    }
                }
            }
        } else if (hit(Layout::stop_actions_top) && can_edit) {
            if (mixed_stops) {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Stop hỗn hợp bị khóa · chọn một Path để chỉnh riêng"
                    : "Mixed stops are locked · select one Path to edit individually";
            } else if (stop_action_width > 0) {
                const auto* current = presenter.draft_paint();
                const auto selected = std::find_if(current->stops.begin(), current->stops.end(),
                    [&](const auto& stop) { return stop.id == presenter.selected_stop_id(); });
                const size_t current_index = selected == current->stops.end()
                    ? current->stops.size() : static_cast<size_t>(selected - current->stops.begin());
                const int action = std::clamp(local_x / (stop_action_width + 6), 0, 3);
                if (action == 0 && current_index > 0 && current_index < current->stops.size()) {
                    if (commit_or_stop()) return true;
                    const auto* refreshed = presenter.draft_paint();
                    const auto selected_after = std::find_if(refreshed->stops.begin(), refreshed->stops.end(),
                        [&](const auto& stop) { return stop.id == presenter.selected_stop_id(); });
                    if (selected_after != refreshed->stops.end() &&
                        selected_after != refreshed->stops.begin())
                        (void)presenter.select_stop((selected_after - 1)->id);
                } else if (action == 1) {
                    if (commit_or_stop()) return true;
                    if (add_adjacent_stop()) status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Đã thêm stop liền kề vào bản nháp" : "Added adjacent stop to draft";
                } else if (action == 2 && current->stops.size() > 2 && current_index < current->stops.size()) {
                    if (commit_or_stop()) return true;
                    const auto* refreshed = presenter.draft_paint();
                    const auto selected_after = std::find_if(refreshed->stops.begin(), refreshed->stops.end(),
                        [&](const auto& stop) { return stop.id == presenter.selected_stop_id(); });
                    const size_t remove_index = static_cast<size_t>(selected_after - refreshed->stops.begin());
                    if (presenter.delete_stop(remove_index)) {
                        const auto* updated = presenter.draft_paint();
                        if (updated && !updated->stops.empty())
                            (void)presenter.select_stop(updated->stops[std::min(remove_index, updated->stops.size() - 1)].id);
                        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã xóa stop khỏi bản nháp" : "Removed stop from draft";
                    }
                } else if (action == 3 && current_index + 1 < current->stops.size()) {
                    if (commit_or_stop()) return true;
                    const auto* refreshed = presenter.draft_paint();
                    const auto selected_after = std::find_if(refreshed->stops.begin(), refreshed->stops.end(),
                        [&](const auto& stop) { return stop.id == presenter.selected_stop_id(); });
                    if (selected_after != refreshed->stops.end() && selected_after + 1 != refreshed->stops.end())
                        (void)presenter.select_stop((selected_after + 1)->id);
                }
            }
        } else if (hit(Layout::color_top) && local_x >= 28 && can_edit) {
            if (commit_or_stop()) return true;
            if (!mixed_stops && count > 0) {
                const auto* current = presenter.draft_paint();
                const auto selected = std::find_if(current->stops.begin(), current->stops.end(),
                    [&](const auto& stop) { return stop.id == presenter.selected_stop_id(); });
                if (selected != current->stops.end()) {
                    const uint64_t id = selected->id;
                    if (const auto edited = edit_source_color(hwnd_, selected->color, 0.0, false,
                            language_ == UILanguage::Vietnamese, ColorEditContext::Generic,
                            false, false, root().document().color_mode(),
                            theme_ == ThemeMode::CyberDark, effective_color_preview())) {
                        const auto* updated = presenter.draft_paint();
                        const auto item = std::find_if(updated->stops.begin(), updated->stops.end(),
                            [id](const auto& stop) { return stop.id == id; });
                        if (item != updated->stops.end()) {
                            (void)presenter.set_stop_color(static_cast<size_t>(item - updated->stops.begin()), edited->color);
                            gradient_tool_->tool().set_draft_paint(*presenter.draft_paint());
                        }
                    }
                }
            }
        } else if (hit(Layout::offset_top, Layout::field_height)) {
            if (commit_or_stop()) return true;
            start_numeric_input(Field::GradientStopOffset);
        } else if (hit(Layout::opacity_top, Layout::field_height)) {
            if (commit_or_stop()) return true;
            start_numeric_input(Field::GradientStopOpacity);
        } else if (hit(Layout::midpoint_top, Layout::field_height)) {
            if (commit_or_stop()) return true;
            start_numeric_input(Field::GradientStopMidpoint);
        } else if (hit(Layout::presets_top, 24) && can_edit) {
            if (commit_or_stop()) return true;
            const auto& presets = get_gradient_presets();
            const int preset_count = static_cast<int>(presets.size());
            const int p_gap = 4;
            const int p_width = (Layout::content_width - (preset_count - 1) * p_gap) / std::max(1, preset_count);
            const int clicked_idx = local_x / std::max(1, p_width + p_gap);
            if (clicked_idx >= 0 && clicked_idx < preset_count && presenter.draft_paint()) {
                const auto& p = presets[clicked_idx];
                document::GradientPaint new_paint = *presenter.draft_paint();
                new_paint.stops.clear();
                new_paint.stops.reserve(p.stops.size());
                for (const auto& [off, rgb] : p.stops) {
                    document::GradientStop stop;
                    stop.id = document::allocate_gradient_stop_id();
                    stop.offset = off;
                    stop.color = color::ColorValue{rgb};
                    stop.opacity = 1.0;
                    stop.midpoint_to_next = 0.5;
                    new_paint.stops.push_back(std::move(stop));
                }
                presenter.set_draft_paint(new_paint);
                gradient_tool_->tool().set_draft_paint(new_paint);
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đã áp dụng mẫu Gradient có sẵn"
                    : "Applied gradient preset";
            }
        } else if (hit(Layout::align_top, 24) && can_edit) {
            if (commit_or_stop()) return true;
            const int half = (Layout::content_width - 6) / 2;
            if (local_x < half) {
                if (summary.mixed_stops && summary.supported_count > 1) {
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Stop hỗn hợp bị khóa · chọn một Path để đảo chiều"
                        : "Mixed stops are locked · select one Path before reversing";
                } else {
                    presenter.reverse_stops();
                    gradient_tool_->tool().reverse_stops();
                    status_msg_ = language_ == UILanguage::Vietnamese ? "Đã đảo chiều dải màu" : "Reversed Gradient stops";
                }
            } else if (local_x >= half + 6) {
                const auto* cur = presenter.draft_paint();
                if (cur && cur->stops.size() > 2) {
                    document::GradientPaint distributed = *cur;
                    const size_t n = distributed.stops.size();
                    for (size_t i = 0; i < n; ++i) {
                        distributed.stops[i].offset = static_cast<double>(i) / (n - 1);
                        distributed.stops[i].midpoint_to_next = 0.5;
                    }
                    presenter.set_draft_paint(distributed);
                    gradient_tool_->tool().set_draft_paint(distributed);
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Đã phân bố đều các điểm dừng màu"
                        : "Distributed color stops evenly";
                }
            }
        } else if (hit(Layout::angle_top, Layout::field_height)) {
            if (commit_or_stop()) return true;
            start_numeric_input(Field::GradientAngle);
        } else if (hit(Layout::length_top, Layout::field_height)) {
            if (commit_or_stop()) return true;
            start_numeric_input(Field::GradientLength);
        } else if (hit(Layout::center_x_top, Layout::field_height)) {
            if (commit_or_stop()) return true;
            start_numeric_input(Field::GradientCenterX);
        } else if (hit(Layout::center_y_top, Layout::field_height)) {
            if (commit_or_stop()) return true;
            start_numeric_input(Field::GradientCenterY);
        } else if (hit(Layout::hint_top, 26) && can_edit) {
            if (commit_or_stop()) return true;
            const auto snapshot = root().document().current_snapshot();
            const auto bounds = snapshot ? root().vector_controller().selection().bounds(*snapshot)
                .value_or(geometry::Rect2D{0, 0, 100, 100}) : geometry::Rect2D{0, 0, 100, 100};
            presenter.fit_to_bounds(bounds);
            gradient_tool_->tool().fit_to_object_bounds(bounds);
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã khớp dải màu theo khung đối tượng" : "Fit Gradient to bounds";
        } else if (x >= left && x < right) {
            return false;
        }
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    const int topbar_h = 32;
    const int content_top = topbar_h + 32 + 40;
    if (y < topbar_h + 32 || y > content_top) return false;
    if (x >= 80 && x < 145) {
        if (!commit_active()) return true;
        presenter.set_kind(document::GradientKind::Linear);
        gradient_tool_->tool().set_kind(document::GradientKind::Linear);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Kiểu: Tuyến tính (Linear)" : "Kind: Linear";
    } else if (x >= 150 && x < 215) {
        if (!commit_active()) return true;
        presenter.set_kind(document::GradientKind::Radial);
        gradient_tool_->tool().set_kind(document::GradientKind::Radial);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Kiểu: Tỏa tròn (Radial)" : "Kind: Radial";
    } else if (x >= 220 && x < 295) {
        if (!commit_active()) return true;
        if (presenter.summary().mixed_stops && presenter.summary().supported_count > 1)
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Stop hỗn hợp bị khóa · chọn một Path để đảo chiều"
                : "Mixed stops are locked · select one Path before reversing";
        else {
            presenter.reverse_stops();
            gradient_tool_->tool().reverse_stops();
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã đảo chiều dải màu" : "Reversed Gradient stops";
        }
    } else if (x >= 305 && x <= 415) {
        active_dock_tab_ = 0; // Focus properties inspector
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Bảng thuộc tính: Tùy chỉnh đầy đủ kiểu tô chuyển sắc"
            : "Properties: Full gradient settings";
    } else {
        return false;
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}
#endif
} // namespace pikdraw::desktop
