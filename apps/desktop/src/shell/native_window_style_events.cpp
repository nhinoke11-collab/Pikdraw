#include "native_style_dialog.hpp"
#include "native_window_menus.hpp"
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
    return field == NativeWindow::NumericFieldId::EffectSigmaX ||
           field == NativeWindow::NumericFieldId::EffectSigmaY ||
           field == NativeWindow::NumericFieldId::EffectOffsetX ||
           field == NativeWindow::NumericFieldId::EffectOffsetY ||
           field == NativeWindow::NumericFieldId::EffectOpacity;
}

} // namespace

void NativeWindow::open_style_editor(bool stroke) {
    const auto summary =
        selection_style(*root().document().current_snapshot(),
                        root().vector_controller().selection().selected_ids(), default_style_);
    if (stroke && !summary.all_paths)
        return;
    if (const auto edited =
            edit_source_color(hwnd_, stroke ? summary.values.stroke : summary.values.fill,
                              summary.values.width_mm, stroke, language_ == UILanguage::Vietnamese)) {
        vector::StylePatch patch;
        if (stroke) {
            patch.stroke = edited->color;
            patch.width_mm = edited->width_mm;
        } else {
            patch.fill = edited->color;
        }
        (void)apply_style_patch(patch);
    }
}

void NativeWindow::choose_stroke_width(int x, int y) {
    constexpr double widths[] = {.1, .25, .5, 1, 2, 5, 10};
    const auto menu = CreatePopupMenu();
    for (int i = 0; i < 7; ++i) {
        std::wostringstream text;
        text << widths[i] << L" mm";
        AppendMenuW(menu, MF_STRING, 100 + i, text.str().c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 200,
                language_ == UILanguage::Vietnamese ? L"Nhập kích thước…" : L"Custom width…");
    POINT point{x, y};
    ClientToScreen(hwnd_, &point);
    const auto chosen =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (chosen >= 100 && chosen < 107)
        (void)set_selection_stroke_width(widths[chosen - 100]);
    else if (chosen == 200)
        open_style_editor(true);
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
        return active_num_field_ == NumericFieldId::EffectSigmaX ||
               active_num_field_ == NumericFieldId::EffectSigmaY
            ? logical_y >= node_top + 49 && logical_y < node_top + 74 : false;
    if (active_num_field_ == NumericFieldId::EffectOffsetX ||
        active_num_field_ == NumericFieldId::EffectOffsetY)
        return logical_y >= node_top + 49 && logical_y < node_top + 74;
    if (active_num_field_ == NumericFieldId::EffectSigmaX ||
        active_num_field_ == NumericFieldId::EffectSigmaY)
        return logical_y >= node_top + 79 && logical_y < node_top + 104;
    if (active_num_field_ == NumericFieldId::EffectOpacity)
        return x_column && logical_y >= node_top + 141 && logical_y < node_top + 166;
    return false;
}

bool NativeWindow::handle_style_click(int x, int y, bool right_click) {
    if (active_dock_tab_ != 0 || !show_style_inspector_ || x < width_ - 280 ||
        y < kStylePanelTop || y >= height_ - 26)
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
            (void)apply_selection_color(
                target_stroke, editor_palette(palette_cmyk_)[static_cast<size_t>(row * 9 + column)]);
        }
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
        if (dx >= 12 && dx < 134) palette_cmyk_ = false;
        if (dx >= 142 && dx < 268) palette_cmyk_ = true;
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
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Nhập kích thước mm hoặc Opacity % · Enter xác nhận ô · Tab chuyển · Esc hủy"
                    : "Enter mm or Opacity % · Enter confirms field · Tab moves · Esc cancels";
            }
        } else if (shadow && dy >= draft_top + 96 && dy < draft_top + 122 &&
                   dx >= 184 && dx < 268) {
            if (!workflow().effect_draft_numeric_input_active() ||
                workflow().commit_effect_draft_numeric_input()) {
                if (const auto edited = edit_source_color(
                        hwnd_, shadow->color, 0.0, false,
                        language_ == UILanguage::Vietnamese)) {
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
                    if (dx < 140) start_effect_numeric_input(NumericFieldId::EffectSigmaX, index);
                    else start_effect_numeric_input(NumericFieldId::EffectSigmaY, index);
                    return true;
                }
            } else {
                if (dy >= first_row + 1 && dy < first_row + 26 && dx >= 12 && dx < 268) {
                    if (dx < 140) start_effect_numeric_input(NumericFieldId::EffectOffsetX, index);
                    else start_effect_numeric_input(NumericFieldId::EffectOffsetY, index);
                    return true;
                }
                if (dy >= first_row + 31 && dy < first_row + 56 && dx >= 12 && dx < 268) {
                    if (dx < 140) start_effect_numeric_input(NumericFieldId::EffectSigmaX, index);
                    else start_effect_numeric_input(NumericFieldId::EffectSigmaY, index);
                    return true;
                }
                if (dy >= first_row + 62 && dy < first_row + 88 && dx >= 96 && dx < 268) {
                    if (can_edit_node && !effect.mixed_color) {
                        const auto &shadow = std::get<document::DropShadowEffect>(effect.parameters);
                        if (const auto edited = edit_source_color(
                                hwnd_, shadow.color, 0.0, false,
                                language_ == UILanguage::Vietnamese)) {
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
                    start_effect_numeric_input(NumericFieldId::EffectOpacity, index);
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
#endif
} // namespace pikdraw::desktop
