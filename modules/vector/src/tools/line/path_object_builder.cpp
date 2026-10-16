#include "path_object_builder.hpp"

#include <algorithm>

namespace pikdraw::vector::tools::line::detail {
namespace {

PathObjectBuildResult fail(std::string message) {
    return {{}, std::move(message)};
}

} // namespace

PathObjectBuildResult build_path_object(
    PathDrawingKind kind, const std::vector<geometry::Point2D>& input_points,
    bool closed) {
    if (kind == PathDrawingKind::Line && (closed || input_points.size() != 2))
        return fail("A Line must contain exactly one open segment");
    if (kind == PathDrawingKind::Polyline && input_points.size() < 2)
        return fail("A Polyline requires at least two confirmed points");
    if (kind != PathDrawingKind::Line && kind != PathDrawingKind::Polyline)
        return fail("Unknown path drawing kind");

    std::vector<geometry::Point2D> points = input_points;
    for (const auto point : points) {
        if (!point.is_finite() ||
            geometry::validate_point(point) != geometry::GeometryStatus::Ok)
            return fail("Path coordinates must be finite and within the document range");
    }
    if (closed && points.size() > 1 && points.back() == points.front())
        points.pop_back();
    if (closed && points.size() < 3)
        return fail("A closed Polyline requires three distinct vertices");
    if (points.size() < 2)
        return fail("A path must contain at least one non-zero segment");
    for (size_t index = 1; index < points.size(); ++index) {
        if (points[index - 1] == points[index])
            return fail("Zero-length path segments are not allowed");
    }
    if (closed && points.back() == points.front())
        return fail("A closing edge cannot have zero length");

    document::EditablePath source;
    document::EditableSubpath subpath;
    subpath.id = document::generate_path_element_id<document::SubpathId>();
    subpath.closed = closed;
    if (!subpath.id.valid()) return fail("Subpath identity allocator is exhausted");
    subpath.nodes.reserve(points.size());
    const size_t segment_count = closed ? points.size() : points.size() - 1;
    subpath.segments.reserve(segment_count);
    for (const auto point : points) {
        const auto id = document::generate_path_element_id<document::NodeId>();
        if (!id.valid()) return fail("Node identity allocator is exhausted");
        subpath.nodes.push_back(document::PathNode{id, point});
    }
    for (size_t index = 0; index < segment_count; ++index) {
        const auto id = document::generate_path_element_id<document::SegmentId>();
        if (!id.valid()) return fail("Segment identity allocator is exhausted");
        subpath.segments.push_back(
            document::EditableSegment{id, document::PathSegmentKind::Line});
    }
    source.subpaths.push_back(std::move(subpath));
    if (const auto error = source.validation_error()) return fail(*error);

    document::PathObject object;
    object.id = document::generate_object_id();
    if (object.id == document::kInvalidObjectId)
        return fail("Object identity allocator is exhausted");
    object.name = kind == PathDrawingKind::Line ? "Line" : "Polyline";
    object.has_fill = closed;
    object.has_stroke = true;
    try {
        object.set_authoring_source(std::move(source));
    } catch (const std::exception& error) {
        return fail(error.what());
    }
    return {std::move(object), {}};
}

} // namespace pikdraw::vector::tools::line::detail
