#include "pikdraw/document/commands/node_commands.hpp"
#include "path_command_support.hpp"
#include <algorithm>
#include <cmath>

namespace pikdraw::document {

MoveNodeHandleCommand::MoveNodeHandleCommand(
    ObjectId object_id, SubpathId subpath_id, NodeId node_id,
    bool is_incoming, geometry::Point2D new_handle_pos,
    std::shared_ptr<PathTopologyMapping> mapping_result) noexcept
    : PathTopologyCommand(object_id, std::move(mapping_result)),
      subpath_id_(subpath_id), node_id_(node_id),
      is_incoming_(is_incoming), new_handle_pos_(new_handle_pos) {}

bool MoveNodeHandleCommand::apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                                       std::string& error) {
    auto* subpath = detail::find_subpath(path, subpath_id_);
    if (!subpath) {
        error = "Subpath ID was not found";
        return false;
    }
    auto it = std::find_if(subpath->nodes.begin(), subpath->nodes.end(),
        [&](const PathNode& n) { return n.id == node_id_; });
    if (it == subpath->nodes.end()) {
        error = "Node ID was not found";
        return false;
    }
    PathNode& node = *it;
    if (node.mode == PathNodeMode::Corner || node.mode == PathNodeMode::AutoSmooth) {
        node.mode = PathNodeMode::Cusp;
    }

    if (is_incoming_) {
        node.has_incoming_handle = true;
        node.incoming_handle = new_handle_pos_;
        const auto v_in = new_handle_pos_ - node.anchor;
        const double len_in = v_in.distance_to({0.0, 0.0});
        if (node.mode == PathNodeMode::Smooth && len_in > 1e-12 && node.has_outgoing_handle) {
            const double len_out = (node.outgoing_handle - node.anchor).distance_to({0.0, 0.0});
            node.outgoing_handle = node.anchor - (v_in / len_in) * len_out;
        } else if (node.mode == PathNodeMode::Symmetric) {
            node.has_outgoing_handle = true;
            node.outgoing_handle = node.anchor - v_in;
        }
    } else {
        node.has_outgoing_handle = true;
        node.outgoing_handle = new_handle_pos_;
        const auto v_out = new_handle_pos_ - node.anchor;
        const double len_out = v_out.distance_to({0.0, 0.0});
        if (node.mode == PathNodeMode::Smooth && len_out > 1e-12 && node.has_incoming_handle) {
            const double len_in = (node.incoming_handle - node.anchor).distance_to({0.0, 0.0});
            node.incoming_handle = node.anchor - (v_out / len_out) * len_in;
        } else if (node.mode == PathNodeMode::Symmetric) {
            node.has_incoming_handle = true;
            node.incoming_handle = node.anchor - v_out;
        }
    }

    detail::add_mapping(mapping, PathElementKind::Node, node_id_.value(), node_id_.value(),
                        IdMappingRelation::Preserved);
    return true;
}

SetNodeModeCommand::SetNodeModeCommand(
    ObjectId object_id, SubpathId subpath_id, NodeId node_id,
    PathNodeMode new_mode,
    std::shared_ptr<PathTopologyMapping> mapping_result) noexcept
    : PathTopologyCommand(object_id, std::move(mapping_result)),
      subpath_id_(subpath_id), node_id_(node_id), new_mode_(new_mode) {}

bool SetNodeModeCommand::apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                                    std::string& error) {
    auto* subpath = detail::find_subpath(path, subpath_id_);
    if (!subpath) {
        error = "Subpath ID was not found";
        return false;
    }
    auto it = std::find_if(subpath->nodes.begin(), subpath->nodes.end(),
        [&](const PathNode& n) { return n.id == node_id_; });
    if (it == subpath->nodes.end()) {
        error = "Node ID was not found";
        return false;
    }
    PathNode& node = *it;
    if (new_mode_ == PathNodeMode::Corner) {
        node.mode = PathNodeMode::Corner;
        node.has_incoming_handle = false;
        node.has_outgoing_handle = false;
    } else if (new_mode_ == PathNodeMode::Cusp) {
        node.mode = PathNodeMode::Cusp;
    } else if (new_mode_ == PathNodeMode::Smooth || new_mode_ == PathNodeMode::Symmetric) {
        node.mode = new_mode_;
        const size_t idx = static_cast<size_t>(it - subpath->nodes.begin());
        const size_t n = subpath->nodes.size();
        if (!node.has_incoming_handle && !node.has_outgoing_handle) {
            geometry::Point2D prev_anchor = (idx > 0) ? subpath->nodes[idx - 1].anchor
                : (subpath->closed && n > 1 ? subpath->nodes[n - 1].anchor : node.anchor);
            geometry::Point2D next_anchor = (idx + 1 < n) ? subpath->nodes[idx + 1].anchor
                : (subpath->closed && n > 1 ? subpath->nodes[0].anchor : node.anchor);
            geometry::Point2D tangent = next_anchor - prev_anchor;
            double tlen = tangent.distance_to({0.0, 0.0});
            if (tlen < 1e-12) tangent = {1.0, 0.0};
            else tangent = tangent * (1.0 / tlen);
            double handle_len = std::min(node.anchor.distance_to(prev_anchor),
                                         node.anchor.distance_to(next_anchor)) * 0.33;
            if (handle_len < 1e-4) handle_len = 5.0;
            node.has_incoming_handle = true;
            node.incoming_handle = node.anchor - tangent * handle_len;
            node.has_outgoing_handle = true;
            node.outgoing_handle = node.anchor + tangent * handle_len;
        } else if (node.has_incoming_handle && !node.has_outgoing_handle) {
            auto vin = node.incoming_handle - node.anchor;
            node.has_outgoing_handle = true;
            node.outgoing_handle = node.anchor - vin;
        } else if (!node.has_incoming_handle && node.has_outgoing_handle) {
            auto vout = node.outgoing_handle - node.anchor;
            node.has_incoming_handle = true;
            node.incoming_handle = node.anchor - vout;
        } else {
            auto vin = node.incoming_handle - node.anchor;
            auto vout = node.outgoing_handle - node.anchor;
            double lin = vin.distance_to({0.0, 0.0});
            double lout = vout.distance_to({0.0, 0.0});
            if (new_mode_ == PathNodeMode::Symmetric) {
                double lavg = (lin + lout) * 0.5;
                if (lavg < 1e-6) lavg = 5.0;
                if (lin > 1e-12) {
                    node.incoming_handle = node.anchor + (vin * (1.0 / lin)) * lavg;
                    node.outgoing_handle = node.anchor - (vin * (1.0 / lin)) * lavg;
                } else if (lout > 1e-12) {
                    node.outgoing_handle = node.anchor + (vout * (1.0 / lout)) * lavg;
                    node.incoming_handle = node.anchor - (vout * (1.0 / lout)) * lavg;
                }
            } else {
                if (lin > 1e-12 && lout > 1e-12) {
                    node.outgoing_handle = node.anchor - (vin * (1.0 / lin)) * lout;
                }
            }
        }
    }

    detail::add_mapping(mapping, PathElementKind::Node, node_id_.value(), node_id_.value(),
                        IdMappingRelation::Preserved);
    return true;
}

BreakPathAtNodeCommand::BreakPathAtNodeCommand(
    ObjectId object_id, SubpathId subpath_id, NodeId node_id,
    std::shared_ptr<PathTopologyMapping> mapping_result) noexcept
    : PathTopologyCommand(object_id, std::move(mapping_result)),
      subpath_id_(subpath_id), node_id_(node_id) {}

bool BreakPathAtNodeCommand::apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                                        std::string& error) {
    auto* subpath = detail::find_subpath(path, subpath_id_);
    if (!subpath) {
        error = "Subpath ID was not found";
        return false;
    }
    auto it = std::find_if(subpath->nodes.begin(), subpath->nodes.end(),
        [&](const PathNode& n) { return n.id == node_id_; });
    if (it == subpath->nodes.end()) {
        error = "Node ID was not found";
        return false;
    }
    const size_t index = static_cast<size_t>(it - subpath->nodes.begin());
    const size_t n = subpath->nodes.size();

    if (subpath->closed) {
        // Open the closed subpath at this node: rotate so node_id becomes index 0.
        std::rotate(subpath->nodes.begin(), it, subpath->nodes.end());
        std::rotate(subpath->segments.begin(),
                    subpath->segments.begin() + static_cast<std::ptrdiff_t>(index),
                    subpath->segments.end());
        if (!subpath->segments.empty()) {
            detail::add_mapping(mapping, PathElementKind::Segment,
                                subpath->segments.back().id.value(), std::nullopt,
                                IdMappingRelation::Removed);
            subpath->segments.pop_back();
        }
        subpath->closed = false;
        detail::add_mapping(mapping, PathElementKind::Node, node_id_.value(), node_id_.value(),
                            IdMappingRelation::Preserved);
        return true;
    } else {
        if (index == 0 || index + 1 >= n) {
            error = "Cannot break at an endpoint of an already open path";
            return false;
        }
        EditableSubpath second;
        second.id = generate_path_element_id<SubpathId>();
        second.closed = false;

        PathNode split_node = subpath->nodes[index];
        split_node.id = generate_path_element_id<NodeId>();
        second.nodes.push_back(split_node);
        for (size_t i = index + 1; i < n; ++i) {
            second.nodes.push_back(subpath->nodes[i]);
        }
        for (size_t i = index; i < subpath->segments.size(); ++i) {
            second.segments.push_back(subpath->segments[i]);
        }

        subpath->nodes.erase(subpath->nodes.begin() + static_cast<std::ptrdiff_t>(index + 1),
                             subpath->nodes.end());
        subpath->segments.erase(subpath->segments.begin() + static_cast<std::ptrdiff_t>(index),
                                subpath->segments.end());
        path.subpaths.push_back(std::move(second));

        detail::add_mapping(mapping, PathElementKind::Subpath, std::nullopt,
                            path.subpaths.back().id.value(), IdMappingRelation::Created);
        detail::add_mapping(mapping, PathElementKind::Node, std::nullopt,
                            split_node.id.value(), IdMappingRelation::Created);
        return true;
    }
}

ReverseSubpathCommand::ReverseSubpathCommand(
    ObjectId object_id, SubpathId subpath_id,
    std::shared_ptr<PathTopologyMapping> mapping_result) noexcept
    : PathTopologyCommand(object_id, std::move(mapping_result)), subpath_id_(subpath_id) {}

bool ReverseSubpathCommand::apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                                       std::string& error) {
    auto* subpath = detail::find_subpath(path, subpath_id_);
    if (!subpath) {
        error = "Subpath ID was not found";
        return false;
    }
    std::reverse(subpath->nodes.begin(), subpath->nodes.end());
    std::reverse(subpath->segments.begin(), subpath->segments.end());
    for (auto& node : subpath->nodes) {
        std::swap(node.has_incoming_handle, node.has_outgoing_handle);
        std::swap(node.incoming_handle, node.outgoing_handle);
    }
    for (auto& segment : subpath->segments) {
        if (segment.kind == PathSegmentKind::EllipticArc) {
            segment.arc.start_angle_rad += segment.arc.sweep_angle_rad;
            segment.arc.sweep_angle_rad = -segment.arc.sweep_angle_rad;
        }
    }
    detail::add_mapping(mapping, PathElementKind::Subpath, subpath_id_.value(),
                        subpath_id_.value(), IdMappingRelation::Preserved);
    return true;
}

ConvertSegmentKindCommand::ConvertSegmentKindCommand(
    ObjectId object_id, SubpathId subpath_id, SegmentId segment_id,
    PathSegmentKind new_kind,
    std::shared_ptr<PathTopologyMapping> mapping_result) noexcept
    : PathTopologyCommand(object_id, std::move(mapping_result)),
      subpath_id_(subpath_id), segment_id_(segment_id), new_kind_(new_kind) {}

bool ConvertSegmentKindCommand::apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                                           std::string& error) {
    auto* subpath = detail::find_subpath(path, subpath_id_);
    if (!subpath) {
        error = "Subpath ID was not found";
        return false;
    }
    auto it = std::find_if(subpath->segments.begin(), subpath->segments.end(),
        [&](const EditableSegment& s) { return s.id == segment_id_; });
    if (it == subpath->segments.end()) {
        error = "Segment ID was not found";
        return false;
    }
    const size_t idx = static_cast<size_t>(it - subpath->segments.begin());
    const size_t n = subpath->nodes.size();
    const size_t next_idx = (idx + 1) % n;

    if (new_kind_ == PathSegmentKind::CubicBezier) {
        it->kind = PathSegmentKind::CubicBezier;
        const auto p0 = subpath->nodes[idx].anchor;
        const auto p1 = subpath->nodes[next_idx].anchor;
        subpath->nodes[idx].outgoing_handle = p0 + (p1 - p0) * (1.0 / 3.0);
        subpath->nodes[idx].has_outgoing_handle = true;
        if (subpath->nodes[idx].mode == PathNodeMode::Corner ||
            subpath->nodes[idx].mode == PathNodeMode::AutoSmooth) {
            subpath->nodes[idx].mode = PathNodeMode::Cusp;
        }
        subpath->nodes[next_idx].incoming_handle = p0 + (p1 - p0) * (2.0 / 3.0);
        subpath->nodes[next_idx].has_incoming_handle = true;
        if (subpath->nodes[next_idx].mode == PathNodeMode::Corner ||
            subpath->nodes[next_idx].mode == PathNodeMode::AutoSmooth) {
            subpath->nodes[next_idx].mode = PathNodeMode::Cusp;
        }
    } else if (new_kind_ == PathSegmentKind::Line) {
        it->kind = PathSegmentKind::Line;
        subpath->nodes[idx].has_outgoing_handle = false;
        subpath->nodes[next_idx].has_incoming_handle = false;
        if (!subpath->nodes[idx].has_incoming_handle) {
            subpath->nodes[idx].mode = PathNodeMode::Corner;
        }
        if (!subpath->nodes[next_idx].has_outgoing_handle) {
            subpath->nodes[next_idx].mode = PathNodeMode::Corner;
        }
    }

    detail::add_mapping(mapping, PathElementKind::Segment, segment_id_.value(),
                        segment_id_.value(), IdMappingRelation::Preserved);
    return true;
}

BatchMovePathNodesCommand::BatchMovePathNodesCommand(
    ObjectId object_id,
    std::vector<std::pair<SubpathId, NodeId>> target_nodes,
    geometry::Point2D delta,
    std::shared_ptr<PathTopologyMapping> mapping_result) noexcept
    : PathTopologyCommand(object_id, std::move(mapping_result)),
      target_nodes_(std::move(target_nodes)), delta_(delta) {}

bool BatchMovePathNodesCommand::apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                                           std::string& error) {
    (void)error;
    if (target_nodes_.empty()) return true;
    for (const auto& [sub_id, node_id] : target_nodes_) {
        auto* subpath = detail::find_subpath(path, sub_id);
        if (!subpath) continue;
        auto it = std::find_if(subpath->nodes.begin(), subpath->nodes.end(),
            [&](const PathNode& n) { return n.id == node_id; });
        if (it == subpath->nodes.end()) continue;
        it->anchor = it->anchor + delta_;
        if (it->has_incoming_handle) it->incoming_handle = it->incoming_handle + delta_;
        if (it->has_outgoing_handle) it->outgoing_handle = it->outgoing_handle + delta_;
        detail::add_mapping(mapping, PathElementKind::Node, node_id.value(), node_id.value(),
                            IdMappingRelation::Preserved);
    }
    return true;
}

} // namespace pikdraw::document
