#include "pikdraw/vector/tools/node/node_tool.hpp"
#include "pikdraw/document/commands/path_commands.hpp"
#include "pikdraw/document/model/path_adapter.hpp"
#include <algorithm>
#include <cmath>

namespace pikdraw::vector::tools::node {

void NodeTool::set_target_object(document::ObjectId object_id) {
    if (target_object_id_ != object_id) {
        target_object_id_ = object_id;
        clear_selection();
        if (target_object_id_ != document::kInvalidObjectId) {
            auto snap = document_.current_snapshot();
            if (snap) {
                auto obj = snap->get_object(target_object_id_);
                if (obj && std::holds_alternative<document::PathObject>(*obj)) {
                    const auto& po = std::get<document::PathObject>(*obj);
                    if (po.contour_spec.starts_with("rect_corner:")) {
                        auto rcs = deserialize_rect_corner_spec(po.contour_spec);
                        if (rcs) {
                            corner_type_ = rcs->type;
                        }
                    }
                }
            }
        }
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

geometry::Transform2D NodeTool::target_transform() const {
    if (target_object_id_ == document::kInvalidObjectId) return geometry::Transform2D::identity();
    auto snap = document_.current_snapshot();
    if (!snap) return geometry::Transform2D::identity();
    auto obj = snap->get_object(target_object_id_);
    if (!obj || !std::holds_alternative<document::PathObject>(*obj)) return geometry::Transform2D::identity();
    const auto& path_obj = std::get<document::PathObject>(*obj);
    return path_obj.transform;
}

geometry::Transform2D NodeTool::target_inverse_transform() const {
    return target_transform().inverted().value_or(geometry::Transform2D::identity());
}

std::vector<NodeHandleRef> NodeTool::visible_handles() const {
    std::vector<NodeHandleRef> result;
    const auto* path = get_editable_path();
    if (!path) return result;

    const auto tr = target_transform();
    const auto delta = drag_delta();

    for (const auto& [sub_id, node_id] : selected_nodes_) {
        for (const auto& sub : path->subpaths) {
            if (sub.id != sub_id) continue;
            for (const auto& node : sub.nodes) {
                if (node.id != node_id) continue;
                const auto anchor_w = tr.map_point(node.anchor);
                const bool is_active_node = (drag_mode_ == NodeDragMode::MoveHandle && active_handle_ &&
                                             active_handle_->subpath_id == sub.id && active_handle_->node_id == node.id);

                if (node.has_incoming_handle) {
                    geometry::Point2D hpos_w = tr.map_point(node.incoming_handle);
                    if (is_active_node && active_handle_->is_incoming) {
                        hpos_w = initial_handle_pos_ + delta;
                    } else if (is_active_node && !active_handle_->is_incoming &&
                               (node.mode == document::PathNodeMode::Smooth || node.mode == document::PathNodeMode::Symmetric)) {
                        const auto active_h_w = initial_handle_pos_ + delta;
                        auto vec = active_h_w - anchor_w;
                        const double d_act = vec.distance_to({0.0, 0.0});
                        if (d_act > 1e-6) {
                            vec = vec / d_act;
                            double d_opp = tr.map_point(node.incoming_handle).distance_to(anchor_w);
                            if (node.mode == document::PathNodeMode::Symmetric) {
                                d_opp = d_act;
                            }
                            hpos_w = anchor_w - vec * d_opp;
                        }
                    } else if (drag_mode_ == NodeDragMode::MoveNode && has_dragged_) {
                        hpos_w = hpos_w + delta;
                    }
                    result.push_back({sub.id, node.id, true, hpos_w});
                }
                if (node.has_outgoing_handle) {
                    geometry::Point2D hpos_w = tr.map_point(node.outgoing_handle);
                    if (is_active_node && !active_handle_->is_incoming) {
                        hpos_w = initial_handle_pos_ + delta;
                    } else if (is_active_node && active_handle_->is_incoming &&
                               (node.mode == document::PathNodeMode::Smooth || node.mode == document::PathNodeMode::Symmetric)) {
                        const auto active_h_w = initial_handle_pos_ + delta;
                        auto vec = active_h_w - anchor_w;
                        const double d_act = vec.distance_to({0.0, 0.0});
                        if (d_act > 1e-6) {
                            vec = vec / d_act;
                            double d_opp = tr.map_point(node.outgoing_handle).distance_to(anchor_w);
                            if (node.mode == document::PathNodeMode::Symmetric) {
                                d_opp = d_act;
                            }
                            hpos_w = anchor_w - vec * d_opp;
                        }
                    } else if (drag_mode_ == NodeDragMode::MoveNode && has_dragged_) {
                        hpos_w = hpos_w + delta;
                    }
                    result.push_back({sub.id, node.id, false, hpos_w});
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

std::vector<CornerWidget> NodeTool::corner_widgets(double zoom) const {
    if (target_object_id_ != document::kInvalidObjectId) {
        auto snap = document_.current_snapshot();
        if (snap) {
            auto obj = snap->get_object(target_object_id_);
            if (obj && std::holds_alternative<document::PathObject>(*obj)) {
                const auto& po = std::get<document::PathObject>(*obj);
                if (po.contour_spec.starts_with("rect_corner:")) {
                    auto rcs = deserialize_rect_corner_spec(po.contour_spec);
                    if (rcs) {
                        const auto bounds = po.path.bounds();
                        if (bounds.is_finite() && bounds.width > 0 && bounds.height > 0) {
                            std::vector<document::NodeId> nids;
                            if (po.authoring_source && !po.authoring_source->subpaths.empty()) {
                                for (const auto& node : po.authoring_source->subpaths[0].nodes) {
                                    nids.push_back(node.id);
                                }
                            }
                            return detect_rectangle_corner_widgets(bounds, *rcs, zoom, target_transform(), nids);
                        }
                    }
                }
            }
        }
    }
    const auto* path = get_editable_path();
    if (!path) return {};
    return detect_corner_widgets(*path, zoom, target_transform());
}

std::optional<CornerWidget> NodeTool::hit_test_corner_widget(
    geometry::Point2D world_pos, double tol, double zoom) const {
    const auto widgets = corner_widgets(zoom);
    for (const auto& w : widgets) {
        if (w.widget_position.distance_to(world_pos) <= tol) {
            return w;
        }
    }
    return std::nullopt;
}

std::optional<document::EditablePath> NodeTool::preview_rounded_path() const {
    if (drag_mode_ != NodeDragMode::RoundCorner || !active_corner_widget_) {
        return std::nullopt;
    }
    if (target_object_id_ != document::kInvalidObjectId) {
        auto snap = document_.current_snapshot();
        if (snap) {
            auto obj = snap->get_object(target_object_id_);
            if (obj && std::holds_alternative<document::PathObject>(*obj)) {
                const auto& po = std::get<document::PathObject>(*obj);
                if (po.contour_spec.starts_with("rect_corner:")) {
                    auto rcs = deserialize_rect_corner_spec(po.contour_spec);
                    if (rcs) {
                        rcs->type = corner_type_;
                        if (rcs->locked || selected_nodes_.empty() || selected_nodes_.size() >= 4) {
                            rcs->tl = current_corner_radius_;
                            rcs->tr = current_corner_radius_;
                            rcs->br = current_corner_radius_;
                            rcs->bl = current_corner_radius_;
                        } else {
                            const auto v = active_corner_widget_->corner_anchor;
                            const auto bounds = po.path.bounds();
                            const auto cx = bounds.center().x;
                            const auto cy = bounds.center().y;
                            if (v.x < cx && v.y < cy) rcs->tl = current_corner_radius_;
                            else if (v.x >= cx && v.y < cy) rcs->tr = current_corner_radius_;
                            else if (v.x >= cx && v.y >= cy) rcs->br = current_corner_radius_;
                            else rcs->bl = current_corner_radius_;
                        }
                        const auto new_path = vector::ShapeFactory::generate_rectangle_path(po.path.bounds(), *rcs);
                        return document::editable_path_from_path2d(new_path, nullptr);
                    }
                }
            }
        }
    }
    if (!baseline_path_) return std::nullopt;
    std::vector<document::NodeId> target_ids;
    if (selected_nodes_.size() == 1 && selected_nodes_[0].second == active_corner_widget_->node_id) {
        target_ids.push_back(active_corner_widget_->node_id);
    } else if (selected_nodes_.size() > 1) {
        for (const auto& [sub_id, nid] : selected_nodes_) {
            target_ids.push_back(nid);
        }
    }
    return create_rounded_corners_path(*baseline_path_, target_ids, current_corner_radius_, target_transform(), corner_type_);
}

std::vector<FilletPreviewSegment> NodeTool::fillet_preview_segments() const {
    if (drag_mode_ != NodeDragMode::RoundCorner || !active_corner_widget_) {
        return {};
    }
    if (target_object_id_ != document::kInvalidObjectId) {
        auto snap = document_.current_snapshot();
        if (snap) {
            auto obj = snap->get_object(target_object_id_);
            if (obj && std::holds_alternative<document::PathObject>(*obj)) {
                const auto& po = std::get<document::PathObject>(*obj);
                if (po.contour_spec.starts_with("rect_corner:")) {
                    auto rcs = deserialize_rect_corner_spec(po.contour_spec);
                    if (rcs) {
                        const auto bounds = po.path.bounds();
                        return generate_rectangle_fillet_preview_segments(
                            bounds, *rcs, current_corner_radius_, target_transform(), corner_type_,
                            active_corner_widget_->corner_anchor);
                    }
                }
            }
        }
    }
    std::vector<document::NodeId> target_ids;
    if (selected_nodes_.size() == 1 && selected_nodes_[0].second == active_corner_widget_->node_id) {
        target_ids.push_back(active_corner_widget_->node_id);
    } else if (selected_nodes_.size() > 1) {
        for (const auto& [sub_id, nid] : selected_nodes_) {
            target_ids.push_back(nid);
        }
    }
    if (!baseline_path_) return {};
    return generate_fillet_preview_segments(*baseline_path_, target_ids, current_corner_radius_, target_transform(), corner_type_);
}

std::pair<geometry::Point2D, geometry::Point2D> NodeTool::current_segment_bend_handles() const {
    if (!active_segment_drag_ || drag_mode_ != NodeDragMode::BendSegment) {
        return {geometry::Point2D{0.0, 0.0}, geometry::Point2D{0.0, 0.0}};
    }
    const auto delta = drag_delta();
    const double t = std::clamp(active_segment_drag_->click_t, 0.15, 0.85);
    const double one_minus_t = 1.0 - t;
    const double denom = 3.0 * t * one_minus_t * (one_minus_t * one_minus_t + t * t);
    double w1 = 1.3333333333333333;
    double w2 = 1.3333333333333333;
    if (std::abs(denom) > 1e-6) {
        w1 = one_minus_t / denom;
        w2 = t / denom;
    }
    const auto c1 = active_segment_drag_->original_c1_world + delta * w1;
    const auto c2 = active_segment_drag_->original_c2_world + delta * w2;
    return {c1, c2};
}

std::optional<std::pair<document::SubpathId, document::NodeId>> NodeTool::hit_test_node(
    geometry::Point2D world_pos, double tol) const {
    const auto* path = get_editable_path();
    if (!path) return std::nullopt;
    const auto tr = target_transform();

    for (const auto& sub : path->subpaths) {
        for (const auto& node : sub.nodes) {
            const auto anchor_w = tr.map_point(node.anchor);
            if (anchor_w.distance_to(world_pos) <= tol) {
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
    const auto tr = target_transform();

    for (const auto& sub : path->subpaths) {
        const size_t seg_count = sub.segments.size();
        for (size_t i = 0; i < seg_count; ++i) {
            const size_t next_i = (i + 1) % sub.nodes.size();
            const auto p0 = tr.map_point(sub.nodes[i].anchor);
            const auto p1 = tr.map_point(sub.nodes[next_i].anchor);

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

bool NodeTool::pointer_down(geometry::Point2D world_pos, bool shift, double tol, double zoom) {
    if (target_object_id_ == document::kInvalidObjectId) return false;
    multi_select_click_ = shift;

    // 1. Check handles of selected nodes first
    if (auto handle = hit_test_handle(world_pos, tol)) {
        active_handle_ = *handle;
        drag_mode_ = NodeDragMode::MoveHandle;
        drag_start_world_ = world_pos;
        current_drag_world_ = world_pos;
        initial_handle_pos_ = handle->position;
        const auto* ep = get_editable_path();
        if (ep) {
            for (const auto& sub : ep->subpaths) {
                if (sub.id != handle->subpath_id) continue;
                for (const auto& node : sub.nodes) {
                    if (node.id != handle->node_id) continue;
                    initial_handle_local_pos_ = handle->is_incoming ? node.incoming_handle : node.outgoing_handle;
                }
            }
        }
        has_dragged_ = false;
        return true;
    }

    // 2. Check Live Corner Rounding widgets
    if (auto corner = hit_test_corner_widget(world_pos, tol, zoom)) {
        active_corner_widget_ = *corner;
        drag_mode_ = NodeDragMode::RoundCorner;
        drag_start_world_ = world_pos;
        current_drag_world_ = world_pos;
        current_corner_radius_ = corner->current_radius;
        has_dragged_ = false;
        const auto* ep = get_editable_path();
        if (ep) baseline_path_ = *ep;
        return true;
    }

    // 3. Check nodes
    if (auto node_hit = hit_test_node(world_pos, tol)) {
        selected_segment_.reset();
        clicked_node_ = *node_hit;
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

        initial_node_anchors_.clear();
        const auto* path = get_editable_path();
        if (path) {
            for (const auto& sel : selected_nodes_) {
                for (const auto& sub : path->subpaths) {
                    if (sub.id != sel.first) continue;
                    for (const auto& node : sub.nodes) {
                        if (node.id == sel.second) {
                            initial_node_anchors_.push_back({sel, node.anchor});
                        }
                    }
                }
            }
        }

        drag_mode_ = NodeDragMode::MoveNode;
        drag_start_world_ = world_pos;
        current_drag_world_ = world_pos;
        has_dragged_ = false;
        return true;
    }

    // 4. Check segments
    double hit_t = 0.5;
    if (auto seg_hit = hit_test_segment(world_pos, tol, &hit_t)) {
        selected_segment_ = *seg_hit;
        selected_nodes_.clear();
        drag_mode_ = NodeDragMode::BendSegment;
        drag_start_world_ = world_pos;
        current_drag_world_ = world_pos;
        has_dragged_ = false;

        const auto* path = get_editable_path();
        if (path) {
            const auto tr = target_transform();
            for (const auto& sub : path->subpaths) {
                if (sub.id != seg_hit->first) continue;
                const size_t seg_count = sub.segments.size();
                for (size_t i = 0; i < seg_count; ++i) {
                    if (sub.segments[i].id != seg_hit->second) continue;
                    const size_t next_i = (i + 1) % sub.nodes.size();
                    ActiveSegmentDrag asd;
                    asd.subpath_id = sub.id;
                    asd.segment_id = sub.segments[i].id;
                    asd.start_node_index = i;
                    asd.end_node_index = next_i;
                    asd.start_node_id = sub.nodes[i].id;
                    asd.end_node_id = sub.nodes[next_i].id;
                    asd.p0_world = tr.map_point(sub.nodes[i].anchor);
                    asd.p3_world = tr.map_point(sub.nodes[next_i].anchor);
                    asd.click_t = std::clamp(hit_t, 0.15, 0.85);

                    if (sub.segments[i].kind == document::PathSegmentKind::CubicBezier) {
                        asd.was_cubic = true;
                        asd.original_c1_world = tr.map_point(sub.nodes[i].has_outgoing_handle ? sub.nodes[i].outgoing_handle : sub.nodes[i].anchor);
                        asd.original_c2_world = tr.map_point(sub.nodes[next_i].has_incoming_handle ? sub.nodes[next_i].incoming_handle : sub.nodes[next_i].anchor);
                    } else {
                        asd.was_cubic = false;
                        asd.original_c1_world = asd.p0_world + (asd.p3_world - asd.p0_world) * (1.0 / 3.0);
                        asd.original_c2_world = asd.p0_world + (asd.p3_world - asd.p0_world) * (2.0 / 3.0);
                    }
                    active_segment_drag_ = asd;
                    break;
                }
            }
        }
        return true;
    }

    // 5. Otherwise marquee selection
    if (!shift) {
        clear_selection();
    }
    drag_mode_ = NodeDragMode::Marquee;
    drag_start_world_ = world_pos;
    current_drag_world_ = world_pos;
    has_dragged_ = false;
    return true;
}

bool NodeTool::pointer_move(geometry::Point2D world_pos) {
    if (drag_mode_ == NodeDragMode::None) return false;
    current_drag_world_ = world_pos;
    has_dragged_ = true;

    if (drag_mode_ == NodeDragMode::MoveHandle && active_handle_) {
        return true;
    } else if (drag_mode_ == NodeDragMode::MoveNode) {
        return true;
    } else if (drag_mode_ == NodeDragMode::BendSegment && active_segment_drag_) {
        return true;
    } else if (drag_mode_ == NodeDragMode::RoundCorner && active_corner_widget_) {
        const auto delta = world_pos - drag_start_world_;
        const double proj = delta.x * active_corner_widget_->bisector.x + delta.y * active_corner_widget_->bisector.y;
        current_corner_radius_ = std::clamp(active_corner_widget_->current_radius + proj, 0.0, active_corner_widget_->max_radius);
        return true;
    } else if (drag_mode_ == NodeDragMode::Marquee) {
        const auto rect = marquee_rect();
        const auto* path = get_editable_path();
        const auto tr = target_transform();
        if (path) {
            for (const auto& sub : path->subpaths) {
                for (const auto& node : sub.nodes) {
                    const auto pt_w = tr.map_point(node.anchor);
                    if (pt_w.x >= rect.x && pt_w.x <= rect.x + rect.width &&
                        pt_w.y >= rect.y && pt_w.y <= rect.y + rect.height) {
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
    current_drag_world_ = world_pos;
    const auto mode = drag_mode_;
    const bool dragged = has_dragged_;

    if (mode == NodeDragMode::MoveHandle && dragged && active_handle_) {
        const auto delta_w = world_pos - drag_start_world_;
        if (delta_w.distance_to({0.0, 0.0}) > 1e-4) {
            const auto inv_tr = target_inverse_transform();
            const auto delta_local = inv_tr.map_vector(delta_w);
            const auto new_pos = initial_handle_local_pos_ + delta_local;
            auto cmd = std::make_unique<document::MoveNodeHandleCommand>(
                target_object_id_, active_handle_->subpath_id, active_handle_->node_id,
                active_handle_->is_incoming, new_pos);
            (void)document_.execute_command(std::move(cmd));
        }
    } else if (mode == NodeDragMode::MoveNode && dragged) {
        const auto delta_w = world_pos - drag_start_world_;
        if (delta_w.distance_to({0.0, 0.0}) > 1e-4) {
            const auto inv_tr = target_inverse_transform();
            const auto delta_local = inv_tr.map_vector(delta_w);
            if (selected_nodes_.size() == 1 && !initial_node_anchors_.empty()) {
                auto cmd = std::make_unique<document::MovePathNodeCommand>(
                    target_object_id_, initial_node_anchors_[0].first.first,
                    initial_node_anchors_[0].first.second,
                    initial_node_anchors_[0].second + delta_local);
                (void)document_.execute_command(std::move(cmd));
            } else if (selected_nodes_.size() > 1) {
                auto cmd = std::make_unique<document::BatchMovePathNodesCommand>(
                    target_object_id_, selected_nodes_, delta_local);
                (void)document_.execute_command(std::move(cmd));
            }
        }
    } else if (mode == NodeDragMode::MoveNode && !dragged && clicked_node_ && !multi_select_click_) {
        selected_nodes_ = { *clicked_node_ };
    } else if (mode == NodeDragMode::RoundCorner && dragged && active_corner_widget_) {
        bool committed = false;
        if (target_object_id_ != document::kInvalidObjectId) {
            auto snap = document_.current_snapshot();
            if (snap) {
                auto obj = snap->get_object(target_object_id_);
                if (obj && std::holds_alternative<document::PathObject>(*obj)) {
                    const auto& po = std::get<document::PathObject>(*obj);
                    if (po.contour_spec.starts_with("rect_corner:")) {
                        auto rcs = deserialize_rect_corner_spec(po.contour_spec);
                        if (rcs) {
                            rcs->type = corner_type_;
                            if (rcs->locked || selected_nodes_.empty() || selected_nodes_.size() >= 4) {
                                rcs->tl = current_corner_radius_;
                                rcs->tr = current_corner_radius_;
                                rcs->br = current_corner_radius_;
                                rcs->bl = current_corner_radius_;
                            } else {
                                const auto v = active_corner_widget_->corner_anchor;
                                const auto bounds = po.path.bounds();
                                const auto cx = bounds.center().x;
                                const auto cy = bounds.center().y;
                                if (v.x < cx && v.y < cy) rcs->tl = current_corner_radius_;
                                else if (v.x >= cx && v.y < cy) rcs->tr = current_corner_radius_;
                                else if (v.x >= cx && v.y >= cy) rcs->br = current_corner_radius_;
                                else rcs->bl = current_corner_radius_;
                            }
                            auto new_spec = serialize_rect_corner_spec(*rcs);
                            const auto new_path = vector::ShapeFactory::generate_rectangle_path(po.path.bounds(), *rcs);
                            auto ep = document::editable_path_from_path2d(new_path, nullptr);
                            if (ep) {
                                auto cmd = std::make_unique<document::SetAuthoringSourceCommand>(
                                    target_object_id_, std::move(*ep), std::move(new_spec));
                                (void)document_.execute_command(std::move(cmd));
                                committed = true;
                            }
                        }
                    }
                }
            }
        }
        if (!committed && baseline_path_ && current_corner_radius_ > 0.05) {
            std::vector<document::NodeId> target_ids;
            if (selected_nodes_.size() == 1 && selected_nodes_[0].second == active_corner_widget_->node_id) {
                target_ids.push_back(active_corner_widget_->node_id);
            } else if (selected_nodes_.size() > 1) {
                for (const auto& [sub_id, nid] : selected_nodes_) {
                    target_ids.push_back(nid);
                }
            }
            auto rounded = create_rounded_corners_path(*baseline_path_, target_ids, current_corner_radius_, target_transform(), corner_type_);
            auto cmd = std::make_unique<document::SetAuthoringSourceCommand>(
                target_object_id_, std::move(rounded), std::nullopt);
            (void)document_.execute_command(std::move(cmd));
        }
    } else if (mode == NodeDragMode::BendSegment && active_segment_drag_) {
        const auto delta_w = world_pos - drag_start_world_;
        if (dragged && delta_w.distance_to({0.0, 0.0}) > 1e-4) {
            const auto [c1_w, c2_w] = current_segment_bend_handles();
            const auto inv_tr = target_inverse_transform();
            const auto c1_local = inv_tr.map_point(c1_w);
            const auto c2_local = inv_tr.map_point(c2_w);
            auto cmd = std::make_unique<document::BendSegmentCommand>(
                target_object_id_, active_segment_drag_->subpath_id, active_segment_drag_->segment_id,
                c1_local, c2_local);
            (void)document_.execute_command(std::move(cmd));
            selected_nodes_ = {
                {active_segment_drag_->subpath_id, active_segment_drag_->start_node_id},
                {active_segment_drag_->subpath_id, active_segment_drag_->end_node_id}
            };
            selected_segment_.reset();
        } else if (!dragged) {
            selected_segment_ = std::make_pair(active_segment_drag_->subpath_id, active_segment_drag_->segment_id);
            selected_nodes_.clear();
        }
    }

    drag_mode_ = NodeDragMode::None;
    has_dragged_ = false;
    active_handle_.reset();
    active_corner_widget_.reset();
    baseline_path_.reset();
    current_corner_radius_ = 0.0;
    initial_node_anchors_.clear();
    clicked_node_.reset();
    multi_select_click_ = false;
    active_segment_drag_.reset();
    return true;
}

void NodeTool::cancel() {
    drag_mode_ = NodeDragMode::None;
    has_dragged_ = false;
    active_handle_.reset();
    active_corner_widget_.reset();
    baseline_path_.reset();
    current_corner_radius_ = 0.0;
    initial_node_anchors_.clear();
    clicked_node_.reset();
    multi_select_click_ = false;
    active_segment_drag_.reset();
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
    if (!selected_nodes_.empty()) {
        const auto* path = get_editable_path();
        if (!path) return false;
        bool any = false;
        std::vector<std::pair<document::SubpathId, document::SegmentId>> segments_to_convert;
        for (const auto& [sub_id, node_id] : selected_nodes_) {
            for (const auto& sub : path->subpaths) {
                if (sub.id != sub_id) continue;
                const size_t n = sub.nodes.size();
                for (size_t i = 0; i < n; ++i) {
                    if (sub.nodes[i].id == node_id) {
                        if (i < sub.segments.size()) {
                            segments_to_convert.push_back({sub.id, sub.segments[i].id});
                        }
                        if (i > 0 && i - 1 < sub.segments.size()) {
                            segments_to_convert.push_back({sub.id, sub.segments[i - 1].id});
                        } else if (i == 0 && sub.closed && !sub.segments.empty()) {
                            segments_to_convert.push_back({sub.id, sub.segments.back().id});
                        }
                    }
                }
            }
        }
        auto cmp = [](const auto& a, const auto& b) {
            if (a.first.value() != b.first.value()) return a.first.value() < b.first.value();
            return a.second.value() < b.second.value();
        };
        auto eq = [](const auto& a, const auto& b) {
            return a.first == b.first && a.second == b.second;
        };
        std::sort(segments_to_convert.begin(), segments_to_convert.end(), cmp);
        segments_to_convert.erase(std::unique(segments_to_convert.begin(), segments_to_convert.end(), eq), segments_to_convert.end());
        for (const auto& [s_id, seg_id] : segments_to_convert) {
            auto cmd = std::make_unique<document::ConvertSegmentKindCommand>(
                target_object_id_, s_id, seg_id, kind);
            if (document_.execute_command(std::move(cmd)).success) {
                any = true;
            }
        }
        return any;
    }
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
    const auto delta_local = target_inverse_transform().map_vector(delta);
    auto cmd = std::make_unique<document::BatchMovePathNodesCommand>(
        target_object_id_, selected_nodes_, delta_local);
    return document_.execute_command(std::move(cmd)).success;
}

bool NodeTool::set_selected_node_position(geometry::Point2D new_pos) {
    if (selected_nodes_.size() != 1 || target_object_id_ == document::kInvalidObjectId) return false;
    const auto new_pos_local = target_inverse_transform().map_point(new_pos);
    auto cmd = std::make_unique<document::MovePathNodeCommand>(
        target_object_id_, selected_nodes_[0].first, selected_nodes_[0].second, new_pos_local);
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
    const auto tr = target_transform();
    if (drag_mode_ == NodeDragMode::MoveNode && has_dragged_ && !initial_node_anchors_.empty()) {
        const auto delta = current_drag_world_ - drag_start_world_;
        return tr.map_point(initial_node_anchors_[0].second) + delta;
    }
    const auto* path = get_editable_path();
    if (!path) return std::nullopt;
    for (const auto& sub : path->subpaths) {
        if (sub.id != selected_nodes_[0].first) continue;
        for (const auto& node : sub.nodes) {
            if (node.id == selected_nodes_[0].second) {
                return tr.map_point(node.anchor);
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
