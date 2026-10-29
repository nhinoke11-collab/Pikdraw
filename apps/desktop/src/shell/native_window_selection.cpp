#include "native_window_powerclip_ui.hpp"
#include "native_window_selection_geometry.hpp"
#include "pikdraw/desktop/shell/cut_shape_tool_catalog.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "../tools/dimension_tool_controller.hpp"
#include "../tools/artboard_tool_controller.hpp"
#include "../tools/annotation/annotation_tool_controller.hpp"
#include "pikdraw/vector/gestures/selection_resize.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include "native_window_length_input.hpp"
#include "../performance/canvas_trace.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace pikdraw::desktop {

void NativeWindow::reset_trim_objects_roles() {
    if ((cut_geometry_job_ &&
         cut_geometry_job_->operation == CutGeometryOperation::TrimObjects) ||
        cut_geometry_preview_ready(CutGeometryOperation::TrimObjects))
        cancel_cut_geometry_job();
    trim_objects_target_id_ = document::kInvalidObjectId;
    trim_objects_source_ids_.clear();
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Đã đặt lại vai trò Target/Source"
        : "Trim Target/Source roles reset";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::reset_shape_builder() {
    if ((cut_geometry_job_ &&
         cut_geometry_job_->operation == CutGeometryOperation::ShapeBuilder) ||
        cut_geometry_preview_ready(CutGeometryOperation::ShapeBuilder))
        cancel_cut_geometry_job();
    shape_builder_seed_points_.clear();
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Đã bỏ chọn các mặt Shape Builder" : "Shape Builder face selection cleared";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::commit_shape_builder(bool erase_faces) {
    if (cut_geometry_preview_ready(CutGeometryOperation::ShapeBuilder)) {
        if (pending_cut_geometry_proposal_->proposal.erase_faces == erase_faces) {
            (void)apply_pending_cut_geometry_proposal(CutGeometryOperation::ShapeBuilder,
                                                      erase_faces);
            return;
        }
        discard_pending_cut_geometry_proposal();
    }
    if (pending_cut_geometry_proposal_)
        discard_pending_cut_geometry_proposal();
    if (shape_builder_seed_points_.empty()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn ít nhất một mặt trước khi áp dụng Shape Builder"
            : "Select at least one face before applying Shape Builder";
    } else {
        CutGeometryRequest request;
        request.operation = CutGeometryOperation::ShapeBuilder;
        request.snapshot = root().document().current_snapshot();
        request.selected_ids = root().vector_controller().selection().selected_ids();
        request.points_world = shape_builder_seed_points_;
        request.erase_faces = erase_faces;
        if (!start_cut_geometry_job(std::move(request))) {
            // start_cut_geometry_job leaves the worker's failure reason in status_msg_.
        }
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::commit_trim_objects() {
    if (cut_geometry_preview_ready(CutGeometryOperation::TrimObjects)) {
        (void)apply_pending_cut_geometry_proposal(CutGeometryOperation::TrimObjects);
        return;
    }
    if (pending_cut_geometry_proposal_)
        discard_pending_cut_geometry_proposal();
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot || trim_objects_target_id_ == document::kInvalidObjectId || trim_objects_source_ids_.empty()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn một Target và ít nhất một Source trước khi áp dụng"
            : "Choose a Target and at least one Source before applying";
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return;
    }
    CutGeometryRequest request;
    request.operation = CutGeometryOperation::TrimObjects;
    request.snapshot = snapshot;
    request.selected_ids = root().vector_controller().selection().selected_ids();
    request.target_id = trim_objects_target_id_;
    request.source_ids = trim_objects_source_ids_;
    request.keep_sources = trim_objects_keep_sources_;
    (void)start_cut_geometry_job(std::move(request));
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

#ifdef _WIN32
namespace {
constexpr int kPendingTextBodyMove = 10;

geometry::Point2D handle_anchor_point(const geometry::Rect2D& b, int handle) {
    constexpr double rx[] = {1.0, 0.5, 0.0, 0.0, 0.0, 0.5, 1.0, 1.0};
    constexpr double ry[] = {0.0, 0.0, 0.0, 0.5, 1.0, 1.0, 1.0, 0.5};
    return (handle >= 0 && handle < 8) ? geometry::Point2D{b.x + b.width * rx[handle], b.y + b.height * ry[handle]} : b.center();
}

std::optional<geometry::Transform2D> text_resize_transform(
    const geometry::Rect2D& bounds, int handle, geometry::Point2D delta) {
    if (handle < 0 || handle > 7) return std::nullopt;
    constexpr int hx[] = {-1, 0, 1, 1, 1, 0, -1, -1}, hy[] = {1, 1, 1, 0, -1, -1, -1, 0};
    const geometry::Point2D anchor = handle_anchor_point(bounds, handle);
    double factor = 1.0;
    if (hx[handle] != 0 && hy[handle] != 0) {
        const geometry::Point2D start{hx[handle] < 0 ? bounds.x : bounds.right(), hy[handle] < 0 ? bounds.y : bounds.bottom()};
        const double x = start.x - anchor.x, y = start.y - anchor.y, len = x * x + y * y;
        if (len <= 1e-12) return std::nullopt;
        factor = ((x + delta.x) * x + (y + delta.y) * y) / len;
    } else if (hx[handle] != 0) {
        if (bounds.width <= 1e-9) return std::nullopt;
        factor = 1.0 + delta.x / (hx[handle] * bounds.width);
    } else {
        if (bounds.height <= 1e-9) return std::nullopt;
        factor = 1.0 + delta.y / (hy[handle] * bounds.height);
    }
    if (!std::isfinite(factor)) return std::nullopt;
    factor = std::clamp(factor, 0.01, 100.0);
    const auto transform = geometry::Transform2D::translation(anchor.x, anchor.y) *
        geometry::Transform2D::scale(factor, factor) *
        geometry::Transform2D::translation(-anchor.x, -anchor.y);
    return transform.is_finite() && transform.is_invertible() ? std::optional(transform) : std::nullopt;
}
}

WORD NativeWindow::canvas_cursor_at(int x, int y) const {
    if (show_settings_modal_ || show_file_menu_ || show_home_screen_ || !is_canvas_point(x, y)) return 32512;
    if (active_tool_ == 17 && annotation_tool_ && annotation_tool_->note_text_editing()) return 32513;
    if (is_panning_) return 32646;
    if (active_tool_ == 3) return 32649;
    if (active_tool_ == 4 && resize_handle_ == kPendingTextBodyMove) return 32646;
    const int handle = resize_handle_ >= 0 ? resize_handle_ : selection_handle_at(x, y);
    if (handle == kPendingTextBodyMove) return 32646;
    if (active_tool_ == 4 && is_inline_text_editing_ && handle < 0) return 32513;
    if (active_tool_ == 4 && handle < 0) {
        const auto snapshot = root().document().current_snapshot();
        const auto bounds = root().vector_controller().selection().bounds(*snapshot);
        if (bounds && is_text_object_selected()) {
            const auto area = canvas_rect(width_, height_);
            const auto pt = root().renderer().camera().screen_to_world(
                {static_cast<float>(x - area.left), static_cast<float>(y - area.top)});
            if (bounds->contains({pt.x, pt.y})) return 32646;
        }
        return 32513;
    }
    if (handle == 9) return 32646; // IDC_SIZEALL: 4-way move cursor for rotation pivot.
    if (handle == 8) return 32649; // IDC_HAND: rotation affordance.
    if (is_rotate_mode_ && (handle == 0 || handle == 2 || handle == 4 || handle == 6)) return 32649;
    if (handle >= 0 && handle <= 7) {
        constexpr WORD cursors[] = {32642, 32645, 32643, 32644, 32642, 32645, 32643, 32644};
        return cursors[handle];
    }
    if (active_tool_ == 1 || active_tool_ == 2 || active_tool_ == 9 || active_tool_ == 18)
        return (hover_object_ || is_dragging_) ? 32646 : 32515;
    if (active_tool_ == 0 && show_guidelines_) {
        const int g_idx = guideline_at(x, y);
        if (g_idx >= 0 && g_idx < static_cast<int>(guidelines_.size()))
            return guidelines_[g_idx].orientation == GuidelineOrientation::Horizontal ? 32645 : 32644;
    }
    return hover_object_ || is_dragging_ ? 32646 : 32512;
}
int NativeWindow::selection_handle_at(int x, int y) const {
    if (active_tool_ == 4 && is_inline_text_editing_ && text_editor_) {
        const auto& editor = *text_editor_;
        if (!editor.composition.empty() || !is_canvas_point(x, y))
            return -1;
        const auto bounds = editor.parent_transform.map_rect(editor.draft.bounds());
        const auto rect = selection_ui::screen_bounds(
            root().renderer().camera(), bounds, canvas_rect(width_, height_));
        const auto pixels = selection_ui::handles(rect);
        for (size_t i = 0; i < pixels.size(); ++i) {
            if (std::abs(x - pixels[i].x) <= 8 && std::abs(y - pixels[i].y) <= 8)
                return static_cast<int>(i);
        }
        constexpr int kTol = 6;
        const bool near_h = (x >= rect.left - kTol && x <= rect.right + kTol) &&
                            (std::abs(y - rect.top) <= kTol || std::abs(y - rect.bottom) <= kTol);
        const bool near_v = (y >= rect.top - kTol && y <= rect.bottom + kTol) &&
                            (std::abs(x - rect.left) <= kTol || std::abs(x - rect.right) <= kTol);
        return (near_h || near_v) ? kPendingTextBodyMove : -1;
    }
    const bool text_transform = active_tool_ == 4 && !is_inline_text_editing_ && is_text_object_selected();
    const bool can_pick = (active_tool_ == 0 || active_tool_ == 1 || active_tool_ == 2 || active_tool_ == 9 || active_tool_ == 18 ||
                           text_transform);
    if (!can_pick || !is_canvas_point(x, y))
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

    // Center pivot point (handle 9) - active and draggable in both normal selection and rotate modes
    if (is_rotate_mode_) {
        const auto pivot_world = custom_pivot_world_.value_or(bounds->center());
        const auto pivot_screen = selection_ui::pivot_point(root().renderer().camera(), pivot_world, area);
        if (std::abs(x - pivot_screen.x) <= 6 && std::abs(y - pivot_screen.y) <= 6)
            return 9;
    }

    for (size_t i = 0; i < pixels.size(); ++i)
        if (std::abs(x - pixels[i].x) <= 8 && std::abs(y - pixels[i].y) <= 8)
            return static_cast<int>(i);
    return -1;
}

void NativeWindow::cancel_canvas_interaction(bool cancel_cut_job) {
    if (cancel_cut_job) cancel_cut_geometry_job();
    root().vector_controller().on_escape();
    if (text_editor_) {
        text_editor_->live_scale_baseline.reset();
        text_editor_->is_moving_frame = false;
        text_editor_->live_move_baseline.reset();
    }
    pointer_active_ = is_dragging_ = is_creating_shape_ = is_marquee_ = is_panning_ = false;
    is_knife_cutting_ = false;
    is_scissors_click_ = false;
    is_trim_role_click_ = false;
    is_smart_trim_click_ = false;
    is_shape_builder_click_ = false;
    shape_builder_gesture_.clear();
    shape_builder_gesture_overflow_ = false;
    is_smart_fill_click_ = false;
    is_raster_eraser_stroke_ = false;
    raster_eraser_stroke_.clear();
    raster_eraser_stroke_overflow_ = false;
    knife_stroke_.clear();
    knife_stroke_overflow_ = false;
    eraser_stroke_.clear();
    eraser_stroke_overflow_ = false;
    is_contour_dragging_ = false;
    resize_handle_ = -1;
    drag_preview_bounds_.reset();
    drag_preview_delta_ = {};
    smart_guide_reference_revision_ = 0;
    smart_guide_reference_page_ = document::kInvalidPageId;
    smart_guide_references_.clear();
    smart_guide_acquisition_ = {};
    smart_guide_solution_ = {};
    smart_guide_moving_bounds_.reset();
    shape_snap_effective_world_.reset();
    if (artboard_tool_) artboard_tool_->cancel();
    if (GetCapture() == hwnd_) ReleaseCapture();
}

void NativeWindow::handle_canvas_mouse_down(int x, int y, unsigned modifiers) {
    synchronize_powerclip_view();
    if (powerclip_target_pick_ && handle_powerclip_target_pick(canvas_to_world(x,y))) return;
    if (!is_canvas_point(x, y))
        return;
    toolbox_keyboard_focus_ = false;
    toolbox_open_group_ = 0;
    cancel_canvas_interaction();
    SetCapture(hwnd_);
    if (active_tool_ == 14) {
        (void)handle_artboard_pointer_down(x, y, modifiers);
        return;
    }
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
    if (active_tool_ == 0 && artboard_overview_enabled()) {
        const auto& boards = snapshot->active_page().artboards;
        for (auto board = boards.rbegin(); board != boards.rend(); ++board) {
            const auto& world = pointer_down_world_;
            if (world.x >= board->x_mm && world.x <= board->x_mm + board->width_mm &&
                world.y >= board->y_mm && world.y <= board->y_mm + board->height_mm) {
                (void)set_active_artboard(board->id);
                break;
            }
        }
    }
    pointer_stamp_ = vector::GestureStamp(*snapshot);
    pointer_start_revision_ = snapshot->revision();
    auto &controller = root().vector_controller();
    if (active_tool_ == 24) {
        if (start_raster_eraser_stroke(pointer_down_world_)) {
            is_raster_eraser_stroke_ = true;
        } else {
            pointer_active_ = false;
            if (GetCapture() == hwnd_) ReleaseCapture();
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (active_tool_ == 19) {
        is_scissors_click_ = true;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (active_tool_ == 20) {
        is_trim_role_click_ = true;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (active_tool_ == 21) {
        is_smart_trim_click_ = true;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (active_tool_ == 22) {
        is_shape_builder_click_ = true;
        shape_builder_gesture_ = {pointer_down_world_};
        shape_builder_erase_gesture_ = (GetKeyState(VK_MENU) & 0x8000) != 0;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (active_tool_ == 23) {
        is_smart_fill_click_ = true;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (active_tool_ == 5 || active_tool_ == 16) {
        is_knife_cutting_ = true;
        knife_start_world_ = pointer_down_world_;
        knife_current_world_ = pointer_down_world_;
        if (active_tool_ == 5 && knife_freehand_mode_)
            knife_stroke_.push_back(pointer_down_world_);
        if (active_tool_ == 16) eraser_stroke_.push_back(pointer_down_world_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const int live_text_handle = active_tool_ == 4 && is_inline_text_editing_
        ? selection_handle_at(x, y) : -1;
    if (live_text_handle >= 0 && text_editor_) {
        auto& editor = *text_editor_;
        editor.selecting = false;
        resize_handle_ = live_text_handle;
        is_dragging_ = true;
        pointer_active_ = true;
        drag_preview_bounds_.reset();
        if (live_text_handle == kPendingTextBodyMove) {
            editor.is_moving_frame = true;
            editor.live_move_baseline = editor.draft;
            editor.live_move_start_pos = editor.draft.position;
            editor.live_move_start_world = pointer_down_world_;
        } else if (live_text_handle <= 7) {
            const auto bounds = editor.parent_transform.map_rect(editor.draft.bounds());
            editor.live_scale_anchor_world = handle_anchor_point(bounds, live_text_handle);
            editor.live_scale_start_world = pointer_down_world_;
            editor.live_scale_last_update_ms = 0;
            editor.live_scale_baseline = editor.draft;
        }
        if (hwnd_) SetCapture(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const bool text_transform_handle = active_tool_ == 4 && !is_inline_text_editing_ &&
        is_text_object_selected() && pointer_selection_mode_ == vector::SelectionMode::Replace &&
        selection_handle_at(x, y) >= 0;
    if (active_tool_ == 4 && !text_transform_handle && !is_inline_text_editing_ &&
        pointer_selection_mode_ == vector::SelectionMode::Replace) {
        auto hit = vector::SelectionModel::hit_test(pointer_down_world_, *snapshot);
        if (hit && is_in_isolation_mode() && !is_object_in_isolated_group(*hit)) hit.reset();
        const auto object = hit ? snapshot->objects().get(*hit) : std::nullopt;
        if (hit && object && std::holds_alternative<document::TextObject>(*object) &&
            tools::editable_text_parent(*snapshot, *hit)) {
            controller.selection().select(*hit);
            resize_handle_ = kPendingTextBodyMove;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
    }
    if (active_tool_ == 4 && !text_transform_handle) {
        pointer_active_ = false;
        if (is_inline_text_editing_ && handle_inline_text_mouse(x, y, 0, (modifiers & MK_SHIFT) != 0))
            return;
        if (is_inline_text_editing_) {
            commit_inline_text_editing();
            const auto current = root().document().current_snapshot();
            const double tol = 6.0 / std::max(0.01, root().renderer().camera().zoom());
            auto hit = vector::SelectionModel::hit_test(pointer_down_world_, *current, tol);
            if (hit && is_in_isolation_mode() && !is_object_in_isolated_group(*hit)) hit.reset();
            const auto object = hit ? current->objects().get(*hit) : std::nullopt;
            if (object && std::holds_alternative<document::TextObject>(*object)) {
                start_inline_text_editing(pointer_down_world_, *hit);
            } else if (hit) {
                set_active_tool(0);
                controller.set_selection({*hit}, *current);
            } else {
                set_active_tool(0);
                controller.selection().clear();
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        const auto current = root().document().current_snapshot();
        auto hit = vector::SelectionModel::hit_test(pointer_down_world_, *current);
        if (hit && is_in_isolation_mode() && !is_object_in_isolated_group(*hit)) hit.reset();
        const auto object = hit ? current->objects().get(*hit) : std::nullopt;
        if (object && std::holds_alternative<document::TextObject>(*object))
            start_inline_text_editing(pointer_down_world_, *hit);
        else
            start_inline_text_editing(pointer_down_world_);
        if (text_editor_) {
            text_editor_->selecting = true;
            text_editor_->creating_frame = !text_editor_->existing;
            text_editor_->drag_start = text_editor_->parent_transform.inverted()->map_point(pointer_down_world_);
            if (hwnd_) SetCapture(hwnd_);
        } else if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (active_tool_ == 10) {
        if (!dimension_tool_) {
            try {
                dimension_tool_ = std::make_unique<tools::DimensionToolController>(root().document());
                apply_preferred_dimension_unit();
            } catch (...) {
                return;
            }
        }
        if (!dimension_tool_->active()) {
            dimension_tool_->activate(document::DimensionKind::Aligned, false);
        }
        const double zoom = root().renderer().camera().zoom();
        const bool ortho = (GetKeyState(VK_SHIFT) < 0 || GetKeyState(VK_CONTROL) < 0);
        dimension_tool_->update_snap(pointer_down_world_, zoom, ortho);
        const double tolerance = 6.0 / zoom;
        const auto resolved=controller.pick(pointer_down_world_,*snapshot,tolerance);
            auto hit=resolved?std::optional(resolved->id):std::nullopt;
        dimension_tool_->handle_pointer_press(pointer_down_world_, hit);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (active_tool_ == 17) {
        if (!annotation_tool_) {
            try {
                annotation_tool_ = std::make_unique<tools::AnnotationToolController>(root().document());
            } catch (...) {
                return;
            }
        }
        if (!annotation_tool_->active()) {
            annotation_tool_->activate(tools::AnnotationToolMode::DetailView);
        }
        const double zoom = root().renderer().camera().zoom();
        const double tolerance = 6.0 / zoom;
        const auto resolved = controller.pick(pointer_down_world_, *snapshot, tolerance);
        auto hit = resolved ? std::optional(resolved->id) : std::nullopt;
        const double dip_scale = std::max(0.01, window_dpi_scale());
        annotation_tool_->handle_pointer_press(pointer_down_world_, hit, std::nullopt,
            {static_cast<double>(x) / dip_scale, static_cast<double>(y) / dip_scale});
        if (annotation_tool_->note_text_editing()) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Nhập ghi chú · Enter xuống dòng · Ctrl+Enter hoàn tất · Esc hủy"
                : "Type note text · Enter new line · Ctrl+Enter finishes · Esc cancels";
        } else if (annotation_tool_->mode() == tools::AnnotationToolMode::LeaderNote &&
                   annotation_tool_->state() == tools::AnnotationSessionState::LeaderAnchorPicked) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Chọn điểm gấp khúc của đường dẫn"
                : "Choose the leader elbow point";
        } else if (annotation_tool_->mode() == tools::AnnotationToolMode::LeaderNote &&
                   annotation_tool_->state() == tools::AnnotationSessionState::LeaderElbowPicked) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đặt vị trí ghi chú"
                : "Place the note text position";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (active_tool_ == 12) {
        const double tolerance = 6.0 / root().renderer().camera().zoom();
        const auto resolved=controller.pick(pointer_down_world_,*snapshot,tolerance);
            auto hit=resolved?std::optional(resolved->id):std::nullopt;
        if (hit) {
            const auto obj = snapshot->objects().get(*hit);
            if (obj && std::holds_alternative<document::TextObject>(*obj)) {
                if (workflow().convert_text_to_curves(*hit)) {
                    const auto updated_snap = root().document().current_snapshot();
                    controller.set_selection({*hit}, *updated_snap);
                }
            } else {
                controller.set_selection({*hit}, *snapshot);
            }
            if (contour_tool_) {
                contour_applied_ = false;
                contour_tool_->activate(*hit);
            }
        }
        if (contour_tool_ && contour_tool_->active() &&
            contour_tool_->target_object() != document::kInvalidObjectId) {
            is_contour_dragging_ = true;
            contour_drag_start_world_ = pointer_down_world_;
            pointer_active_ = true;
            if (hwnd_) SetCapture(hwnd_);
            status_msg_ = (language_ == UILanguage::Vietnamese)
                ? "Kéo chuột để chỉnh khoảng cách và hướng viền Contour trực tiếp..."
                : "Drag pointer to interactively adjust Contour spacing and direction...";
        } else {
            pointer_active_ = false;
            if (GetCapture() == hwnd_) ReleaseCapture();
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (quick_dimension_active_) {
        end_quick_dimension();
    }
    const bool is_quick_shape = (active_tool_ == 1 || active_tool_ == 2 || active_tool_ == 9 || active_tool_ == 18);
    if (active_tool_ == 0 || is_quick_shape || text_transform_handle) {
        if (active_tool_ == 0 && show_guidelines_) {
            const int g_idx = guideline_at(x, y);
            if (g_idx >= 0 && g_idx < static_cast<int>(guidelines_.size())) {
                selected_guideline_index_ = g_idx;
                root().vector_controller().selection().clear();
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
        const bool can_resize = (active_tool_ == 0 || text_transform_handle) &&
            pointer_selection_mode_ == vector::SelectionMode::Replace;
        resize_handle_ = can_resize ? selection_handle_at(x, y) : -1;
        selected_guideline_index_ = -1;
        if (resize_handle_ >= 0) {
            if (resize_handle_ == 9) {
                is_dragging_ = true;
            } else {
                is_dragging_ = controller.gesture().begin(pointer_down_world_,
                                                          controller.selection().selected_ids(), *snapshot);
            }
        } else {
            const double tolerance = 6.0 / root().renderer().camera().zoom();
            const auto resolved=controller.pick(pointer_down_world_,*snapshot,tolerance);
            auto hit=resolved?std::optional(resolved->id):std::nullopt;
            if(hit && is_in_isolation_mode() && !isolation_reveal_ && !isolation_frame_) {
                const auto normal=vector::SelectionModel::hit_test(pointer_down_world_,*snapshot,tolerance);
                if(!normal || !snapshot->hierarchy().contains(*normal,*current_isolated_group_id())) hit.reset();
            }
            if (hit && is_in_isolation_mode() && !is_object_in_isolated_group(*hit))
                hit.reset();
            if(hit && !is_in_isolation_mode() && (GetKeyState(VK_MENU)&0x8000) && workflow().is_clipping_container(*hit)) {
                const auto clip=*hit; select_powerclip_contents(clip);
                hit=vector::SelectionModel::hit_test_in_scope(pointer_down_world_,*snapshot,clip,tolerance);
            }
            if (hit && !is_in_isolation_mode()) {
                document::ObjectId parent = *hit;
                while (const auto group = workflow().find_parent_group(parent))
                    parent = *group;
                hit = parent;
            }
            if (hit) {
                const bool already_selected = controller.selection().is_selected(*hit);
                if (is_quick_shape) {
                    previous_drawing_tool_ = active_tool_;
                    set_active_tool(0);
                    controller.on_resolved_pointer_down(pointer_down_world_, *snapshot, *hit,
                                                        pointer_selection_mode_);
                } else {
                    controller.on_resolved_pointer_down(pointer_down_world_, *snapshot, *hit,
                                                        pointer_selection_mode_);
                }
                is_dragging_ = controller.gesture().is_active();
                if (!already_selected) {
                    is_rotate_mode_ = false;
                    custom_pivot_world_.reset();
                }
            } else {
                if (is_quick_shape) {
                    controller.selection().clear();
                    is_rotate_mode_ = false;
                    custom_pivot_world_.reset();
                    is_creating_shape_ = true;
                    quick_dimension_origin_ = pointer_down_world_;
                } else {
                    is_marquee_ = true;
                    is_rotate_mode_ = false;
                    custom_pivot_world_.reset();
                }
            }
        }
        if (is_dragging_)
            drag_preview_bounds_ = controller.selection().bounds(*snapshot);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::handle_canvas_mouse_move(int x, int y, unsigned modifiers) {
    synchronize_powerclip_view();
    if (text_editor_ && text_editor_->selecting && handle_inline_text_mouse(x, y, 1)) return;
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
    if (artboard_tool_ && artboard_tool_->active()) {
        (void)handle_artboard_pointer_move(x, y, modifiers);
        return;
    }
    if (!pointer_active_) {
        std::optional<document::ObjectId> hover;
        const bool can_pick = (active_tool_ == 0 || active_tool_ == 1 || active_tool_ == 2 || active_tool_ == 9 || active_tool_ == 18);
        if (can_pick && is_canvas_point(x, y))
            hover =
                vector::SelectionModel::hit_test_in_scope(canvas_to_world(x, y), *root().document().current_snapshot(),
                    current_isolated_group_id().value_or(0),6.0/root().renderer().camera().zoom(),isolation_frame_);
        if(powerclip_target_pick_&&is_canvas_point(x,y)) {
            hover=powerclip_ui::pick_frame(*root().document().current_snapshot(),canvas_to_world(x,y),
                6.0/root().renderer().camera().zoom(),powerclip_pick_selection_,powerclip_target_pick_==2);
            status_msg_=hover?"Click to use frame #"+std::to_string(*hover):"Choose an eligible PowerClip frame · Esc cancels";
        }
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
        update_smart_trim_hover_preview(
            is_canvas_point(x, y) ? canvas_to_world(x, y) : geometry::Point2D{},
            active_tool_ == static_cast<int>(CutShapeToolId::SmartTrim) &&
                is_canvas_point(x, y));
        return;
    }
    pointer_current_world_ = canvas_to_world(x, y);
    const auto& page = root().document().current_snapshot()->page();
    const double half_canvas = geometry::kMaxCanvasDimensionMm * 0.5;
    pointer_current_world_.x = std::clamp(pointer_current_world_.x,
        page.width_mm * 0.5 - half_canvas, page.width_mm * 0.5 + half_canvas);
    pointer_current_world_.y = std::clamp(pointer_current_world_.y,
        page.height_mm * 0.5 - half_canvas, page.height_mm * 0.5 + half_canvas);
    update_smart_trim_hover_preview(pointer_current_world_,
        !pointer_active_ && is_canvas_point(x, y));
    if (is_raster_eraser_stroke_) {
        if (!raster_eraser_stroke_overflow_ &&
            (raster_eraser_stroke_.empty() ||
             std::hypot(pointer_current_world_.x - raster_eraser_stroke_.back().x,
                        pointer_current_world_.y - raster_eraser_stroke_.back().y) > 1e-9)) {
            if (raster_eraser_stroke_.size() >= document::RasterEraserParams::kMaxPoints)
                raster_eraser_stroke_overflow_ = true;
            else raster_eraser_stroke_.push_back(pointer_current_world_);
        }
        record_performance_event(CanvasTraceKind::CutGestureMove, last_trace_event_id_);
        invalidate_canvas_overlay();
        return;
    }
    if (active_tool_ == 10 && dimension_tool_ && dimension_tool_->active()) {
        const double zoom = root().renderer().camera().zoom();
        const bool ortho = (GetKeyState(VK_SHIFT) < 0 || GetKeyState(VK_CONTROL) < 0);
        dimension_tool_->update_snap(pointer_current_world_, zoom, ortho);
        dimension_tool_->handle_pointer_move(pointer_current_world_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (active_tool_ == 17 && annotation_tool_ && annotation_tool_->active()) {
        const double dip_scale = std::max(0.01, window_dpi_scale());
        annotation_tool_->handle_pointer_move(pointer_current_world_,
            {static_cast<double>(x) / dip_scale, static_cast<double>(y) / dip_scale});
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (is_contour_dragging_ && active_tool_ == 12 && contour_tool_ && contour_tool_->active()) {
        contour_applied_ = false;
        const auto target = contour_tool_->target_object();
        const auto snapshot = root().document().current_snapshot();
        const auto* object = snapshot ? snapshot->objects().lookup_ref(target) : nullptr;
        const auto* path = object ? std::get_if<document::PathObject>(object) : nullptr;
        if (path) {
            const auto bounds = path->bounds();
            const double center_x = bounds.x + bounds.width * 0.5;
            const double center_y = bounds.y + bounds.height * 0.5;
            const double start_dist = std::hypot(contour_drag_start_world_.x - center_x, contour_drag_start_world_.y - center_y);
            const double curr_dist = std::hypot(pointer_current_world_.x - center_x, pointer_current_world_.y - center_y);
            const double delta = curr_dist - start_dist;

            if (delta >= 0.0) {
                contour_tool_->set_direction(geometry::ContourDirection::Outside);
            } else {
                contour_tool_->set_direction(geometry::ContourDirection::Inside);
            }
            double dist = std::max(0.1, std::abs(delta));
            dist = std::round(dist * 10.0) / 10.0;
            contour_tool_->set_spacing_mm(dist);

            status_msg_ = (language_ == UILanguage::Vietnamese ? "Khoảng cách Contour: " : "Contour spacing: ") +
                format_mm_display_utf8(dist, display_length_unit_, 5) + " (" +
                (delta >= 0.0 ? "Outside / Ngoài" : "Inside / Trong") + ")";
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
    }
    if (pointer_active_ && text_editor_ && text_editor_->is_moving_frame) {
        auto& editor = *text_editor_;
        geometry::Point2D delta = {pointer_current_world_.x - editor.live_move_start_world.x,
                                   pointer_current_world_.y - editor.live_move_start_world.y};
        if (const auto inv = editor.parent_transform.inverted()) {
            delta = inv->map_point(pointer_current_world_) - inv->map_point(editor.live_move_start_world);
        }
        editor.draft.position = {editor.live_move_start_pos.x + delta.x,
                                 editor.live_move_start_pos.y + delta.y};
        refresh_inline_text_preview();
        status_msg_ = (language_ == UILanguage::Vietnamese ? "Vị trí chữ: " : "Text position: ") +
            format_mm_display_utf8(editor.draft.position.x, display_length_unit_, 5) + ", " +
            format_mm_display_utf8(editor.draft.position.y, display_length_unit_, 5);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (pointer_active_ && text_editor_ && text_editor_->live_scale_baseline) {
        auto& editor = *text_editor_;
        const uint64_t now = GetTickCount64();
        if (!force_live_text_scale_update_ && editor.live_scale_last_update_ms != 0 &&
            now - editor.live_scale_last_update_ms < 16)
            return;
        const auto& baseline = *editor.live_scale_baseline;
        editor.live_scale_last_update_ms = now;
        const bool is_corner = (resize_handle_ % 2 == 0);
        const bool is_horiz = (resize_handle_ == 3 || resize_handle_ == 7);
        const bool is_vert = (resize_handle_ == 1 || resize_handle_ == 5);

        if (baseline.frame_width_mm > 0 && is_horiz) {
            const auto inv = editor.parent_transform.inverted();
            const double curr_x = inv ? inv->map_point(pointer_current_world_).x : pointer_current_world_.x;
            const double start_x = inv ? inv->map_point(editor.live_scale_start_world).x : editor.live_scale_start_world.x;
            const double delta_x = curr_x - start_x;
            editor.draft = baseline;
            if (resize_handle_ == 3) {
                editor.draft.frame_width_mm = std::clamp(baseline.frame_width_mm + delta_x, 5.0, geometry::kMaxObjectDimensionMm);
            } else {
                const double new_w = std::clamp(baseline.frame_width_mm - delta_x, 5.0, geometry::kMaxObjectDimensionMm);
                editor.draft.position.x = baseline.position.x + (baseline.frame_width_mm - new_w);
                editor.draft.frame_width_mm = new_w;
            }
        } else {
            double factor = 1.0;
            if (is_corner) {
                const double start_dx = editor.live_scale_start_world.x - editor.live_scale_anchor_world.x;
                const double start_dy = editor.live_scale_start_world.y - editor.live_scale_anchor_world.y;
                const double dist_sq = start_dx * start_dx + start_dy * start_dy;
                if (dist_sq > 1e-12) {
                    const double curr_dx = pointer_current_world_.x - editor.live_scale_anchor_world.x;
                    const double curr_dy = pointer_current_world_.y - editor.live_scale_anchor_world.y;
                    factor = (curr_dx * start_dx + curr_dy * start_dy) / dist_sq;
                }
            } else if (is_horiz) {
                const double start_dx = std::abs(editor.live_scale_start_world.x - editor.live_scale_anchor_world.x);
                const double curr_dx = std::abs(pointer_current_world_.x - editor.live_scale_anchor_world.x);
                if (start_dx > 1e-9) factor = curr_dx / start_dx;
            } else if (is_vert) {
                const double start_dy = std::abs(editor.live_scale_start_world.y - editor.live_scale_anchor_world.y);
                const double curr_dy = std::abs(pointer_current_world_.y - editor.live_scale_anchor_world.y);
                if (start_dy > 1e-9) factor = curr_dy / start_dy;
            }
            const double target_size = std::clamp(baseline.font.size_pt * std::max(0.01, factor), 1.0, 2880.0);
            if (!force_live_text_scale_update_ && std::abs(target_size - editor.draft.font.size_pt) < 0.05)
                return;
            const double r = target_size / baseline.font.size_pt;
            editor.draft = baseline;
            editor.draft.font.size_pt = target_size;
            if (baseline.frame_width_mm > 0) {
                editor.draft.frame_width_mm = std::clamp(baseline.frame_width_mm * r, 5.0, geometry::kMaxObjectDimensionMm);
            }
            const auto new_bounds = editor.parent_transform.map_rect(editor.draft.bounds());
            const auto a_curr = handle_anchor_point(new_bounds, resize_handle_);
            const auto inv = editor.parent_transform.inverted();
            const auto d_loc = inv ? (inv->map_point(editor.live_scale_anchor_world) - inv->map_point(a_curr))
                                   : geometry::Point2D{editor.live_scale_anchor_world.x - a_curr.x,
                                                       editor.live_scale_anchor_world.y - a_curr.y};
            editor.draft.position.x += d_loc.x;
            editor.draft.position.y += d_loc.y;
        }
        refresh_inline_text_preview();
        if (is_inline_text_editing_) {
            std::ostringstream hint;
            if (baseline.frame_width_mm > 0 && is_horiz) {
                hint << format_mm_display_utf8(editor.draft.frame_width_mm, display_length_unit_, 5);
                status_msg_ = (language_ == UILanguage::Vietnamese ? "Khung chữ: " : "Frame: ") + hint.str();
            } else {
                hint << std::fixed << std::setprecision(1) << editor.draft.font.size_pt << " pt";
                status_msg_ = (language_ == UILanguage::Vietnamese ? "Cỡ chữ: " : "Font size: ") + hint.str();
            }
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (is_shape_builder_click_ && pointer_active_) {
        if (shape_builder_gesture_.size() >= 4096) shape_builder_gesture_overflow_ = true;
        else if (shape_builder_gesture_.empty() ||
                 pointer_current_world_.distance_to(shape_builder_gesture_.back()) > 0.01)
            shape_builder_gesture_.push_back(pointer_current_world_);
        invalidate_canvas_overlay();
        return;
    }
    if (is_creating_shape_) {
        shape_drag_modifiers_ = modifiers & (MK_CONTROL | MK_SHIFT);
        update_layout_snap_preview(*root().document().current_snapshot(), {}, -1, false, true);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (is_knife_cutting_) {
        knife_current_world_ = pointer_current_world_;
        if (active_tool_ == 5 && knife_freehand_mode_ && !knife_stroke_overflow_ &&
            (knife_stroke_.empty() || std::hypot(pointer_current_world_.x - knife_stroke_.back().x,
                                                  pointer_current_world_.y - knife_stroke_.back().y) > 0.1)) {
            if (knife_stroke_.size() >= 4096) knife_stroke_overflow_ = true;
            else knife_stroke_.push_back(pointer_current_world_);
        }
        if (active_tool_ == 16 && !eraser_stroke_overflow_ &&
            (eraser_stroke_.empty() || std::hypot(pointer_current_world_.x - eraser_stroke_.back().x,
                                                   pointer_current_world_.y - eraser_stroke_.back().y) > 1e-9)) {
            if (eraser_stroke_.size() >= 4096) eraser_stroke_overflow_ = true;
            else eraser_stroke_.push_back(pointer_current_world_);
        }
        record_performance_event(CanvasTraceKind::CutGestureMove, last_trace_event_id_);
        invalidate_canvas_overlay();
        return;
    }
    pointer_preserve_aspect_ = (modifiers & MK_SHIFT) != 0;
    if (pointer_active_ && resize_handle_ == kPendingTextBodyMove &&
        std::hypot(x - drag_start_x_, y - drag_start_y_) >= 4) {
        const auto snapshot = root().document().current_snapshot();
        auto& selection = root().vector_controller().selection();
        if (root().vector_controller().gesture().begin(
                pointer_down_world_, selection.selected_ids(), *snapshot)) {
            is_dragging_ = true;
            resize_handle_ = -1;
            drag_preview_bounds_ = selection.bounds(*snapshot);
        }
    }
    if (is_dragging_ && (drag_preview_bounds_ || resize_handle_ == 9) && std::hypot(x - drag_start_x_, y - drag_start_y_) >= 2) {
        if (resize_handle_ == 9) {
            auto new_pivot = pointer_current_world_;
            if (drag_preview_bounds_) {
                const auto center = drag_preview_bounds_->center();
                const double zoom = root().renderer().camera().zoom();
                const double snap_dist_world = (zoom > 1e-4) ? (7.0 / zoom) : 2.0;
                if (std::hypot(new_pivot.x - center.x, new_pivot.y - center.y) <= snap_dist_world) {
                    new_pivot = center;
                } else {
                    const geometry::Point2D candidates[] = {
                        {drag_preview_bounds_->x, drag_preview_bounds_->y},
                        {drag_preview_bounds_->x + drag_preview_bounds_->width * 0.5, drag_preview_bounds_->y},
                        {drag_preview_bounds_->right(), drag_preview_bounds_->y},
                        {drag_preview_bounds_->right(), drag_preview_bounds_->y + drag_preview_bounds_->height * 0.5},
                        {drag_preview_bounds_->right(), drag_preview_bounds_->bottom()},
                        {drag_preview_bounds_->x + drag_preview_bounds_->width * 0.5, drag_preview_bounds_->bottom()},
                        {drag_preview_bounds_->x, drag_preview_bounds_->bottom()},
                        {drag_preview_bounds_->x, drag_preview_bounds_->y + drag_preview_bounds_->height * 0.5}
                    };
                    for (const auto& pt : candidates) {
                        if (std::hypot(new_pivot.x - pt.x, new_pivot.y - pt.y) <= snap_dist_world) {
                            new_pivot = pt;
                            break;
                        }
                    }
                }
            }
            custom_pivot_world_ = new_pivot;
            status_msg_ = (language_ == UILanguage::Vietnamese ? "TÃ¢m xoay: " : "Pivot: ") +
                format_mm_display_utf8(new_pivot.x, display_length_unit_, 5) + ", " +
                format_mm_display_utf8(new_pivot.y, display_length_unit_, 5);
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
            const bool preserve = active_tool_ == 4 && is_text_object_selected() && resize_handle_ <= 7;
            const bool preserve_aspect = preserve ||
                ((resize_handle_ % 2 == 0) && pointer_preserve_aspect_);
            std::optional<geometry::Transform2D> transform;
            if (active_tool_ == 0 && !is_rotate_mode_) {
                update_layout_snap_preview(*root().document().current_snapshot(), delta,
                                           resize_handle_, preserve_aspect);
                transform = vector::selection_resize_transform(*drag_preview_bounds_, resize_handle_,
                    smart_guide_solution_.accepted_delta, preserve_aspect);
            } else {
                transform = preserve
                    ? text_resize_transform(*drag_preview_bounds_, resize_handle_, delta)
                    : vector::selection_resize_transform(*drag_preview_bounds_, resize_handle_, delta,
                        (resize_handle_ % 2 == 0) && pointer_preserve_aspect_);
                smart_guide_solution_ = {};
                smart_guide_acquisition_ = {};
                smart_guide_moving_bounds_.reset();
            }
            if (transform) {
                (void)gesture.update_transform(*transform);
                const auto preview = transform->map_rect(*drag_preview_bounds_);
                const std::string hint = format_mm_display_utf8(preview.width, display_length_unit_, 5) +
                    " × " + format_mm_display_utf8(preview.height, display_length_unit_, 5);
                status_msg_ = (language_ == UILanguage::Vietnamese ? "Kích thước: " : "Scale: ") + hint +
                    (preserve ? (language_ == UILanguage::Vietnamese ? " · giữ tỉ lệ chữ" : " · text proportions locked")
                              : (language_ == UILanguage::Vietnamese ? " · tự do" : " · free"));
            }
        } else {
            (void)gesture.update(pointer_current_world_);
            if (active_tool_ == 0)
                update_layout_snap_preview(*root().document().current_snapshot(), delta);
            else {
                smart_guide_acquisition_ = {};
                smart_guide_solution_ = {};
                smart_guide_moving_bounds_.reset();
            }
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

geometry::Rect2D NativeWindow::shape_drag_bounds(unsigned modifiers) const {
    if (active_tool_ == 18) {
        const auto end = is_creating_shape_ && shape_snap_effective_world_
            ? *shape_snap_effective_world_ : pointer_current_world_;
        const double radius = pointer_down_world_.distance_to(end);
        return {pointer_down_world_.x - radius, pointer_down_world_.y - radius,
                radius * 2.0, radius * 2.0};
    }
    const bool ctrl = (modifiers & MK_CONTROL) != 0, shift = (modifiers & MK_SHIFT) != 0;
    return vector::ShapeFactory::constrained_drag_bounds(
        pointer_down_world_, is_creating_shape_ && shape_snap_effective_world_
            ? *shape_snap_effective_world_ : pointer_current_world_, active_tool_ != 9 && shift, ctrl);
}

double NativeWindow::shape_drag_polygon_rotation(unsigned modifiers) const noexcept {
    if ((modifiers & MK_SHIFT) == 0) return polygon_rotation_degrees_;
    geometry::Rect2D b;
    try { b = shape_drag_bounds(modifiers); } catch (...) { return polygon_rotation_degrees_; }
    const auto drag_end = is_creating_shape_ && shape_snap_effective_world_
        ? *shape_snap_effective_world_ : pointer_current_world_;
    const double dx = drag_end.x - b.center().x, dy = drag_end.y - b.center().y;
    if (std::hypot(dx, dy) <= 1e-12) return polygon_rotation_degrees_;
    return std::round((std::atan2(dy, dx) * 180.0 / 3.14159265358979323846) / 15.0) * 15.0;
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
    const size_t destination_layer = active_layer_index();
    if (destination_layer >= snapshot->page().layers.size() ||
        snapshot->page().effective_layer_locked(destination_layer) ||
        !snapshot->page().effective_layer_visible(destination_layer))
        return false;
    document::PathObject object;
    try {
        if (active_tool_ == 1) {
            vector::RectCornerSettings settings;
            settings.type = creation_rect_corner_type_;
            settings.tl = creation_rect_corner_tl_;
            settings.tr = creation_rect_corner_tr_;
            settings.br = creation_rect_corner_br_;
            settings.bl = creation_rect_corner_bl_;
            settings.locked = creation_rect_corners_locked_;
            settings.scale_relative = creation_rect_corners_scale_relative_;
            object = vector::ShapeFactory::create_rounded_rectangle(bounds, settings, default_style_.fill,
                default_style_.stroke, default_style_.width_mm);
        } else if (active_tool_ == 2) {
            object = vector::ShapeFactory::create_ellipse(bounds.center(), bounds.width * .5,
                bounds.height * .5, default_style_.fill, default_style_.stroke,
                default_style_.width_mm);
        } else if (active_tool_ == 18) {
            const auto end = is_creating_shape_ && shape_snap_effective_world_
                ? *shape_snap_effective_world_ : pointer_current_world_;
            const auto circle = geometry::circle_from_center_radius(
                pointer_down_world_, pointer_down_world_.distance_to(end));
            if (!circle) throw std::invalid_argument("Invalid circle radius");
            object = vector::ShapeFactory::create_circle(*circle, default_style_.fill,
                default_style_.stroke, default_style_.width_mm);
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
    object.has_fill = active_tool_ == 18 ? false : default_style_.has_fill;
    object.has_stroke = default_style_.has_stroke;
    const auto result = root().document().execute_command(
        std::make_unique<document::AddObjectCommand>(object, destination_layer));
    if (result.success) {
        root().vector_controller().selection().clear();

        // Kích hoạt chế độ gõ nhanh kích thước ngang dài kiểu SketchUp (chỉ áp dụng cho lần vẽ đầu tiên)
        quick_dimension_active_ = active_tool_ != 18;
        quick_dimension_object_id_ = object.id;
        quick_dimension_buffer_.clear();
        quick_dimension_origin_ = pointer_down_world_;
        quick_dimension_drag_dir_x_ = (pointer_current_world_.x >= pointer_down_world_.x) ? 1.0 : -1.0;
        quick_dimension_drag_dir_y_ = (pointer_current_world_.y >= pointer_down_world_.y) ? 1.0 : -1.0;
        quick_dimension_initial_w_ = bounds.width;
        quick_dimension_initial_h_ = bounds.height;

        const std::string dims = active_tool_ == 18
            ? ("R " + format_mm_display_utf8(bounds.width * 0.5, display_length_unit_, 5) +
               " · Ø " + format_mm_display_utf8(bounds.width, display_length_unit_, 5))
            : format_mm_display_utf8(bounds.width, display_length_unit_, 5) +
              " × " + format_mm_display_utf8(bounds.height, display_length_unit_, 5);
        status_msg_ = language_ == UILanguage::Vietnamese
            ? (active_tool_ == 18 ? "Đã tạo đường tròn [" + dims + "]" :
               "Đã tạo hình [" + dims + "] · Gõ kích thước theo đơn vị trang; hậu tố ghi đè (vd: 30,40) · Enter nhận | Esc hủy")
            : (active_tool_ == 18 ? "Circle created [" + dims + "]" :
               "Shape created [" + dims + "] · Type page-unit dimensions; suffix overrides (e.g. 30,40) · Enter applies | Esc cancels");
    }
    return result.success;
}

void NativeWindow::handle_canvas_mouse_up(int x, int y, unsigned modifiers) {
    if (artboard_tool_ && artboard_tool_->active()) {
        (void)handle_artboard_pointer_up(x, y, modifiers);
        return;
    }
    if (active_tool_ == 10 && dimension_tool_ && dimension_tool_->active()) {
        dimension_tool_->handle_pointer_release(pointer_current_world_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (active_tool_ == 17 && annotation_tool_ && annotation_tool_->active()) {
        pointer_current_world_ = canvas_to_world(x, y);
        const double dip_scale = std::max(0.01, window_dpi_scale());
        const geometry::Point2D screen_dip{static_cast<double>(x) / dip_scale,
                                           static_cast<double>(y) / dip_scale};
        annotation_tool_->handle_pointer_move(pointer_current_world_, screen_dip);
        const auto state_before_release = annotation_tool_->state();
        const bool handled = annotation_tool_->handle_pointer_release(pointer_current_world_, screen_dip);
        pointer_active_ = false;
        if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
        if (annotation_tool_->last_committed_id().has_value()) {
            const auto id = *annotation_tool_->last_committed_id();
            set_active_tool(0);
            if (const auto snapshot = root().document().current_snapshot()) {
                root().vector_controller().set_selection({id}, *snapshot);
            }
        } else if (state_before_release == tools::AnnotationSessionState::DetailSourceDragging && !handled) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Kéo xa hơn để chọn vùng nguồn"
                : "Drag farther to select the source region";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (text_editor_ && text_editor_->selecting && handle_inline_text_mouse(x, y, 2)) return;
    if (!pointer_active_) return;
    force_live_text_scale_update_ = true;
    handle_canvas_mouse_move(x, y, modifiers); // release can arrive without a final move event
    force_live_text_scale_update_ = false;
    if (is_contour_dragging_) {
        is_contour_dragging_ = false;
        pointer_active_ = false;
        if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã điều chỉnh viền Contour · Nhấp 'Áp dụng' trên thanh thuộc tính để xác nhận"
            : "Adjusted Contour · Click 'Apply' on the Property Bar to commit";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (text_editor_ && (text_editor_->live_scale_baseline || text_editor_->is_moving_frame)) {
        if (text_editor_->is_moving_frame && text_editor_->live_move_baseline) {
            const auto& b = *text_editor_->live_move_baseline;
            if (std::hypot(text_editor_->draft.position.x - b.position.x,
                           text_editor_->draft.position.y - b.position.y) >= 0.05)
                text_editor_->push_undo(b);
            text_editor_->is_moving_frame = false;
            text_editor_->live_move_baseline.reset();
        }
        if (text_editor_->live_scale_baseline) {
            const auto& b = *text_editor_->live_scale_baseline;
            const bool changed = std::abs(text_editor_->draft.font.size_pt - b.font.size_pt) >= 0.05 ||
                                 std::abs(text_editor_->draft.frame_width_mm - b.frame_width_mm) >= 0.05 ||
                                 std::hypot(text_editor_->draft.position.x - b.position.x,
                                            text_editor_->draft.position.y - b.position.y) >= 0.05;
            if (changed) text_editor_->push_undo(b);
            text_editor_->live_scale_baseline.reset();
        }
        resize_handle_ = -1;
        pointer_active_ = is_dragging_ = false;
        drag_preview_bounds_.reset();
        if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (resize_handle_ == kPendingTextBodyMove) {
        resize_handle_ = -1;
        pointer_active_ = false;
        if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto& page = root().document().current_snapshot()->page();
    const double half_canvas = geometry::kMaxCanvasDimensionMm * 0.5;
    pointer_current_world_.x = std::clamp(pointer_current_world_.x,
        page.width_mm * 0.5 - half_canvas, page.width_mm * 0.5 + half_canvas);
    pointer_current_world_.y = std::clamp(pointer_current_world_.y,
        page.height_mm * 0.5 - half_canvas, page.height_mm * 0.5 + half_canvas);
    const bool stale = !pointer_stamp_.matches(*root().document().current_snapshot());
    bool changed = false;
    bool geometry_job_started = false;
    bool geometry_job_attempted = false;
    std::string geometry_job_start_status;
    const bool knife = is_knife_cutting_;
    const bool trim_role_click = is_trim_role_click_;
    const bool smart_trim_click = is_smart_trim_click_;
    const bool shape_builder_click = is_shape_builder_click_;
    const bool smart_fill_click = is_smart_fill_click_;
    const bool raster_eraser_stroke = is_raster_eraser_stroke_;
    const bool raster_eraser_overflow = raster_eraser_stroke && raster_eraser_stroke_overflow_;
    const bool eraser_overflow = knife && active_tool_ == 16 && eraser_stroke_overflow_;
    const bool knife_overflow = knife && active_tool_ == 5 && knife_stroke_overflow_;
    const bool is_corner_rotate = is_rotate_mode_ && (resize_handle_ == 0 || resize_handle_ == 2 ||
                                                      resize_handle_ == 4 || resize_handle_ == 6);
    const bool rotation = resize_handle_ == 8 || is_corner_rotate;
    const bool pivot_drag = resize_handle_ == 9;
    const bool clicked_without_drag = std::hypot(x - drag_start_x_, y - drag_start_y_) < 3;
    if (!stale) {
        if (raster_eraser_stroke) {
            if (!raster_eraser_overflow)
                changed = commit_raster_eraser_stroke(raster_eraser_stroke_);
            else
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Nét tẩy ảnh vượt giới hạn; tài liệu giữ nguyên"
                    : "Raster eraser stroke exceeded its limit; document unchanged";
        } else if (is_marquee_) {
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
                if (active_tool_ == 0 && previous_drawing_tool_ > 0) {
                    const int return_tool = previous_drawing_tool_;
                    previous_drawing_tool_ = 0;
                    set_active_tool(return_tool);
                }
            }
        } else if (is_creating_shape_) {
            changed = commit_created_shape();
        } else if (is_knife_cutting_) {
            knife_current_world_ = pointer_current_world_;
            if (active_tool_ == 16) {
                if (!eraser_stroke_overflow_) {
                    CutGeometryRequest request;
                    request.operation = CutGeometryOperation::VectorEraser;
                    request.snapshot = root().document().current_snapshot();
                    request.selected_ids = root().vector_controller().selection().selected_ids();
                    request.points_world = eraser_stroke_;
                    request.diameter_mm = vector_eraser_diameter_mm_;
                    request.round_nib = vector_eraser_round_nib_;
                    geometry_job_attempted = true;
                    geometry_job_started = start_cut_geometry_job(std::move(request));
                    if (!geometry_job_started) geometry_job_start_status = status_msg_;
                } else status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Nét tẩy quá dài; thao tác đã hủy" : "Eraser stroke too long; operation cancelled";
            } else if (knife_overflow) {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đường Knife quá dài; thao tác đã hủy" : "Knife gesture too long; operation cancelled";
            } else if (knife_freehand_mode_) {
                if (knife_stroke_.empty() ||
                    knife_stroke_.back().distance_to(pointer_current_world_) > 1e-9) {
                    if (knife_stroke_.size() >= 4096) knife_stroke_overflow_ = true;
                    else knife_stroke_.push_back(pointer_current_world_);
                }
                if (!knife_stroke_overflow_) {
                    CutGeometryRequest request;
                    request.operation = CutGeometryOperation::Knife;
                    request.snapshot = root().document().current_snapshot();
                    request.selected_ids = root().vector_controller().selection().selected_ids();
                    request.points_world = knife_stroke_;
                    request.gap_mm = knife_gap_mm_;
                    geometry_job_attempted = true;
                    geometry_job_started = start_cut_geometry_job(std::move(request));
                    if (!geometry_job_started) geometry_job_start_status = status_msg_;
                }
            } else {
                CutGeometryRequest request;
                request.operation = CutGeometryOperation::Knife;
                request.snapshot = root().document().current_snapshot();
                request.selected_ids = root().vector_controller().selection().selected_ids();
                request.points_world = {knife_start_world_, knife_current_world_};
                request.gap_mm = knife_gap_mm_;
                geometry_job_attempted = true;
                geometry_job_started = start_cut_geometry_job(std::move(request));
                if (!geometry_job_started) geometry_job_start_status = status_msg_;
            }
            if (!geometry_job_attempted && !geometry_job_started && !changed && !eraser_overflow &&
                !knife_overflow && !knife_stroke_overflow_)
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Không có path phù hợp bị tác động · tài liệu giữ nguyên"
                    : "No eligible path was affected · document unchanged";
        } else if (is_scissors_click_) {
            if (clicked_without_drag && is_canvas_point(x, y)) {
                const double zoom = std::max(0.01, root().renderer().camera().zoom());
                CutGeometryRequest request;
                request.operation = CutGeometryOperation::Scissors;
                request.snapshot = root().document().current_snapshot();
                request.selected_ids = root().vector_controller().selection().selected_ids();
                request.points_world = {pointer_down_world_};
                request.hit_tolerance_mm = 6.0 / zoom;
                geometry_job_attempted = true;
                geometry_job_started = start_cut_geometry_job(std::move(request));
                if (!geometry_job_started) geometry_job_start_status = status_msg_;
            } else {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Scissors là thao tác nhấp; không kéo chuột"
                    : "Scissors is a click tool; dragging does not cut";
            }
        } else if (trim_role_click) {
            if (clicked_without_drag && is_canvas_point(x, y)) {
                const double zoom = std::max(0.01, root().renderer().camera().zoom());
                const auto hit = root().vector_controller().pick(
                    pointer_down_world_, *root().document().current_snapshot(), 6.0 / zoom);
                const auto snapshot = root().document().current_snapshot();
                const auto* object = hit && snapshot ? snapshot->objects().lookup_ref(hit->id) : nullptr;
                const auto* path = object ? std::get_if<document::PathObject>(object) : nullptr;
                const auto* owner = hit && snapshot ? snapshot->hierarchy().find(hit->id) : nullptr;
                if (!path || !owner || owner->mask ||
                    !snapshot->hierarchy().editable(hit->id)) {
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Trim Objects chỉ nhận path kín, hiển thị và mở khóa"
                        : "Trim Objects requires a visible, unlocked closed path";
                } else if (!trim_objects_target_id_) {
                    discard_pending_cut_geometry_proposal();
                    trim_objects_target_id_ = hit->id;
                    trim_objects_source_ids_.clear();
                    root().vector_controller().set_selection({hit->id}, *snapshot);
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Đã chọn Target · nhấp một hoặc nhiều Source"
                        : "Target selected · click one or more Sources";
                } else if (hit->id == trim_objects_target_id_) {
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Đây đã là Target · chọn Source khác hoặc áp dụng"
                        : "This is already the Target · choose another Source or apply";
                } else {
                    discard_pending_cut_geometry_proposal();
                    const auto source = std::find(trim_objects_source_ids_.begin(),
                                                  trim_objects_source_ids_.end(), hit->id);
                    if (source == trim_objects_source_ids_.end()) {
                        if (trim_objects_source_ids_.size() >= 64) {
                            status_msg_ = language_ == UILanguage::Vietnamese
                                ? "Trim Objects giới hạn 64 Source cho mỗi giao dịch"
                                : "Trim Objects is limited to 64 Source paths per transaction";
                            InvalidateRect(hwnd_, nullptr, FALSE);
                            return;
                        }
                        trim_objects_source_ids_.push_back(hit->id);
                    } else {
                        trim_objects_source_ids_.erase(source);
                    }
                    auto roles = trim_objects_source_ids_;
                    roles.insert(roles.begin(), trim_objects_target_id_);
                    root().vector_controller().set_selection(std::move(roles), *snapshot);
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Đã cập nhật danh sách Source · nhấp lại Source để bỏ"
                        : "Source list updated · click a Source again to remove it";
                }
            } else {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Chọn vai trò bằng từng lần nhấp; không kéo"
                    : "Choose object roles with clicks; dragging is ignored";
            }
        } else if (smart_trim_click) {
            if (clicked_without_drag && is_canvas_point(x, y)) {
                const double zoom = std::max(0.01, root().renderer().camera().zoom());
                CutGeometryRequest request;
                request.operation = CutGeometryOperation::SmartTrim;
                request.snapshot = root().document().current_snapshot();
                request.selected_ids = root().vector_controller().selection().selected_ids();
                request.points_world = {pointer_down_world_};
                request.hit_tolerance_mm = 6.0 / zoom;
                geometry_job_attempted = true;
                const double preview_match_mm = 1.5 / zoom;
                if (cut_geometry_preview_ready(CutGeometryOperation::SmartTrim) &&
                    pending_cut_geometry_proposal_->state &&
                    pending_cut_geometry_proposal_->state->points_world.size() == 1 &&
                    pending_cut_geometry_proposal_->state->points_world.front().distance_to(
                        pointer_down_world_) <= preview_match_mm) {
                    changed = apply_pending_cut_geometry_proposal(
                        CutGeometryOperation::SmartTrim);
                    geometry_job_attempted = false;
                } else if (cut_geometry_job_ &&
                           cut_geometry_job_->operation == CutGeometryOperation::SmartTrim &&
                           cut_geometry_job_->preview_only &&
                           cut_geometry_job_->points_world.size() == 1 &&
                           cut_geometry_job_->points_world.front().distance_to(
                               pointer_down_world_) <= preview_match_mm) {
                    // The hover worker already has the interval under this click.
                    // Promote it to the click's commit so latency never changes the tool's gesture.
                    cut_geometry_job_->preview_only = false;
                    smart_trim_hover_world_ = pointer_down_world_;
                    geometry_job_started = true;
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Đang áp dụng đoạn Smart Trim đã chọn · Esc để hủy"
                        : "Applying the selected Smart Trim interval · Esc to cancel";
                } else {
                    if (cut_geometry_preview_ready(CutGeometryOperation::SmartTrim))
                        discard_pending_cut_geometry_proposal();
                    request.preview_only = false;
                    if (cut_geometry_job_ &&
                        cut_geometry_job_->operation == CutGeometryOperation::SmartTrim &&
                        cut_geometry_job_->preview_only) {
                        smart_trim_click_pending_request_ = request;
                        smart_trim_hover_world_ = pointer_down_world_;
                        ++cut_geometry_generation_;
                        if (cut_geometry_worker_.joinable())
                            cut_geometry_worker_.request_stop();
                        geometry_job_started = true;
                        if (hwnd_ && SetTimer(hwnd_,
                                static_cast<UINT_PTR>(kSmartTrimHoverTimerId), 50, nullptr) == 0)
                            status_msg_ = language_ == UILanguage::Vietnamese
                                ? "Đang chờ hủy preview cũ · sau đó sẽ áp dụng thao tác đã nhấp"
                                : "Waiting for the prior preview to cancel before applying this click";
                    } else {
                        geometry_job_started = start_cut_geometry_job(std::move(request));
                        if (!geometry_job_started) geometry_job_start_status = status_msg_;
                    }
                }
            } else {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Smart Trim là thao tác nhấp vào đoạn nét giữa hai giao điểm"
                    : "Smart Trim is a click tool; click a stroke interval between intersections";
            }
        } else if (shape_builder_click) {
            if ((!clicked_without_drag || shape_builder_erase_gesture_) && is_canvas_point(x, y)) {
                if (shape_builder_gesture_overflow_) {
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Nét Shape Builder vượt giới hạn; tài liệu giữ nguyên"
                        : "Shape Builder gesture exceeded its limit; document unchanged";
                } else {
                    if (shape_builder_gesture_.empty() ||
                        pointer_current_world_.distance_to(shape_builder_gesture_.back()) > 1e-9)
                        shape_builder_gesture_.push_back(pointer_current_world_);
                    shape_builder_seed_points_ = shape_builder_gesture_;
                    CutGeometryRequest request;
                    request.operation = CutGeometryOperation::ShapeBuilder;
                    request.snapshot = root().document().current_snapshot();
                    request.selected_ids = root().vector_controller().selection().selected_ids();
                    request.points_world = shape_builder_seed_points_;
                    request.erase_faces = shape_builder_erase_gesture_;
                    request.shape_builder_gesture = true;
                    geometry_job_attempted = true;
                    geometry_job_started = start_cut_geometry_job(std::move(request));
                    if (!geometry_job_started) geometry_job_start_status = status_msg_;
                }
            } else if (clicked_without_drag && is_canvas_point(x, y)) {
                if (shape_builder_seed_points_.size() >= 64) {
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Shape Builder giới hạn 64 mặt cho mỗi giao dịch"
                        : "Shape Builder is limited to 64 faces per transaction";
                } else {
                    discard_pending_cut_geometry_proposal();
                    shape_builder_seed_points_.push_back(pointer_down_world_);
                    status_msg_ = (language_ == UILanguage::Vietnamese
                        ? "Đã thêm điểm mặt cần kiểm tra · tổng số: "
                        : "Added face point for validation · count: ") +
                        std::to_string(shape_builder_seed_points_.size()) +
                        (language_ == UILanguage::Vietnamese
                            ? " · áp dụng trên thanh thuộc tính để tính và xem lỗi"
                            : " · apply on the Property Bar to compute and validate");
                }
            } else {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Shape Builder chọn từng mặt bằng nhấp; kéo chuột không chọn"
                    : "Shape Builder selects faces with clicks; dragging does not select";
            }
        } else if (smart_fill_click) {
            if (clicked_without_drag && is_canvas_point(x, y)) {
                CutGeometryRequest request;
                request.operation = CutGeometryOperation::SmartFill;
                request.snapshot = root().document().current_snapshot();
                request.selected_ids = root().vector_controller().selection().selected_ids();
                request.points_world = {pointer_down_world_};
                geometry_job_attempted = true;
                geometry_job_started = start_cut_geometry_job(std::move(request));
                if (!geometry_job_started) geometry_job_start_status = status_msg_;
            } else {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Smart Fill là thao tác nhấp vào bên trong một mặt kín"
                    : "Smart Fill is a click tool; click inside a closed face";
            }
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
    cancel_canvas_interaction(!geometry_job_started);
    const auto selected_after_pointer = root().vector_controller().selection().selected_ids();
    if (!selected_after_pointer.empty()) {
        const auto selected_snapshot = root().document().current_snapshot();
        if (selected_snapshot) {
            const auto* selected_entry = selected_snapshot->hierarchy().find(
                selected_after_pointer.back());
            if (selected_entry) set_active_layer_index(selected_entry->layer);
        }
    }
    workflow().sync_status_bar();
    sync_rect_corner_controls_from_selection();
    if (geometry_job_started) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đang tính hình học Cut & Shape · Esc để hủy"
            : "Computing Cut & Shape geometry · Esc to cancel";
        // The worker owns the validated snapshot request; its completion
        // handler publishes success, cancellation, or stale-result feedback.
    } else if (geometry_job_attempted) {
        status_msg_ = std::move(geometry_job_start_status);
    } else if (eraser_overflow) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Nét tẩy quá dài; thao tác đã hủy" : "Eraser stroke too long; operation cancelled";
    } else if (raster_eraser_overflow) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Nét tẩy ảnh vượt giới hạn; tài liệu giữ nguyên"
            : "Raster eraser stroke exceeded its limit; document unchanged";
    } else if (stale) {
        status_msg_ = language_ == UILanguage::Vietnamese ? "Bản vẽ đã đổi; thao tác đã được hủy an toàn." : "Document changed; interaction cancelled.";
    } else if (pivot_drag) {
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã điều chỉnh tâm xoay · kéo góc để xoay quanh tâm" : "Adjusted rotation pivot · drag corners to rotate";
    } else if (clicked_without_drag && is_rotate_mode_) {
        status_msg_ = language_ == UILanguage::Vietnamese ? "Chế độ xoay: kéo 4 góc để xoay quanh tâm, kéo tâm để dời vị trí" : "Rotate mode: drag 4 corners to rotate, drag pivot to reposition";
    } else if (clicked_without_drag && !is_rotate_mode_ && active_tool_ == 0) {
        status_msg_ = language_ == UILanguage::Vietnamese ? "Chế độ biến đổi: kéo tay nắm để đổi kích thước" : "Transform mode: drag handles to scale";
    } else if (trim_role_click) {
        // Keep the role-selection hint set by the click resolver.
    } else if (smart_trim_click) {
        // Keep the intersection/no-op explanation from the Smart Trim resolver.
    } else if (shape_builder_click) {
        // Preserve face-resolution feedback; this click only changes tool state.
    } else if (raster_eraser_stroke && changed) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã tẩy alpha không phá hủy · ảnh gốc được giữ · Ctrl+Z để hoàn tác"
            : "Applied non-destructive alpha erase · source pixels remain unchanged · Ctrl+Z to undo";
    } else if (raster_eraser_stroke) {
        // Keep the validation or command failure reported by the eraser.
    } else if (geometry_job_attempted) {
        // Keep the reason the geometry worker could not be started.
    } else if (changed && knife) {
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã dùng dao cắt phân tách hình khối thành công · Ctrl+Z để hoàn tác" : "Knife split completed · Ctrl+Z to undo";
    } else if (changed && rotation) {
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã xoay vùng chọn · Ctrl+Z để hoàn tác" : "Rotated selection · Ctrl+Z to undo";
    } else if (changed) {
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã cập nhật · Ctrl+Z để hoàn tác" : "Updated · Ctrl+Z to undo";
    } else {
        status_msg_ = (language_ == UILanguage::Vietnamese ? "Đang chọn: " : "Selected: ") + std::to_string(root().vector_controller().selection().count());
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
#endif
} // namespace pikdraw::desktop
