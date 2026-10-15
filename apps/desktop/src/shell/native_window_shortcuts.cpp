#include "pikdraw/desktop/shell/native_window.hpp"
#include <algorithm>
#include <cmath>

namespace pikdraw::desktop {
#ifdef _WIN32
bool NativeWindow::handle_key_down(WPARAM key) {
    if (show_settings_modal_) {
        if (key == VK_ESCAPE)
            close_settings_modal();
        return true;
    }
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (control) {
        if (key == 'N') {
            create_new_document();
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
            const auto snapshot = root_.document().current_snapshot();
            std::vector<document::ObjectId> ids;
            for (const auto &layer : snapshot->page().layers)
                ids.insert(ids.end(), layer.object_ids.begin(), layer.object_ids.end());
            root_.vector_controller().set_selection(ids, *snapshot);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == 'Z' && !shift) {
            workflow_.undo();
            root_.vector_controller().selection().clear();
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã hoàn tác" : "Undo complete";
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (key == 'Y' || (key == 'Z' && shift)) {
            workflow_.redo();
            root_.vector_controller().selection().clear();
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã làm lại" : "Redo complete";
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
    }
    if (key == VK_ESCAPE) {
        if (show_settings_modal_)
            close_settings_modal();
        else if (show_file_menu_)
            close_file_menu();
        else if (is_panning_) {
            cancel_canvas_interaction();
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (pointer_active_) {
            cancel_canvas_interaction();
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else {
            root_.vector_controller().selection().clear();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return true;
    }
    if (key == VK_DELETE) {
        cancel_canvas_interaction();
        const auto result = root_.vector_controller().delete_selected(root_.document());
        status_msg_ = result.success
                          ? (language_ == UILanguage::Vietnamese ? "Đã xóa đối tượng" : "Objects deleted")
                          : result.error_message;
        workflow_.sync_status_bar();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (key == 'F') {
        fit_page_to_canvas();
        return true;
    }
    if (key == 'H') {
        set_active_tool(3);
        return true;
    }
    if (key == '1') {
        auto &camera = root_.renderer().camera();
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
    if (key == 'R') {
        set_active_tool(1);
        return true;
    }
    if (key == 'E') {
        set_active_tool(2);
        return true;
    }
    return false;
}

#endif
} // namespace pikdraw::desktop
