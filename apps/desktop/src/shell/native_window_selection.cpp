#include "native_window_selection_geometry.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "pikdraw/vector/gestures/selection_resize.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace pikdraw::desktop {
#ifdef _WIN32
WORD NativeWindow::canvas_cursor_at(int x, int y) const {
    if (show_settings_modal_ || show_file_menu_ || show_home_screen_ || !is_canvas_point(x, y))
        return 32512;
    if (is_panning_)
        return 32646;
    if (active_tool_ == 3)
        return 32649;
    const int handle = resize_handle_ >= 0 ? resize_handle_ : selection_handle_at(x, y);
    if (handle == 9)
        return 32646; // IDC_SIZEALL: 4-way move cursor for the rotation pivot.
    if (handle == 8)
        return 32649; // IDC_HAND: direct manipulation on the rotation affordance.
    if (is_rotate_mode_ && (handle == 0 || handle == 2 || handle == 4 || handle == 6))
        return 32649; // IDC_HAND: rotation affordance on corner handles in rotate mode.
    if (handle >= 0) {
        constexpr WORD cursors[] = {32642, 32645, 32643, 32644, 32642, 32645, 32643, 32644};
        return cursors[handle];
    }
    if (active_tool_ == 1 || active_tool_ == 2 || active_tool_ == 9)
        return 32515;
    if (active_tool_ == 0 && show_guidelines_) {
        const int g_idx = guideline_at(x, y);
        if (g_idx >= 0 && g_idx < static_cast<int>(guidelines_.size())) {
            return guidelines_[g_idx].orientation == GuidelineOrientation::Horizontal ? 32645 : 32644;
        }
    }
    return hover_object_ || is_dragging_ ? 32646 : 32512;
}
int NativeWindow::selection_handle_at(int x, int y) const {
    if (active_tool_ != 0 || !is_canvas_point(x, y))
        return -1;
    const auto snapshot = root().document().current_snapshot();
    const auto bounds = root().vector_controller().selection().bounds(*snapshot);
    if (!bounds)
        return -1;
    const auto area = canvas_rect(width_, height_);
    const auto rect = selection_ui::screen_bounds(
        root().renderer().camera(), *bounds, area);
    const auto pixels = selection_ui::handles(rect);
    const auto rotate = selection_ui::rotation_handle(rect);
    if (std::abs(x - rotate.x) <= 8 && std::abs(y - rotate.y) <= 8)
        return 8;

    // Center pivot point (handle 9) - only active and draggable in rotate mode
    if (is_rotate_mode_) {
        const auto pivot_world = custom_pivot_world_.value_or(bounds->center());
        const auto pivot_screen = selection_ui::pivot_point(root().renderer().camera(), pivot_world, area);
        if (std::abs(x - pivot_screen.x) <= 8 && std::abs(y - pivot_screen.y) <= 8)
            return 9;
    }

    for (size_t i = 0; i < pixels.size(); ++i)
        if (std::abs(x - pixels[i].x) <= 8 && std::abs(y - pixels[i].y) <= 8)
            return static_cast<int>(i);
    return -1;
}

void NativeWindow::cancel_canvas_interaction() {
    root().vector_controller().on_escape();
    pointer_active_ = is_dragging_ = is_creating_shape_ = is_marquee_ = is_panning_ = false;
    is_knife_cutting_ = false;
    resize_handle_ = -1;
    drag_preview_bounds_.reset();
    drag_preview_delta_ = {};
    if (GetCapture() == hwnd_)
        ReleaseCapture();
}

void NativeWindow::handle_canvas_mouse_down(int x, int y, unsigned modifiers) {
    if (!is_canvas_point(x, y))
        return;
    cancel_canvas_interaction();
    SetCapture(hwnd_);
    if (active_tool_ == 3) {
        is_panning_ = true;
        last_pointer_x_ = x;
        last_pointer_y_ = y;
        return;
    }
    pointer_active_ = true;
    pointer_down_world_ = pointer_current_world_ = canvas_to_world(x, y);
    shape_drag_modifiers_ = modifiers & (MK_CONTROL | MK_SHIFT);
    drag_start_x_ = x;
    drag_start_y_ = y;
    pointer_selection_mode_ = (modifiers & MK_CONTROL) ? vector::SelectionMode::Toggle
                              : (modifiers & MK_SHIFT) ? vector::SelectionMode::Add
                                                       : vector::SelectionMode::Replace;
    const auto snapshot = root().document().current_snapshot();
    pointer_stamp_ = vector::GestureStamp(*snapshot);
    pointer_start_revision_ = snapshot->revision();
    auto &controller = root().vector_controller();
    if (active_tool_ == 5) {
        is_knife_cutting_ = true;
        knife_start_world_ = pointer_down_world_;
        knife_current_world_ = pointer_down_world_;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (active_tool_ == 4) {
        if (is_inline_text_editing_)
            commit_inline_text_editing();
        auto hit = vector::SelectionModel::hit_test(pointer_down_world_, *snapshot);
        if (hit && is_in_isolation_mode() && !is_object_in_isolated_group(*hit))
            hit.reset();
        const auto object = hit ? snapshot->objects().get(*hit) : std::nullopt;
        if (object && std::holds_alternative<document::TextObject>(*object))
            start_inline_text_editing(pointer_down_world_, *hit);
        else
            start_inline_text_editing(pointer_down_world_, document::kInvalidObjectId);
        pointer_active_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (active_tool_ == 12) {
        const double tolerance = 6.0 / root().renderer().camera().zoom();
        auto hit = vector::SelectionModel::hit_test(pointer_down_world_, *snapshot, tolerance);
        if (hit) {
            controller.set_selection({*hit}, *snapshot);
            if (contour_tool_) {
                contour_tool_->activate(*hit);
            }
        }
        pointer_active_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (active_tool_ == 0) {
        if (show_guidelines_) {
            const int g_idx = guideline_at(x, y);
            if (g_idx >= 0 && g_idx < static_cast<int>(guidelines_.size())) {
                selected_guideline_index_ = g_idx;
                is_dragging_guideline_ = true;
                drag_guideline_orientation_ = guidelines_[g_idx].orientation;
                drag_guideline_pos_mm_ = guidelines_[g_idx].position_mm;
                status_msg_ = (language_ == UILanguage::Vietnamese)
                    ? (drag_guideline_orientation_ == GuidelineOrientation::Horizontal
                        ? "Đang di chuyển thước gióng ngang..." : "Đang di chuyển thước gióng dọc...")
                    : (drag_guideline_orientation_ == GuidelineOrientation::Horizontal
                        ? "Moving horizontal guideline..." : "Moving vertical guideline...");
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        }
        resize_handle_ =
            pointer_selection_mode_ == vector::SelectionMode::Replace ? selection_handle_at(x, y) : -1;
        if (resize_handle_ >= 0) {
            if (resize_handle_ == 9) {
                is_dragging_ = true;
            } else {
                is_dragging_ = controller.gesture().begin(pointer_down_world_,
                                                          controller.selection().selected_ids(), *snapshot);
            }
        } else {
            const double tolerance = 6.0 / root().renderer().camera().zoom();
            auto hit = vector::SelectionModel::hit_test(pointer_down_world_, *snapshot, tolerance);
            if (hit && is_in_isolation_mode() && !is_object_in_isolated_group(*hit))
                hit.reset();
            if (hit && !is_in_isolation_mode()) {
                document::ObjectId parent = *hit;
                while (const auto group = workflow().find_parent_group(parent))
                    parent = *group;
                hit = parent;
            }
            if (hit) {
                const bool already_selected = controller.selection().is_selected(*hit);
                controller.on_pointer_down(pointer_down_world_, *snapshot, pointer_selection_mode_,
                                           tolerance);
                if (workflow().is_group(*hit) && controller.gesture().is_active()) {
                    std::vector<document::ObjectId> gesture_ids{*hit};
                    std::function<void(document::ObjectId)> append_children = [&](document::ObjectId group_id) {
                        const auto group_object = snapshot->objects().get(group_id);
                        if (!group_object) return;
                        const auto* group = std::get_if<document::GroupObject>(&*group_object);
                        if (!group) return;
                        for (const auto child : group->children_ids) {
                            gesture_ids.push_back(child);
                            append_children(child);
                        }
                    };
                    append_children(*hit);
                    (void)controller.gesture().begin(pointer_down_world_, gesture_ids, *snapshot);
                }
                is_dragging_ = controller.gesture().is_active();
                if (!already_selected) {
                    is_rotate_mode_ = false;
                    custom_pivot_world_.reset();
                }
            } else {
                is_marquee_ = true;
                is_rotate_mode_ = false;
                custom_pivot_world_.reset();
            }
        }
        if (is_dragging_)
            drag_preview_bounds_ = controller.selection().bounds(*snapshot);
    } else if (active_tool_ == 1 || active_tool_ == 2 || active_tool_ == 9) {
        is_creating_shape_ = true;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::handle_canvas_mouse_move(int x, int y, unsigned modifiers) {
    if (is_canvas_point(x, y)) {
        const auto point = canvas_to_world(x, y);
        cursor_mm_x_ = point.x;
        cursor_mm_y_ = point.y;
    }
    if (is_panning_) {
        root().renderer().camera().pan_pixels(x - last_pointer_x_, y - last_pointer_y_);
        last_pointer_x_ = x;
        last_pointer_y_ = y;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (!pointer_active_) {
        std::optional<document::ObjectId> hover;
        if (active_tool_ == 0 && is_canvas_point(x, y))
            hover =
                vector::SelectionModel::hit_test(canvas_to_world(x, y), *root().document().current_snapshot(),
                                                 6.0 / root().renderer().camera().zoom());
        if (hover != hover_object_) {
            hover_object_ = hover;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        const int handle = selection_handle_at(x, y);
        std::wstring handle_tip;
        if (handle == 9)
            handle_tip = language_ == UILanguage::Vietnamese
                ? L"Tâm xoay · kéo chuột để thay đổi vị trí tâm xoay"
                : L"Rotation pivot · drag to reposition center";
        else if (handle == 8 || (is_rotate_mode_ && (handle == 0 || handle == 2 || handle == 4 || handle == 6)))
            handle_tip = language_ == UILanguage::Vietnamese
                ? L"Xoay vùng chọn · kéo chuột; Shift khóa bước 15°"
                : L"Rotate selection · drag; Shift snaps to 15°";
        else if (handle >= 0)
            handle_tip = language_ == UILanguage::Vietnamese
                ? L"Đổi kích thước vùng chọn · kéo tay nắm; Shift khóa tỉ lệ"
                : L"Resize selection · drag a handle; Shift constrains proportions";
        if (tooltip_.text != handle_tip || tooltip_.visible != !handle_tip.empty()) {
            tooltip_.visible = !handle_tip.empty();
            tooltip_.x = x;
            tooltip_.y = y;
            tooltip_.text = std::move(handle_tip);
            if (tooltip_.visible)
                status_msg_ = wide_to_utf8(tooltip_.text);
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }
    pointer_current_world_ = canvas_to_world(x, y);
    const auto& page = root().document().current_snapshot()->page();
    const double half_canvas = geometry::kMaxCanvasDimensionMm * 0.5;
    pointer_current_world_.x = std::clamp(pointer_current_world_.x,
        page.width_mm * 0.5 - half_canvas, page.width_mm * 0.5 + half_canvas);
    pointer_current_world_.y = std::clamp(pointer_current_world_.y,
        page.height_mm * 0.5 - half_canvas, page.height_mm * 0.5 + half_canvas);
    if (is_creating_shape_)
        shape_drag_modifiers_ = modifiers & (MK_CONTROL | MK_SHIFT);
    if (is_knife_cutting_) {
        knife_current_world_ = pointer_current_world_;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    pointer_preserve_aspect_ = (modifiers & MK_SHIFT) != 0;
    if (is_dragging_ && (drag_preview_bounds_ || resize_handle_ == 9) && std::hypot(x - drag_start_x_, y - drag_start_y_) >= 2) {
        if (resize_handle_ == 9) {
            custom_pivot_world_ = pointer_current_world_;
            std::ostringstream hint;
            hint << std::fixed << std::setprecision(1) << pointer_current_world_.x << ", "
                 << pointer_current_world_.y << " mm";
            status_msg_ = (language_ == UILanguage::Vietnamese ? "Tâm xoay: " : "Pivot: ") + hint.str();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        auto &gesture = root().vector_controller().gesture();
        const geometry::Point2D delta{pointer_current_world_.x - pointer_down_world_.x,
                                      pointer_current_world_.y - pointer_down_world_.y};
        const bool is_corner_rotate = is_rotate_mode_ && (resize_handle_ == 0 || resize_handle_ == 2 ||
                                                          resize_handle_ == 4 || resize_handle_ == 6);
        if (resize_handle_ == 8 || is_corner_rotate) {
            constexpr double pi = 3.14159265358979323846;
            const auto pivot = custom_pivot_world_.value_or(drag_preview_bounds_->center());
            const double start = std::atan2(pointer_down_world_.y - pivot.y,
                                            pointer_down_world_.x - pivot.x);
            const double current = std::atan2(pointer_current_world_.y - pivot.y,
                                               pointer_current_world_.x - pivot.x);
            double angle_degrees = std::remainder(current - start, 2.0 * pi) * 180.0 / pi;
            if ((modifiers & MK_SHIFT) != 0)
                angle_degrees = std::round(angle_degrees / 15.0) * 15.0;
            const auto rotation = geometry::Transform2D::translation(pivot.x, pivot.y) *
                                  geometry::Transform2D::rotation_degrees(angle_degrees) *
                                  geometry::Transform2D::translation(-pivot.x, -pivot.y);
            (void)gesture.update_transform(rotation);
            std::ostringstream hint;
            hint << std::fixed << std::setprecision(1) << angle_degrees << "°"
                 << ((modifiers & MK_SHIFT) != 0 ? " · Shift 15°" : " · Shift: 15°");
            status_msg_ = (language_ == UILanguage::Vietnamese ? "Xoay: " : "Rotate: ") + hint.str();
        } else if (resize_handle_ >= 0) {
            const bool preserve = (resize_handle_ % 2 == 0) && pointer_preserve_aspect_;
            if (const auto transform = vector::selection_resize_transform(
                    *drag_preview_bounds_, resize_handle_, delta, preserve)) {
                (void)gesture.update_transform(*transform);
                const auto preview = transform->map_rect(*drag_preview_bounds_);
                std::ostringstream hint;
                hint << std::fixed << std::setprecision(1) << preview.width << " × " << preview.height << " mm";
                status_msg_ = (language_ == UILanguage::Vietnamese ? "Kích thước: " : "Scale: ") + hint.str() +
                    (preserve ? (language_ == UILanguage::Vietnamese ? " · khóa tỉ lệ" : " · locked")
                              : (language_ == UILanguage::Vietnamese ? " · tự do" : " · free"));
            }
        } else {
            (void)gesture.update(pointer_current_world_);
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

geometry::Rect2D NativeWindow::shape_drag_bounds(unsigned modifiers) const {
    if (active_tool_ == 9)
        return vector::ShapeFactory::constrained_drag_bounds(
            pointer_down_world_, pointer_current_world_, false, (modifiers & MK_CONTROL) != 0);
    return vector::ShapeFactory::constrained_drag_bounds(
        pointer_down_world_, pointer_current_world_, (modifiers & MK_SHIFT) != 0,
        (modifiers & MK_CONTROL) != 0);
}

double NativeWindow::shape_drag_polygon_rotation(unsigned modifiers) const noexcept {
    if ((modifiers & MK_SHIFT) == 0)
        return polygon_rotation_degrees_;
    geometry::Rect2D bounds;
    try {
        bounds = shape_drag_bounds(modifiers);
    } catch (...) {
        return polygon_rotation_degrees_;
    }
    const auto center = bounds.center();
    const double dx = pointer_current_world_.x - center.x;
    const double dy = pointer_current_world_.y - center.y;
    if (std::hypot(dx, dy) <= 1e-12)
        return polygon_rotation_degrees_;
    constexpr double radians_to_degrees = 180.0 / 3.14159265358979323846;
    const double drag_angle = std::atan2(dy, dx) * radians_to_degrees;
    return std::round(drag_angle / 15.0) * 15.0;
}

bool NativeWindow::commit_created_shape() {
    geometry::Rect2D bounds;
    try {
        bounds = shape_drag_bounds(shape_drag_modifiers_);
    } catch (const std::exception&) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Kích thước hình không hợp lệ" : "Invalid shape dimensions";
        return false;
    }
    if (bounds.width * root().renderer().camera().zoom() < 2 ||
        bounds.height * root().renderer().camera().zoom() < 2)
        return false;
    const auto snapshot = root().document().current_snapshot();
    if (snapshot->page().layers.empty() || snapshot->page().layers.front().locked ||
        !snapshot->page().layers.front().visible)
        return false;
    document::PathObject object;
    try {
        if (active_tool_ == 1) {
            object = vector::ShapeFactory::create_rectangle(bounds, default_style_.fill,
                default_style_.stroke, default_style_.width_mm);
        } else if (active_tool_ == 2) {
            object = vector::ShapeFactory::create_ellipse(bounds.center(), bounds.width * .5,
                bounds.height * .5, default_style_.fill, default_style_.stroke,
                default_style_.width_mm);
        } else {
            const double radius = std::min(bounds.width, bounds.height) * .5;
            object = vector::ShapeFactory::create_regular_polygon(bounds.center(), radius,
                polygon_sides_, shape_drag_polygon_rotation(shape_drag_modifiers_),
                default_style_.fill, default_style_.stroke, default_style_.width_mm);
        }
    } catch (const std::exception&) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không thể tạo hình với các thông số hiện tại"
            : "Could not create a shape with the current settings";
        return false;
    }
    object.has_fill = default_style_.has_fill;
    object.has_stroke = default_style_.has_stroke;
    const auto result =
        root().document().execute_command(std::make_unique<document::AddObjectCommand>(object, 0));
    if (result.success) {
        if (is_in_isolation_mode()) {
            auto iso_id = *current_isolated_group_id();
            auto snap = root().document().current_snapshot();
            auto obj = snap->objects().get(iso_id);
            if (obj && std::holds_alternative<document::GroupObject>(*obj)) {
                document::GroupObject updated_grp = std::get<document::GroupObject>(*obj);
                updated_grp.children_ids.push_back(object.id);
                auto map = snap->objects().set(iso_id, std::move(updated_grp));
                root().document().apply_mutation(snap->page(), std::move(map));
            }
        }
        root().vector_controller().set_selection({object.id}, *root().document().current_snapshot());
        previous_drawing_tool_ = active_tool_;
        set_active_tool(0);
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã tạo đối tượng và chọn ngay (Pick) · Kéo tay nắm để scale/xoay, click ngoài để tiếp tục vẽ"
            : "Created object and selected (Pick) · Drag handles to scale/rotate, click outside to resume drawing";
    }
    return result.success;
}

void NativeWindow::handle_canvas_mouse_up(int x, int y, unsigned modifiers) {
    if (!pointer_active_)
        return;
    handle_canvas_mouse_move(x, y, modifiers); // release can arrive without a final move event
    const auto& page = root().document().current_snapshot()->page();
    const double half_canvas = geometry::kMaxCanvasDimensionMm * 0.5;
    pointer_current_world_.x = std::clamp(pointer_current_world_.x,
        page.width_mm * 0.5 - half_canvas, page.width_mm * 0.5 + half_canvas);
    pointer_current_world_.y = std::clamp(pointer_current_world_.y,
        page.height_mm * 0.5 - half_canvas, page.height_mm * 0.5 + half_canvas);
    const bool stale = !pointer_stamp_.matches(*root().document().current_snapshot());
    bool changed = false;
    const bool knife = is_knife_cutting_;
    const bool is_corner_rotate = is_rotate_mode_ && (resize_handle_ == 0 || resize_handle_ == 2 ||
                                                      resize_handle_ == 4 || resize_handle_ == 6);
    const bool rotation = resize_handle_ == 8 || is_corner_rotate;
    const bool pivot_drag = resize_handle_ == 9;
    const bool clicked_without_drag = std::hypot(x - drag_start_x_, y - drag_start_y_) < 3;
    if (!stale) {
        if (is_marquee_) {
            const auto snapshot = root().document().current_snapshot();
            if (!clicked_without_drag) {
                root().vector_controller().select_region(
                    selection_ui::between(pointer_down_world_, pointer_current_world_),
                    x >= drag_start_x_ ? vector::RegionMode::Window : vector::RegionMode::Crossing, *snapshot,
                    pointer_selection_mode_);
            } else if (pointer_selection_mode_ == vector::SelectionMode::Replace) {
                root().vector_controller().set_selection({}, *snapshot);
                is_rotate_mode_ = false;
                custom_pivot_world_.reset();
                if (previous_drawing_tool_ > 0) {
                    set_active_tool(previous_drawing_tool_);
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Đã bỏ chọn · Khôi phục công cụ vẽ để tiếp tục tạo hình"
                        : "Deselected · Resumed drawing tool";
                }
            }
        } else if (is_creating_shape_) {
            changed = commit_created_shape();
        } else if (is_knife_cutting_) {
            knife_current_world_ = pointer_current_world_;
            changed = workflow().split_objects_by_line(knife_start_world_, knife_current_world_);
        } else if (pivot_drag) {
            custom_pivot_world_ = pointer_current_world_;
            changed = true;
        } else if (is_dragging_ && !clicked_without_drag) {
            const bool alt_pressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
            if (alt_pressed && resize_handle_ == -1) {
                const auto delta = root().vector_controller().gesture().current_delta();
                root().vector_controller().gesture().cancel();
                const double dx = delta.tx;
                const double dy = delta.ty;
                if (std::abs(dx) > 1e-4 || std::abs(dy) > 1e-4) {
                    changed = workflow().duplicate_selected(dx, dy);
                    if (changed) {
                        status_msg_ = language_ == UILanguage::Vietnamese
                            ? "Đã nhân bản theo khoảng kéo (Alt+Drag) · Gõ *N nhân chuỗi hoặc /N chia đều"
                            : "Duplicated by drag distance (Alt+Drag) · Type *N to multiply or /N to divide";
                    }
                }
            } else {
                const auto result = root().vector_controller().gesture().commit(root().document());
                changed = result.success && root().document().revision() != pointer_start_revision_;
            }
        }
    }
    cancel_canvas_interaction();
    workflow().sync_status_bar();
    if (stale)
        status_msg_ = language_ == UILanguage::Vietnamese ? "Bản vẽ đã đổi; thao tác đã được hủy an toàn."
                                                          : "Document changed; interaction cancelled.";
    else if (pivot_drag)
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã điều chỉnh tâm xoay · kéo góc để xoay quanh tâm"
            : "Adjusted rotation pivot · drag corners to rotate";
    else if (clicked_without_drag && is_rotate_mode_)
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chế độ xoay: kéo 4 góc để xoay quanh tâm, kéo tâm để dời vị trí"
            : "Rotate mode: drag 4 corners to rotate, drag pivot to reposition";
    else if (clicked_without_drag && !is_rotate_mode_ && active_tool_ == 0)
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chế độ biến đổi: kéo tay nắm để đổi kích thước"
            : "Transform mode: drag handles to scale";
    else if (changed && knife)
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã dùng dao cắt phân tách hình khối thành công · Ctrl+Z để hoàn tác"
            : "Knife split completed · Ctrl+Z to undo";
    else if (changed && rotation)
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã xoay vùng chọn · Ctrl+Z để hoàn tác"
            : "Rotated selection · Ctrl+Z to undo";
    else if (changed)
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã cập nhật · Ctrl+Z để hoàn tác"
                                                          : "Updated · Ctrl+Z to undo";
    else
        status_msg_ = (language_ == UILanguage::Vietnamese ? "Đang chọn: " : "Selected: ") +
                      std::to_string(root().vector_controller().selection().count());
    InvalidateRect(hwnd_, nullptr, FALSE);
}
#endif
} // namespace pikdraw::desktop
