#include "pikdraw/document/commands/node_commands.hpp"
#include "pikdraw/document/model/path_adapter.hpp"
#include "path_command_support.hpp"
#include <algorithm>
#include <array>
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
    return detail::break_path_at_node(path, subpath_id_, node_id_, mapping, error);
}

ScissorsPathCommand::ScissorsPathCommand(
    ObjectId object_id, SubpathId subpath_id, SegmentId segment_id,
    double parameter, NodeId endpoint_node,
    std::shared_ptr<PathTopologyMapping> mapping_result) noexcept
    : PathTopologyCommand(object_id, std::move(mapping_result)), subpath_id_(subpath_id),
      segment_id_(segment_id), parameter_(parameter), endpoint_node_(endpoint_node) {}

bool ScissorsPathCommand::apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                                     std::string& error) {
    if (!subpath_id_.valid()) {
        error = "Scissors subpath identity is invalid";
        return false;
    }
    if (endpoint_node_.valid())
        return detail::break_path_at_node(path, subpath_id_, endpoint_node_, mapping, error);
    if (!segment_id_.valid()) {
        error = "Scissors segment identity is invalid";
        return false;
    }
    const NodeId split_node = detail::split_path_segment(
        path, subpath_id_, segment_id_, parameter_, mapping, error);
    if (!split_node.valid()) return false;
    return detail::break_path_at_node(path, subpath_id_, split_node, mapping, error);
}

SmartTrimPathCommand::SmartTrimPathCommand(
    ObjectId object_id, SubpathId subpath_id, SmartTrimBoundary start,
    SmartTrimBoundary end, bool wraps_closed_subpath,
    std::shared_ptr<PathTopologyMapping> mapping_result) noexcept
    : PathTopologyCommand(object_id, std::move(mapping_result)),
      subpath_id_(subpath_id), start_(start), end_(end),
      wraps_closed_subpath_(wraps_closed_subpath) {}

bool SmartTrimPathCommand::apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                                      std::string& error) {
    constexpr double kEndpointTolerance = 1e-9;
    if (!subpath_id_.valid() || !start_.segment_id.valid() || !end_.segment_id.valid() ||
        !std::isfinite(start_.parameter) || !std::isfinite(end_.parameter) ||
        start_.parameter < 0.0 || start_.parameter > 1.0 ||
        end_.parameter < 0.0 || end_.parameter > 1.0) {
        error = "Smart Trim boundaries are invalid";
        return false;
    }
    auto* subpath = detail::find_subpath(path, subpath_id_);
    if (!subpath) {
        error = "Smart Trim subpath was not found";
        return false;
    }
    const auto locate_segment = [&](SegmentId id) -> std::optional<size_t> {
        const auto found = std::find_if(subpath->segments.begin(), subpath->segments.end(),
            [&](const EditableSegment& segment) { return segment.id == id; });
        if (found == subpath->segments.end()) return std::nullopt;
        return static_cast<size_t>(found - subpath->segments.begin());
    };
    auto start_index = locate_segment(start_.segment_id);
    auto end_index = locate_segment(end_.segment_id);
    if (!start_index || !end_index) {
        error = "Smart Trim boundary segment identity is stale";
        return false;
    }
    const bool same_segment = start_.segment_id == end_.segment_id;
    if (same_segment && std::abs(start_.parameter - end_.parameter) <= kEndpointTolerance) {
        error = "Smart Trim boundaries collapse to one point";
        return false;
    }
    if (!subpath->closed && wraps_closed_subpath_) {
        error = "An open path cannot wrap a Smart Trim interval";
        return false;
    }

    struct WorkBoundary {
        SmartTrimBoundary boundary;
        size_t original_index{0};
        bool is_start{false};
    };
    std::array<WorkBoundary, 2> work{{
        {start_, *start_index, true}, {end_, *end_index, false}}};
    std::sort(work.begin(), work.end(), [](const auto& left, const auto& right) {
        if (left.original_index != right.original_index)
            return left.original_index > right.original_index;
        return left.boundary.parameter > right.boundary.parameter;
    });
    NodeId start_node{};
    NodeId end_node{};
    std::optional<double> higher_split_parameter;
    SegmentId higher_split_segment{};
    for (const auto& item : work) {
        double local_parameter = item.boundary.parameter;
        if (same_segment && higher_split_segment == item.boundary.segment_id &&
            higher_split_parameter && *higher_split_parameter < 1.0 - kEndpointTolerance) {
            local_parameter /= *higher_split_parameter;
        }
        const auto index = locate_segment(item.boundary.segment_id);
        if (!index) {
            error = "Smart Trim could not resolve a split segment after subdivision";
            return false;
        }
        NodeId node{};
        if (local_parameter <= kEndpointTolerance) {
            node = subpath->nodes[*index].id;
        } else if (local_parameter >= 1.0 - kEndpointTolerance) {
            node = subpath->nodes[(*index + 1) % subpath->nodes.size()].id;
        } else {
            node = detail::split_path_segment(path, subpath_id_, item.boundary.segment_id,
                                              local_parameter, mapping, error);
            if (!node.valid()) return false;
            // Splitting keeps the original segment ID on the first half. If
            // both boundaries are on this segment, the lower parameter is
            // now relative to that first half.
            higher_split_parameter = item.boundary.parameter;
            higher_split_segment = item.boundary.segment_id;
        }
        if (item.is_start) start_node = node;
        else end_node = node;
    }
    const auto node_index = [&](NodeId id) -> std::optional<size_t> {
        const auto found = std::find_if(subpath->nodes.begin(), subpath->nodes.end(),
            [&](const PathNode& node) { return node.id == id; });
        if (found == subpath->nodes.end()) return std::nullopt;
        return static_cast<size_t>(found - subpath->nodes.begin());
    };
    const auto first = node_index(start_node);
    const auto last = node_index(end_node);
    if (!first || !last || *first == *last) {
        error = "Smart Trim interval endpoints are not distinct";
        return false;
    }
    if (!subpath->closed && *first >= *last) {
        error = "Smart Trim interval order does not match the open path";
        return false;
    }
    if (subpath->closed && wraps_closed_subpath_ != (*first > *last)) {
        error = "Smart Trim interval order does not match the closed path";
        return false;
    }

    const auto old_nodes = subpath->nodes;
    const auto old_segments = subpath->segments;
    auto mark_removed = [&](size_t node_idx) {
        detail::add_mapping(mapping, PathElementKind::Node, old_nodes[node_idx].id.value(),
                            std::nullopt, IdMappingRelation::Removed);
    };
    auto mark_removed_segment = [&](size_t segment_idx) {
        detail::add_mapping(mapping, PathElementKind::Segment,
                            old_segments[segment_idx].id.value(), std::nullopt,
                            IdMappingRelation::Removed);
    };

    if (subpath->closed) {
        std::vector<EditableSegment> retained_segments;
        std::vector<PathNode> retained_nodes;
        size_t cursor = *last;
        retained_nodes.push_back(old_nodes[cursor]);
        while (cursor != *first) {
            retained_segments.push_back(old_segments[cursor]);
            cursor = (cursor + 1) % old_nodes.size();
            retained_nodes.push_back(old_nodes[cursor]);
        }
        cursor = *first;
        while (cursor != *last) {
            mark_removed_segment(cursor);
            cursor = (cursor + 1) % old_nodes.size();
            if (cursor != *last) mark_removed(cursor);
        }
        if (retained_segments.empty()) {
            error = "Smart Trim would remove the entire closed outline";
            return false;
        }
        subpath->nodes = std::move(retained_nodes);
        subpath->segments = std::move(retained_segments);
        subpath->closed = false;
        return true;
    }

    std::vector<PathNode> before_nodes(old_nodes.begin(), old_nodes.begin() + *first + 1);
    std::vector<EditableSegment> before_segments(old_segments.begin(),
        old_segments.begin() + static_cast<std::ptrdiff_t>(*first));
    std::vector<PathNode> after_nodes(old_nodes.begin() + *last, old_nodes.end());
    std::vector<EditableSegment> after_segments(old_segments.begin() + static_cast<std::ptrdiff_t>(*last),
                                                 old_segments.end());
    for (size_t segment = *first; segment < *last; ++segment)
        mark_removed_segment(segment);
    for (size_t node = *first + 1; node < *last; ++node)
        mark_removed(node);
    if (before_segments.empty() && after_segments.empty()) {
        error = "Smart Trim would remove the entire open path";
        return false;
    }
    if (before_segments.empty()) {
        subpath->nodes = std::move(after_nodes);
        subpath->segments = std::move(after_segments);
        subpath->closed = false;
        return true;
    }
    subpath->nodes = std::move(before_nodes);
    subpath->segments = std::move(before_segments);
    subpath->closed = false;
    if (!after_segments.empty()) {
        EditableSubpath remainder;
        remainder.id = generate_path_element_id<SubpathId>();
        if (!remainder.id.valid()) {
            error = "Path identity allocator exhausted during Smart Trim";
            return false;
        }
        remainder.nodes = std::move(after_nodes);
        remainder.segments = std::move(after_segments);
        remainder.closed = false;
        detail::add_mapping(mapping, PathElementKind::Subpath, std::nullopt,
                            remainder.id.value(), IdMappingRelation::Created);
        path.subpaths.insert(path.subpaths.begin() +
            static_cast<std::ptrdiff_t>(subpath - path.subpaths.data() + 1), std::move(remainder));
    }
    return true;
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

        auto prepare_node = [&](PathNode& node, size_t node_idx, bool modify_outgoing, const geometry::Point2D& new_handle) {
            if (node.mode == PathNodeMode::AutoSmooth) {
                const auto [in_h, out_h] = editable_path_handles_for(*subpath, node_idx);
                node.incoming_handle = in_h;
                node.has_incoming_handle = (subpath->closed || node_idx > 0);
                node.outgoing_handle = out_h;
                node.has_outgoing_handle = (subpath->closed || node_idx + 1 < n);
            }
            if (modify_outgoing) {
                node.outgoing_handle = new_handle;
                node.has_outgoing_handle = true;
            } else {
                node.incoming_handle = new_handle;
                node.has_incoming_handle = true;
            }
            node.mode = PathNodeMode::Cusp;
        };

        prepare_node(subpath->nodes[idx], idx, true, p0 + (p1 - p0) * (1.0 / 3.0));
        prepare_node(subpath->nodes[next_idx], next_idx, false, p0 + (p1 - p0) * (2.0 / 3.0));
    } else if (new_kind_ == PathSegmentKind::Line) {
        it->kind = PathSegmentKind::Line;
        subpath->nodes[idx].has_outgoing_handle = false;
        subpath->nodes[next_idx].has_incoming_handle = false;
        if (!subpath->nodes[idx].has_incoming_handle) {
            subpath->nodes[idx].mode = PathNodeMode::Corner;
        } else {
            subpath->nodes[idx].mode = PathNodeMode::Cusp;
        }
        if (!subpath->nodes[next_idx].has_outgoing_handle) {
            subpath->nodes[next_idx].mode = PathNodeMode::Corner;
        } else {
            subpath->nodes[next_idx].mode = PathNodeMode::Cusp;
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

BendSegmentCommand::BendSegmentCommand(
    ObjectId object_id, SubpathId subpath_id, SegmentId segment_id,
    geometry::Point2D outgoing_handle, geometry::Point2D incoming_handle,
    std::shared_ptr<PathTopologyMapping> mapping_result) noexcept
    : PathTopologyCommand(object_id, std::move(mapping_result)),
      subpath_id_(subpath_id), segment_id_(segment_id),
      outgoing_handle_(outgoing_handle), incoming_handle_(incoming_handle) {}

bool BendSegmentCommand::apply_edit(EditablePath& path, PathTopologyMapping& mapping,
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
    if (n == 0) {
        error = "Subpath has no nodes";
        return false;
    }
    const size_t next_idx = (idx + 1) % n;

    it->kind = PathSegmentKind::CubicBezier;

    auto prepare_node_for_bend = [&](PathNode& node, size_t node_idx, bool modify_outgoing, const geometry::Point2D& new_handle) {
        if (node.mode == PathNodeMode::AutoSmooth) {
            const auto [in_h, out_h] = editable_path_handles_for(*subpath, node_idx);
            node.incoming_handle = in_h;
            node.has_incoming_handle = (subpath->closed || node_idx > 0);
            node.outgoing_handle = out_h;
            node.has_outgoing_handle = (subpath->closed || node_idx + 1 < n);
        }
        if (modify_outgoing) {
            node.outgoing_handle = new_handle;
            node.has_outgoing_handle = true;
        } else {
            node.incoming_handle = new_handle;
            node.has_incoming_handle = true;
        }
        node.mode = PathNodeMode::Cusp;
    };

    prepare_node_for_bend(subpath->nodes[idx], idx, true, outgoing_handle_);
    prepare_node_for_bend(subpath->nodes[next_idx], next_idx, false, incoming_handle_);

    detail::add_mapping(mapping, PathElementKind::Segment, segment_id_.value(),
                        segment_id_.value(), IdMappingRelation::Preserved);
    detail::add_mapping(mapping, PathElementKind::Node, subpath->nodes[idx].id.value(),
                        subpath->nodes[idx].id.value(), IdMappingRelation::Preserved);
    detail::add_mapping(mapping, PathElementKind::Node, subpath->nodes[next_idx].id.value(),
                        subpath->nodes[next_idx].id.value(), IdMappingRelation::Preserved);
    return true;
}

} // namespace pikdraw::document
