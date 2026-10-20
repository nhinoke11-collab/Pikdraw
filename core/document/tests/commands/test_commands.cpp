#include "pikdraw/document/document.hpp"
#include "pikdraw/document/commands/appearance_commands.hpp"
#include "pikdraw/document/commands/path_commands.hpp"
#include "pikdraw/document/commands/import_content_command.hpp"
#include "pikdraw/document/commands/page_commands.hpp"
#include "pikdraw/geometry/primitives/validation.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

using namespace pikdraw;
using namespace pikdraw::document;

EditableSubpath make_line_subpath(const std::vector<geometry::Point2D>& points,
                                  bool closed = false) {
    EditableSubpath subpath;
    subpath.id = generate_path_element_id<SubpathId>();
    subpath.closed = closed;
    for (const auto point : points)
        subpath.nodes.push_back(PathNode{generate_path_element_id<NodeId>(), point});
    const size_t segment_count = closed ? points.size() : points.size() - 1;
    for (size_t i = 0; i < segment_count; ++i)
        subpath.segments.push_back(EditableSegment{generate_path_element_id<SegmentId>(),
                                                   PathSegmentKind::Line});
    return subpath;
}

PathObject make_authoring_object(const std::vector<geometry::Point2D>& points) {
    PathObject object;
    object.id = generate_object_id();
    EditablePath source;
    source.subpaths.push_back(make_line_subpath(points));
    object.set_authoring_source(std::move(source));
    return object;
}

PathObject snapshot_path(const Document& doc, ObjectId id) {
    auto object = doc.current_snapshot()->get_object(id);
    assert(object && std::holds_alternative<PathObject>(*object));
    return std::get<PathObject>(*object);
}

void test_add_and_undo_path() {
    Document doc("doc-test-1");
    assert(doc.revision() == 0);
    assert(!doc.can_undo());

    PathObject po;
    po.id = generate_object_id();
    po.name = "Test Path";
    po.path.move_to({10.0, 20.0});
    po.path.line_to({100.0, 20.0});
    po.path.close();

    auto res = doc.execute_command(std::make_unique<AddObjectCommand>(po));
    assert(res.success);
    assert(doc.revision() == 1);
    assert(doc.can_undo());
    assert(doc.current_snapshot()->object_count() == 1);

    // Undo
    bool undo_res = doc.undo();
    assert(undo_res);
    assert(doc.revision() == 2); // Monotonic revision bump
    assert(doc.current_snapshot()->object_count() == 0);
    assert(doc.can_redo());

    // Redo
    bool redo_res = doc.redo();
    assert(redo_res);
    assert(doc.revision() == 3);
    assert(doc.current_snapshot()->object_count() == 1);
}

void test_remove_and_transform() {
    Document doc("doc-test-2");
    PathObject po;
    po.id = generate_object_id();
    po.name = "Box";
    po.path.move_to({0, 0});
    po.path.line_to({50, 50});

    doc.execute_command(std::make_unique<AddObjectCommand>(po));
    ObjectId id = po.id;

    // Transform
    auto t = geometry::Transform2D::translation(25.0, 30.0);
    doc.execute_command(std::make_unique<TransformObjectCommand>(id, t));
    auto snap = doc.current_snapshot();
    auto obj = snap->get_object(id);
    assert(obj.has_value());
    const auto& po_trans = std::get<PathObject>(*obj);
    assert(po_trans.transform.tx == 25.0);
    assert(po_trans.transform.ty == 30.0);

    // Remove
    doc.execute_command(std::make_unique<RemoveObjectCommand>(id));
    assert(doc.current_snapshot()->object_count() == 0);

    // Undo Remove
    doc.undo();
    assert(doc.current_snapshot()->object_count() == 1);
}

void test_transaction_writer() {
    Document doc("doc-test-3");
    auto& writer = doc.writer();

    writer.begin_transaction("Add Two Objects");

    PathObject p1;
    p1.id = generate_object_id();
    p1.path.move_to({0, 0});
    writer.stage_command(std::make_unique<AddObjectCommand>(p1));

    PathObject p2;
    p2.id = generate_object_id();
    p2.path.move_to({10, 10});
    writer.stage_command(std::make_unique<AddObjectCommand>(p2));

    auto res = writer.commit_transaction();
    assert(res.success);
    assert(doc.current_snapshot()->object_count() == 2);
    assert(doc.revision() == 1); // Only 1 revision bump for entire transaction

    // Single undo undos both objects
    doc.undo();
    assert(doc.current_snapshot()->object_count() == 0);
    assert(doc.revision() == 2);
}

void test_appearance_command_undo_and_noop_history() {
    Document doc("doc-appearance-command");
    PathObject object;
    object.id = generate_object_id();
    object.path.move_to({0, 0});
    object.path.line_to({10, 0});
    const auto object_id = object.id;
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(object)).success);
    assert(doc.revision() == 1);

    auto changed_appearance = object.appearance;
    changed_appearance.object_opacity = 0.5;
    auto changed = doc.execute_command(std::make_unique<SetAppearanceCommand>(
        std::vector<AppearanceReplacement>{{object_id, changed_appearance}}));
    assert(changed.success && !changed.is_no_change);
    assert(doc.revision() == 2);
    const auto current = doc.current_snapshot()->get_object(object_id);
    assert(current && std::get<PathObject>(*current).appearance.object_opacity == 0.5);

    auto unchanged = doc.execute_command(std::make_unique<SetAppearanceCommand>(
        std::vector<AppearanceReplacement>{{object_id, changed_appearance}}));
    assert(unchanged.success && unchanged.is_no_change);
    assert(doc.revision() == 2);

    // One undo must undo the actual appearance edit, not consume a no-op record.
    assert(doc.undo());
    assert(doc.revision() == 3);
    const auto undone = doc.current_snapshot()->get_object(object_id);
    assert(undone && std::get<PathObject>(*undone).appearance.object_opacity == 1.0);
    assert(doc.redo());
    assert(doc.revision() == 4);
    const auto redone = doc.current_snapshot()->get_object(object_id);
    assert(redone && std::get<PathObject>(*redone).appearance.object_opacity == 0.5);
}

void test_composite_skips_noop_children_for_undo() {
    Document doc("doc-appearance-composite-noop");
    PathObject existing;
    existing.id = generate_object_id();
    existing.path.move_to({0, 0});
    existing.path.line_to({10, 0});
    const auto existing_id = existing.id;
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(existing)).success);

    PathObject added;
    added.id = generate_object_id();
    added.path.move_to({20, 0});
    added.path.line_to({30, 0});
    const auto added_id = added.id;

    auto& writer = doc.writer();
    writer.begin_transaction("No-op appearance and add object");
    assert(writer.stage_command(std::make_unique<SetAppearanceCommand>(
        std::vector<AppearanceReplacement>{{existing_id, existing.appearance}})).success);
    assert(writer.stage_command(std::make_unique<AddObjectCommand>(added)).success);
    const auto committed = writer.commit_transaction();
    assert(committed.success && !committed.is_no_change);
    assert(doc.revision() == 2 && doc.current_snapshot()->object_count() == 2);

    assert(doc.undo());
    assert(doc.revision() == 3 && doc.current_snapshot()->object_count() == 1);
    assert(doc.current_snapshot()->get_object(existing_id));
    assert(!doc.current_snapshot()->get_object(added_id));
    assert(doc.redo());
    assert(doc.revision() == 4 && doc.current_snapshot()->object_count() == 2);
    assert(doc.current_snapshot()->get_object(added_id));
}

void test_editable_node_move_preserves_identity_and_style() {
    Document doc("doc-node-move");
    auto object = make_authoring_object({{0, 0}, {100, 0}, {150, 40}});
    object.stroke_width_mm = 3.25;
    object.name = "Editable curve";
    const auto object_id = object.id;
    auto original = *object.authoring_source;
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(object)).success);
    const auto target = original.subpaths.front().nodes[1];

    auto move = std::make_unique<MovePathNodeCommand>(object_id, original.subpaths.front().id,
                                                       target.id, geometry::Point2D{100, 25});
    assert(doc.execute_command(std::move(move)).success);
    assert(doc.revision() == 2);
    auto moved = snapshot_path(doc, object_id);
    const auto& moved_source = *moved.authoring_source;
    assert(moved_source.subpaths.front().nodes[1].id == target.id);
    assert((moved_source.subpaths.front().nodes[1].anchor == geometry::Point2D{100, 25}));
    assert(moved.stroke_width_mm == 3.25 && moved.name == "Editable curve");
    assert(editable_path_matches_adapter(moved_source, moved.path));
    for (size_t i = 0; i < original.subpaths.front().nodes.size(); ++i)
        assert(moved_source.subpaths.front().nodes[i].id == original.subpaths.front().nodes[i].id);

    assert(doc.undo());
    assert(doc.revision() == 3);
    assert((snapshot_path(doc, object_id).authoring_source->subpaths.front().nodes[1].anchor ==
            geometry::Point2D{100, 0}));
    assert(doc.redo());
    assert(doc.revision() == 4);
    assert((snapshot_path(doc, object_id).authoring_source->subpaths.front().nodes[1].anchor ==
            geometry::Point2D{100, 25}));
}

void test_split_line_matches_identity_vector_and_undo() {
    // Consumer case: contracts/drawing/vectors.json:path_identity/split_line_preserves_endpoint_ids.
    Document doc("doc-split-line");
    auto object = make_authoring_object({{0, 0}, {100, 0}});
    const auto object_id = object.id;
    const auto before = *object.authoring_source;
    const auto& subpath = before.subpaths.front();
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(object)).success);
    auto mapping = std::make_shared<PathTopologyMapping>();
    auto result = doc.execute_command(std::make_unique<SplitPathSegmentCommand>(
        object_id, subpath.id, subpath.segments.front().id, 0.25, mapping));
    assert(result.success && doc.revision() == 2);
    auto after = *snapshot_path(doc, object_id).authoring_source;
    const auto& split = after.subpaths.front();
    assert(split.id == subpath.id && split.nodes.size() == 3 && split.segments.size() == 2);
    assert(split.nodes[0].id == subpath.nodes[0].id);
    assert((split.nodes[1].anchor == geometry::Point2D{25, 0}));
    assert(split.nodes[2].id == subpath.nodes[1].id);
    assert(split.segments[0].id == subpath.segments[0].id);
    assert(split.segments[1].id != subpath.segments[0].id);
    assert(mapping->elements.size() == 3);
    assert(mapping->elements[0].relation == IdMappingRelation::Preserved);
    assert(mapping->elements[1].relation == IdMappingRelation::Created);
    assert(mapping->elements[2].relation == IdMappingRelation::Created);
    assert(editable_path_matches_adapter(*snapshot_path(doc, object_id).authoring_source,
        snapshot_path(doc, object_id).path));

    assert(doc.undo());
    const auto undone = *snapshot_path(doc, object_id).authoring_source;
    assert(undone.subpaths.front().nodes.size() == 2);
    assert(undone.subpaths.front().nodes[1].id == subpath.nodes[1].id);
    assert(doc.redo());
    const auto redone = *snapshot_path(doc, object_id).authoring_source;
    assert(redone.subpaths.front().nodes[1].id == after.subpaths.front().nodes[1].id);

    const auto revision = doc.revision();
    auto invalid = doc.execute_command(std::make_unique<SplitPathSegmentCommand>(
        object_id, subpath.id, subpath.segments.front().id, 0.0));
    assert(!invalid.success && doc.revision() == revision);
}

void test_analytic_arc_split_keeps_arc_source() {
    // Consumer case: path_identity/split_arc_preserves_analytic_source and quarter_circle_midpoint.
    const geometry::EllipticArc arc{{2, 3}, {10, 0}, {0, 10}, 0.0, 1.57079632679489661923};
    const auto encoded = geometry::encode_endpoint_arc(arc);
    assert(encoded);
    EditablePath source;
    EditableSubpath subpath;
    subpath.id = generate_path_element_id<SubpathId>();
    subpath.nodes.push_back(PathNode{generate_path_element_id<NodeId>(), arc.point_at(0)});
    subpath.nodes.push_back(PathNode{generate_path_element_id<NodeId>(), arc.point_at(1)});
    const auto arc_id = generate_path_element_id<SegmentId>();
    subpath.segments.push_back(EditableSegment{arc_id, PathSegmentKind::EllipticArc, {}, arc});
    source.subpaths.push_back(subpath);
    PathObject object;
    object.id = generate_object_id();
    object.set_authoring_source(source);
    const auto object_id = object.id;
    Document doc("doc-split-arc");
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(object)).success);

    assert(doc.execute_command(std::make_unique<SplitPathSegmentCommand>(
        object_id, subpath.id, arc_id, 0.5)).success);
    const auto result = snapshot_path(doc, object_id);
    const auto& split = result.authoring_source->subpaths.front();
    const geometry::Point2D expected{9.071067811865476, 10.071067811865476};
    assert(std::abs(split.nodes[1].anchor.x - expected.x) < 1e-10);
    assert(std::abs(split.nodes[1].anchor.y - expected.y) < 1e-10);
    assert(split.segments.size() == 2);
    assert(split.segments[0].kind == PathSegmentKind::EllipticArc);
    assert(split.segments[1].kind == PathSegmentKind::EllipticArc);
    assert(split.segments[0].arc.center == arc.center);
    assert(split.segments[0].arc.axis_u == arc.axis_u);
    assert(split.segments[0].arc.axis_v == arc.axis_v);
    assert(std::abs(split.segments[0].arc.sweep_angle_rad - arc.sweep_angle_rad * 0.5) < 1e-15);
    assert(std::abs(split.segments[1].arc.sweep_angle_rad - arc.sweep_angle_rad * 0.5) < 1e-15);
    for (double t : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        assert(split.segments[0].arc.point_at(t).distance_to(arc.point_at(t * 0.5)) < 1e-10);
        assert(split.segments[1].arc.point_at(t).distance_to(arc.point_at(0.5 + t * 0.5)) < 1e-10);
    }
    size_t arc_verbs = 0;
    for (const auto& element : result.path.elements())
        if (element.verb == geometry::PathVerb::ArcTo) ++arc_verbs;
    assert(arc_verbs == 2 && split.segments[0].id == arc_id);
}

void test_cubic_and_quadratic_splits_are_exact() {
    const geometry::CubicBezier original_cubic{{0, 0}, {20, 80}, {70, -30}, {100, 10}};
    EditablePath cubic_source;
    EditableSubpath cubic_path;
    cubic_path.id = generate_path_element_id<SubpathId>();
    PathNode cubic_start{generate_path_element_id<NodeId>(), original_cubic.p0};
    cubic_start.mode = PathNodeMode::Cusp;
    cubic_start.has_outgoing_handle = true;
    cubic_start.outgoing_handle = original_cubic.p1;
    PathNode cubic_end{generate_path_element_id<NodeId>(), original_cubic.p3};
    cubic_end.mode = PathNodeMode::Cusp;
    cubic_end.has_incoming_handle = true;
    cubic_end.incoming_handle = original_cubic.p2;
    cubic_path.nodes = {cubic_start, cubic_end};
    const auto cubic_segment_id = generate_path_element_id<SegmentId>();
    cubic_path.segments.push_back({cubic_segment_id, PathSegmentKind::CubicBezier});
    cubic_source.subpaths.push_back(cubic_path);
    PathObject cubic_object;
    cubic_object.id = generate_object_id();
    cubic_object.set_authoring_source(cubic_source);
    const auto cubic_object_id = cubic_object.id;
    Document cubic_doc("doc-split-cubic");
    assert(cubic_doc.execute_command(std::make_unique<AddObjectCommand>(cubic_object)).success);
    assert(cubic_doc.execute_command(std::make_unique<SplitPathSegmentCommand>(
        cubic_object_id, cubic_path.id, cubic_segment_id, 0.37)).success);
    const auto cubic_after = *snapshot_path(cubic_doc, cubic_object_id).authoring_source;
    const auto& split_cubic = cubic_after.subpaths.front();
    const auto [start_in, start_out] = editable_path_handles_for(split_cubic, 0);
    const auto [middle_in, middle_out] = editable_path_handles_for(split_cubic, 1);
    const auto [end_in, end_out] = editable_path_handles_for(split_cubic, 2);
    (void)start_in;
    (void)end_out;
    const geometry::CubicBezier actual_first{
        split_cubic.nodes[0].anchor, start_out, middle_in, split_cubic.nodes[1].anchor};
    const geometry::CubicBezier actual_second{
        split_cubic.nodes[1].anchor, middle_out, end_in, split_cubic.nodes[2].anchor};
    const auto expected_split = geometry::split_curve(geometry::CurveSegment{original_cubic}, 0.37);
    assert(expected_split.status == geometry::GeometryOperationStatus::Success);
    for (double t : {0.0, 0.2, 0.5, 0.8, 1.0}) {
        const auto expected_first = geometry::curve_point_at(*expected_split.first, t);
        const auto actual_first_point = actual_first.point_at(t);
        const auto expected_second = geometry::curve_point_at(*expected_split.second, t);
        const auto actual_second_point = actual_second.point_at(t);
        assert(expected_first.distance_to(actual_first_point) < 1e-10);
        assert(expected_second.distance_to(actual_second_point) < 1e-10);
    }

    const geometry::QuadraticBezier original_quad{{0, 0}, {40, 75}, {100, 5}};
    EditableSubpath quad_path;
    quad_path.id = generate_path_element_id<SubpathId>();
    quad_path.nodes = {
        PathNode{generate_path_element_id<NodeId>(), original_quad.p0},
        PathNode{generate_path_element_id<NodeId>(), original_quad.p2}};
    const auto quad_segment_id = generate_path_element_id<SegmentId>();
    quad_path.segments.push_back({quad_segment_id, PathSegmentKind::QuadraticBezier,
                                  original_quad.p1});
    EditablePath quad_source;
    quad_source.subpaths.push_back(quad_path);
    PathObject quad_object;
    quad_object.id = generate_object_id();
    quad_object.set_authoring_source(quad_source);
    const auto quad_object_id = quad_object.id;
    Document quad_doc("doc-split-quadratic");
    assert(quad_doc.execute_command(std::make_unique<AddObjectCommand>(quad_object)).success);
    assert(quad_doc.execute_command(std::make_unique<SplitPathSegmentCommand>(
        quad_object_id, quad_path.id, quad_segment_id, 0.63)).success);
    const auto quad_after = *snapshot_path(quad_doc, quad_object_id).authoring_source;
    const auto& split_quad = quad_after.subpaths.front();
    const auto expected_quad = geometry::split_curve(geometry::CurveSegment{original_quad}, 0.63);
    assert(expected_quad.status == geometry::GeometryOperationStatus::Success);
    for (double t : {0.0, 0.3, 0.7, 1.0}) {
        const geometry::QuadraticBezier actual_first_curve{
            split_quad.nodes[0].anchor, split_quad.segments[0].quadratic_control,
            split_quad.nodes[1].anchor};
        const geometry::QuadraticBezier actual_second_curve{
            split_quad.nodes[1].anchor, split_quad.segments[1].quadratic_control,
            split_quad.nodes[2].anchor};
        assert(geometry::curve_point_at(*expected_quad.first, t).distance_to(
               actual_first_curve.point_at(t)) < 1e-10);
        assert(geometry::curve_point_at(*expected_quad.second, t).distance_to(
               actual_second_curve.point_at(t)) < 1e-10);
    }
}

void test_delete_node_preserves_survivor_and_reports_removed_ids() {
    Document doc("doc-delete-node");
    auto object = make_authoring_object({{0, 0}, {50, 30}, {100, 0}});
    const auto object_id = object.id;
    const auto source = *object.authoring_source;
    const auto& before = source.subpaths.front();
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(object)).success);
    auto mapping = std::make_shared<PathTopologyMapping>();
    assert(doc.execute_command(std::make_unique<DeletePathNodeCommand>(
        object_id, before.id, before.nodes[1].id, mapping)).success);
    const auto after = *snapshot_path(doc, object_id).authoring_source;
    const auto& remaining = after.subpaths.front();
    assert(remaining.nodes.size() == 2 && remaining.segments.size() == 1);
    assert(remaining.nodes[0].id == before.nodes[0].id);
    assert(remaining.nodes[1].id == before.nodes[2].id);
    assert(remaining.segments[0].id == before.segments[0].id);
    assert(remaining.segments[0].kind == PathSegmentKind::Line);
    assert(mapping->elements.size() == 3);
    assert(mapping->elements[0].relation == IdMappingRelation::Preserved);
    assert(mapping->elements[1].before == before.segments[1].id.value());
    assert(mapping->elements[1].relation == IdMappingRelation::Removed);
    assert(mapping->elements[2].before == before.nodes[1].id.value());
    assert(mapping->elements[2].relation == IdMappingRelation::Removed);
    assert(doc.undo());
    assert(snapshot_path(doc, object_id).authoring_source->subpaths.front().nodes.size() == 3);
}

void test_adapter_conversion_and_document_identity_uniqueness() {
    auto path = geometry::Path2D::make_ellipse(15, 20, 8, 4);
    std::string error;
    auto source = editable_path_from_path2d(path, &error);
    assert(source && error.empty());
    assert(editable_path_matches_adapter(*source, path));

    Document doc("doc-identity-unique");
    auto first = make_authoring_object({{0, 0}, {5, 5}});
    const auto duplicated_source = first.authoring_source;
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(first)).success);
    PathObject duplicate;
    duplicate.id = generate_object_id();
    duplicate.authoring_source = duplicated_source;
    duplicate.path = first.path;
    const auto revision = doc.revision();
    const auto rejected = doc.execute_command(std::make_unique<AddObjectCommand>(duplicate));
    assert(!rejected.success && doc.revision() == revision);

    PathObject object_collision;
    object_collision.id = generate_object_id();
    EditablePath collision_source;
    auto collision = make_line_subpath({{0, 0}, {1, 1}});
    collision.id = SubpathId{object_collision.id};
    collision_source.subpaths.push_back(collision);
    object_collision.set_authoring_source(std::move(collision_source));
    const auto collision_result = doc.execute_command(std::make_unique<AddObjectCommand>(object_collision));
    assert(!collision_result.success && doc.revision() == revision);
}

void test_join_requires_bridge_or_explicit_survivor() {
    Document doc("doc-join-paths");
    auto object = make_authoring_object({{0, 0}, {10, 0}});
    EditablePath source = *object.authoring_source;
    const auto first_id = source.subpaths.front().id;
    const auto first_end = source.subpaths.front().nodes.back().id;
    auto second = make_line_subpath({{20, 0}, {30, 0}});
    const auto second_id = second.id;
    const auto second_start = second.nodes.front().id;
    source.subpaths.push_back(second);
    object.set_authoring_source(std::move(source));
    const auto object_id = object.id;
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(object)).success);
    const auto revision = doc.revision();

    auto missing_bridge = doc.execute_command(std::make_unique<JoinPathSubpathsCommand>(
        object_id, first_id, first_end, second_id, second_start, false));
    assert(!missing_bridge.success && doc.revision() == revision);
    auto mapping = std::make_shared<PathTopologyMapping>();
    assert(doc.execute_command(std::make_unique<JoinPathSubpathsCommand>(
        object_id, first_id, first_end, second_id, second_start, true, NodeId{}, mapping)).success);
    const auto joined = snapshot_path(doc, object_id);
    const auto& joined_source = *joined.authoring_source;
    assert(joined_source.subpaths.size() == 1);
    assert(joined_source.subpaths.front().id == first_id);
    assert(joined_source.subpaths.front().nodes.size() == 4);
    assert(joined_source.subpaths.front().segments.size() == 3);
    assert(joined_source.subpaths.front().segments[1].kind == PathSegmentKind::Line);
    assert(mapping->elements.size() == 2);
    assert(mapping->elements[0].relation == IdMappingRelation::Created);
    assert(mapping->elements[1].relation == IdMappingRelation::Merged);

    Document coincident_doc("doc-join-coincident");
    auto coincident = make_authoring_object({{0, 0}, {10, 0}});
    EditablePath coincident_source = *coincident.authoring_source;
    const auto a_id = coincident_source.subpaths.front().id;
    const auto a_end = coincident_source.subpaths.front().nodes.back().id;
    auto tail = make_line_subpath({{10, 0}, {20, 0}});
    const auto b_id = tail.id;
    const auto b_start = tail.nodes.front().id;
    coincident_source.subpaths.push_back(tail);
    coincident.set_authoring_source(std::move(coincident_source));
    const auto coincident_object_id = coincident.id;
    assert(coincident_doc.execute_command(std::make_unique<AddObjectCommand>(coincident)).success);
    auto missing_survivor = coincident_doc.execute_command(std::make_unique<JoinPathSubpathsCommand>(
        coincident_object_id, a_id, a_end, b_id, b_start, false));
    assert(!missing_survivor.success && coincident_doc.revision() == 1);
    auto merge_map = std::make_shared<PathTopologyMapping>();
    assert(coincident_doc.execute_command(std::make_unique<JoinPathSubpathsCommand>(
        coincident_object_id, a_id, a_end, b_id, b_start, false, a_end, merge_map)).success);
    assert(snapshot_path(coincident_doc, coincident_object_id).authoring_source->subpaths.front().nodes.size() == 3);
    assert(merge_map->elements.front().before == b_start.value());
    assert(merge_map->elements.front().after == a_end.value());
}

void test_close_zero_length_requires_explicit_target() {
    Document doc("doc-close-path");
    auto object = make_authoring_object({{4, 7}, {20, 7}, {4, 7}});
    EditablePath source = *object.authoring_source;
    const auto subpath_id = source.subpaths.front().id;
    const auto first_id = source.subpaths.front().nodes.front().id;
    const auto object_id = object.id;
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(object)).success);
    auto no_target = doc.execute_command(std::make_unique<ClosePathSubpathCommand>(object_id, subpath_id));
    assert(!no_target.success && doc.revision() == 1);
    auto mapping = std::make_shared<PathTopologyMapping>();
    assert(doc.execute_command(std::make_unique<ClosePathSubpathCommand>(
        object_id, subpath_id, first_id, mapping)).success);
    const auto closed = snapshot_path(doc, object_id);
    assert(closed.authoring_source->subpaths.front().closed);
    assert(closed.authoring_source->subpaths.front().nodes.size() == 2);
    assert(closed.authoring_source->subpaths.front().segments.size() == 2);
    assert(mapping->elements.front().relation == IdMappingRelation::Merged);
    assert(editable_path_matches_adapter(*closed.authoring_source, closed.path));
}

void test_hidden_locked_and_failed_composite_edits_do_not_publish() {
    Document hidden_layer_doc("doc-hidden-layer", [] {
        auto page = Page::make_default();
        page.layers.front().visible = false;
        return page;
    }());
    auto hidden_object = make_authoring_object({{0, 0}, {10, 0}});
    const auto hidden_id = hidden_object.id;
    const auto hidden_source = *hidden_object.authoring_source;
    assert(hidden_layer_doc.execute_command(std::make_unique<AddObjectCommand>(hidden_object)).success);
    auto hidden_edit = hidden_layer_doc.execute_command(std::make_unique<MovePathNodeCommand>(
        hidden_id, hidden_source.subpaths.front().id, hidden_source.subpaths.front().nodes.front().id,
        geometry::Point2D{5, 5}));
    assert(!hidden_edit.success && hidden_layer_doc.revision() == 1);

    Document locked_doc("doc-locked-path");
    auto locked_object = make_authoring_object({{0, 0}, {10, 0}});
    locked_object.locked = true;
    const auto locked_id = locked_object.id;
    const auto locked_source = *locked_object.authoring_source;
    assert(locked_doc.execute_command(std::make_unique<AddObjectCommand>(locked_object)).success);
    auto locked_edit = locked_doc.execute_command(std::make_unique<MovePathNodeCommand>(
        locked_id, locked_source.subpaths.front().id, locked_source.subpaths.front().nodes.front().id,
        geometry::Point2D{5, 5}));
    assert(!locked_edit.success && locked_doc.revision() == 1);

    Document rollback_doc("doc-path-rollback");
    auto object = make_authoring_object({{0, 0}, {10, 0}});
    const auto object_id = object.id;
    const auto source = *object.authoring_source;
    assert(rollback_doc.execute_command(std::make_unique<AddObjectCommand>(object)).success);
    auto& writer = rollback_doc.writer();
    writer.begin_transaction("Move then reject invalid split");
    assert(writer.stage_command(std::make_unique<MovePathNodeCommand>(
        object_id, source.subpaths.front().id, source.subpaths.front().nodes.front().id,
        geometry::Point2D{5, 5})).success);
    auto rolled_back_mapping = std::make_shared<PathTopologyMapping>();
    assert(writer.stage_command(std::make_unique<SplitPathSegmentCommand>(
        object_id, source.subpaths.front().id, source.subpaths.front().segments.front().id,
        0.5, rolled_back_mapping)).success);
    assert(writer.stage_command(std::make_unique<SplitPathSegmentCommand>(
        object_id, source.subpaths.front().id, source.subpaths.front().segments.front().id, 1.0)).success);
    const auto rollback_result = writer.commit_transaction();
    assert(!rollback_result.success && rollback_doc.revision() == 1);
    assert((snapshot_path(rollback_doc, object_id).authoring_source->subpaths.front().nodes.front().anchor ==
            geometry::Point2D{0, 0}));
    assert(rolled_back_mapping->elements.empty());
}

void test_import_content_preserves_layers_and_undoes_atomically() {
    Document doc("import-command-test", Page::make_default());
    const auto original_layers = doc.current_snapshot()->page().layers.size();
    const auto original_width_mm = doc.current_snapshot()->page().width_mm;
    const auto original_height_mm = doc.current_snapshot()->page().height_mm;

    PathObject path;
    path.id = generate_object_id();
    path.name = "Imported path";
    path.path.move_to(0, 0);
    path.path.line_to(10, 5);
    GroupObject group;
    group.id = generate_object_id();
    group.name = "Imported group";
    group.children_ids.push_back(path.id);
    Layer source_layer{"Source layer", false, true, {group.id}};

    std::vector<ObjectVariant> objects;
    objects.emplace_back(path);
    objects.emplace_back(group);
    const auto imported_group_id = group.id;
    auto result = doc.execute_command(std::make_unique<ImportContentCommand>(
        std::vector<Layer>{source_layer}, std::move(objects), std::pair{400.0, 200.0}));
    if (!result.success) std::cerr << "ImportContentCommand rejected valid hierarchy: " << result.error_message << '\n';
    assert(result.success && doc.revision() == 1);
    assert(doc.current_snapshot()->page().layers.size() == original_layers + 1);
    assert(doc.current_snapshot()->page().layers.back().name == "Source layer");
    assert(!doc.current_snapshot()->page().layers.back().visible);
    assert(doc.current_snapshot()->page().layers.back().locked);
    assert(doc.current_snapshot()->page().width_mm == 400.0);
    assert(doc.current_snapshot()->page().height_mm == 200.0);
    assert(doc.current_snapshot()->object_count() == 2);
    assert(doc.undo());
    assert(doc.current_snapshot()->page().layers.size() == original_layers);
    assert(doc.current_snapshot()->page().width_mm == original_width_mm);
    assert(doc.current_snapshot()->page().height_mm == original_height_mm);
    assert(doc.current_snapshot()->object_count() == 0);
    assert(doc.redo());
    assert(doc.current_snapshot()->page().width_mm == 400.0);
    assert(doc.current_snapshot()->page().height_mm == 200.0);
    const auto restored = doc.current_snapshot()->get_object(imported_group_id);
    assert(restored && std::get<GroupObject>(*restored).children_ids.size() == 1);

    Document rejected("invalid-import-test", Page::make_default());
    PathObject orphan;
    orphan.id = generate_object_id();
    orphan.path.move_to(0, 0);
    orphan.path.line_to(1, 1);
    Layer invalid_layer{"Invalid", true, false, {}};
    auto rejected_result = rejected.execute_command(std::make_unique<ImportContentCommand>(
        std::vector<Layer>{invalid_layer}, std::vector<ObjectVariant>{orphan}));
    assert(!rejected_result.success);
    assert(rejected.revision() == 0 && rejected.current_snapshot()->object_count() == 0);
    assert(rejected.current_snapshot()->page().layers.size() == 1);

    Document oversized_source("oversized-import-page-test", Page::make_default());
    PathObject oversized_path;
    oversized_path.id = generate_object_id();
    oversized_path.path.move_to(0, 0);
    oversized_path.path.line_to(1, 1);
    Layer oversized_layer{"Oversized source", true, false, {oversized_path.id}};
    const auto original_page_width = oversized_source.current_snapshot()->page().width_mm;
    const auto oversized_result = oversized_source.execute_command(
        std::make_unique<ImportContentCommand>(
            std::vector<Layer>{oversized_layer},
            std::vector<ObjectVariant>{oversized_path},
            std::pair{geometry::limits::kCanvasMaxMm + 1.0, 200.0}));
    assert(!oversized_result.success);
    assert(oversized_source.revision() == 0);
    assert(oversized_source.current_snapshot()->page().width_mm == original_page_width);
    assert(oversized_source.current_snapshot()->object_count() == 0);
}

void test_import_pages_is_atomic_and_keeps_page_and_object_ids_on_redo() {
    Document doc("import-pages-command-test", Page::make_default());
    const PageId first_existing_id = doc.active_page_id();
    const auto second_existing = make_document_page("Existing page 2");
    assert(doc.execute_command(std::make_unique<AddPageCommand>(second_existing)).success);
    const PageId second_existing_id = second_existing.id;

    ImportedPageContent artwork_page;
    artwork_page.name = "Imported artwork";
    artwork_page.layout = Page::make_default();
    artwork_page.layout.width_mm = 210.0;
    artwork_page.layout.height_mm = 297.0;
    PathObject artwork;
    artwork.id = generate_object_id();
    artwork.name = "Page one path";
    artwork.path.move_to({5.0, 8.0});
    artwork.path.line_to({25.0, 8.0});
    const ObjectId artwork_id = artwork.id;
    artwork_page.layout.layers.front().object_ids.push_back(artwork_id);
    artwork_page.objects.emplace_back(artwork);

    ImportedPageContent blank_page;
    blank_page.name = "Blank source page";
    blank_page.layout = Page::make_default();
    blank_page.layout.width_mm = 100.0;
    blank_page.layout.height_mm = 80.0;

    const uint64_t revision_before = doc.revision();
    const auto result = doc.execute_command(std::make_unique<ImportPagesCommand>(
        std::vector<ImportedPageContent>{artwork_page, blank_page}, first_existing_id));
    if (!result.success) std::cerr << "ImportPagesCommand rejected valid pages: " << result.error_message << '\n';
    assert(result.success && doc.revision() == revision_before + 1);
    auto imported = doc.current_snapshot();
    assert(imported->page_count() == 4);
    assert(imported->active_page_id() == second_existing_id);
    assert(imported->pages()[0].id == first_existing_id);
    assert(imported->pages()[1].name == "Imported artwork");
    assert(imported->pages()[1].layout.width_mm == 210.0);
    assert(imported->pages()[1].objects.contains(artwork_id));
    assert(imported->pages()[2].name == "Blank source page");
    assert(imported->pages()[2].objects.empty());
    assert(imported->pages()[3].id == second_existing_id);
    const PageId imported_artwork_page_id = imported->pages()[1].id;
    const PageId imported_blank_page_id = imported->pages()[2].id;

    assert(doc.undo());
    assert(doc.current_snapshot()->page_count() == 2);
    assert(doc.current_snapshot()->active_page_id() == second_existing_id);
    assert(doc.redo());
    imported = doc.current_snapshot();
    assert(imported->page_count() == 4);
    assert(imported->pages()[1].id == imported_artwork_page_id);
    assert(imported->pages()[2].id == imported_blank_page_id);
    assert(imported->pages()[1].objects.contains(artwork_id));

    Document rejected("invalid-import-pages-test", Page::make_default());
    const auto before_reject = rejected.current_snapshot();
    auto invalid_page = blank_page;
    invalid_page.layout.width_mm = 0.0;
    const auto invalid_result = rejected.execute_command(std::make_unique<ImportPagesCommand>(
        std::vector<ImportedPageContent>{artwork_page, invalid_page}, kInvalidPageId));
    assert(!invalid_result.success);
    assert(rejected.revision() == 0);
    assert(rejected.current_snapshot() == before_reject);
    assert(rejected.current_snapshot()->page_count() == 1);

    Document opened("replace-import-pages-test", Page::make_default());
    const PageId old_page_id = opened.active_page_id();
    const auto replace_result = opened.execute_command(std::make_unique<ImportPagesCommand>(
        std::vector<ImportedPageContent>{blank_page}, kInvalidPageId, true));
    assert(replace_result.success);
    assert(opened.current_snapshot()->page_count() == 1);
    assert(opened.current_snapshot()->pages().front().name == "Blank source page");
    assert(opened.current_snapshot()->pages().front().id != old_page_id);
    assert(opened.undo());
    assert(opened.current_snapshot()->page_count() == 1);
    assert(opened.current_snapshot()->pages().front().id == old_page_id);
}

void test_pdf_print_locked_document_rejects_content_mutation() {
    auto asset = std::make_shared<PdfSourceAsset>();
    asset->sha256_hex = std::string(64, 'a');
    asset->page_count = 2;
    asset->bytes = std::make_shared<const std::vector<uint8_t>>(
        std::vector<uint8_t>{'%', 'P', 'D', 'F', '-', '1', '.', '7'});

    Page first_layout = Page::make_default();
    first_layout.width_mm = 210.0;
    first_layout.height_mm = 297.0;
    Page second_layout = first_layout;
    second_layout.width_mm = 297.0;
    second_layout.height_mm = 210.0;
    auto first = make_document_page("PDF Page 1", first_layout);
    auto second = make_document_page("PDF Page 2", second_layout);
    first.pdf_source = PdfSourcePageReference{asset, 0, 0};
    second.pdf_source = PdfSourcePageReference{asset, 1, 1};
    DocumentSnapshot locked_snapshot{1, 0, {first, second}, first.id, "pdf-print-locked",
        DocumentContentMode::PdfPrintLocked};
    Document doc(locked_snapshot);

    PathObject path;
    path.id = generate_object_id();
    path.path.move_to({0.0, 0.0});
    path.path.line_to({10.0, 0.0});
    const auto before = doc.current_snapshot();
    const auto rejected = doc.execute_command(std::make_unique<AddObjectCommand>(path));
    assert(!rejected.success && rejected.error_message.find("locked") != std::string::npos);
    assert(doc.current_snapshot() == before && doc.revision() == 0);
    assert(!doc.undo() && !doc.redo());

    bool writer_denied = false;
    try {
        static_cast<void>(doc.writer());
    } catch (const std::logic_error&) {
        writer_denied = true;
    }
    assert(writer_denied);
    assert(doc.set_active_page(second.id));
    assert(doc.current_snapshot()->active_page_id() == second.id);
    assert(doc.current_snapshot()->is_content_locked());
}

void test_document_color_commands();
int main() {
    test_document_color_commands();
    std::cout << "[test_commands] Running command tests...\n";
    test_add_and_undo_path();
    test_remove_and_transform();
    test_transaction_writer();
    test_appearance_command_undo_and_noop_history();
    test_composite_skips_noop_children_for_undo();
    test_editable_node_move_preserves_identity_and_style();
    test_split_line_matches_identity_vector_and_undo();
    test_analytic_arc_split_keeps_arc_source();
    test_cubic_and_quadratic_splits_are_exact();
    test_delete_node_preserves_survivor_and_reports_removed_ids();
    test_adapter_conversion_and_document_identity_uniqueness();
    test_join_requires_bridge_or_explicit_survivor();
    test_close_zero_length_requires_explicit_target();
    test_hidden_locked_and_failed_composite_edits_do_not_publish();
    test_import_content_preserves_layers_and_undoes_atomically();
    test_import_pages_is_atomic_and_keeps_page_and_object_ids_on_redo();
    test_pdf_print_locked_document_rejects_content_mutation();
    std::cout << "[test_commands] All tests passed!\n";
    return 0;
}
