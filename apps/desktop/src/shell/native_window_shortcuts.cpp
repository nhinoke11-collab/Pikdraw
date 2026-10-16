#include "pikdraw/desktop/shell/native_window.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include <algorithm>
#include <cmath>

namespace pikdraw::desktop {
#ifdef _WIN32
bool NativeWindow::handle_key_down(WPARAM key) {
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

    if (show_settings_modal_) {
        if (key == VK_ESCAPE)
            close_settings_modal();
        return true;
    }
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

    // TranslateMessage posts WM_CHAR even when the window procedure leaves
    // WM_KEYDOWN unhandled. Let E/R reach WM_CHAR while a numeric expression is
    // active so the "deg" and "rad" suffixes cannot switch drawing tools.
    if (drawing_tool_ && drawing_tool_->active() && drawing_tool_->has_numeric_entry() &&
        !control && !alt && (key == 'E' || key == 'R'))
        return false;

    if (drawing_tool_ && drawing_tool_->active()) {
        if (key == VK_BACK) {
            const auto state = drawing_tool_->visual_state();
            if (state.numeric_entry.empty())
                (void)drawing_tool_->backspace();
            else
                drawing_tool_->erase_numeric_character();
            status_msg_ = drawing_tool_->visual_state().message;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == VK_RETURN) {
            const auto result = drawing_tool_->enter();
            if (result.committed() && result.object_id)
                root().vector_controller().set_selection({*result.object_id},
                    *root().document().current_snapshot());
            status_msg_ = drawing_tool_->visual_state().message;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == VK_ESCAPE) {
            (void)drawing_tool_->escape();
            status_msg_ = drawing_tool_->visual_state().message;
            if (!drawing_tool_->active()) finish_drawing_tool(true);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
    }

    if (control) {
        if (key == VK_OEM_1 || key == ';') {
            if (alt) {
                clear_guidelines();
            } else {
                toggle_guidelines();
            }
            return true;
        }
        if (key == 'N') {
            open_new_doc_modal();
            return true;
        }
        if (key == 'W') {
            close_tab(active_tab_index_);
            return true;
        }
        if (key == VK_TAB) {
            if (tabs_.size() > 1) switch_to_tab((active_tab_index_ + 1) % tabs_.size());
            return true;
        }
        if (key == 'D') {
            workflow().duplicate_selected(15.0);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == 'O') {
            open_document_dialog();
            return true;
        }
        if (key == 'S') {
            save_document_dialog(shift);
            return true;
        }
        if (key == 'E') {
            export_svg_dialog();
            return true;
        }
        if (key == 'J') {
            open_settings_modal();
            return true;
        }
        if (key == 'A') {
            if (shift) {
                open_inspector_properties_panel();
                return true;
            }
            const auto snapshot = root().document().current_snapshot();
            std::vector<document::ObjectId> ids;
            for (const auto &layer : snapshot->page().layers)
                ids.insert(ids.end(), layer.object_ids.begin(), layer.object_ids.end());
            root().vector_controller().set_selection(ids, *snapshot);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == 'Z' && !shift) {
            workflow().undo();
            root().vector_controller().selection().clear();
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã hoàn tác" : "Undo complete";
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == 'Y' || (key == 'Z' && shift)) {
            workflow().redo();
            root().vector_controller().selection().clear();
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã làm lại" : "Redo complete";
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
        if (key == 'Q') {
            convert_selected_text_to_curves();
            return true;
        }
        if (key == 'B') {
            toggle_font_bold();
            return true;
        }
        if (key == 'I') {
            toggle_font_italic();
            return true;
        }
        if (key == 'U') {
            if (workflow().ungroup_selected()) {
                status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đã rã nhóm (Ungroup)" : "Ungrouped";
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return true;
        }
        if (shift) {
            if (key == 'U' || key == 'W') {
                trigger_boolean_operation(geometry::BooleanOp::Union);
                return true;
            }
            if (key == 'T') {
                trigger_boolean_operation(geometry::BooleanOp::Difference);
                return true;
            }
            if (key == 'I') {
                trigger_boolean_operation(geometry::BooleanOp::Intersection);
                return true;
            }
            if (key == 'X') {
                trigger_boolean_operation(geometry::BooleanOp::Xor);
                return true;
            }
            if (key == 'B') {
                trigger_boolean_operation(geometry::BooleanOp::Boundary);
                return true;
            }
        }
    }

    if (key == VK_RETURN) {
        if (is_inline_text_editing_) {
            commit_inline_text_editing();
            return true;
        }
        const auto& sel = root().vector_controller().selection().selected_ids();
        if (sel.size() == 1 && workflow().is_group(sel.front())) {
            enter_group_isolation(sel.front());
            return true;
        }
    }

    if (key == VK_ESCAPE) {
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
        if (is_in_isolation_mode()) {
            exit_group_isolation();
            return true;
        }
        if (show_new_doc_modal_) close_new_doc_modal();
        else if (show_settings_modal_) close_settings_modal();
        else if (show_file_menu_) close_file_menu();
        else if (show_page_preset_dropdown_) close_page_preset_dropdown();
        else if (is_panning_) {
            cancel_canvas_interaction();
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (pointer_active_) {
            cancel_canvas_interaction();
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else {
            root().vector_controller().selection().clear();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return true;
    }

    if (key == VK_DELETE || key == VK_BACK) {
        if (is_inline_text_editing_) {
            return false; // let WM_CHAR handle backspace
        }
        cancel_canvas_interaction();
        if (workflow().delete_selected()) {
            status_msg_ = (language_ == UILanguage::Vietnamese) ?
                "Đã xóa các đối tượng được chọn · Nhấn Ctrl+Z để hoàn tác" :
                "Deleted selected object(s) · Press Ctrl+Z to undo";
            workflow().sync_status_bar();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
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
            if (key == 'L' && !control) { align_selected_objects(geometry::AlignMode::Left); return true; }
            if (key == 'R') { align_selected_objects(geometry::AlignMode::Right); return true; }
            if (key == 'T') { align_selected_objects(geometry::AlignMode::Top); return true; }
            if (key == 'B') { align_selected_objects(geometry::AlignMode::Bottom); return true; }
            if (key == 'C') { align_selected_objects(geometry::AlignMode::CenterHorizontal); return true; }
            if (key == 'E') { align_selected_objects(geometry::AlignMode::CenterVertical); return true; }
        }
    }

    if (key == 'F') {
        fit_page_to_canvas();
        return true;
    }
    if (key == 'H') {
        set_active_tool(3);
        return true;
    }
    if (key == VK_F3) {
        (void)root().window().action_dispatcher().dispatch_shortcut("F3");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (key == '1') {
        auto &camera = root().renderer().camera();
        camera.set_viewport_size(std::max(1, width_ - 352), std::max(1, height_ - 118));
        camera.set_zoom(1.0);
        zoom_level_ = 100;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (key == 'V') {
        set_active_tool(0);
        return true;
    }
    if (key == 'R' && sel_count == 0) {
        set_active_tool(1);
        return true;
    }
    if (key == 'E' && sel_count == 0) {
        set_active_tool(2);
        return true;
    }
    if (key == 'L' && sel_count < 2 && align_reference_ != geometry::AlignReference::Page) {
        (void)root().window().action_dispatcher().dispatch_shortcut(
            shift ? "Shift+L" : "L");
        return true;
    }
    if (key == 'A' || key == 'T') {
        if (sel_count == 0 || (key == 'A')) {
            set_active_tool(6);
            return true;
        }
    }
    if (key == 'K' || (key == 'C' && sel_count == 0)) {
        set_active_tool(7);
        return true;
    }
    return false;
}

#endif
} // namespace pikdraw::desktop
