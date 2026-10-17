#include "pikdraw/vector/tools/node/node_tool.hpp"
#include <algorithm>
#include <cmath>

namespace pikdraw::vector::tools::node {

void NodeTool::set_target_object(document::ObjectId object_id) {
    if (target_object_id_ != object_id) {
        target_object_id_ = object_id;
        clear_selection();
    }
}

const document::EditablePath* NodeTool::get_editable_path() const {
    if (target_object_id_ == document::kInvalidObjectId) return nullptr;
    auto snap = document_.current_snapshot();
    if (!snap) return nullptr;
    auto obj = snap->get_object(target_object_id_);
    if (!obj || !std::holds_alternative<document::PathObject>(*obj)) return nullptr;
    const auto& path_obj = std::get<document::PathObject>(*obj);
    if (!path_obj.authoring_source) return nullptr;
    return &*path_obj.authoring_source;
}

std::vector<NodeHandleRef> NodeTool::visible_handles() const {
    std::vector<NodeHandleRef> result;
    const auto* path = get_editable_path();
    if (!path) return result;

    for (const auto& [sub_id, node_id] : selected_nodes_) {
        for (const auto& sub : path->subpaths) {
            if (sub.id != sub_id) continue;
            for (const auto& node : sub.nodes) {
                if (node.id != node_id) continue;
                if (node.has_incoming_handle) {
                    result.push_back({sub.id, node.id, true, node.incoming_handle});
                }
                if (node.has_outgoing_handle) {
                    result.push_back({sub.id, node.id, false, node.outgoing_handle});
                }
            }
        }
    }
    return result;
}

std::optional<NodeHandleRef> NodeTool::hit_test_handle(geometry::Point2D world_pos, double tol) const {
    const auto handles = visible_handles();
    for (const auto& h : handles) {
        if (h.position.distance_to(world_pos) <= tol) {
            return h;
        }
    }
    return std::nullopt;
}

std::optional<std::pair<document::SubpathId, document::NodeId>> NodeTool::hit_test_node(
    geometry::Point2D world_pos, double tol) const {
    const auto* path = get_editable_path();
    if (!path) return std::nullopt;

    for (const auto& sub : path->subpaths) {
        for (const auto& node : sub.nodes) {
            if (node.anchor.distance_to(world_pos) <= tol) {
                return std::make_pair(sub.id, node.id);
            }
        }
    }
    return std::nullopt;
}

std::optional<std::pair<document::SubpathId, document::SegmentId>> NodeTool::hit_test_segment(
    geometry::Point2D world_pos, double tol, double* out_t) const {
    const auto* path = get_editable_path();
    if (!path) return std::nullopt;

    for (const auto& sub : path->subpaths) {
        const size_t seg_count = sub.segments.size();
        for (size_t i = 0; i < seg_count; ++i) {
            const size_t next_i = (i + 1) % sub.nodes.size();
            const auto p0 = sub.nodes[i].anchor;
            const auto p1 = sub.nodes[next_i].anchor;

            const auto v = p1 - p0;
            const double len_sq = v.x * v.x + v.y * v.y;
            if (len_sq < 1e-12) continue;

            const auto w = world_pos - p0;
            double t = (w.x * v.x + w.y * v.y) / len_sq;
            t = std::clamp(t, 0.0, 1.0);
            const auto proj = p0 + v * t;
            if (proj.distance_to(world_pos) <= tol) {
                if (out_t) *out_t = t;
                return std::make_pair(sub.id, sub.segments[i].id);
            }
        }
    }
    return std::nullopt;
}

bool NodeTool::pointer_down(geometry::Point2D world_pos, bool shift, double tol) {
    if (target_object_id_ == document::kInvalidObjectId) return false;

    // 1. Check handles of selected nodes first
    if (auto handle = hit_test_handle(world_pos, tol)) {
        active_handle_ = *handle;
        drag_mode_ = NodeDragMode::MoveHandle;
        drag_start_world_ = world_pos;
        current_drag_world_ = world_pos;
        initial_handle_pos_ = handle->position;
        return true;
    }

    // 2. Check nodes
    if (auto node_hit = hit_test_node(world_pos, tol)) {
        if (shift) {
            auto it = std::find(selected_nodes_.begin(), selected_nodes_.end(), *node_hit);
            if (it != selected_nodes_.end()) {
                selected_nodes_.erase(it);
            } else {
                selected_nodes_.push_back(*node_hit);
            }
        } else {
            if (!is_node_selected(node_hit->first, node_hit->second)) {
                selected_nodes_ = { *node_hit };
            }
        }
        drag_mode_ = NodeDragMode::MoveNode;
        drag_start_world_ = world_pos;
        current_drag_world_ = world_pos;
        return true;
    }

    // 3. Check segments
    if (auto seg_hit = hit_test_segment(world_pos, tol)) {
        selected_segment_ = *seg_hit;
        selected_nodes_.clear();
        drag_mode_ = NodeDragMode::None;
        return true;
    }

    // 4. Otherwise marquee selection
    if (!shift) {
        clear_selection();
    }
    drag_mode_ = NodeDragMode::Marquee;
    drag_start_world_ = world_pos;
    current_drag_world_ = world_pos;
    return true;
}

bool NodeTool::pointer_move(geometry::Point2D world_pos) {
    if (drag_mode_ == NodeDragMode::None) return false;
    current_drag_world_ = world_pos;

    if (drag_mode_ == NodeDragMode::MoveHandle && active_handle_) {
        const auto delta = world_pos - drag_start_world_;
        const auto new_pos = initial_handle_pos_ + delta;
        auto cmd = std::make_unique<document::MoveNodeHandleCommand>(
            target_object_id_, active_handle_->subpath_id, active_handle_->node_id,
            active_handle_->is_incoming, new_pos);
        if (document_.execute_command(std::move(cmd)).success) {
            drag_start_world_ = world_pos;
            initial_handle_pos_ = new_pos;
            return true;
        }
    } else if (drag_mode_ == NodeDragMode::MoveNode) {
        const auto delta = world_pos - drag_start_world_;
        if (delta.distance_to({0.0, 0.0}) > 1e-9) {
            if (selected_nodes_.size() == 1) {
                const auto* path = get_editable_path();
                if (path) {
                    for (const auto& sub : path->subpaths) {
                        if (sub.id != selected_nodes_[0].first) continue;
                        for (const auto& node : sub.nodes) {
                            if (node.id != selected_nodes_[0].second) continue;
                            auto cmd = std::make_unique<document::MovePathNodeCommand>(
                                target_object_id_, sub.id, node.id, node.anchor + delta);
                            if (document_.execute_command(std::move(cmd)).success) {
                                drag_start_world_ = world_pos;
                                return true;
                            }
                        }
                    }
                }
            } else if (selected_nodes_.size() > 1) {
                auto cmd = std::make_unique<document::BatchMovePathNodesCommand>(
                    target_object_id_, selected_nodes_, delta);
                if (document_.execute_command(std::move(cmd)).success) {
                    drag_start_world_ = world_pos;
                    return true;
                }
            }
        }
    } else if (drag_mode_ == NodeDragMode::Marquee) {
        const auto rect = marquee_rect();
        const auto* path = get_editable_path();
        if (path) {
            for (const auto& sub : path->subpaths) {
                for (const auto& node : sub.nodes) {
                    if (node.anchor.x >= rect.x && node.anchor.x <= rect.x + rect.width &&
                        node.anchor.y >= rect.y && node.anchor.y <= rect.y + rect.height) {
                        if (!is_node_selected(sub.id, node.id)) {
                            selected_nodes_.push_back({sub.id, node.id});
                        }
                    }
                }
            }
        }
        return true;
    }
    return false;
}

bool NodeTool::pointer_up(geometry::Point2D world_pos) {
    (void)world_pos;
    drag_mode_ = NodeDragMode::None;
    active_handle_.reset();
    return true;
}

void NodeTool::cancel() {
    drag_mode_ = NodeDragMode::None;
    active_handle_.reset();
}

bool NodeTool::delete_selected() {
    if (selected_nodes_.empty() || target_object_id_ == document::kInvalidObjectId) return false;
    bool any_success = false;
    for (const auto& [sub_id, node_id] : selected_nodes_) {
        auto cmd = std::make_unique<document::DeletePathNodeCommand>(
            target_object_id_, sub_id, node_id);
        if (document_.execute_command(std::move(cmd)).success) {
            any_success = true;
        }
    }
    clear_selection();
    return any_success;
}

bool NodeTool::add_node_at_midpoint() {
    if (target_object_id_ == document::kInvalidObjectId) return false;
    if (selected_segment_) {
        auto cmd = std::make_unique<document::SplitPathSegmentCommand>(
            target_object_id_, selected_segment_->first, selected_segment_->second, 0.5);
        return document_.execute_command(std::move(cmd)).success;
    }
    if (selected_nodes_.size() == 1) {
        const auto* path = get_editable_path();
        if (!path) return false;
        for (const auto& sub : path->subpaths) {
            if (sub.id != selected_nodes_[0].first) continue;
            for (size_t i = 0; i < sub.nodes.size(); ++i) {
                if (sub.nodes[i].id == selected_nodes_[0].second && i < sub.segments.size()) {
                    auto cmd = std::make_unique<document::SplitPathSegmentCommand>(
                        target_object_id_, sub.id, sub.segments[i].id, 0.5);
                    return document_.execute_command(std::move(cmd)).success;
                }
            }
        }
    }
    return false;
}

bool NodeTool::break_selected() {
    if (selected_nodes_.empty() || target_object_id_ == document::kInvalidObjectId) return false;
    bool any = false;
    for (const auto& [sub_id, node_id] : selected_nodes_) {
        auto cmd = std::make_unique<document::BreakPathAtNodeCommand>(
            target_object_id_, sub_id, node_id);
        if (document_.execute_command(std::move(cmd)).success) {
            any = true;
        }
    }
    return any;
}

bool NodeTool::reverse_selected() {
    if (selected_nodes_.empty() || target_object_id_ == document::kInvalidObjectId) return false;
    const auto sub_id = selected_nodes_[0].first;
    auto cmd = std::make_unique<document::ReverseSubpathCommand>(target_object_id_, sub_id);
    return document_.execute_command(std::move(cmd)).success;
}

bool NodeTool::set_selected_mode(document::PathNodeMode mode) {
    if (selected_nodes_.empty() || target_object_id_ == document::kInvalidObjectId) return false;
    bool any = false;
    for (const auto& [sub_id, node_id] : selected_nodes_) {
        auto cmd = std::make_unique<document::SetNodeModeCommand>(
            target_object_id_, sub_id, node_id, mode);
        if (document_.execute_command(std::move(cmd)).success) {
            any = true;
        }
    }
    return any;
}

bool NodeTool::convert_selected_segment(document::PathSegmentKind kind) {
    if (target_object_id_ == document::kInvalidObjectId) return false;
    if (selected_segment_) {
        auto cmd = std::make_unique<document::ConvertSegmentKindCommand>(
            target_object_id_, selected_segment_->first, selected_segment_->second, kind);
        return document_.execute_command(std::move(cmd)).success;
    }
    return false;
}

bool NodeTool::join_selected(bool bridge) {
    if (selected_nodes_.size() != 2 || target_object_id_ == document::kInvalidObjectId) return false;
    auto cmd = std::make_unique<document::JoinPathSubpathsCommand>(
        target_object_id_, selected_nodes_[0].first, selected_nodes_[0].second,
        selected_nodes_[1].first, selected_nodes_[1].second, bridge);
    return document_.execute_command(std::move(cmd)).success;
}

bool NodeTool::close_selected() {
    if (selected_nodes_.empty() || target_object_id_ == document::kInvalidObjectId) return false;
    const auto sub_id = selected_nodes_[0].first;
    auto cmd = std::make_unique<document::ClosePathSubpathCommand>(target_object_id_, sub_id);
    return document_.execute_command(std::move(cmd)).success;
}

bool NodeTool::move_selected_delta(geometry::Point2D delta) {
    if (selected_nodes_.empty() || target_object_id_ == document::kInvalidObjectId) return false;
    auto cmd = std::make_unique<document::BatchMovePathNodesCommand>(
        target_object_id_, selected_nodes_, delta);
    return document_.execute_command(std::move(cmd)).success;
}

bool NodeTool::set_selected_node_position(geometry::Point2D new_pos) {
    if (selected_nodes_.size() != 1 || target_object_id_ == document::kInvalidObjectId) return false;
    auto cmd = std::make_unique<document::MovePathNodeCommand>(
        target_object_id_, selected_nodes_[0].first, selected_nodes_[0].second, new_pos);
    return document_.execute_command(std::move(cmd)).success;
}

void NodeTool::select_all() {
    selected_nodes_.clear();
    const auto* path = get_editable_path();
    if (!path) return;
    for (const auto& sub : path->subpaths) {
        for (const auto& node : sub.nodes) {
            selected_nodes_.push_back({sub.id, node.id});
        }
    }
}

void NodeTool::clear_selection() {
    selected_nodes_.clear();
    selected_segment_.reset();
    active_handle_.reset();
}

bool NodeTool::is_node_selected(document::SubpathId sub_id, document::NodeId node_id) const noexcept {
    return std::find(selected_nodes_.begin(), selected_nodes_.end(),
                     std::make_pair(sub_id, node_id)) != selected_nodes_.end();
}

std::optional<geometry::Point2D> NodeTool::single_selected_node_position() const {
    if (selected_nodes_.size() != 1) return std::nullopt;
    const auto* path = get_editable_path();
    if (!path) return std::nullopt;
    for (const auto& sub : path->subpaths) {
        if (sub.id != selected_nodes_[0].first) continue;
        for (const auto& node : sub.nodes) {
            if (node.id == selected_nodes_[0].second) {
                return node.anchor;
            }
        }
    }
    return std::nullopt;
}

std::optional<document::PathNodeMode> NodeTool::selected_nodes_mode() const {
    if (selected_nodes_.empty()) return std::nullopt;
    const auto* path = get_editable_path();
    if (!path) return std::nullopt;

    std::optional<document::PathNodeMode> mode;
    for (const auto& [sub_id, node_id] : selected_nodes_) {
        for (const auto& sub : path->subpaths) {
            if (sub.id != sub_id) continue;
            for (const auto& node : sub.nodes) {
                if (node.id != node_id) continue;
                if (!mode) {
                    mode = node.mode;
                } else if (*mode != node.mode) {
                    return std::nullopt; // Mixed
                }
            }
        }
    }
    return mode;
}

geometry::Rect2D NodeTool::marquee_rect() const noexcept {
    const double min_x = std::min(drag_start_world_.x, current_drag_world_.x);
    const double min_y = std::min(drag_start_world_.y, current_drag_world_.y);
    const double max_x = std::max(drag_start_world_.x, current_drag_world_.x);
    const double max_y = std::max(drag_start_world_.y, current_drag_world_.y);
    return {min_x, min_y, max_x - min_x, max_y - min_y};
}

} // namespace pikdraw::vector::tools::node
