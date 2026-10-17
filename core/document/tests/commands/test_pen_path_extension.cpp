#include "pikdraw/document/commands/path_commands.hpp"
#include "pikdraw/document/document.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

using namespace pikdraw;
using namespace pikdraw::document;

namespace {

PathObject add_path(Document& document, EditableSubpath subpath) {
    EditablePath source;
    source.subpaths.push_back(std::move(subpath));

    PathObject object;
    object.id = generate_object_id();
    object.name = "Original source path";
    object.has_fill = true;
    object.has_stroke = true;
    object.stroke_width_mm = 2.75;
    object.transform = geometry::Transform2D::translation(4.0, -3.0);
    object.set_authoring_source(std::move(source));
    const auto added = document.execute_command(
        std::make_unique<AddObjectCommand>(object));
    assert(added.success);
    return object;
}

PathObject current_path(const Document& document, ObjectId id) {
    const auto object = document.current_snapshot()->get_object(id);
    assert(object && std::holds_alternative<PathObject>(*object));
    return std::get<PathObject>(*object);
}

EditableSubpath two_node_line() {
    EditableSubpath subpath;
    subpath.id = generate_path_element_id<SubpathId>();
    PathNode first;
    first.id = generate_path_element_id<NodeId>();
    first.anchor = {0.0, 0.0};
    PathNode last;
    last.id = generate_path_element_id<NodeId>();
    last.anchor = {10.0, 0.0};
    subpath.nodes = {first, last};
    subpath.segments.push_back(
        EditableSegment{generate_path_element_id<SegmentId>(), PathSegmentKind::Line});
    return subpath;
}

EditableSubpath three_node_line() {
    auto subpath = two_node_line();
    PathNode middle;
    middle.id = generate_path_element_id<NodeId>();
    middle.anchor = {20.0, 0.0};
    subpath.nodes.push_back(middle);
    EditableSegment segment;
    segment.id = generate_path_element_id<SegmentId>();
    segment.kind = PathSegmentKind::Line;
    subpath.segments.push_back(segment);
    return subpath;
}

EditableSubpath cubic_then_arc(geometry::EllipticArc& arc) {
    EditableSubpath subpath;
    subpath.id = generate_path_element_id<SubpathId>();

    PathNode first;
    first.id = generate_path_element_id<NodeId>();
    first.anchor = {0.0, 0.0};
    first.mode = PathNodeMode::Cusp;
    first.has_outgoing_handle = true;
    first.outgoing_handle = {3.0, 1.0};

    PathNode middle;
    middle.id = generate_path_element_id<NodeId>();
    middle.anchor = {10.0, 0.0};
    middle.mode = PathNodeMode::Cusp;
    middle.has_incoming_handle = true;
    middle.incoming_handle = {7.0, 1.0};

    arc = {{15.0, 0.0}, {5.0, 0.0}, {0.0, 5.0}, std::numbers::pi, std::numbers::pi};
    PathNode last;
    last.id = generate_path_element_id<NodeId>();
    last.anchor = arc.point_at(1.0);

    EditableSegment cubic;
    cubic.id = generate_path_element_id<SegmentId>();
    cubic.kind = PathSegmentKind::CubicBezier;
    EditableSegment elliptic;
    elliptic.id = generate_path_element_id<SegmentId>();
    elliptic.kind = PathSegmentKind::EllipticArc;
    elliptic.arc = arc;

    subpath.nodes = {first, middle, last};
    subpath.segments = {cubic, elliptic};
    return subpath;
}

bool has_mapping(const PathTopologyMapping& mapping, PathElementKind kind,
                 std::optional<uint64_t> before, std::optional<uint64_t> after,
                 IdMappingRelation relation) {
    for (const auto& entry : mapping.elements) {
        if (entry.element == kind && entry.before == before && entry.after == after &&
            entry.relation == relation)
            return true;
    }
    return false;
}

void test_extend_from_end_preserves_style_and_undo_redo() {
    Document document("pen-extension-end");
    auto object = add_path(document, two_node_line());
    const auto source = *object.authoring_source;
    const auto subpath = source.subpaths.front();
    auto mapping = std::make_shared<PathTopologyMapping>();

    PathNode extension;
    extension.anchor = {20.0, 5.0};
    const auto result = document.execute_command(
        std::make_unique<ExtendPathEndpointCommand>(
            object.id, subpath.id, subpath.nodes.back().id,
            std::vector<PathNode>{extension},
            std::vector<PathSegmentKind>{PathSegmentKind::Line}, mapping));
    assert(result.success);

    auto updated = current_path(document, object.id);
    assert(updated.name == object.name);
    assert(updated.stroke_width_mm == object.stroke_width_mm);
    assert(updated.has_fill == object.has_fill);
    assert(updated.has_stroke == object.has_stroke);
    assert(updated.transform.tx == object.transform.tx);
    assert(updated.transform.ty == object.transform.ty);
    assert(updated.authoring_source->subpaths.front().nodes.size() == 3);
    assert(updated.authoring_source->subpaths.front().segments.size() == 2);
    assert(updated.authoring_source->subpaths.front().nodes[0].id == subpath.nodes[0].id);
    assert(updated.authoring_source->subpaths.front().nodes[1].id == subpath.nodes[1].id);
    const auto& added_node = updated.authoring_source->subpaths.front().nodes.back();
    const auto& added_segment = updated.authoring_source->subpaths.front().segments.back();
    assert(added_node.id.valid() && added_node.anchor.x == 20.0 && added_node.anchor.y == 5.0);
    assert(added_segment.id.valid() && added_segment.kind == PathSegmentKind::Line);
    assert(has_mapping(*mapping, PathElementKind::Node, std::nullopt,
                       added_node.id.value(), IdMappingRelation::Created));
    assert(has_mapping(*mapping, PathElementKind::Segment, std::nullopt,
                       added_segment.id.value(), IdMappingRelation::Created));
    assert(has_mapping(*mapping, PathElementKind::Node, subpath.nodes[0].id.value(),
                       subpath.nodes[0].id.value(), IdMappingRelation::Preserved));
    assert(has_mapping(*mapping, PathElementKind::Segment,
                       subpath.segments[0].id.value(), subpath.segments[0].id.value(),
                       IdMappingRelation::Preserved));
    assert(editable_path_matches_adapter(*updated.authoring_source, updated.path));

    const auto committed_node_id = added_node.id;
    assert(document.undo());
    const auto undone = current_path(document, object.id);
    assert(undone.authoring_source->subpaths.front().nodes.size() == 2);
    assert(undone.authoring_source->subpaths.front().nodes.back().id == subpath.nodes.back().id);
    assert(document.redo());
    const auto redone = current_path(document, object.id);
    assert(redone.authoring_source->subpaths.front().nodes.back().id == committed_node_id);
    assert(redone.authoring_source->subpaths.front().segments.back().id == added_segment.id);
}

void test_extend_from_start_reverses_cubic_and_arc_without_changing_shape() {
    Document document("pen-extension-start");
    geometry::EllipticArc original_arc;
    auto source_subpath = cubic_then_arc(original_arc);
    auto object = add_path(document, source_subpath);
    source_subpath = object.authoring_source->subpaths.front();
    const auto old_first = source_subpath.nodes.front();
    const auto old_middle = source_subpath.nodes[1];
    const auto old_last = source_subpath.nodes.back();
    const auto old_cubic = source_subpath.segments.front();
    const auto old_arc = source_subpath.segments.back();

    PathNode extension;
    extension.anchor = {-5.0, -2.0};
    auto mapping = std::make_shared<PathTopologyMapping>();
    const auto result = document.execute_command(
        std::make_unique<ExtendPathEndpointCommand>(
            object.id, source_subpath.id, old_first.id,
            std::vector<PathNode>{extension},
            std::vector<PathSegmentKind>{PathSegmentKind::Line}, mapping));
    assert(result.success);

    const auto updated = current_path(document, object.id);
    const auto& subpath = updated.authoring_source->subpaths.front();
    assert(subpath.nodes.size() == 4 && subpath.segments.size() == 3);
    assert(subpath.nodes[0].id == old_last.id);
    assert(subpath.nodes[1].id == old_middle.id);
    assert(subpath.nodes[2].id == old_first.id);
    assert(subpath.nodes[2].has_incoming_handle);
    assert(subpath.nodes[2].incoming_handle.x == old_first.outgoing_handle.x);
    assert(subpath.nodes[2].incoming_handle.y == old_first.outgoing_handle.y);
    assert(subpath.segments[0].id == old_arc.id);
    assert(subpath.segments[1].id == old_cubic.id);
    assert(subpath.segments[0].kind == PathSegmentKind::EllipticArc);
    assert(subpath.segments[1].kind == PathSegmentKind::CubicBezier);

    const auto& reversed_arc = subpath.segments[0].arc;
    for (const double t : std::array{0.0, 0.25, 0.5, 0.75, 1.0})
        assert(reversed_arc.point_at(t).distance_to(original_arc.point_at(1.0 - t)) < 1e-9);

    const geometry::CubicBezier original_cubic{
        old_first.anchor, old_first.outgoing_handle, old_middle.incoming_handle,
        old_middle.anchor};
    const geometry::CubicBezier reversed_cubic{
        subpath.nodes[1].anchor, subpath.nodes[1].outgoing_handle,
        subpath.nodes[2].incoming_handle, subpath.nodes[2].anchor};
    for (const double t : std::array{0.0, 0.25, 0.5, 0.75, 1.0})
        assert(reversed_cubic.point_at(t).distance_to(original_cubic.point_at(1.0 - t)) < 1e-9);

    assert(has_mapping(*mapping, PathElementKind::Node, old_first.id.value(),
                       old_first.id.value(), IdMappingRelation::Preserved));
    assert(has_mapping(*mapping, PathElementKind::Segment, old_arc.id.value(),
                       old_arc.id.value(), IdMappingRelation::Preserved));
    assert(editable_path_matches_adapter(*updated.authoring_source, updated.path));
}

void test_invalid_endpoint_extension_is_atomic() {
    Document document("pen-extension-invalid");
    auto object = add_path(document, three_node_line());
    const auto source = object.authoring_source->subpaths.front();
    PathNode extension;
    extension.anchor = {20.0, 0.0};
    const auto revision = document.revision();

    auto middle_result = document.execute_command(
        std::make_unique<ExtendPathEndpointCommand>(
            object.id, source.id, source.nodes[1].id,
            std::vector<PathNode>{extension},
            std::vector<PathSegmentKind>{PathSegmentKind::Line}));
    assert(!middle_result.success);
    assert(document.revision() == revision);
    assert(current_path(document, object.id).authoring_source->subpaths.front().nodes.size() == 3);

    auto mismatch_result = document.execute_command(
        std::make_unique<ExtendPathEndpointCommand>(
            object.id, source.id, source.nodes.back().id,
            std::vector<PathNode>{extension}, std::vector<PathSegmentKind>{}));
    assert(!mismatch_result.success);
    assert(document.revision() == revision);

    auto assigned = extension;
    assigned.id = generate_path_element_id<NodeId>();
    auto assigned_id_result = document.execute_command(
        std::make_unique<ExtendPathEndpointCommand>(
            object.id, source.id, source.nodes.back().id,
            std::vector<PathNode>{assigned},
            std::vector<PathSegmentKind>{PathSegmentKind::Line}));
    assert(!assigned_id_result.success);
    assert(document.revision() == revision);
}

} // namespace

int main() {
    test_extend_from_end_preserves_style_and_undo_redo();
    test_extend_from_start_reverses_cubic_and_arc_without_changing_shape();
    test_invalid_endpoint_extension_is_atomic();
    std::cout << "[test_pen_path_extension] all checks passed\n";
    return 0;
}
