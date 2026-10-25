#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_selection_geometry.hpp"
#include "native_window_toolbox.hpp"
#include "pikdraw/desktop/shell/native_font_picker.hpp"
#include "native_window_file_menu.hpp"
#include "native_window_menus.hpp"
#include "native_window_page_presets.hpp"
#include "native_window_length_input.hpp"
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

static std::pair<double, double> snap_shadow_offset(double raw_dx, double raw_dy, unsigned modifiers) {
    double dx = raw_dx;
    double dy = raw_dy;
    if ((modifiers & MK_SHIFT) != 0) {
        const double radius = std::hypot(dx, dy);
        if (radius > 1e-4) {
            double angle = std::atan2(dy, dx);
            constexpr double k15Deg = 15.0 * 3.14159265358979323846 / 180.0;
            angle = std::round(angle / k15Deg) * k15Deg;
            dx = radius * std::cos(angle);
            dy = radius * std::sin(angle);
        }
    }
    if ((modifiers & MK_CONTROL) != 0) {
        dx = std::round(dx * 2.0) / 2.0;
        dy = std::round(dy * 2.0) / 2.0;
    } else {
        dx = std::round(dx * 10.0) / 10.0;
        dy = std::round(dy * 10.0) / 10.0;
    }
    return {dx, dy};
}

void NativeWindow::handle_mouse_down(int x, int y) {
    handle_mouse_down(x, y, 0);
}

void NativeWindow::handle_mouse_down(int x, int y, unsigned modifiers) {
    if (import_placement_active() && active_tab().id == import_placement_->target_tab_id) {
        (void)place_pending_import_at(x, y);
        return;
    }
    if (is_font_picker_open()) {
        if (font_picker_->handle_mouse_down(x, y)) {
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
    }
    if (is_inline_text_editing_ && (active_tool_ != 4 || (x < 52 && y >= 122)))
        commit_inline_text_editing();
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
    if (active_num_field_ == NumericFieldId::RasterExposureEv) {
        const auto& selected = root().vector_controller().selection().selected_ids();
        const bool single_editable = selected.size() == 1 && can_edit_selected_bitmap(true);
        const bool on_property_bar = single_editable && y >= 66 && y <= 102 &&
            x >= 720 && x <= 844 && x + 124 <= w - 280;
        const bool on_properties_field = single_editable && active_dock_tab_ == 0 &&
            !show_style_inspector_ && !show_align_panel_ &&
            x >= w - 280 + 14 && x <= w - 14 && y >= 250 && y <= 276;
        if (!on_property_bar && !on_properties_field) {
            commit_numeric_field();
            if (active_num_field_ == NumericFieldId::RasterExposureEv)
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
    if (active_num_field_ == NumericFieldId::BlurSigmaX ||
        active_num_field_ == NumericFieldId::BlurSigmaY ||
        active_num_field_ == NumericFieldId::ShadowFeatherX ||
        active_num_field_ == NumericFieldId::ShadowFeatherY ||
        active_num_field_ == NumericFieldId::ShadowOffsetX ||
        active_num_field_ == NumericFieldId::ShadowOffsetY ||
        active_num_field_ == NumericFieldId::ShadowOpacity) {
        if (!style_numeric_field_at(x, y)) {
            commit_numeric_field();
            if (active_num_field_ != NumericFieldId::None)
                return;
        }
    }

    if (show_settings_modal_ && settings_active_tab_ == 4) {
        if (handle_shortcuts_settings_click(*this, x, y))
            return;
    }

    if (handle_modal_and_home_mouse_down(x, y))
        return;

    if (handle_workspace_chrome_mouse_down(x, y, modifiers))
        return;

    if (!show_style_inspector_ && !show_align_panel_ && active_dock_tab_ == 0 && active_tool_ == 0 &&
        handle_powerclip_controls(x,y,w-AlignPanelMetrics::dock_width+14,138,w-14,true)) return;

    const auto effect_ids = root().vector_controller().selection().selected_ids();
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
                    ribbon_drop_shadow_mode_ = true;
                    const auto ctrl_step = format_mm_display_utf8(0.5, display_length_unit_, 4);
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Kéo bóng · Shift: góc 15° · Ctrl: bước " + ctrl_step + " · thả chuột lưu · Esc hủy"
                        : "Drag shadow · Shift: 15° · Ctrl step " + ctrl_step + " · release saves · Esc cancels";
                    if (hwnd_) SetCapture(hwnd_);
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return;
                }
                if (workflow().has_effect_draft()) workflow().cancel_effect_draft();
            }
        }
    }
    if (active_tool_ == 0 && node_tool_) {
        document::ObjectId rect_id;
        if (is_selected_rectangle(&rect_id)) {
            if (node_tool_->target_object() != rect_id) {
                node_tool_->activate(rect_id);
            }
            node_tool_->set_corner_type(rect_corner_type_);
            const auto world_pt = canvas_to_world(x, y);
            const double zoom = root().renderer().camera().zoom();
            const double tol_mm = (zoom > 1e-4) ? (10.0 / zoom) : 5.0;
            if (node_tool_->tool().hit_test_corner_widget(world_pt, tol_mm, zoom)) {
                is_corner_dragging_in_pick_tool_ = true;
                if (handle_node_pointer_down(x, y, modifiers)) {
                    return;
                }
                is_corner_dragging_in_pick_tool_ = false;
            }
        }
    }
    if (active_tool_ == 11) {
        if (handle_node_pointer_down(x, y, modifiers))
            return;
        return;
    }
    if (active_tool_ == 15 && is_canvas_point(x, y)) {
        if (handle_eyedropper_mouse_down(x, y, modifiers))
            return;
    }
    handle_canvas_mouse_down(x, y, modifiers);
}

void NativeWindow::handle_mouse_move(int x, int y) {
    handle_mouse_move(x, y, 0);
}

void NativeWindow::handle_mouse_move(int x, int y, unsigned modifiers) {
    last_canvas_pointer_x_ = x;
    last_canvas_pointer_y_ = y;
    if (raster_dock_drag_control_ != 0) {
        if (handle_raster_dock_interactive_mouse_move(x, y))
            return;
    }
    if (is_canvas_point(x, y)) {
        if (handle_eyedropper_mouse_move(x, y, modifiers)) {
            if (active_tool_ == 15 && hwnd_) {
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        }
    } else if (hover_color_sample_.valid) {
        hover_color_sample_.valid = false;
        if (active_tool_ == 15 && hwnd_) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
    if (import_placement_active() && active_tab().id == import_placement_->target_tab_id) {
        const std::string color_hint = language_ == UILanguage::Vietnamese
            ? " · giữ ICC nguồn · màu xem trước chưa xác minh"
            : " · source ICC kept · preview color unverified";
        if (is_canvas_point(x, y)) {
            import_placement_->pointer_world = canvas_to_world(x, y);
            import_placement_->pointer_valid = true;
            const std::string position = format_mm_display_utf8(
                    import_placement_->pointer_world.x, display_length_unit_, 3) + ", " +
                format_mm_display_utf8(import_placement_->pointer_world.y,
                                       display_length_unit_, 3);
            status_msg_ = (language_ == UILanguage::Vietnamese
                ? "Vị trí góc trên trái: " : "Top-left position: ") + position +
                (language_ == UILanguage::Vietnamese
                    ? " · nhấp để đặt · Esc hủy" : " · click to place · Esc cancels") +
                color_hint;
        } else {
            import_placement_->pointer_valid = false;
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đưa con trỏ vào canvas · nhấp để đặt · Esc để hủy"
                : "Move into the canvas · click to place · Esc cancels";
            status_msg_ += color_hint;
        }
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    bool prev_visible = tooltip_.visible;
    std::wstring prev_text = tooltip_.text;

    if (toolbox_open_group_ > 0) {
        const int group_row = toolbox_open_group_ == 1 ? get_visible_row_for_slot(ToolboxSlotId::QuickShapes)
                            : toolbox_open_group_ == 2 ? get_visible_row_for_slot(ToolboxSlotId::PathDrawing)
                            : toolbox_open_group_ == 3 ? get_visible_row_for_slot(ToolboxSlotId::Blur)
                            : toolbox_open_group_ == 4 ? get_visible_row_for_slot(ToolboxSlotId::Gradient)
                            : toolbox_open_group_ == 5 ? get_visible_row_for_slot(ToolboxSlotId::Eyedropper)
                            : get_visible_row_for_slot(ToolboxSlotId::Knife);
        const int flyout_top = 112 + (group_row >= 0 ? group_row : 1) * 42;
        const int flyout_height = toolbox_open_group_ == 6 ? 90
            : toolbox_open_group_ == 3 ? 174
            : toolbox_open_group_ == 2 ? 145 : 116;
        const int max_items = toolbox_open_group_ == 6 ? 2
            : toolbox_open_group_ == 3 ? 5
            : toolbox_open_group_ == 2 ? 4 : 3;
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
        if (x >= 54 && x <= 242 && y >= flyout_top + 26 && y < flyout_top + 174) {
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

    if (open_menu_id_ >= 0 && handle_text_case_submenu_mouse_move(x, y)) {
        // The nested Change Case popup owns hover and focus within its two panels.
    } else if (open_menu_id_ >= 0) {
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
                } else if (item->action == MenuActionId::TextFitToPath) {
                    tooltip_.text = language_ == UILanguage::Vietnamese
                        ? L"Chọn 1 Text và 1 Path chưa khóa; giữ Shift+click để thêm đối tượng thứ hai."
                        : L"Select one unlocked Text and one unlocked Path; Shift-click to add the second object.";
                } else if (item->action == MenuActionId::TextConvertArtisticParagraph) {
                    tooltip_.text = language_ == UILanguage::Vietnamese
                        ? L"Chọn một đối tượng Text để đổi giữa chữ tự do và khung đoạn văn. Ctrl+F8."
                        : L"Select one Text object to switch between point text and a paragraph frame. Ctrl+F8.";
                } else if (item->action == MenuActionId::TextStraighten ||
                           item->action == MenuActionId::TextReversePath) {
                    tooltip_.text = language_ == UILanguage::Vietnamese
                        ? L"Chọn một đối tượng chữ đã uốn theo đường dẫn trước."
                        : L"Select a text object already fitted to a path.";
                } else if (item->action == MenuActionId::TextCaseSentence ||
                           item->action == MenuActionId::TextCaseLower ||
                           item->action == MenuActionId::TextCaseUpper ||
                           item->action == MenuActionId::TextCaseTitle ||
                           item->action == MenuActionId::TextCaseToggle) {
                    tooltip_.text = language_ == UILanguage::Vietnamese
                        ? L"Chọn đối tượng Text chưa khóa; khi soạn, áp dụng lên vùng bôi đen hoặc toàn bộ chữ."
                        : L"Select unlocked text objects; while editing, applies to the selection or all text.";
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

    if (is_font_picker_open()) {
        if (font_picker_->handle_mouse_move(x, y)) {
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
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
        const auto snapped = snap_shadow_offset(
            drag_init_shape_x_ + current_world.x - pointer_down_world_.x,
            drag_init_shape_y_ + current_world.y - pointer_down_world_.y,
            modifiers);
        shadow.dx_mm = snapped.first;
        shadow.dy_mm = snapped.second;
        if (!workflow().set_effect_draft_parameters(shadow)) {
            workflow().cancel_effect_draft();
            resize_handle_ = -1;
            if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Bản xem trước bóng không hợp lệ · tài liệu chưa đổi"
                : "Shadow preview is invalid · document unchanged";
        } else {
            drag_preview_delta_ = {shadow.dx_mm, shadow.dy_mm};
            const std::string values = format_mm_display_utf8(shadow.dx_mm, display_length_unit_, 5) +
                ", " + format_mm_display_utf8(shadow.dy_mm, display_length_unit_, 5);
            std::string mod_hints;
            if ((modifiers & MK_SHIFT) != 0) mod_hints += " · Shift: 15°";
            if ((modifiers & MK_CONTROL) != 0)
                mod_hints += " · Ctrl: " + format_mm_display_utf8(0.5, display_length_unit_, 4);
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Lệch bóng X, Y: " + values + mod_hints + " · Enter lưu · Esc hủy"
                : "Shadow offset X, Y: " + values + mod_hints + " · Enter applies · Esc cancels";
        }
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (is_dragging_guideline_) {
        const auto world_pt = canvas_to_world(x, y);
        if (drag_guideline_orientation_ == GuidelineOrientation::Horizontal) {
            drag_guideline_pos_mm_ = world_pt.y;
            const auto value = format_mm_display_utf8(drag_guideline_pos_mm_, display_length_unit_, 5);
            status_msg_ = (language_ == UILanguage::Vietnamese)
                ? (selected_guideline_index_ >= 0 ? "Dời đường gióng ngang Y: " + value : "Đường gióng ngang Y: " + value)
                : (selected_guideline_index_ >= 0 ? "Move horizontal guideline Y: " + value : "Horizontal guideline Y: " + value);
        } else {
            drag_guideline_pos_mm_ = world_pt.x;
            const auto value = format_mm_display_utf8(drag_guideline_pos_mm_, display_length_unit_, 5);
            status_msg_ = (language_ == UILanguage::Vietnamese)
                ? (selected_guideline_index_ >= 0 ? "Dời đường gióng dọc X: " + value : "Đường gióng dọc X: " + value)
                : (selected_guideline_index_ >= 0 ? "Move vertical guideline X: " + value : "Vertical guideline X: " + value);
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

    if ((active_tool_ == 11 || is_corner_dragging_in_pick_tool_) && node_tool_ && node_tool_->active() && !is_panning_ &&
        handle_node_pointer_move(x, y, modifiers))
        return;

    if (gradient_ramp_drag_kind_ != GradientRampDragKind::None &&
        handle_gradient_ramp_pointer_move(x, y))
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
    if (raster_dock_drag_control_ != 0) {
        if (handle_raster_dock_interactive_mouse_up(x, y))
            return;
    }
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
                    const auto snapped = snap_shadow_offset(
                        drag_init_shape_x_ + current_world.x - pointer_down_world_.x,
                        drag_init_shape_y_ + current_world.y - pointer_down_world_.y,
                        modifiers);
                    shadow.dx_mm = snapped.first;
                    shadow.dy_mm = snapped.second;
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
            // Keep selected_guideline_index_ so user can delete or inspect it
            const bool inside = (drag_guideline_orientation_ == GuidelineOrientation::Horizontal)
                ? (y >= area.top && y <= area.bottom)
                : (x >= area.left && x <= area.right);
            if (inside) {
                guidelines_[moving_index].position_mm = drag_guideline_pos_mm_;
                status_msg_ = (language_ == UILanguage::Vietnamese)
                    ? "Đã chọn thước gióng · Nhấn Delete để xóa" : "Selected guideline · Press Delete to remove";
            } else {
                guidelines_.erase(guidelines_.begin() + moving_index);
                selected_guideline_index_ = -1;
                status_msg_ = (language_ == UILanguage::Vietnamese)
                    ? "Đã xóa thước gióng kéo ra ngoài bản vẽ" : "Deleted guideline dragged off-canvas";
            }
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }

        if (drag_guideline_orientation_ == GuidelineOrientation::Horizontal) {
            if (y >= area.top && y <= area.bottom) {
                add_guideline(GuidelineOrientation::Horizontal, world_pt.y);
                selected_guideline_index_ = static_cast<int>(guidelines_.size()) - 1;
            } else {
                status_msg_ = (language_ == UILanguage::Vietnamese)
                    ? "Đã hủy kéo đường gióng"
                    : "Cancelled guideline";
            }
        } else {
            if (x >= area.left && x <= area.right) {
                add_guideline(GuidelineOrientation::Vertical, world_pt.x);
                selected_guideline_index_ = static_cast<int>(guidelines_.size()) - 1;
            } else {
                status_msg_ = (language_ == UILanguage::Vietnamese)
                    ? "Đã hủy kéo đường gióng"
                    : "Cancelled guideline";
            }
        }
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (is_font_picker_open()) {
        if (font_picker_->handle_mouse_up(x, y)) {
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
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
    if (handle_node_pointer_up(x, y, modifiers)) {
        is_corner_dragging_in_pick_tool_ = false;
        return;
    }
    is_corner_dragging_in_pick_tool_ = false;
    if (gradient_ramp_drag_kind_ != GradientRampDragKind::None &&
        handle_gradient_ramp_pointer_up(x, y))
        return;
    if (handle_gradient_pointer_up(x, y, modifiers))
        return;
    if (drawing_tool_ && drawing_tool_->active())
        return;
    handle_canvas_mouse_up(x, y, modifiers);
}


#endif

} // namespace pikdraw::desktop
