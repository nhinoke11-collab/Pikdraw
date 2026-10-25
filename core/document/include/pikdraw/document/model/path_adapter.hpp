#pragma once

#include "pikdraw/document/model/editable_path.hpp"
#include <stdexcept>

namespace pikdraw::document {

[[nodiscard]] inline std::pair<geometry::Point2D, geometry::Point2D>
editable_path_handles_for(const EditableSubpath& subpath, size_t node_index) {
    const auto& node = subpath.nodes[node_index];
    if (node.mode != PathNodeMode::AutoSmooth)
        return {node.has_incoming_handle ? node.incoming_handle : node.anchor,
                node.has_outgoing_handle ? node.outgoing_handle : node.anchor};
    if (subpath.nodes.size() < 2) return {node.anchor, node.anchor};
    const bool has_previous = subpath.closed || node_index > 0;
    const bool has_next = subpath.closed || node_index + 1 < subpath.nodes.size();
    const auto previous = has_previous
        ? subpath.nodes[(node_index + subpath.nodes.size() - 1) % subpath.nodes.size()].anchor
        : node.anchor;
    const auto next = has_next
        ? subpath.nodes[(node_index + 1) % subpath.nodes.size()].anchor
        : node.anchor;
    auto direction = has_previous && has_next ? next - previous
        : has_next ? next - node.anchor : node.anchor - previous;
    const double length = std::hypot(direction.x, direction.y);
    if (!(length > 0.0) || !std::isfinite(length)) return {node.anchor, node.anchor};
    direction = direction / length;
    const double incoming_length = has_previous
        ? node.auto_smooth_tension * node.anchor.distance_to(previous) / 3.0 : 0.0;
    const double outgoing_length = has_next
        ? node.auto_smooth_tension * node.anchor.distance_to(next) / 3.0 : 0.0;
    return {node.anchor - direction * incoming_length,
            node.anchor + direction * outgoing_length};
}

[[nodiscard]] inline geometry::Path2D editable_path_to_path2d(const EditablePath& source) {
    if (const auto error = source.validation_error()) throw std::invalid_argument(*error);
    geometry::Path2D path;
    path.set_winding_rule(source.winding_rule);
    for (const auto& subpath : source.subpaths) {
        path.move_to(subpath.nodes.front().anchor);
        for (size_t i = 0; i < subpath.segments.size(); ++i) {
            const size_t next = (i + 1) % subpath.nodes.size();
            const auto& segment = subpath.segments[i];
            const auto end = subpath.nodes[next].anchor;
            if (subpath.closed && i + 1 == subpath.segments.size() &&
                segment.kind == PathSegmentKind::Line) continue;
            switch (segment.kind) {
            case PathSegmentKind::Line:
                path.line_to(end);
                break;
            case PathSegmentKind::QuadraticBezier:
                path.quad_to(segment.quadratic_control.x, segment.quadratic_control.y,
                             end.x, end.y);
                break;
            case PathSegmentKind::CubicBezier: {
                const auto [start_in, start_out] = editable_path_handles_for(subpath, i);
                const auto [end_in, end_out] = editable_path_handles_for(subpath, next);
                (void)start_in;
                (void)end_out;
                path.cubic_to(start_out.x, start_out.y, end_in.x, end_in.y, end.x, end.y);
                break;
            }
            case PathSegmentKind::EllipticArc: {
                const auto encoded = geometry::encode_endpoint_arc(segment.arc);
                if (!encoded) throw std::invalid_argument("Analytic arc cannot be encoded for the geometry adapter");
                path.arc_to(encoded->rx, encoded->ry, encoded->rotation_deg,
                            encoded->large_arc, encoded->sweep, end.x, end.y);
                break;
            }
            }
        }
        if (subpath.closed) path.close();
    }
    return path;
}

[[nodiscard]] inline std::optional<EditablePath> editable_path_from_path2d(
    const geometry::Path2D& source, std::string* error) {
    if (error) error->clear();
    EditablePath result;
    result.winding_rule = source.winding_rule();
    EditableSubpath* current = nullptr;
    auto fail = [&](std::string message) -> std::optional<EditablePath> {
        if (error) *error = std::move(message);
        return std::nullopt;
    };
    auto new_id = []<class Id>() -> std::optional<Id> {
        const auto id = generate_path_element_id<Id>();
        if (!id.valid()) return std::nullopt;
        return id;
    };

    for (const auto& element : source.elements()) {
        switch (element.verb) {
        case geometry::PathVerb::MoveTo: {
            const auto subpath_id = new_id.template operator()<SubpathId>();
            const auto node_id = new_id.template operator()<NodeId>();
            if (!subpath_id || !node_id) return fail("Path identity allocator exhausted");
            result.subpaths.push_back(EditableSubpath{});
            current = &result.subpaths.back();
            current->id = *subpath_id;
            current->nodes.push_back(PathNode{*node_id, element.pt});
            break;
        }
        case geometry::PathVerb::LineTo:
        case geometry::PathVerb::QuadTo:
        case geometry::PathVerb::CubicTo:
        case geometry::PathVerb::ArcTo: {
            if (!current || current->closed || current->nodes.empty())
                return fail("Path segment has no open source subpath");
            const auto node_id = new_id.template operator()<NodeId>();
            const auto segment_id = new_id.template operator()<SegmentId>();
            if (!node_id || !segment_id) return fail("Path identity allocator exhausted");
            auto& start = current->nodes.back();
            PathNode end{*node_id, element.pt};
            EditableSegment segment;
            segment.id = *segment_id;
            switch (element.verb) {
            case geometry::PathVerb::LineTo:
                segment.kind = PathSegmentKind::Line;
                break;
            case geometry::PathVerb::QuadTo:
                segment.kind = PathSegmentKind::QuadraticBezier;
                segment.quadratic_control = element.ctrl1;
                break;
            case geometry::PathVerb::CubicTo:
                segment.kind = PathSegmentKind::CubicBezier;
                if (start.mode == PathNodeMode::Corner) start.mode = PathNodeMode::Cusp;
                start.has_outgoing_handle = true;
                start.outgoing_handle = element.ctrl1;
                end.mode = PathNodeMode::Cusp;
                end.has_incoming_handle = true;
                end.incoming_handle = element.ctrl2;
                break;
            case geometry::PathVerb::ArcTo: {
                if (!(element.rx > 0.0) || !(element.ry > 0.0) || start.anchor == element.pt)
                    return fail("Degenerate endpoint arc requires explicit conversion before editing");
                const auto resolved = geometry::resolve_endpoint_arc(geometry::EndpointArcInput{
                    start.anchor, element.rx, element.ry, element.rotation_deg,
                    element.large_arc, element.sweep, element.pt});
                if (resolved.kind != geometry::ArcResolutionKind::Arc)
                    return fail("Endpoint arc could not be represented as analytic source geometry");
                segment.kind = PathSegmentKind::EllipticArc;
                segment.arc = resolved.arc;
                break;
            }
            case geometry::PathVerb::MoveTo:
            case geometry::PathVerb::Close:
                break;
            }
            current->segments.push_back(segment);
            current->nodes.push_back(end);
            break;
        }
        case geometry::PathVerb::Close:
            if (!current || current->closed || current->nodes.size() < 2)
                return fail("Close requires an open subpath with at least two nodes");
            if (current->nodes.back().anchor == current->nodes.front().anchor) {
                const auto closing_node = current->nodes.back();
                if (closing_node.has_incoming_handle) {
                    current->nodes.front().mode = PathNodeMode::Cusp;
                    current->nodes.front().has_incoming_handle = true;
                    current->nodes.front().incoming_handle = closing_node.incoming_handle;
                }
                current->nodes.pop_back();
                if (current->nodes.size() < 2)
                    return fail("Close would create a zero-length closed path");
            } else {
                const auto id = new_id.template operator()<SegmentId>();
                if (!id) return fail("Path identity allocator exhausted");
                current->segments.push_back(EditableSegment{*id, PathSegmentKind::Line});
            }
            current->closed = true;
            break;
        }
    }
    if (const auto invalid = result.validation_error()) return fail(*invalid);
    for (const auto& subpath : result.subpaths) {
        reserve_path_element_id(subpath.id);
        for (const auto& node : subpath.nodes) reserve_path_element_id(node.id);
        for (const auto& segment : subpath.segments) reserve_path_element_id(segment.id);
    }
    return result;
}

[[nodiscard]] inline bool editable_path_matches_adapter(
    const EditablePath& source, const geometry::Path2D& adapter) {
    const auto expected = editable_path_to_path2d(source);
    if (expected.winding_rule() != adapter.winding_rule()) return false;
    const auto& left = expected.elements();
    const auto& right = adapter.elements();
    size_t redundant_close_line = right.size();
    if (left.size() + 1 == right.size() && left.size() >= 2 &&
        right.size() >= 3 && left.back().verb == geometry::PathVerb::Close &&
        right.back().verb == geometry::PathVerb::Close &&
        right.front().verb == geometry::PathVerb::MoveTo &&
        right[right.size() - 2].verb == geometry::PathVerb::LineTo &&
        right[right.size() - 2].pt == right.front().pt)
        redundant_close_line = right.size() - 2;
    if (left.size() != right.size() -
            static_cast<size_t>(redundant_close_line < right.size()))
        return false;
    size_t right_index = 0;
    for (size_t left_index = 0; left_index < left.size(); ++left_index) {
        if (right_index == redundant_close_line) ++right_index;
        const auto& expected_element = left[left_index];
        const auto& actual_element = right[right_index++];
        if (expected_element.verb != actual_element.verb ||
            expected_element.pt != actual_element.pt ||
            expected_element.ctrl1 != actual_element.ctrl1 ||
            expected_element.ctrl2 != actual_element.ctrl2 ||
            expected_element.rx != actual_element.rx ||
            expected_element.ry != actual_element.ry ||
            expected_element.rotation_deg != actual_element.rotation_deg ||
            expected_element.large_arc != actual_element.large_arc ||
            expected_element.sweep != actual_element.sweep)
            return false;
    }
    return true;
}

} // namespace pikdraw::document
