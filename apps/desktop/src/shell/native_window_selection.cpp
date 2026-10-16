#include "native_window_selection_geometry.hpp"
#include "pikdraw/vector/gestures/selection_resize.hpp"
#include <cmath>

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
    if (handle >= 0) {
        constexpr WORD cursors[] = {32642, 32645, 32643, 32644, 32642, 32645, 32643, 32644};
        return cursors[handle];
    }
    if (active_tool_ == 1 || active_tool_ == 2)
        return 32515;
    if (active_tool_ == 7)
        return 32515; // Knife crosshair
    return hover_object_ || is_dragging_ ? 32646 : 32512;
}

int NativeWindow::selection_handle_at(int x, int y) const {
    if (active_tool_ != 0 || !is_canvas_point(x, y))
        return -1;
    const auto snapshot = root().document().current_snapshot();
    const auto bounds = root().vector_controller().selection().bounds(*snapshot);
    if (!bounds)
        return -1;
    const auto pixels = selection_ui::handles(
        selection_ui::screen_bounds(root().renderer().camera(), *bounds, canvas_rect(width_, height_)));
    for (size_t i = 0; i < pixels.size(); ++i)
        if (std::abs(x - pixels[i].x) <= 6 && std::abs(y - pixels[i].y) <= 6)
            return static_cast<int>(i);
    return -1;
}

void NativeWindow::cancel_canvas_interaction() {
    root().vector_controller().on_escape();
    pointer_active_ = is_dragging_ = is_creating_shape_ = is_marquee_ = is_panning_ = is_knife_cutting_ = false;
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
    drag_start_x_ = x;
    drag_start_y_ = y;
    pointer_selection_mode_ = (modifiers & MK_CONTROL) ? vector::SelectionMode::Toggle
                              : (modifiers & MK_SHIFT) ? vector::SelectionMode::Add
                                                       : vector::SelectionMode::Replace;
    const auto snapshot = root().document().current_snapshot();
    pointer_stamp_ = vector::GestureStamp(*snapshot);
    pointer_start_revision_ = snapshot->revision();
    auto &controller = root().vector_controller();

    if (active_tool_ == 0) {
        resize_handle_ =
            pointer_selection_mode_ == vector::SelectionMode::Replace ? selection_handle_at(x, y) : -1;
        if (resize_handle_ >= 0) {
            is_dragging_ = controller.gesture().begin(pointer_down_world_,
                                                      controller.selection().selected_ids(), *snapshot);
        } else {
            const double tolerance = 6.0 / root().renderer().camera().zoom();
            auto hit = vector::SelectionModel::hit_test(pointer_down_world_, *snapshot, tolerance);
            if (is_in_isolation_mode()) {
                if (hit && !is_object_in_isolated_group(*hit)) {
                    hit = std::nullopt;
                }
            } else if (hit) {
                document::ObjectId cur = *hit;
                while (auto parent = workflow().find_parent_group(cur)) {
                    cur = *parent;
                }
                hit = cur;
            }

            if (hit) {
                controller.on_pointer_down(pointer_down_world_, *snapshot, pointer_selection_mode_,
                                           tolerance);
                is_dragging_ = controller.gesture().is_active();
            } else {
                is_marquee_ = true;
            }
        }
        if (is_dragging_)
            drag_preview_bounds_ = controller.selection().bounds(*snapshot);
    } else if (active_tool_ == 7) {
        is_knife_cutting_ = true;
        knife_start_world_ = pointer_down_world_;
        knife_current_world_ = pointer_down_world_;
    } else if (active_tool_ == 6) {
        if (is_inline_text_editing_) {
            commit_inline_text_editing();
        }
        const double tolerance = 6.0 / root().renderer().camera().zoom();
        auto hit = vector::SelectionModel::hit_test(pointer_down_world_, *snapshot, tolerance);
        if (hit) {
            auto opt = snapshot->objects().get(*hit);
            if (opt && std::holds_alternative<document::TextObject>(*opt)) {
                start_inline_text_editing(pointer_down_world_, *hit);
            } else {
                start_inline_text_editing(pointer_down_world_, document::kInvalidObjectId);
            }
        } else {
            start_inline_text_editing(pointer_down_world_, document::kInvalidObjectId);
        }
    } else if (active_tool_ == 1 || active_tool_ == 2) {
        if (is_inline_text_editing_) {
            commit_inline_text_editing();
        }
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
        return;
    }
    pointer_current_world_ = canvas_to_world(x, y);
    pointer_preserve_aspect_ = (modifiers & MK_SHIFT) != 0;

    if (is_knife_cutting_) {
        knife_current_world_ = pointer_current_world_;
    } else if (is_dragging_ && drag_preview_bounds_ && std::hypot(x - drag_start_x_, y - drag_start_y_) >= 2) {
        auto &gesture = root().vector_controller().gesture();
        const geometry::Point2D delta{pointer_current_world_.x - pointer_down_world_.x,
                                      pointer_current_world_.y - pointer_down_world_.y};
        if (resize_handle_ >= 0) {
            if (const auto transform = vector::selection_resize_transform(
                    *drag_preview_bounds_, resize_handle_, delta, pointer_preserve_aspect_))
                (void)gesture.update_transform(*transform);
        } else {
            (void)gesture.update(pointer_current_world_);
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool NativeWindow::commit_created_shape() {
    const auto bounds = selection_ui::between(pointer_down_world_, pointer_current_world_);
    if (bounds.width * root().renderer().camera().zoom() < 2 ||
        bounds.height * root().renderer().camera().zoom() < 2)
        return false;
    const auto snapshot = root().document().current_snapshot();
    if (snapshot->page().layers.empty() || snapshot->page().layers.front().locked ||
        !snapshot->page().layers.front().visible)
        return false;
    auto object = active_tool_ == 1 ? vector::ShapeFactory::create_rectangle(bounds)
                                    : vector::ShapeFactory::create_ellipse(bounds.center(), bounds.width * .5,
                                                                           bounds.height * .5);
    object.fill = default_style_.fill;
    object.stroke = default_style_.stroke;
    object.stroke_width_mm = default_style_.width_mm;
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
    }
    return result.success;
}

void NativeWindow::handle_canvas_mouse_up(int x, int y, unsigned modifiers) {
    if (!pointer_active_)
        return;
    handle_canvas_mouse_move(x, y, modifiers); // release can arrive without a final move event
    const bool stale = !pointer_stamp_.matches(*root().document().current_snapshot());
    bool changed = false;
    if (!stale) {
        if (is_knife_cutting_) {
            knife_current_world_ = pointer_current_world_;
            if (workflow().split_objects_by_line(knife_start_world_, knife_current_world_)) {
                changed = true;
                status_msg_ = (language_ == UILanguage::Vietnamese)
                    ? "Đã dùng dao cắt phân tách hình khối thành công · Ctrl+Z để hoàn tác"
                    : "Knife split completed · Ctrl+Z to undo";
            }
            is_knife_cutting_ = false;
        } else if (is_marquee_) {
            const auto snapshot = root().document().current_snapshot();
            if (std::hypot(x - drag_start_x_, y - drag_start_y_) >= 3) {
                root().vector_controller().select_region(
                    selection_ui::between(pointer_down_world_, pointer_current_world_),
                    x >= drag_start_x_ ? vector::RegionMode::Window : vector::RegionMode::Crossing, *snapshot,
                    pointer_selection_mode_);
            } else if (pointer_selection_mode_ == vector::SelectionMode::Replace) {
                root().vector_controller().set_selection({}, *snapshot);
            }
        } else if (is_creating_shape_) {
            changed = commit_created_shape();
        } else if (is_dragging_ && std::hypot(x - drag_start_x_, y - drag_start_y_) >= 2) {
            const auto result = root().vector_controller().gesture().commit(root().document());
            changed = result.success && root().document().revision() != pointer_start_revision_;
        }
    }
    cancel_canvas_interaction();
    workflow().sync_status_bar();
    if (stale)
        status_msg_ = language_ == UILanguage::Vietnamese ? "Bản vẽ đã đổi; thao tác đã được hủy an toàn."
                                                          : "Document changed; interaction cancelled.";
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
