#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_file_menu.hpp"
#include "native_window_menus.hpp"
#include "native_window_page_presets.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "../tools/gradient_tool_controller.hpp"
#include "pikdraw/desktop/shell/native_window_shortcuts_settings.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windowsx.h>
#endif

namespace pikdraw::desktop {

#ifdef _WIN32

double NativeWindow::window_dpi_scale() const noexcept {
    // Drawing controllers receive the same DIP coordinates as the camera.
    return 1.0;
}

LRESULT CALLBACK NativeWindow::window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<NativeWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (uMsg) {
    case kImportWorkerMessage:
        if (self) self->handle_import_worker_message(wParam);
        return 0;
    case kExportWorkerMessage:
        if (self) self->handle_export_worker_message(wParam);
        return 0;
    case WM_CHAR:
        if (self && self->show_recovery_modal_)
            return 0;
        if (self && self->show_settings_modal_ && self->settings_active_tab_ == 6)
            return 0;
        if (self && self->active_numeric_field() != NumericFieldId::None) {
            self->handle_numeric_char_input(static_cast<wchar_t>(wParam));
            return 0;
        }
        if (self && self->is_quick_dimension_active() && !self->is_inline_text_editing_) {
            if (wParam == VK_BACK) {
                if (!self->quick_dimension_buffer_.empty()) {
                    self->quick_dimension_buffer_.pop_back();
                    self->status_msg_ = (self->language_ == UILanguage::Vietnamese ? "Kích thước: " : "Dimensions: ") +
                                        self->quick_dimension_buffer_ +
                                        (self->language_ == UILanguage::Vietnamese ? " mm (Enter để nhận, Esc hủy)" : " mm (Enter to apply, Esc to cancel)");
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (wParam == VK_RETURN) {
                self->apply_quick_dimension();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                self->end_quick_dimension();
                self->status_msg_ = self->language_ == UILanguage::Vietnamese ? "Đã hủy nhập kích thước" : "Cancelled dimension input";
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if ((wParam >= '0' && wParam <= '9') || wParam == '.' || wParam == ',' || wParam == ';' ||
                wParam == 'x' || wParam == 'X' || wParam == ' ' || wParam == '-') {
                self->quick_dimension_buffer_ += static_cast<char>(wParam);
                self->status_msg_ = (self->language_ == UILanguage::Vietnamese ? "Kích thước: " : "Dimensions: ") +
                                    self->quick_dimension_buffer_ +
                                    (self->language_ == UILanguage::Vietnamese ? " mm (Enter để nhận, Esc hủy)" : " mm (Enter to apply, Esc to cancel)");
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            // Ký tự khác khi đang ở chế độ gõ nhanh thì kết thúc gõ nhanh và để lệnh tiếp diễn
            self->end_quick_dimension();
        }
        if (self && self->workflow().is_array_session_active() && !self->is_inline_text_editing_) {
            if (wParam == VK_BACK) {
                self->handle_array_backspace();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_RETURN) {
                self->commit_array_input();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                self->workflow().end_array_session();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if ((wParam >= '0' && wParam <= '9') || wParam == '*' || wParam == 'x' || wParam == 'X' || wParam == '/') {
                self->handle_array_char(static_cast<char>(wParam));
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        if (self && self->workflow().effect_draft_numeric_input_active()) {
            if (wParam == VK_BACK || wParam == VK_RETURN || wParam == VK_ESCAPE || wParam == VK_TAB)
                return 0;
            if (wParam <= 0x7e && self->workflow().input_effect_draft_character(static_cast<char>(wParam))) {
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam <= 0x7e) return 0;
        }
        if (self && !self->is_inline_text_editing_ && wParam >= 0x20 && wParam <= 0x7e &&
            self->handle_pen_character(static_cast<char>(wParam)))
            return 0;
        if (self && !self->is_inline_text_editing_ && wParam >= 0x20 && wParam <= 0x7e &&
            self->handle_drawing_character(static_cast<char>(wParam)))
            return 0;
        if (self && self->handle_inline_text_character(static_cast<uint32_t>(wParam))) return 0;
        if (self && wParam >= 0x20 && wParam <= 0x7e &&
            self->handle_drawing_character(static_cast<char>(wParam)))
            return 0;
        break;
    case WM_UNICHAR:
        if (wParam == UNICODE_NOCHAR) return TRUE;
        if (self && self->handle_inline_text_character(static_cast<uint32_t>(wParam))) return 0;
        break;
    case WM_IME_COMPOSITION:
        if (self && self->handle_inline_text_ime(lParam)) return 0;
        break;
    case WM_IME_ENDCOMPOSITION:
        if (self && self->text_editor_) {
            self->text_editor_->composition.clear();
            self->refresh_inline_text_preview();
        }
        break;
    case WM_SYSKEYDOWN:
        if (self && self->show_recovery_modal_)
            return 0;
        if (self && wParam == VK_F10) {
            const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            if (alt) {
                if (self->is_menu_open()) self->close_menu();
                else self->open_menu(static_cast<int>(MenuId::File));
            } else {
                self->set_active_tool(11);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (self && self->handle_toolbox_key(
                wParam, true, (GetKeyState(VK_SHIFT) & 0x8000) != 0)) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (self && self->show_recovery_modal_ && self->handle_recovery_key(wParam))
            return 0;
        if (self && wParam == VK_ESCAPE && self->export_job_active()) {
            self->request_cancel_export_job();
            return 0;
        }
        if (self && wParam == VK_ESCAPE && self->import_job_active()) {
            self->request_cancel_import_job();
            return 0;
        }
        if (self && self->is_menu_open()) {
            const auto menu = static_cast<MenuId>(self->open_menu_id());
            if (wParam == VK_ESCAPE) {
                self->close_menu();
                return 0;
            }
            if (wParam == VK_DOWN || wParam == VK_UP) {
                const auto action = get_adjacent_menu_action(
                    menu, self->hovered_menu_item_, wParam == VK_UP ? -1 : 1,
                    [self](MenuActionId candidate) { return self->is_menu_action_enabled(candidate); });
                self->hovered_menu_item_ = action ? static_cast<int>(*action) : -1;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_LEFT || wParam == VK_RIGHT) {
                const int direction = wParam == VK_LEFT ? -1 : 1;
                const int count = static_cast<int>(MenuId::Count);
                const int next = (self->open_menu_id() + direction + count) % count;
                self->open_menu(next);
                return 0;
            }
            if (wParam == VK_RETURN || wParam == VK_SPACE) {
                const auto action = self->hovered_menu_item_ >= 0
                    ? std::optional(static_cast<MenuActionId>(self->hovered_menu_item_))
                    : get_adjacent_menu_action(menu, -1, 1,
                        [self](MenuActionId candidate) { return self->is_menu_action_enabled(candidate); });
                self->close_menu();
                if (action && self->is_menu_action_enabled(*action))
                    self->execute_menu_action(static_cast<int>(*action));
                return 0;
            }
        }
        if (self && self->show_settings_modal_ && self->settings_active_tab_ == 6) {
            if (wParam == VK_ESCAPE) {
                self->close_settings_modal();
                return 0;
            }
            if (wParam == VK_TAB) {
                constexpr int kFocusCount = 7;
                const int step = (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1;
                self->settings_ai_focus_index_ =
                    (self->settings_ai_focus_index_ + step + kFocusCount) % kFocusCount;
                if (self->settings_ai_focus_index_ == 2 || self->settings_ai_focus_index_ == 3) {
                    const bool endpoint = self->settings_ai_focus_index_ == 2;
                    self->status_msg_ = self->language_ == UILanguage::Vietnamese
                        ? (endpoint
                            ? "Chép URL MCP · nhấn Enter hoặc Space"
                            : "Chép khóa MCP · nhấn Enter hoặc Space")
                        : (endpoint
                            ? "Copy MCP URL · press Enter or Space"
                            : "Copy MCP access key · press Enter or Space");
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_RETURN || wParam == VK_SPACE) {
                self->activate_ai_connections_focus();
                return 0;
            }
            return 0;
        }
        if (self && !self->is_menu_open() && self->handle_inline_text_key(
                static_cast<uint32_t>(wParam), (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                (GetKeyState(VK_SHIFT) & 0x8000) != 0)) return 0;
        if (self && self->handle_toolbox_key(
                wParam, false, (GetKeyState(VK_SHIFT) & 0x8000) != 0)) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (self && self->workflow().has_effect_draft()) {
            auto &workflow = self->workflow();
            if (wParam == VK_ESCAPE) {
                if (self->resize_handle_ == -2) {
                    self->resize_handle_ = -1;
                    if (GetCapture() == hwnd) ReleaseCapture();
                }
                workflow.cancel_effect_draft();
                self->status_msg_ = self->language_ == UILanguage::Vietnamese
                    ? "Đã hủy bản nháp/chỉnh hiệu ứng · tài liệu không đổi"
                    : "Effect draft/edit cancelled · document unchanged";
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_RETURN) {
                const auto draft = workflow.effect_draft();
                const bool edits_existing = draft && draft->editing_existing;
                const bool editing = workflow.effect_draft_numeric_input_active();
                const bool applied = editing ? workflow.commit_effect_draft_numeric_input()
                                             : workflow.apply_effect_draft();
                self->status_msg_ = self->language_ == UILanguage::Vietnamese
                    ? (applied ? (editing ? "Đã cập nhật thông số bản nháp · Apply để lưu"
                                      : (edits_existing
                                          ? "Đã cập nhật hiệu ứng hiện có · Ctrl+Z hoàn tác"
                                          : "Đã thêm hiệu ứng bằng một bước Undo · Ctrl+Z hoàn tác"))
                               : "Thông số không hợp lệ hoặc bản nháp đã cũ")
                    : (applied ? (editing ? "Draft value updated · Apply to keep"
                                      : (edits_existing
                                          ? "Updated existing effect · Ctrl+Z undoes"
                                          : "Applied effect in one undo step · Ctrl+Z undoes"))
                               : "Invalid value or stale effect draft");
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_TAB && workflow.effect_draft_numeric_input_active()) {
                (void)workflow.focus_next_effect_draft_numeric_field();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_BACK && workflow.effect_draft_numeric_input_active()) {
                (void)workflow.backspace_effect_draft_numeric_input();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (!workflow.effect_draft_numeric_input_active() &&
                (wParam == 'V' || wParam == 'R' || wParam == 'E' || wParam == 'K' ||
                 wParam == 'C' || wParam == 'A' || wParam == 'T' || wParam == 'H')) {
                workflow.cancel_effect_draft();
                self->status_msg_ = self->language_ == UILanguage::Vietnamese
                    ? "Đã hủy bản nháp khi chuyển công cụ"
                    : "Effect draft cancelled on tool change";
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        if (self && wParam == VK_SPACE) {
            self->space_down_ = true;
            return 0;
        }
        if (self && self->show_settings_modal_ && self->settings_active_tab_ == 4) {
            if (handle_shortcuts_settings_key(*self, wParam))
                return 0;
        }
        if (self && !self->show_settings_modal_ && dispatch_canvas_shortcuts(*self, wParam))
            return 0;
        if (self && self->handle_key_down(wParam))
            return 0;
        break;
    case WM_KEYUP:
        if (self && wParam == VK_SPACE) {
            self->space_down_ = false;
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        if (self) {
            bool focus_remains_in_owned_dialog = false;
            for (HWND focus = reinterpret_cast<HWND>(wParam); focus;
                 focus = GetWindow(focus, GW_OWNER)) {
                if (focus == hwnd) {
                    focus_remains_in_owned_dialog = true;
                    break;
                }
            }
            if (self->workflow().has_effect_draft() && !focus_remains_in_owned_dialog) {
                self->workflow().cancel_effect_draft();
                self->status_msg_ = self->language_ == UILanguage::Vietnamese
                    ? "Đã hủy bản nháp hiệu ứng khi rời cửa sổ"
                    : "Effect draft cancelled when the window lost focus";
            }
            if (self->active_num_field_ == NumericFieldId::PenExpression) {
                self->cancel_numeric_field();
            } else {
                self->commit_numeric_field();
            }
            self->space_down_ = false;
            if (self->drawing_tool_ && self->drawing_tool_->active()) {
                KillTimer(hwnd, 9);
                if (self->is_panning_)
                    self->drawing_tool_->resume_after_pan();
                self->status_msg_ = self->language_ == UILanguage::Vietnamese
                    ? "Tạm dừng đường vẽ · bản nháp được giữ lại"
                    : "Drawing paused · draft preserved";
            }
            if (self->is_panning_) {
                self->is_panning_ = false;
                if (GetCapture() == hwnd) ReleaseCapture();
            }
            if (self->pen_tool_ && self->pen_tool_->active()) {
                if ((self->pen_tool_->pointer_pending() || self->pen_close_candidate_) &&
                    GetCapture() == hwnd)
                    ReleaseCapture();
                (void)self->pen_tool_->on_focus_lost();
                self->pen_pointer_dragged_ = false;
                self->pen_close_candidate_ = false;
                self->status_msg_ = self->language_ == UILanguage::Vietnamese
                    ? "Tạm dừng Pen · bản nháp được giữ lại"
                    : "Pen paused · draft preserved";
            }
        }
        break;
    case WM_SETFOCUS:
        if (self && self->drawing_tool_ && self->drawing_tool_->active()) {
            SetTimer(hwnd, 9, 33, nullptr);
            self->status_msg_ = self->language_ == UILanguage::Vietnamese
                ? "Tiếp tục đường vẽ" : "Drawing resumed";
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (self && self->pen_tool_ && self->pen_tool_->active()) {
            self->status_msg_ = self->language_ == UILanguage::Vietnamese
                ? "Tiếp tục Pen · nhấp điểm kế hoặc Enter để kết thúc"
                : "Pen resumed · click the next point or press Enter to finish";
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;
    case WM_LBUTTONDOWN:
        if (self) {
            SetFocus(hwnd);
            self->handle_mouse_down(self->logical_pixel(GET_X_LPARAM(lParam)), self->logical_pixel(GET_Y_LPARAM(lParam)),
                                    static_cast<unsigned>(wParam));
        }
        return 0;
    case WM_LBUTTONDBLCLK:
        if (self) {
            SetFocus(hwnd);
            self->handle_double_click(self->logical_pixel(GET_X_LPARAM(lParam)), self->logical_pixel(GET_Y_LPARAM(lParam)));
        }
        return 0;
    case WM_MOUSEMOVE:
        if (self && !self->show_recovery_modal_) self->handle_mouse_move(self->logical_pixel(GET_X_LPARAM(lParam)), self->logical_pixel(GET_Y_LPARAM(lParam)),
                                          static_cast<unsigned>(wParam));
        return 0;
    case WM_LBUTTONUP:
        if (self) self->handle_mouse_up(self->logical_pixel(GET_X_LPARAM(lParam)), self->logical_pixel(GET_Y_LPARAM(lParam)),
                                        static_cast<unsigned>(wParam));
        return 0;
    case WM_RBUTTONDOWN:
        if (self && !self->show_recovery_modal_)
            self->handle_style_click(self->logical_pixel(GET_X_LPARAM(lParam)), self->logical_pixel(GET_Y_LPARAM(lParam)), true);
        return 0;
    case WM_MBUTTONDOWN:
        if (self && self->show_recovery_modal_) return 0;
        if (self && self->is_canvas_point(self->logical_pixel(GET_X_LPARAM(lParam)), self->logical_pixel(GET_Y_LPARAM(lParam)))) {
            if (self->drawing_tool_ && self->drawing_tool_->active())
                self->drawing_tool_->suspend_for_pan();
            else if (self->pen_tool_ && self->pen_tool_->active())
                (void)self->pen_tool_->suspend_for_pan();
            else
                self->cancel_canvas_interaction();
            self->is_panning_ = true;
            self->last_pointer_x_ = self->logical_pixel(GET_X_LPARAM(lParam));
            self->last_pointer_y_ = self->logical_pixel(GET_Y_LPARAM(lParam));
            SetCapture(hwnd);
        }
        return 0;
    case WM_MBUTTONUP:
        if (self && self->show_recovery_modal_) return 0;
        if (self && self->is_panning_) {
            self->is_panning_ = false;
            if (self->drawing_tool_ && self->drawing_tool_->active())
                self->drawing_tool_->resume_after_pan();
            if (self->pen_tool_ && self->pen_tool_->active())
                (void)self->pen_tool_->resume_after_pan();
            if (GetCapture() == hwnd) ReleaseCapture();
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (self && self->show_recovery_modal_) return 0;
        if (self) {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &point);
            point.x = self->logical_pixel(point.x);
            point.y = self->logical_pixel(point.y);
            if (self->show_settings_modal_ && self->settings_active_tab_ == 4) {
                if (handle_shortcuts_settings_wheel(*self, GET_WHEEL_DELTA_WPARAM(wParam)))
                    return 0;
            }
            if (self->active_dock_tab_ == 0 && self->show_style_inspector_ &&
                point.x >= self->width_ - 280 && point.x < self->width_ &&
                point.y >= 162 && point.y < self->height_ - 26) {
                self->scroll_style_inspector(GET_WHEEL_DELTA_WPARAM(wParam));
            } else if (self->is_canvas_point(point.x, point.y)) {
                const auto area = self->canvas_rect(self->width_, self->height_);
                auto& camera = self->root().renderer().camera();
                const double factor = std::pow(1.1,
                    static_cast<double>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA);
                const double target_zoom = std::clamp(camera.zoom() * factor, 0.01, 64.0);
                camera.zoom_at(target_zoom / camera.zoom(),
                    static_cast<float>(point.x - area.left),
                    static_cast<float>(point.y - area.top));
                self->zoom_level_ = std::clamp(
                    static_cast<int>(std::lround(camera.zoom() * 100.0)), 1, 6400);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_TIMER:
        if (self && wParam == 12 && self->text_editor_) {
            self->text_editor_->caret_visible = !self->text_editor_->caret_visible;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (self && wParam == kAutoSaveTimerId) {
            self->autosave_documents();
            return 0;
        }
        if (self && wParam == 9 && self->drawing_tool_ && self->drawing_tool_->active()) {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd, &point);
            point.x = self->logical_pixel(point.x);
            point.y = self->logical_pixel(point.y);
            if (self->is_canvas_point(point.x, point.y)) {
                const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                self->last_canvas_pointer_x_ = point.x;
                self->last_canvas_pointer_y_ = point.y;
                self->drawing_tool_->pointer_move(self->canvas_to_world(point.x, point.y),
                    self->root().renderer().camera(), self->window_dpi_scale(), shift, control);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_SETCURSOR:
        if (self && LOWORD(lParam) == HTCLIENT) {
            const WORD cursor_id = self->is_panning_ ? 32646
                : self->active_tool_ == 3 ? 32649
                : self->active_tool_ == 4 ? 32513 : self->active_tool_ >= 6 ? 32515 : 32512;
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(cursor_id)));
            return TRUE;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (self) self->paint(hdc, self->width_, self->height_);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DPICHANGED:
        if (self && HIWORD(wParam) > 0) {
            self->cancel_canvas_interaction();
            self->dpi_scale_ = HIWORD(wParam) / 96.0f;
            self->cleanup_ui_fonts();
            self->cleanup_canvas_cache();
            self->cleanup_backbuffer();
            self->ensure_ui_fonts();
            if (const auto* bounds = reinterpret_cast<const RECT*>(lParam))
                SetWindowPos(hwnd, nullptr, bounds->left, bounds->top,
                    bounds->right - bounds->left, bounds->bottom - bounds->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            RECT client{};
            if (GetClientRect(hwnd, &client)) {
                self->width_ = self->logical_pixel(client.right);
                self->height_ = self->logical_pixel(client.bottom);
                self->shell_.set_window_size(self->width_, self->height_);
                const auto area = self->canvas_rect(self->width_, self->height_);
                self->root().renderer().camera().set_viewport_size(
                    area.right - area.left, area.bottom - area.top);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_SIZE: {
        int w = self ? self->logical_pixel(LOWORD(lParam)) : LOWORD(lParam);
        int h = self ? self->logical_pixel(HIWORD(lParam)) : HIWORD(lParam);
        if (self && w > 0 && h > 0) {
            self->width_ = w;
            self->height_ = h;
            self->shell_.set_window_size(static_cast<double>(w), static_cast<double>(h));
            const auto area = self->canvas_rect(w, h);
            self->root().renderer().camera().set_viewport_size(
                area.right - area.left, area.bottom - area.top);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (self) {
            if (self->text_editor_) self->text_editor_->selecting = self->text_editor_->creating_frame = false;
            if (self->resize_handle_ == -2) {
                self->resize_handle_ = -1;
                self->workflow().cancel_effect_draft();
                self->status_msg_ = self->language_ == UILanguage::Vietnamese
                    ? "Đã hủy kéo tay nắm bóng · tài liệu không đổi"
                    : "Shadow handle drag cancelled · document unchanged";
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            if (self->drawing_tool_ && self->drawing_tool_->active()) {
                self->is_panning_ = false;
                self->drawing_tool_->resume_after_pan();
            }
            if (self->pen_tool_ && self->pen_tool_->active()) {
                if (self->is_panning_) {
                    (void)self->pen_tool_->resume_after_pan();
                } else if (self->pen_tool_->pointer_pending()) {
                    (void)self->pen_tool_->on_capture_lost();
                    self->pen_pointer_dragged_ = false;
                    self->pen_close_candidate_ = false;
                    self->status_msg_ = self->language_ == UILanguage::Vietnamese
                        ? "Đã hủy thao tác kéo Pen chưa xác nhận · bản nháp được giữ lại"
                        : "Unconfirmed Pen gesture cancelled · draft preserved";
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            self->is_panning_ = false;
            self->is_dragging_guideline_ = false;
            if (self->pointer_active_) {
                self->root().vector_controller().on_escape();
                self->pointer_active_ = false;
                self->is_dragging_ = false;
                self->is_creating_shape_ = false;
                self->drag_preview_bounds_.reset();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kAutoSaveTimerId);
        KillTimer(hwnd, 12);
        if (self) {
            self->cancel_import_job();
            self->cancel_export_job();
        }
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

#endif

} // namespace pikdraw::desktop
