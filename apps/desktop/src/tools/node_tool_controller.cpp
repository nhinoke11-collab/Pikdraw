#include "node_tool_controller.hpp"

namespace pikdraw::desktop::tools {

bool NodeToolController::activate(document::ObjectId target_id) {
    if (target_id != document::kInvalidObjectId) {
        tool_->set_target_object(target_id);
    } else {
        // If no target provided, pick the first eligible path in document
        auto snap = document_.current_snapshot();
        if (snap) {
            for (const auto& [id, obj] : snap->objects().all()) {
                if (std::holds_alternative<document::PathObject>(obj)) {
                    const auto& po = std::get<document::PathObject>(obj);
                    if (po.authoring_source && !po.locked && po.visible) {
                        tool_->set_target_object(id);
                        break;
                    }
                }
            }
        }
    }
    is_active_ = true;
    return true;
}

void NodeToolController::deactivate() {
    is_active_ = false;
    tool_->cancel();
}

bool NodeToolController::pointer_down(geometry::Point2D world, bool shift, double tol_mm) {
    if (!is_active_) return false;
    return tool_->pointer_down(world, shift, tol_mm);
}

bool NodeToolController::pointer_move(geometry::Point2D world) {
    if (!is_active_) return false;
    return tool_->pointer_move(world);
}

bool NodeToolController::pointer_up(geometry::Point2D world) {
    if (!is_active_) return false;
    return tool_->pointer_up(world);
}

void NodeToolController::cancel() {
    tool_->cancel();
}

bool NodeToolController::delete_selected() {
    return tool_->delete_selected();
}

bool NodeToolController::add_node_at_midpoint() {
    return tool_->add_node_at_midpoint();
}

bool NodeToolController::break_selected() {
    return tool_->break_selected();
}

bool NodeToolController::reverse_selected() {
    return tool_->reverse_selected();
}

bool NodeToolController::set_selected_mode(document::PathNodeMode mode) {
    return tool_->set_selected_mode(mode);
}

bool NodeToolController::convert_selected_segment(document::PathSegmentKind kind) {
    return tool_->convert_selected_segment(kind);
}

bool NodeToolController::join_selected(bool bridge) {
    return tool_->join_selected(bridge);
}

bool NodeToolController::close_selected() {
    return tool_->close_selected();
}

bool NodeToolController::move_selected_delta(geometry::Point2D delta) {
    return tool_->move_selected_delta(delta);
}

bool NodeToolController::set_selected_node_position(geometry::Point2D new_pos) {
    return tool_->set_selected_node_position(new_pos);
}

size_t NodeToolController::selected_node_count() const noexcept {
    return tool_->selected_node_count();
}

std::optional<geometry::Point2D> NodeToolController::single_selected_node_position() const {
    return tool_->single_selected_node_position();
}

std::optional<document::PathNodeMode> NodeToolController::selected_nodes_mode() const {
    return tool_->selected_nodes_mode();
}

bool NodeToolController::is_marquee() const noexcept {
    return tool_->is_marquee();
}

geometry::Rect2D NodeToolController::marquee_rect() const noexcept {
    return tool_->marquee_rect();
}

std::vector<vector::tools::node::NodeHandleRef> NodeToolController::visible_handles() const {
    return tool_->visible_handles();
}

document::ObjectId NodeToolController::target_object() const noexcept {
    return tool_->target_object();
}

} // namespace pikdraw::desktop::tools
