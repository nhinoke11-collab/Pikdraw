#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_menus.hpp"
#include "native_window_length_input.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "../tools/dimension_tool_controller.hpp"
#include <algorithm>

namespace pikdraw::desktop {
#ifdef _WIN32

void NativeWindow::finish_drawing_tool(bool select_tool) {
    if (drawing_tool_) drawing_tool_->cancel();
    if (hwnd_) KillTimer(hwnd_, 9);
    if (select_tool) set_active_tool(0);
}

bool NativeWindow::handle_pen_key_down(WPARAM key) {
    if (active_tool_ != 8 || !pen_tool_ || !pen_tool_->active())
        return false;

    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    using ResultCode = vector::tools::pen::PenDrawingResultCode;

    const bool pen_field_active = active_num_field_ == NumericFieldId::PenExpression ||
                                  active_num_field_ == NumericFieldId::PenLength ||
                                  active_num_field_ == NumericFieldId::PenAngle;
    if (key == VK_ESCAPE) {
        if (pen_field_active) return false;
        if (shift) return finish_pen_session(false, true);
        if (pen_tool_->anchor_count() >= 2) return finish_pen_session(false);
        return finish_pen_session(false, true);
    }
    if (key == VK_RETURN) {
        if (pen_field_active) return false;
        if (pen_numeric_buffer_.empty()) return finish_pen_session(false);
        const auto result = pen_tool_->confirm_candidate();
        if (result.code == ResultCode::Applied || result.code == ResultCode::PreviewUpdated) {
            pen_numeric_buffer_.clear();
            (void)pen_tool_->pointer_move(pen_last_pointer_world_, false);
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đã đặt điểm theo kích thước · Enter để kết thúc đường"
                : "Point placed from dimensions · Enter finishes the path";
        } else {
            status_msg_ = result.message;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (key == VK_BACK) {
        if (!pen_numeric_buffer_.empty()) {
            pen_numeric_buffer_.pop_back();
            if (pen_numeric_buffer_.empty())
                (void)pen_tool_->pointer_move(pen_last_pointer_world_, false);
            else {
                const auto result = pen_tool_->update_numeric_candidate(pen_numeric_buffer_,
                    vector::precision::numeric::ParseOptions{'.', display_length_unit_});
                if (result.code == ResultCode::InvalidInput) status_msg_ = result.message;
            }
        } else {
            const auto result = pen_tool_->undo_local();
            if (result.code == ResultCode::InvalidInput || result.code == ResultCode::Stale ||
                result.code == ResultCode::BudgetExceeded)
                status_msg_ = result.message;
            else
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đã lùi một điểm trong bản nháp Pen"
                    : "Undid one point in the Pen draft";
            (void)pen_tool_->pointer_move(pen_last_pointer_world_, false);
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (control && !alt && !shift && pen_numeric_buffer_.empty() && key == 'Z') {
        const auto result = pen_tool_->undo_local();
        if (result.code == ResultCode::InvalidInput || result.code == ResultCode::Stale ||
            result.code == ResultCode::BudgetExceeded)
            status_msg_ = result.message;
        else
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đã hoàn tác một bước Pen"
                : "Undid one Pen draft step";
        (void)pen_tool_->pointer_move(pen_last_pointer_world_, false);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (control && !alt && (key == 'Y' || (shift && key == 'Z')) &&
        pen_numeric_buffer_.empty()) {
        const auto result = pen_tool_->redo_local();
        if (result.code == ResultCode::InvalidInput || result.code == ResultCode::Stale ||
            result.code == ResultCode::BudgetExceeded)
            status_msg_ = result.message;
        else
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đã làm lại một bước Pen"
                : "Redid one Pen draft step";
        (void)pen_tool_->pointer_move(pen_last_pointer_world_, false);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    if (!control && !alt && (key == 'L' || key == 'l')) {
        if (pen_tool_->anchor_count() == 0) {
            (void)pen_tool_->pointer_down(pen_last_pointer_world_);
            (void)pen_tool_->pointer_up();
        }
        pen_numeric_length_.clear();
        pen_numeric_angle_.clear();
        num_input_buffer_.clear();
        start_numeric_input(NumericFieldId::PenLength);
        const std::string unit(display_length_unit_suffix(display_length_unit_));
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Nhập chiều dài (mặc định " + unit + "; hậu tố ghi đè) · Enter tạo đoạn · Tab nhập góc"
            : "Enter length (default " + unit + "; suffix overrides) · Enter creates line · Tab enters angle";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    // Printable keys belong to a direct Pen expression once its first character is entered.
    // Digits are reserved for point input even when the buffer is empty (for example, 1 mm).
    if (!control && !alt && key >= '0' && key <= '9')
        return true;
    if (!control && !alt && !pen_numeric_buffer_.empty() &&
        ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') ||
         key == VK_OEM_MINUS || key == VK_OEM_PLUS || key == VK_OEM_PERIOD ||
         key == VK_OEM_COMMA || key == VK_OEM_1 || key == VK_OEM_2 ||
         key == VK_OEM_3 || key == VK_OEM_4 || key == VK_OEM_5 ||
         key == VK_OEM_6 || key == VK_OEM_7))
        return true;
    return false;
}

bool NativeWindow::handle_drawing_key_down(WPARAM key) {
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    if (control || alt || !drawing_tool_)
        return false;

    if (drawing_tool_->active()) {
        if (!control && !alt && (key == 'L' || key == 'l')) {
            if (drawing_tool_->path_point_count() == 0) {
                (void)drawing_tool_->confirm_candidate();
            }
            pen_numeric_length_.clear();
            pen_numeric_angle_.clear();
            num_input_buffer_.clear();
            start_numeric_input(NumericFieldId::PenLength);
            const std::string unit(display_length_unit_suffix(display_length_unit_));
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Nhập chiều dài (mặc định " + unit + "; hậu tố ghi đè) · Enter tạo đoạn · Tab nhập góc"
                : "Enter length (default " + unit + "; suffix overrides) · Enter creates line · Tab enters angle";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }

        // Numeric coordinate syntax contains letters such as "mm", "deg" and "rad".
        // Suppress tool shortcuts while a numeric expression is being entered; WM_CHAR
        // still delivers the character to the drawing controller.
        if (drawing_tool_->has_numeric_entry() && key != VK_RETURN && key != VK_ESCAPE &&
            key != VK_BACK)
            return true;
        if (key == VK_BACK) {
            if (drawing_tool_->has_numeric_entry())
                drawing_tool_->erase_numeric_character();
            else
                (void)drawing_tool_->backspace();
            status_msg_ = drawing_tool_->visual_state().message;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == VK_RETURN) {
            const auto result = drawing_tool_->enter();
            if (result.committed() && result.object_id) {
                const auto snapshot = root().document().current_snapshot();
                if (snapshot)
                    root().vector_controller().set_selection({*result.object_id}, *snapshot);
            }
            status_msg_ = drawing_tool_->visual_state().message;
            if (!drawing_tool_->active()) finish_drawing_tool(true);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == VK_ESCAPE) {
            const auto result = drawing_tool_->escape();
            if (result.committed() && result.object_id) {
                const auto snapshot = root().document().current_snapshot();
                if (snapshot)
                    root().vector_controller().set_selection({*result.object_id}, *snapshot);
            }
            status_msg_ = drawing_tool_->visual_state().message;
            if (!drawing_tool_->active()) finish_drawing_tool(true);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
    }
    return false;
}

bool NativeWindow::handle_key_down(WPARAM key) {
    if (show_recovery_modal_) return handle_recovery_key(key);
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 && key == 'T') {
        open_inspector_properties_panel();
        return true;
    }
    if (is_inline_text_editing_ && handle_inline_text_key(static_cast<uint32_t>(key),
        (GetKeyState(VK_CONTROL) & 0x8000) != 0, (GetKeyState(VK_SHIFT) & 0x8000) != 0)) return true;
    if (active_num_field_ != NumericFieldId::None) {
        if (key == VK_ESCAPE) {
            cancel_numeric_field();
            return true;
        }
        if (key == VK_RETURN) {
            commit_numeric_field();
            return true;
        }
        if (key == VK_TAB) {
            handle_numeric_char_input(9);
            return true;
        }
        if (key == VK_BACK) {
            return false;
        }
        return true;
    }

    if (show_page_units_flyout_) {
        if (key == VK_ESCAPE) {
            show_page_units_flyout_ = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == VK_UP || key == VK_DOWN) {
            page_units_flyout_focus_index_ = (page_units_flyout_focus_index_ +
                (key == VK_DOWN ? 1 : 7)) % 8;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == VK_RETURN) {
            constexpr MenuActionId unit_actions[] = {
                MenuActionId::LayoutUnitMillimeter, MenuActionId::LayoutUnitCentimeter,
                MenuActionId::LayoutUnitMeter, MenuActionId::LayoutUnitInch,
                MenuActionId::LayoutUnitFoot, MenuActionId::LayoutUnitYard,
                MenuActionId::LayoutUnitPoint, MenuActionId::LayoutUnitPica};
            const int index = std::clamp(page_units_flyout_focus_index_, 0, 7);
            show_page_units_flyout_ = false;
            execute_menu_action(static_cast<int>(unit_actions[index]));
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        show_page_units_flyout_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    if (quick_dimension_active_) {
        if (key == VK_ESCAPE) {
            end_quick_dimension();
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã hủy nhập kích thước" : "Cancelled dimension input";
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == VK_RETURN) {
            apply_quick_dimension();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == VK_BACK) {
            return false; // let WM_CHAR handle backspace
        }
    }

    if (handle_pen_key_down(key))
        return true;

    if (handle_drawing_key_down(key))
        return true;

    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    if (control) {
        if (key == VK_OEM_1 || key == ';') {
            if (alt) {
                clear_guidelines();
            } else {
                toggle_guidelines();
            }
            return true;
        }
        if (key == 'N') { open_new_doc_modal(); return true; }
        if (key == 'W') { close_tab(active_tab_index_); return true; }
        if (key == VK_TAB) {
            if (tabs_.size() > 1) switch_to_tab((active_tab_index_ + 1) % tabs_.size());
            return true;
        }
        if (key == 'C') {
            if (workflow().copy_selected(hwnd_)) {
                status_msg_ = language_ == UILanguage::Vietnamese ? "Đã sao chép vào Clipboard" : "Copied to clipboard";
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return true;
        }
        if (key == 'X') {
            if (workflow().cut_selected(hwnd_)) {
                status_msg_ = language_ == UILanguage::Vietnamese ? "Đã cắt vào Clipboard" : "Cut to clipboard";
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return true;
        }
        if (key == 'V') {
            if (shift) {
                if (workflow().paste_in_place(hwnd_)) {
                    status_msg_ = language_ == UILanguage::Vietnamese ? "Đã dán tại chỗ (Paste in Place)" : "Pasted in place";
                    InvalidateRect(hwnd_, nullptr, FALSE);
                }
            } else {
                if (workflow().paste(hwnd_)) {
                    status_msg_ = language_ == UILanguage::Vietnamese ? "Đã dán từ Clipboard" : "Pasted from clipboard";
                    InvalidateRect(hwnd_, nullptr, FALSE);
                }
            }
            return true;
        }
        if (key == 'F') {
            if (workflow().paste_in_front(hwnd_)) {
                status_msg_ = language_ == UILanguage::Vietnamese ? "Đã dán lên trước (Paste in Front)" : "Pasted in front";
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return true;
        }
        if (key == 'D') {
            if (shift) {
                workflow().duplicate_selected(10.0, 10.0);
            } else {
                workflow().duplicate_selected(5.0, 5.0);
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == 'O') { open_document_dialog(); return true; }
        if (key == 'I') { open_import_dialog(); return true; }
        if (key == 'S') { save_document_dialog(shift); return true; }
        if (key == 'E') { trigger_export(); return true; }
        if (key == 'J') {
            if (is_text_object_selected()) {
                set_font_alignment(typography::TextAlignment::Justify);
                return true;
            }
            open_settings_modal();
            return true;
        }
        if (key == 'A') {
            if (shift) {
                open_align_distribute_panel();
            } else if (active_tool_ == 11 && node_tool_ && node_tool_->active()) {
                node_tool_->tool().select_all();
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đã chọn toàn bộ " + std::to_string(node_tool_->selected_node_count()) + " node (Ctrl+A)"
                    : "Selected all " + std::to_string(node_tool_->selected_node_count()) + " nodes (Ctrl+A)";
                InvalidateRect(hwnd_, nullptr, FALSE);
            } else {
                const auto snapshot = root().document().current_snapshot();
                const auto ids=vector::SelectionModel::scope_roots(*snapshot,current_isolated_group_id().value_or(0),isolation_frame_);
                root().vector_controller().set_selection(ids, *snapshot);
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return true;
        }
        if (key == 'Z' && !shift) {
            workflow().undo();
            root().vector_controller().selection().clear();
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã hoàn tác" : "Undo complete";
            update_color_preview_options();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == 'Z' && shift) {
            workflow().redo();
            root().vector_controller().selection().clear();
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã làm lại" : "Redo complete";
            update_color_preview_options();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == 'Y') {
            if (alt && shift) {
                execute_menu_action(static_cast<int>(MenuActionId::ViewOverprintPreview));
                return true;
            }
            if (shift) {
                execute_menu_action(static_cast<int>(MenuActionId::ViewGamutWarning));
                return true;
            }
            if (alt) {
                execute_menu_action(static_cast<int>(MenuActionId::ViewProofColors));
                return true;
            }
            workflow().redo();
            root().vector_controller().selection().clear();
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã làm lại" : "Redo complete";
            update_color_preview_options();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == 'G') {
            if (shift) {
                if (workflow().ungroup_selected()) {
                    status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đã rã nhóm (Ungroup)" : "Ungrouped";
                }
            } else {
                auto gid = workflow().group_selected();
                if (gid != document::kInvalidObjectId) {
                    status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đã nhóm các đối tượng (Group)" : "Grouped selected objects";
                }
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == '7') {
            if (alt) {
                if (workflow().release_clipping_mask_selected()) {
                    status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đã hủy mặt nạ cắt (Release)" : "Released Clipping Mask";
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return true;
                }
            } else {
                auto gid = workflow().make_clipping_mask_selected(true);
                if (gid != document::kInvalidObjectId) {
                    status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đã tạo mặt nạ cắt / PowerClip (Ctrl+7)" : "Created PowerClip / Clipping Mask (Ctrl+7)";
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return true;
                }
            }
        }
        if (key == 'Q') {
            convert_selected_text_to_curves();
            return true;
        }
        if (key == 'C' && shift) {
            combine_selected_text_objects();
            return true;
        }
        if (key == 'S' && shift) {
            split_selected_text_lines();
            return true;
        }
        if (key == VK_F8) {
            toggle_convert_artistic_paragraph();
            return true;
        }
        if (key == 'K') {
            trigger_break_contour_apart();
            return true;
        }
        if (key == VK_F9) {
            trigger_effects_contour();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == 'B') {
            const auto& sel = root().vector_controller().selection().selected_ids();
            bool has_text = false;
            const auto snap = root().document().current_snapshot();
            for (auto id : sel) {
                if (auto obj = snap->objects().get(id)) {
                    if (std::holds_alternative<document::TextObject>(*obj)) {
                        has_text = true;
                        break;
                    }
                }
            }
            if (has_text) {
                toggle_font_bold();
            } else {
                if (workflow().paste_in_back(hwnd_)) {
                    status_msg_ = language_ == UILanguage::Vietnamese ? "Đã dán ra sau (Paste in Back)" : "Pasted in back";
                    InvalidateRect(hwnd_, nullptr, FALSE);
                }
            }
            return true;
        }
        if (shift && key == 'I') {
            const auto& sel = root().vector_controller().selection().selected_ids();
            bool has_text = false;
            const auto snap = root().document().current_snapshot();
            for (auto id : sel) {
                if (auto obj = snap->objects().get(id)) {
                    if (std::holds_alternative<document::TextObject>(*obj)) {
                        has_text = true;
                        break;
                    }
                }
            }
            if (has_text) {
                toggle_font_italic();
            } else {
                trigger_boolean_operation(geometry::BooleanOp::Intersection);
            }
            return true;
        }
        if (key == 'U') {
            if (workflow().ungroup_selected()) {
                status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đã rã nhóm (Ungroup)" : "Ungrouped";
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return true;
        }
        if (shift && key == 'L') {
            align_selected_objects(geometry::AlignMode::Left);
            return true;
        }
        if (shift) {
            if (key == 'H') {
                execute_menu_action(static_cast<int>(MenuActionId::ObjectFlipHorizontal));
                return true;
            }
            if (key == 'U' || key == 'W') {
                trigger_boolean_operation(geometry::BooleanOp::Union);
                return true;
            }
            if (key == 'T') {
                trigger_boolean_operation(geometry::BooleanOp::Difference);
                return true;
            }
            if (key == 'X') {
                trigger_boolean_operation(geometry::BooleanOp::Xor);
                return true;
            }
        }
        if (key == VK_OEM_6 || key == ']') {
            execute_menu_action(static_cast<int>(MenuActionId::ObjectRotate90CW));
            return true;
        }
        if (key == VK_OEM_4 || key == '[') {
            execute_menu_action(static_cast<int>(MenuActionId::ObjectRotate90CCW));
            return true;
        }
    }
    if (!control && !alt && (key == VK_ADD || (key == VK_OEM_PLUS && !shift))) {
        if (workflow().duplicate_at_origin()) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã nhân bản tại chỗ (+)" : "Duplicated at origin (+)";
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
    }
    if (key == VK_RETURN) {
        if (is_inline_text_editing_) {
            return false;
        }
        if (active_tool_ == 12 && contour_tool_ && contour_tool_->active()) {
            apply_contour_effect();
            return true;
        }
        const auto& sel = root().vector_controller().selection().selected_ids();
        if (sel.size() == 1 && workflow().is_group(sel.front())) {
            enter_group_isolation(sel.front());
            return true;
        }
    }
    if (key == VK_ESCAPE) {
        if (active_tool_ == 10 && dimension_tool_ && dimension_tool_->active()) {
            if (dimension_tool_->handle_escape()) {
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
        }
        if (workflow().is_array_session_active()) {
            workflow().end_array_session();
            array_input_buffer_.clear();
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã chốt mảng sao chép" : "Finished array session";
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (is_dragging_guideline_) {
            is_dragging_guideline_ = false;
            if (GetCapture() == hwnd_) ReleaseCapture();
            status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đã hủy đường gióng" : "Cancelled guideline";
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (is_inline_text_editing_) {
            cancel_inline_text_editing();
            return true;
        }
        if (powerclip_target_pick_) { powerclip_target_pick_=0; powerclip_pick_stamp_.reset(); return true; }
        if (is_in_isolation_mode() && (pointer_active_ || root().vector_controller().gesture().is_active())) {
            cancel_canvas_interaction(); return true;
        }
        if (is_in_isolation_mode()) {
            exit_group_isolation();
            return true;
        }
        if (show_new_doc_modal_) close_new_doc_modal();
        else if (show_settings_modal_) close_settings_modal();
        else if (show_file_menu_) close_file_menu();
        else if (show_page_preset_dropdown_) close_page_preset_dropdown();
        else if (is_panning_) {
            is_panning_ = false;
            if (GetCapture() == hwnd_) ReleaseCapture();
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (pointer_active_) {
            root().vector_controller().on_escape();
            pointer_active_ = false;
            is_dragging_ = false;
            is_creating_shape_ = false;
            is_knife_cutting_ = false;
            drag_preview_bounds_.reset();
            if (GetCapture() == hwnd_) ReleaseCapture();
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (is_rotate_mode_) {
            is_rotate_mode_ = false;
            custom_pivot_world_.reset();
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đã trở về chế độ biến đổi kích thước"
                : "Returned to transform/resize mode";
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (active_tool_ == 11 && node_tool_ && node_tool_->active()) {
            node_tool_->cancel();
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (active_tool_ == 12 && contour_tool_ && contour_tool_->active()) {
            contour_tool_->cancel();
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else {
            root().vector_controller().selection().clear();
            selected_guideline_index_ = -1;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return true;
    }
    if (key == VK_DELETE || key == VK_BACK) {
        if (is_inline_text_editing_) {
            return false; // let WM_CHAR handle backspace
        }
        if (active_tool_ == 11 && node_tool_ && node_tool_->active()) {
            if (node_tool_->delete_selected()) {
                status_msg_ = (language_ == UILanguage::Vietnamese) ?
                    "Đã xóa các điểm node được chọn · Nhấn Ctrl+Z để hoàn tác" :
                    "Deleted selected node(s) · Press Ctrl+Z to undo";
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
        }
        if (delete_selected_guideline()) {
            return true;
        }
        cancel_canvas_interaction();
        if (workflow().delete_selected()) {
            status_msg_ = (language_ == UILanguage::Vietnamese) ?
                "Đã xóa các đối tượng được chọn · Nhấn Ctrl+Z để hoàn tác" :
                "Deleted selected object(s) · Press Ctrl+Z to undo";
        }
        workflow().sync_status_bar();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    const auto sel_count = root().vector_controller().selection().count();
    if (!is_inline_text_editing_ && sel_count >= 1) {
        if (key == 'P') {
            if (shift) align_selected_objects(geometry::AlignMode::CenterPageHorizontal);
            else if (alt) align_selected_objects(geometry::AlignMode::CenterPageVertical);
            else align_selected_objects(geometry::AlignMode::CenterPage);
            return true;
        }
        if (sel_count >= 2 || align_reference_ == geometry::AlignReference::Page) {
            if (key == 'R') { align_selected_objects(geometry::AlignMode::Right); return true; }
            if (key == 'T') { align_selected_objects(geometry::AlignMode::Top); return true; }
            if (key == 'B') { align_selected_objects(geometry::AlignMode::Bottom); return true; }
            if (key == 'C') { align_selected_objects(geometry::AlignMode::CenterHorizontal); return true; }
            if (key == 'E') { align_selected_objects(geometry::AlignMode::CenterVertical); return true; }
        }
    }

    if (key == 'L') {
        root().window().action_dispatcher().dispatch_shortcut(shift ? "Shift+L" : "L");
        return true;
    }
    if (key == VK_F3) {
        if (shift) {
            cycle_change_case();
            return true;
        }
        root().window().action_dispatcher().dispatch_shortcut("F3");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (key == VK_F10) {
        set_active_tool(11);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (key == VK_F8) { set_active_tool(4); return true; }
    if (key == 'F') { fit_page_to_canvas(); return true; }
    if (key == 'H') { set_active_tool(3); return true; }
    if (key == '1') {
        auto& camera = root().renderer().camera();
        camera.set_viewport_size(std::max(1, width_ - 352), std::max(1, height_ - 148));
        camera.set_zoom(1.0);
        zoom_level_ = 100;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (key == VK_SPACE) {
        if (active_tool_ == 0) {
            const int tool_to_restore = (previous_drawing_tool_ > 0) ? previous_drawing_tool_ : 1;
            set_active_tool(tool_to_restore);
            status_msg_ = (language_ == UILanguage::Vietnamese)
                ? "Khôi phục công cụ vẽ (Space)"
                : "Resumed drawing tool (Space)";
        } else {
            previous_drawing_tool_ = active_tool_;
            finish_drawing_tool(true);
            set_active_tool(0);
            status_msg_ = (language_ == UILanguage::Vietnamese)
                ? "Chuyển sang công cụ Chọn (Space)"
                : "Switched to Pick Tool (Space)";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (key == 'V') {
        if (active_tool_ != 0) {
            previous_drawing_tool_ = active_tool_;
            finish_drawing_tool(true);
        }
        set_active_tool(0);
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Công cụ Chọn (Pick Tool · V)"
            : "Pick Tool (V)";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (key == 'R') { set_active_tool(1); return true; }
    if (key == 'E') { set_active_tool(2); return true; }
    if (key == 'K' || key == 'C') { set_active_tool(5); return true; }
    if (key == 'A' || key == 'T') { set_active_tool(4); return true; }
    if (key == 'D') { set_active_tool(10); InvalidateRect(hwnd_, nullptr, FALSE); return true; }
    return false;
}

#endif
} // namespace pikdraw::desktop
