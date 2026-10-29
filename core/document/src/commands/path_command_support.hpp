#pragma once

#include "pikdraw/document/commands/path_commands.hpp"
#include "pikdraw/document/document.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <unordered_set>

namespace pikdraw::document::detail {

inline EditableSubpath* find_subpath(EditablePath& path, SubpathId id) {
    const auto found = std::find_if(path.subpaths.begin(), path.subpaths.end(),
        [&](const EditableSubpath& candidate) { return candidate.id == id; });
    return found == path.subpaths.end() ? nullptr : &*found;
}

inline bool path_is_editable(const Document& doc, const PathObject& object, std::string& error) {
    const auto snapshot = doc.current_snapshot();
    const auto* entry = snapshot ? snapshot->hierarchy().find(object.id) : nullptr;
    if (!entry) {
        error = "Path is not attached to the active document hierarchy";
        return false;
    }
    if (entry->mask || !snapshot->hierarchy().editable(object.id)) {
        error = entry->mask ? "Cannot edit a clip mask path"
                            : "Cannot edit a locked or hidden path";
        return false;
    }
    if (!object.authoring_source) {
        error = "Path has no editable identity source; migrate it before topology edits";
        return false;
    }
    if (const auto invalid = object.authoring_source->validation_error()) {
        error = *invalid;
        return false;
    }
    if (!editable_path_matches_adapter(*object.authoring_source, object.path)) {
        error = "Editable path source and renderer adapter are out of sync";
        return false;
    }
    return true;
}

inline geometry::CurveSegment segment_curve(const EditableSubpath& subpath, size_t index) {
    const size_t next_index = (index + 1) % subpath.nodes.size();
    const auto& start = subpath.nodes[index];
    const auto& end = subpath.nodes[next_index];
    const auto& segment = subpath.segments[index];
    switch (segment.kind) {
    case PathSegmentKind::Line:
        return geometry::LineSegment{start.anchor, end.anchor};
    case PathSegmentKind::QuadraticBezier:
        return geometry::QuadraticBezier{start.anchor, segment.quadratic_control, end.anchor};
    case PathSegmentKind::CubicBezier: {
        const auto [start_in, start_out] = editable_path_handles_for(subpath, index);
        const auto [end_in, end_out] = editable_path_handles_for(subpath, next_index);
        (void)start_in;
        (void)end_out;
        return geometry::CubicBezier{start.anchor, start_out, end_in, end.anchor};
    }
    case PathSegmentKind::EllipticArc:
        return segment.arc;
    }
    throw std::invalid_argument("Unknown editable path segment kind");
}

inline void set_manual_handle(PathNode& node, bool incoming, geometry::Point2D point) {
    if (node.mode == PathNodeMode::AutoSmooth || node.mode == PathNodeMode::Corner ||
        node.mode == PathNodeMode::Symmetric)
        node.mode = PathNodeMode::Cusp;
    if (incoming) {
        node.has_incoming_handle = true;
        node.incoming_handle = point;
    } else {
        node.has_outgoing_handle = true;
        node.outgoing_handle = point;
    }
}

inline void reverse_open_subpath(EditableSubpath& subpath) {
    std::reverse(subpath.nodes.begin(), subpath.nodes.end());
    std::reverse(subpath.segments.begin(), subpath.segments.end());
    for (auto& node : subpath.nodes) {
        std::swap(node.has_incoming_handle, node.has_outgoing_handle);
        std::swap(node.incoming_handle, node.outgoing_handle);
    }
    for (auto& segment : subpath.segments) {
        if (segment.kind != PathSegmentKind::EllipticArc) continue;
        segment.arc.start_angle_rad += segment.arc.sweep_angle_rad;
        segment.arc.sweep_angle_rad = -segment.arc.sweep_angle_rad;
    }
}

inline void add_mapping(PathTopologyMapping& result, PathElementKind element,
                        std::optional<uint64_t> before, std::optional<uint64_t> after,
                        IdMappingRelation relation) {
    result.elements.push_back({element, before, after, relation});
}

inline NodeId split_path_segment(EditablePath& path, SubpathId subpath_id,
                                 SegmentId segment_id, double parameter,
                                 PathTopologyMapping& mapping, std::string& error) {
    if (!std::isfinite(parameter) || parameter <= 0.0 || parameter >= 1.0) {
        error = "Segment split parameter must be strictly between zero and one";
        return {};
    }
    auto* subpath = find_subpath(path, subpath_id);
    if (!subpath) {
        error = "Subpath ID was not found";
        return {};
    }
    const auto segment_it = std::find_if(subpath->segments.begin(), subpath->segments.end(),
        [&](const EditableSegment& segment) { return segment.id == segment_id; });
    if (segment_it == subpath->segments.end()) {
        error = "Segment ID was not found";
        return {};
    }
    const size_t index = static_cast<size_t>(segment_it - subpath->segments.begin());
    geometry::CurveSplitResult split;
    try {
        split = geometry::split_curve(segment_curve(*subpath, index), parameter);
    } catch (const std::exception& exception) {
        error = exception.what();
        return {};
    }
    if (split.status != geometry::GeometryOperationStatus::Success || !split.first || !split.second) {
        error = "Geometry kernel could not split this source segment";
        return {};
    }
    const auto new_segment_id = generate_path_element_id<SegmentId>();
    const auto new_node_id = generate_path_element_id<NodeId>();
    if (!new_segment_id.valid() || !new_node_id.valid()) {
        error = "Path identity allocator exhausted";
        return {};
    }
    const auto middle = geometry::curve_point_at(segment_curve(*subpath, index), parameter);
    EditableSegment first{segment_id, PathSegmentKind::Line};
    EditableSegment second{new_segment_id, PathSegmentKind::Line};
    PathNode split_node{new_node_id, middle};

    auto assign_curve = [&](const geometry::CurveSegment& curve, EditableSegment& target,
                            bool is_first) {
        if (std::holds_alternative<geometry::LineSegment>(curve)) {
            target.kind = PathSegmentKind::Line;
        } else if (const auto* quad = std::get_if<geometry::QuadraticBezier>(&curve)) {
            target.kind = PathSegmentKind::QuadraticBezier;
            target.quadratic_control = quad->p1;
        } else if (const auto* cubic = std::get_if<geometry::CubicBezier>(&curve)) {
            target.kind = PathSegmentKind::CubicBezier;
            if (is_first) {
                set_manual_handle(subpath->nodes[index], false, cubic->p1);
                split_node.mode = PathNodeMode::Cusp;
                split_node.has_incoming_handle = true;
                split_node.incoming_handle = cubic->p2;
            } else {
                split_node.mode = PathNodeMode::Cusp;
                split_node.has_outgoing_handle = true;
                split_node.outgoing_handle = cubic->p1;
                const size_t end_index = (index + 1) % subpath->nodes.size();
                set_manual_handle(subpath->nodes[end_index], true, cubic->p2);
            }
        } else if (const auto* arc = std::get_if<geometry::EllipticArc>(&curve)) {
            target.kind = PathSegmentKind::EllipticArc;
            target.arc = *arc;
        }
    };

    assign_curve(*split.first, first, true);
    assign_curve(*split.second, second, false);
    subpath->segments[index] = first;
    subpath->segments.insert(subpath->segments.begin() + static_cast<std::ptrdiff_t>(index + 1), second);
    if (subpath->closed && index + 1 == subpath->nodes.size())
        subpath->nodes.push_back(split_node);
    else
        subpath->nodes.insert(subpath->nodes.begin() + static_cast<std::ptrdiff_t>(index + 1), split_node);
    add_mapping(mapping, PathElementKind::Segment, segment_id.value(), segment_id.value(),
                IdMappingRelation::Preserved);
    add_mapping(mapping, PathElementKind::Segment, std::nullopt, new_segment_id.value(),
                IdMappingRelation::Created);
    add_mapping(mapping, PathElementKind::Node, std::nullopt, new_node_id.value(),
                IdMappingRelation::Created);
    return new_node_id;
}

inline bool break_path_at_node(EditablePath& path, SubpathId subpath_id, NodeId node_id,
                                PathTopologyMapping& mapping, std::string& error) {
    auto* subpath = find_subpath(path, subpath_id);
    if (!subpath) {
        error = "Subpath ID was not found";
        return false;
    }
    const auto node_it = std::find_if(subpath->nodes.begin(), subpath->nodes.end(),
        [&](const PathNode& node) { return node.id == node_id; });
    if (node_it == subpath->nodes.end()) {
        error = "Node ID was not found";
        return false;
    }
    const size_t index = static_cast<size_t>(node_it - subpath->nodes.begin());
    const size_t node_count = subpath->nodes.size();
    if (subpath->closed) {
        const auto start_handles = editable_path_handles_for(*subpath, index);
        const size_t previous_segment = (index + subpath->segments.size() - 1) %
                                        subpath->segments.size();
        const bool previous_is_cubic =
            subpath->segments[previous_segment].kind == PathSegmentKind::CubicBezier;
        const bool next_is_cubic = subpath->segments[index].kind == PathSegmentKind::CubicBezier;
        std::rotate(subpath->nodes.begin(), node_it, subpath->nodes.end());
        std::rotate(subpath->segments.begin(),
                    subpath->segments.begin() + static_cast<std::ptrdiff_t>(index),
                    subpath->segments.end());

        // Keep every source segment. An open-path adapter needs a duplicate
        // terminal node at the old start so the closing curve is not dropped.
        auto& start = subpath->nodes.front();
        if (start.mode == PathNodeMode::AutoSmooth) {
            start.mode = PathNodeMode::Cusp;
            start.has_incoming_handle = previous_is_cubic;
            start.incoming_handle = start_handles.first;
            start.has_outgoing_handle = next_is_cubic;
            start.outgoing_handle = start_handles.second;
        }
        PathNode terminal = start;
        terminal.id = generate_path_element_id<NodeId>();
        if (!terminal.id.valid()) {
            error = "Path identity allocator exhausted";
            return false;
        }
        terminal.has_outgoing_handle = false;
        terminal.outgoing_handle = {};
        if (!previous_is_cubic) {
            terminal.has_incoming_handle = false;
            terminal.incoming_handle = {};
        }
        terminal.mode = terminal.has_incoming_handle ? PathNodeMode::Cusp : PathNodeMode::Corner;
        subpath->nodes.push_back(terminal);
        subpath->closed = false;
        add_mapping(mapping, PathElementKind::Node, node_id.value(), node_id.value(),
                    IdMappingRelation::Preserved);
        add_mapping(mapping, PathElementKind::Node, std::nullopt, terminal.id.value(),
                    IdMappingRelation::Created);
        return true;
    }
    if (index == 0 || index + 1 >= node_count) {
        error = "Cannot break at an endpoint of an already open path";
        return false;
    }

    EditableSubpath second;
    second.id = generate_path_element_id<SubpathId>();
    second.closed = false;
    if (!second.id.valid()) {
        error = "Path identity allocator exhausted";
        return false;
    }
    PathNode split_node = subpath->nodes[index];
    split_node.id = generate_path_element_id<NodeId>();
    if (!split_node.id.valid()) {
        error = "Path identity allocator exhausted";
        return false;
    }
    second.nodes.push_back(split_node);
    for (size_t i = index + 1; i < node_count; ++i) second.nodes.push_back(subpath->nodes[i]);
    for (size_t i = index; i < subpath->segments.size(); ++i)
        second.segments.push_back(subpath->segments[i]);
    const auto second_id = second.id;
    subpath->nodes.erase(subpath->nodes.begin() + static_cast<std::ptrdiff_t>(index + 1),
                         subpath->nodes.end());
    subpath->segments.erase(subpath->segments.begin() + static_cast<std::ptrdiff_t>(index),
                            subpath->segments.end());
    path.subpaths.push_back(std::move(second));
    add_mapping(mapping, PathElementKind::Subpath, std::nullopt, second_id.value(),
                IdMappingRelation::Created);
    add_mapping(mapping, PathElementKind::Node, std::nullopt, split_node.id.value(),
                IdMappingRelation::Created);
    return true;
}

inline bool add_source_ids(const PathObject& object, std::unordered_set<uint64_t>& used) {
    if (!object.authoring_source) return true;
    for (const auto& subpath : object.authoring_source->subpaths)
        if (!used.insert(subpath.id.value()).second) return false;
    for (const auto& subpath : object.authoring_source->subpaths) {
        for (const auto& node : subpath.nodes)
            if (!used.insert(node.id.value()).second) return false;
        for (const auto& segment : subpath.segments)
            if (!used.insert(segment.id.value()).second) return false;
    }
    return true;
}

inline bool identities_unique_for_add(const Document& doc, const ObjectVariant& candidate) {
    std::unordered_set<uint64_t> used;
    for (const auto& [id, existing] : doc.objects().all()) {
        if (!used.insert(id).second) return false;
        if (const auto* path = std::get_if<PathObject>(&existing))
            if (!add_source_ids(*path, used)) return false;
    }
    const auto id = std::visit([](const auto& value) { return value.id; }, candidate);
    if (!used.insert(id).second) return false;
    if (const auto* path = std::get_if<PathObject>(&candidate))
        if (!add_source_ids(*path, used)) return false;
    return true;
}

} // namespace pikdraw::document::detail
