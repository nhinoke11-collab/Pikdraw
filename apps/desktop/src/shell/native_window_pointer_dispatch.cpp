#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_file_menu.hpp"
#include "native_window_menus.hpp"
#include "native_window_page_presets.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "../tools/gradient_tool_controller.hpp"

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

void NativeWindow::handle_mouse_down(int x, int y) {
    handle_mouse_down(x, y, 0);
}

void NativeWindow::handle_mouse_down(int x, int y, unsigned modifiers) {
    int w = width_;
    int h = height_;
    if (hwnd_) {
        RECT client_rc;
        if (GetClientRect(hwnd_, &client_rc)) {
            w = logical_pixel(client_rc.right - client_rc.left);
            h = logical_pixel(client_rc.bottom - client_rc.top);
            width_ = w;
            height_ = h;
        }
    }

    // Commit an opacity draft on blur, but keep the pointer action on the field while
    // editing. A rejected draft must not silently carry over to a different selection.
    if (active_num_field_ == NumericFieldId::ObjectOpacity) {
        const auto& selected = root().vector_controller().selection().selected_ids();
        const bool single_editable = selected.size() == 1 && workflow().can_set_selected_object_opacity();
        const bool text_mode = active_tool_ == 4 || is_inline_text_editing_ || is_text_object_selected();
        const bool on_property_bar = single_editable && y >= 66 && y <= 102 &&
            (text_mode ? (x >= 920 && x <= 1044 && x + 124 <= w - 280)
                       : (x >= 820 && x <= 944 && x + 124 <= w - 280));
        const bool on_properties_field = active_dock_tab_ == 0 && show_style_inspector_ &&
            x >= w - 280 + 12 && x <= w - 280 + 268 &&
            y >= 162 + 298 - style_inspector_scroll_ &&
            y <= 162 + 328 - style_inspector_scroll_;
        if (!on_property_bar && !on_properties_field) {
            commit_numeric_field();
            if (active_num_field_ == NumericFieldId::ObjectOpacity)
                return;
        }
    }
    if (active_num_field_ == NumericFieldId::FillOpacity ||
        active_num_field_ == NumericFieldId::StrokeOpacity) {
        if (!style_numeric_field_at(x, y)) {
            commit_numeric_field();
            if (active_num_field_ != NumericFieldId::None)
                return;
        }
    }
    if (active_num_field_ == NumericFieldId::EffectSigmaX ||
        active_num_field_ == NumericFieldId::EffectSigmaY ||
        active_num_field_ == NumericFieldId::EffectOffsetX ||
        active_num_field_ == NumericFieldId::EffectOffsetY ||
        active_num_field_ == NumericFieldId::EffectOpacity) {
        if (!style_numeric_field_at(x, y)) {
            commit_numeric_field();
            if (active_num_field_ != NumericFieldId::None)
                return;
        }
    }

    if (handle_modal_and_home_mouse_down(x, y))
        return;

    if (handle_workspace_chrome_mouse_down(x, y, modifiers))
        return;


    // Direct-manipulation handle for the currently focused Drop Shadow. The workflow
    // keeps the whole drag in an edit draft and commits a single appearance command.
    const auto& effect_ids = root().vector_controller().selection().selected_ids();
    const auto effect_snapshot = root().document().current_snapshot();
    if ((modifiers & MK_LBUTTON) != 0 && active_tool_ == 0 && !workflow().has_effect_draft() &&
        effect_ids.size() == 1 && effect_snapshot &&
        workflow().can_edit_selected_effect(property_effect_index_)) {
        const auto summary = selection_appearance(*effect_snapshot, effect_ids);
        if (summary.can_edit_effect(property_effect_index_)) {
            const auto* shadow = std::get_if<document::DropShadowEffect>(
                &summary.effects[property_effect_index_].parameters);
            const auto bounds = root().vector_controller().selection().bounds(*effect_snapshot);
            if (shadow && bounds) {
                const auto handle_world = geometry::Point2D{
                    bounds->center().x + shadow->dx_mm,
                    bounds->center().y + shadow->dy_mm};
                const auto area = canvas_rect(w, h);
                const auto screen = root().renderer().camera().world_to_screen(handle_world);
                const int handle_x = area.left + static_cast<int>(std::lround(screen.x));
                const int handle_y = area.top + static_cast<int>(std::lround(screen.y));
                if (is_canvas_point(x, y) && std::hypot(x - handle_x, y - handle_y) <= 10.0 &&
                    workflow().begin_selected_effect_edit_draft(property_effect_index_) &&
                    workflow().set_effect_draft_preview(true)) {
                    pointer_down_world_ = canvas_to_world(x, y);
                    drag_init_shape_x_ = shadow->dx_mm;
                    drag_init_shape_y_ = shadow->dy_mm;
                    drag_preview_delta_ = {shadow->dx_mm, shadow->dy_mm};
                    drag_start_x_ = x;
                    drag_start_y_ = y;
                    resize_handle_ = -2;
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Kéo tay nắm để đổi lệch X/Y · thả chuột lưu một bước · Esc hủy"
                        : "Drag the handle to change X/Y offset · release saves once · Esc cancels";
                    if (hwnd_) SetCapture(hwnd_);
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return;
                }
                if (workflow().has_effect_draft()) workflow().cancel_effect_draft();
            }
        }
    }
    handle_canvas_mouse_down(x, y, modifiers);
}

void NativeWindow::handle_mouse_move(int x, int y) {
    handle_mouse_move(x, y, 0);
}

void NativeWindow::handle_mouse_move(int x, int y, unsigned modifiers) {
    last_canvas_pointer_x_ = x;
    last_canvas_pointer_y_ = y;
    bool prev_visible = tooltip_.visible;
    std::wstring prev_text = tooltip_.text;

    if (toolbox_open_group_ > 0) {
        const int group_row = toolbox_open_group_ == 1 ? 1
                            : toolbox_open_group_ == 2 ? 2 : 6;
        const int flyout_top = 112 + group_row * 42;
        const int flyout_height = toolbox_open_group_ == 3 ? 145 : 116;
        const int max_items = toolbox_open_group_ == 3 ? 4 : 3;
        if (x >= 54 && x <= 242 && y >= flyout_top + 26 && y < flyout_top + flyout_height) {
            const int item = (y - flyout_top - 26) / 29;
            if (item >= 0 && item < max_items && toolbox_flyout_index_ != item) {
                toolbox_flyout_index_ = item;
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            }
        }
    }

    update_hover_tooltip(x, y);

    if (toolbox_open_group_ == 3) {
        const int flyout_top = 112 + 6 * 42;
        if (x >= 54 && x <= 242 && y >= flyout_top + 26 && y < flyout_top + 145) {
            const int item = (y - flyout_top - 26) / 29;
            if (item == 1 || item == 2) {
                const auto parameters = item == 1
                    ? document::EffectParameters{document::GaussianBlurEffect{1.0, 1.0}}
                    : document::EffectParameters{document::DropShadowEffect{}};
                const bool existing = workflow().selected_effect_index(parameters).has_value();
                const bool available = workflow().can_activate_selected_effect(parameters);
                tooltip_.visible = true;
                tooltip_.x = x;
                tooltip_.y = y;
                tooltip_.text = available
                    ? (language_ == UILanguage::Vietnamese
                        ? std::wstring(existing ? L"Chọn hiệu ứng hiện có để chỉnh; không tạo node mới. Properties có đầy đủ thông số."
                                                : L"Thêm mặc định một lần; sau đó chọn lại để chỉnh trong Properties.")
                        : std::wstring(L""))
                    : (language_ == UILanguage::Vietnamese
                        ? L"Chọn Path/Text không khóa có stack tương thích; bóng chỉ có một node mới mỗi đối tượng."
                        : L"Select unlocked Path/Text with a compatible stack; new shadows are unique per object.");
                if (available && language_ == UILanguage::English)
                    tooltip_.text = existing
                        ? L"Select the existing effect to edit; no new node is created. Full settings are in Properties."
                        : L"Add the default once; select it again to edit in Properties.";
            } else if (item == 3) {
                tooltip_.visible = true;
                tooltip_.x = x;
                tooltip_.y = y;
                tooltip_.text = language_ == UILanguage::Vietnamese
                    ? L"Sửa màu chuyển sắc bằng tay nắm trên đối tượng."
                    : L"Edit a path fill using gradient handles.";
            }
        }
    }

    if (open_menu_id_ >= 0) {
        const int top_item = get_menu_bar_item_at(x, y, 38);
        if (top_item >= 0 && top_item != open_menu_id_) {
            open_menu(top_item);
        } else if (top_item >= 0) {
            hovered_menu_bar_item_ = top_item;
        } else {
            const auto menu = static_cast<MenuId>(open_menu_id_);
            const int menu_height = get_menu_height(menu);
            const RECT anchor = get_menu_bar_item_rect(open_menu_id_, 38);
            const int menu_x = std::clamp(static_cast<int>(anchor.left), 4, std::max(4, width_ - 304));
            const int menu_y = std::clamp(38, 4, std::max(4, height_ - menu_height - 4));
            const bool in_menu = x >= menu_x && x < menu_x + 300 &&
                                 y >= menu_y && y < menu_y + menu_height;
            const auto* item = in_menu ? get_menu_item_at(menu, y - menu_y) : nullptr;
            const int hovered = item ? static_cast<int>(item->action) : -1;
            if (in_menu && item && item->is_heading) {
                tooltip_.visible = false;
                tooltip_.text.clear();
            } else if (in_menu && item &&
                       (item->is_disabled || !is_menu_action_enabled(item->action))) {
                tooltip_.visible = true;
                tooltip_.x = x;
                tooltip_.y = y;
                if (item->is_disabled) {
                    tooltip_.text = language_ == UILanguage::Vietnamese
                        ? L"Chưa có trong phiên bản này"
                        : L"Not available in this version";
                } else {
                    tooltip_.text = language_ == UILanguage::Vietnamese
                        ? L"Chọn đủ đối tượng phù hợp để bật lệnh này"
                        : L"Select enough compatible objects to enable this command";
                }
            } else if (in_menu) {
                tooltip_.visible = false;
                tooltip_.text.clear();
            }
            if (hovered != hovered_menu_item_) {
                hovered_menu_item_ = hovered;
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            }
        }
    } else {
        const int hovered = get_menu_bar_item_at(x, y, 38);
        if (hovered != hovered_menu_bar_item_) {
            hovered_menu_bar_item_ = hovered;
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    if (prev_visible != tooltip_.visible || prev_text != tooltip_.text) {
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    }

    if (resize_handle_ == -2) {
        const auto draft = workflow().effect_draft();
        const auto* source_shadow = draft ? std::get_if<document::DropShadowEffect>(
            &draft->parameters) : nullptr;
        const auto current_world = canvas_to_world(x, y);
        if (!source_shadow || !draft->editing_existing) {
            workflow().cancel_effect_draft();
            resize_handle_ = -1;
            if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
            return;
        }
        auto shadow = *source_shadow;
        shadow.dx_mm = drag_init_shape_x_ + current_world.x - pointer_down_world_.x;
        shadow.dy_mm = drag_init_shape_y_ + current_world.y - pointer_down_world_.y;
        if (!workflow().set_effect_draft_parameters(shadow)) {
            workflow().cancel_effect_draft();
            resize_handle_ = -1;
            if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Bản xem trước bóng không hợp lệ · tài liệu chưa đổi"
                : "Shadow preview is invalid · document unchanged";
        } else {
            drag_preview_delta_ = {shadow.dx_mm, shadow.dy_mm};
            std::ostringstream values;
            values << std::fixed << std::setprecision(1)
                   << shadow.dx_mm << ", " << shadow.dy_mm << " mm";
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Lệch bóng X, Y: " + values.str() + " · Enter lưu · Esc hủy"
                : "Shadow offset X, Y: " + values.str() + " · Enter applies · Esc cancels";
        }
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (is_dragging_guideline_) {
        const auto world_pt = canvas_to_world(x, y);
        if (drag_guideline_orientation_ == GuidelineOrientation::Horizontal) {
            drag_guideline_pos_mm_ = world_pt.y;
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1) << drag_guideline_pos_mm_;
            status_msg_ = (language_ == UILanguage::Vietnamese)
                ? (selected_guideline_index_ >= 0 ? "Dời đường gióng ngang Y: " + ss.str() + " mm" : "Đường gióng ngang Y: " + ss.str() + " mm")
                : (selected_guideline_index_ >= 0 ? "Move horizontal guideline Y: " + ss.str() + " mm" : "Horizontal guideline Y: " + ss.str() + " mm");
        } else {
            drag_guideline_pos_mm_ = world_pt.x;
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1) << drag_guideline_pos_mm_;
            status_msg_ = (language_ == UILanguage::Vietnamese)
                ? (selected_guideline_index_ >= 0 ? "Dời đường gióng dọc X: " + ss.str() + " mm" : "Đường gióng dọc X: " + ss.str() + " mm")
                : (selected_guideline_index_ >= 0 ? "Move vertical guideline X: " + ss.str() + " mm" : "Vertical guideline X: " + ss.str() + " mm");
        }
        if (selected_guideline_index_ >= 0 && selected_guideline_index_ < static_cast<int>(guidelines_.size())) {
            guidelines_[selected_guideline_index_].position_mm = drag_guideline_pos_mm_;
        }
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_tool_ == 8 && pen_tool_ && pen_tool_->active() && !is_panning_ &&
        handle_pen_pointer_move(x, y, modifiers))
        return;

    if (active_tool_ == 11 && node_tool_ && node_tool_->active() && !is_panning_ &&
        handle_node_pointer_move(x, y, modifiers))
        return;

    if (active_tool_ == 13 && gradient_tool_ && gradient_tool_->active() && !is_panning_ &&
        handle_gradient_pointer_move(x, y, modifiers))
        return;

    if (drawing_tool_ && drawing_tool_->active() && !is_panning_) {
        if (is_canvas_point(x, y)) {
            drawing_tool_->pointer_move(canvas_to_world(x, y), root().renderer().camera(),
                window_dpi_scale(), (modifiers & MK_SHIFT) != 0,
                (modifiers & MK_CONTROL) != 0);
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }
    handle_canvas_mouse_move(x, y, modifiers);
}

void NativeWindow::handle_mouse_up(int x, int y) {
    handle_mouse_up(x, y, 0);
}

void NativeWindow::handle_mouse_up(int x, int y, unsigned modifiers) {
    if (resize_handle_ == -2) {
        const bool moved = std::hypot(x - drag_start_x_, y - drag_start_y_) >= 3.0;
        bool applied = false;
        if (moved) {
            const auto draft = workflow().effect_draft();
            if (draft && draft->editing_existing) {
                if (const auto* current_shadow = std::get_if<document::DropShadowEffect>(
                        &draft->parameters)) {
                    auto shadow = *current_shadow;
                    const auto current_world = canvas_to_world(x, y);
                    shadow.dx_mm = drag_init_shape_x_ + current_world.x - pointer_down_world_.x;
                    shadow.dy_mm = drag_init_shape_y_ + current_world.y - pointer_down_world_.y;
                    applied = workflow().set_effect_draft_parameters(shadow) &&
                              workflow().apply_effect_draft();
                }
            }
        } else {
            workflow().cancel_effect_draft();
        }
        if (!applied && workflow().has_effect_draft()) workflow().cancel_effect_draft();
        resize_handle_ = -1;
        if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
        status_msg_ = !moved
            ? (language_ == UILanguage::Vietnamese ? "Không đổi lệch bóng" : "Shadow offset unchanged")
            : (applied
                ? (language_ == UILanguage::Vietnamese
                    ? "Đã cập nhật lệch bóng một bước · Ctrl+Z hoàn tác"
                    : "Updated shadow offset in one step · Ctrl+Z undoes")
                : (language_ == UILanguage::Vietnamese
                    ? "Không thể cập nhật bóng · tài liệu chưa đổi"
                    : "Could not update the shadow · document unchanged"));
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (is_dragging_guideline_) {
        is_dragging_guideline_ = false;
        if (GetCapture() == hwnd_)
            ReleaseCapture();

        const RECT area = canvas_rect(width_, height_);
        const auto world_pt = canvas_to_world(x, y);

        if (selected_guideline_index_ >= 0 && selected_guideline_index_ < static_cast<int>(guidelines_.size())) {
            const int moving_index = selected_guideline_index_;
            selected_guideline_index_ = -1;
            const bool inside = (drag_guideline_orientation_ == GuidelineOrientation::Horizontal)
                ? (y >= area.top && y <= area.bottom)
                : (x >= area.left && x <= area.right);
            if (inside) {
                guidelines_[moving_index].position_mm = drag_guideline_pos_mm_;
                status_msg_ = (language_ == UILanguage::Vietnamese)
                    ? "Đã dời vị trí thước gióng" : "Moved guideline position";
            } else {
                guidelines_.erase(guidelines_.begin() + moving_index);
                status_msg_ = (language_ == UILanguage::Vietnamese)
                    ? "Đã xóa thước gióng kéo ra ngoài bản vẽ" : "Deleted guideline dragged off-canvas";
            }
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }

        if (drag_guideline_orientation_ == GuidelineOrientation::Horizontal) {
            if (y >= area.top && y <= area.bottom) {
                add_guideline(GuidelineOrientation::Horizontal, world_pt.y);
            } else {
                status_msg_ = (language_ == UILanguage::Vietnamese)
                    ? "Đã hủy kéo đường gióng"
                    : "Cancelled guideline";
            }
        } else {
            if (x >= area.left && x <= area.right) {
                add_guideline(GuidelineOrientation::Vertical, world_pt.x);
            } else {
                status_msg_ = (language_ == UILanguage::Vietnamese)
                    ? "Đã hủy kéo đường gióng"
                    : "Cancelled guideline";
            }
        }
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (is_panning_) {
        is_panning_ = false;
        if (drawing_tool_ && drawing_tool_->active()) drawing_tool_->resume_after_pan();
        if (pen_tool_ && pen_tool_->active()) (void)pen_tool_->resume_after_pan();
        if (GetCapture() == hwnd_)
            ReleaseCapture();
        return;
    }
    if (handle_pen_pointer_up(x, y, modifiers))
        return;
    if (handle_node_pointer_up(x, y, modifiers))
        return;
    if (handle_gradient_pointer_up(x, y, modifiers))
        return;
    if (drawing_tool_ && drawing_tool_->active())
        return;
    handle_canvas_mouse_up(x, y, modifiers);
}


#endif

} // namespace pikdraw::desktop
