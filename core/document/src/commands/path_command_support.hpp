#pragma once

#include "pikdraw/document/commands/path_commands.hpp"
#include "pikdraw/document/document.hpp"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace pikdraw::document::detail {

inline EditableSubpath* find_subpath(EditablePath& path, SubpathId id) {
    const auto found = std::find_if(path.subpaths.begin(), path.subpaths.end(),
        [&](const EditableSubpath& candidate) { return candidate.id == id; });
    return found == path.subpaths.end() ? nullptr : &*found;
}

inline bool path_is_editable(const Document& doc, const PathObject& object, std::string& error) {
    if (object.locked || !object.visible) {
        error = object.locked ? "Cannot edit a locked path" : "Cannot edit a hidden path";
        return false;
    }
    bool found = false;
    for (const auto& layer : doc.page().layers) {
        if (std::find(layer.object_ids.begin(), layer.object_ids.end(), object.id) ==
            layer.object_ids.end()) continue;
        found = true;
        if (layer.locked || !layer.visible) {
            error = layer.locked ? "Cannot edit an object on a locked layer"
                                 : "Cannot edit an object on a hidden layer";
            return false;
        }
        break;
    }
    if (!found) {
        error = "Path is not attached to a document layer";
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
