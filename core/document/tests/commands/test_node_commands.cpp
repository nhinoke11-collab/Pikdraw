#include "pikdraw/document/document.hpp"
#include "pikdraw/document/commands/node_commands.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

using namespace pikdraw;
using namespace pikdraw::document;

static PathObject make_test_triangle() {
    PathObject obj;
    obj.id = generate_object_id();
    EditablePath path;
    EditableSubpath sub;
    sub.id = generate_path_element_id<SubpathId>();
    sub.closed = true;

    PathNode n1{generate_path_element_id<NodeId>(), {0.0, 0.0}, PathNodeMode::Corner};
    PathNode n2{generate_path_element_id<NodeId>(), {100.0, 0.0}, PathNodeMode::Corner};
    PathNode n3{generate_path_element_id<NodeId>(), {50.0, 80.0}, PathNodeMode::Corner};
    sub.nodes = {n1, n2, n3};

    sub.segments.push_back({generate_path_element_id<SegmentId>(), PathSegmentKind::Line});
    sub.segments.push_back({generate_path_element_id<SegmentId>(), PathSegmentKind::Line});
    sub.segments.push_back({generate_path_element_id<SegmentId>(), PathSegmentKind::Line});

    path.subpaths.push_back(std::move(sub));
    obj.set_authoring_source(std::move(path));
    return obj;
}

static PathObject make_test_open_line() {
    PathObject obj;
    obj.id = generate_object_id();
    EditablePath path;
    EditableSubpath sub;
    sub.id = generate_path_element_id<SubpathId>();
    sub.closed = false;

    PathNode n1{generate_path_element_id<NodeId>(), {0.0, 0.0}, PathNodeMode::Corner};
    PathNode n2{generate_path_element_id<NodeId>(), {50.0, 20.0}, PathNodeMode::Corner};
    PathNode n3{generate_path_element_id<NodeId>(), {100.0, 0.0}, PathNodeMode::Corner};
    sub.nodes = {n1, n2, n3};

    sub.segments.push_back({generate_path_element_id<SegmentId>(), PathSegmentKind::Line});
    sub.segments.push_back({generate_path_element_id<SegmentId>(), PathSegmentKind::Line});

    path.subpaths.push_back(std::move(sub));
    obj.set_authoring_source(std::move(path));
    return obj;
}

void test_move_node_handle_cusp_smooth_symmetric() {
    Document doc("doc-handle-test");
    auto obj = make_test_triangle();
    const auto obj_id = obj.id;
    const auto sub_id = obj.authoring_source->subpaths[0].id;
    const auto node_id = obj.authoring_source->subpaths[0].nodes[0].id;
    doc.execute_command(std::make_unique<AddObjectCommand>(obj));

    // 1. Set mode to Cusp and move outgoing handle
    assert(doc.execute_command(std::make_unique<SetNodeModeCommand>(
        obj_id, sub_id, node_id, PathNodeMode::Cusp)).success);
    assert(doc.execute_command(std::make_unique<MoveNodeHandleCommand>(
        obj_id, sub_id, node_id, false, geometry::Point2D{10.0, 20.0})).success);

    auto snap = doc.current_snapshot();
    auto cur_obj = std::get<PathObject>(*snap->get_object(obj_id));
    const auto& node_after = cur_obj.authoring_source->subpaths[0].nodes[0];
    assert(node_after.has_outgoing_handle);
    assert(std::abs(node_after.outgoing_handle.x - 10.0) < 1e-6);
    assert(std::abs(node_after.outgoing_handle.y - 20.0) < 1e-6);

    // 2. Set mode to Symmetric
    assert(doc.execute_command(std::make_unique<SetNodeModeCommand>(
        obj_id, sub_id, node_id, PathNodeMode::Symmetric)).success);
    snap = doc.current_snapshot();
    cur_obj = std::get<PathObject>(*snap->get_object(obj_id));
    const auto& sym_node = cur_obj.authoring_source->subpaths[0].nodes[0];
    assert(sym_node.has_incoming_handle && sym_node.has_outgoing_handle);
    // In symmetric, incoming and outgoing handles are opposing and equal length
    const auto v_in = sym_node.incoming_handle - sym_node.anchor;
    const auto v_out = sym_node.outgoing_handle - sym_node.anchor;
    assert(std::abs(v_in.x + v_out.x) < 1e-5);
    assert(std::abs(v_in.y + v_out.y) < 1e-5);

    // 3. Move incoming handle on Symmetric node
    assert(doc.execute_command(std::make_unique<MoveNodeHandleCommand>(
        obj_id, sub_id, node_id, true, geometry::Point2D{-30.0, -40.0})).success);
    snap = doc.current_snapshot();
    cur_obj = std::get<PathObject>(*snap->get_object(obj_id));
    const auto& sym_moved = cur_obj.authoring_source->subpaths[0].nodes[0];
    assert(std::abs(sym_moved.incoming_handle.x - (-30.0)) < 1e-6);
    assert(std::abs(sym_moved.incoming_handle.y - (-40.0)) < 1e-6);
    assert(std::abs(sym_moved.outgoing_handle.x - 30.0) < 1e-6);
    assert(std::abs(sym_moved.outgoing_handle.y - 40.0) < 1e-6);

    // 4. Undo and Redo
    assert(doc.undo());
    assert(doc.redo());
}

void test_break_path_at_node() {
    Document doc("doc-break-test");
    // Test breaking closed path -> becomes open
    auto closed_obj = make_test_triangle();
    const auto closed_id = closed_obj.id;
    const auto sub_id = closed_obj.authoring_source->subpaths[0].id;
    const auto node_id = closed_obj.authoring_source->subpaths[0].nodes[1].id;
    doc.execute_command(std::make_unique<AddObjectCommand>(closed_obj));

    assert(doc.execute_command(std::make_unique<BreakPathAtNodeCommand>(
        closed_id, sub_id, node_id)).success);
    auto snap = doc.current_snapshot();
    auto after_obj = std::get<PathObject>(*snap->get_object(closed_id));
    assert(!after_obj.authoring_source->subpaths[0].closed);
    assert(after_obj.authoring_source->subpaths[0].nodes[0].id == node_id);
    assert(after_obj.authoring_source->subpaths[0].nodes.size() == 4);
    assert(after_obj.authoring_source->subpaths[0].segments.size() == 3);

    // Test breaking open path -> splits into two subpaths
    auto open_obj = make_test_open_line();
    const auto open_id = open_obj.id;
    const auto open_sub_id = open_obj.authoring_source->subpaths[0].id;
    const auto mid_node_id = open_obj.authoring_source->subpaths[0].nodes[1].id;
    doc.execute_command(std::make_unique<AddObjectCommand>(open_obj));

    assert(doc.execute_command(std::make_unique<BreakPathAtNodeCommand>(
        open_id, open_sub_id, mid_node_id)).success);
    snap = doc.current_snapshot();
    after_obj = std::get<PathObject>(*snap->get_object(open_id));
    assert(after_obj.authoring_source->subpaths.size() == 2);
    assert(after_obj.authoring_source->subpaths[0].nodes.size() == 2);
    assert(after_obj.authoring_source->subpaths[1].nodes.size() == 2);

    // Undo restores single open subpath
    assert(doc.undo());
    snap = doc.current_snapshot();
    after_obj = std::get<PathObject>(*snap->get_object(open_id));
    assert(after_obj.authoring_source->subpaths.size() == 1);
}

void test_scissors_splits_curve_and_is_one_undo_step() {
    Document doc("doc-scissors-test");
    auto object = make_test_triangle();
    auto editable = *object.authoring_source;
    auto& source_subpath = editable.subpaths[0];
    source_subpath.segments[0].kind = PathSegmentKind::CubicBezier;
    source_subpath.nodes[0].mode = PathNodeMode::Cusp;
    source_subpath.nodes[0].has_outgoing_handle = true;
    source_subpath.nodes[0].outgoing_handle = {25.0, 30.0};
    source_subpath.nodes[1].mode = PathNodeMode::Cusp;
    source_subpath.nodes[1].has_incoming_handle = true;
    source_subpath.nodes[1].incoming_handle = {75.0, 30.0};
    object.set_authoring_source(std::move(editable));
    const auto object_id = object.id;
    const auto subpath_id = object.authoring_source->subpaths[0].id;
    const auto segment_id = object.authoring_source->subpaths[0].segments[0].id;
    const auto before_source = object.authoring_source;
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(object)).success);

    const auto before_cut_revision = doc.revision();
    assert(doc.execute_command(std::make_unique<ScissorsPathCommand>(
        object_id, subpath_id, segment_id, 0.5)).success);
    assert(doc.revision() == before_cut_revision + 1);
    auto after = std::get<PathObject>(*doc.current_snapshot()->get_object(object_id));
    const auto& cut = after.authoring_source->subpaths.front();
    assert(!cut.closed);
    assert(cut.nodes.size() == 5);
    assert(cut.segments.size() == 4);
    assert(cut.segments.front().kind == PathSegmentKind::CubicBezier);
    assert(cut.segments.back().kind == PathSegmentKind::CubicBezier);

    // The combined split + open is a single history entry and restores the
    // exact original source IDs and curve topology.
    assert(doc.undo());
    after = std::get<PathObject>(*doc.current_snapshot()->get_object(object_id));
    assert(after.authoring_source->subpaths[0].closed);
    assert(after.authoring_source->subpaths[0].nodes.size() == 3);
    assert(after.authoring_source->subpaths[0].segments.size() == 3);
    assert(after.authoring_source->subpaths[0].id == before_source->subpaths[0].id);
    assert(after.authoring_source->subpaths[0].segments[0].id == segment_id);
    assert(doc.redo());
    after = std::get<PathObject>(*doc.current_snapshot()->get_object(object_id));
    assert(!after.authoring_source->subpaths[0].closed);
    assert(after.authoring_source->subpaths[0].nodes.size() == 5);
}

void test_scissors_splits_elliptic_arc_and_preserves_geometry() {
    Document doc("doc-scissors-arc-test");
    PathObject object;
    object.id = generate_object_id();
    EditablePath editable;
    EditableSubpath subpath;
    subpath.id = generate_path_element_id<SubpathId>();
    subpath.closed = true;
    subpath.nodes = {
        {generate_path_element_id<NodeId>(), {100.0, 0.0}, PathNodeMode::Corner},
        {generate_path_element_id<NodeId>(), {0.0, 50.0}, PathNodeMode::Corner}
    };
    const auto resolved = geometry::resolve_endpoint_arc(
        {{100.0, 0.0}, 100.0, 50.0, 0.0, false, true, {0.0, 50.0}});
    assert(resolved.kind == geometry::ArcResolutionKind::Arc);
    const auto source_arc = resolved.arc;
    const auto source_segment_id = generate_path_element_id<SegmentId>();
    subpath.segments = {
        {source_segment_id, PathSegmentKind::EllipticArc, {}, source_arc},
        {generate_path_element_id<SegmentId>(), PathSegmentKind::Line}
    };
    editable.subpaths.push_back(std::move(subpath));
    object.set_authoring_source(std::move(editable));

    const auto object_id = object.id;
    const auto subpath_id = object.authoring_source->subpaths.front().id;
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(object)).success);
    const auto before_cut_revision = doc.revision();
    constexpr double split_parameter = 0.37;
    assert(doc.execute_command(std::make_unique<ScissorsPathCommand>(
        object_id, subpath_id, source_segment_id, split_parameter)).success);
    assert(doc.revision() == before_cut_revision + 1);

    auto after = std::get<PathObject>(*doc.current_snapshot()->get_object(object_id));
    const auto& cut = after.authoring_source->subpaths.front();
    assert(!cut.closed);
    std::vector<geometry::EllipticArc> split_arcs;
    bool retained_source_segment_id = false;
    for (const auto& segment : cut.segments) {
        if (segment.kind != PathSegmentKind::EllipticArc) continue;
        split_arcs.push_back(segment.arc);
        retained_source_segment_id = retained_source_segment_id ||
            segment.id == source_segment_id;
    }
    assert(split_arcs.size() == 2);
    assert(retained_source_segment_id);

    const auto first_piece = source_arc.sub_arc(0.0, split_parameter);
    const auto second_piece = source_arc.sub_arc(split_parameter, 1.0);
    const auto same_curve = [](const geometry::EllipticArc& actual,
                               const geometry::EllipticArc& expected) {
        for (int sample = 0; sample <= 8; ++sample) {
            const double t = static_cast<double>(sample) / 8.0;
            const auto a = actual.point_at(t);
            const auto e = expected.point_at(t);
            if (std::hypot(a.x - e.x, a.y - e.y) > 1e-7) return false;
        }
        return true;
    };
    assert((same_curve(split_arcs[0], first_piece) &&
            same_curve(split_arcs[1], second_piece)) ||
           (same_curve(split_arcs[0], second_piece) &&
            same_curve(split_arcs[1], first_piece)));

    assert(doc.undo());
    after = std::get<PathObject>(*doc.current_snapshot()->get_object(object_id));
    const auto& restored = after.authoring_source->subpaths.front();
    assert(restored.closed);
    assert(restored.id == subpath_id);
    assert(restored.segments.size() == 2);
    assert(restored.segments[0].id == source_segment_id);
    assert(restored.segments[0].kind == PathSegmentKind::EllipticArc);
    assert(same_curve(restored.segments[0].arc, source_arc));
    assert(doc.redo());
    after = std::get<PathObject>(*doc.current_snapshot()->get_object(object_id));
    assert(!after.authoring_source->subpaths.front().closed);
    assert(after.authoring_source->subpaths.front().segments.size() == 3);
}

void test_reverse_subpath() {
    Document doc("doc-reverse-test");
    auto obj = make_test_open_line();
    const auto obj_id = obj.id;
    const auto sub_id = obj.authoring_source->subpaths[0].id;
    doc.execute_command(std::make_unique<AddObjectCommand>(obj));

    const auto first_node_before = obj.authoring_source->subpaths[0].nodes.front().id;
    const auto last_node_before = obj.authoring_source->subpaths[0].nodes.back().id;

    assert(doc.execute_command(std::make_unique<ReverseSubpathCommand>(obj_id, sub_id)).success);
    auto snap = doc.current_snapshot();
    auto after_obj = std::get<PathObject>(*snap->get_object(obj_id));
    const auto& sub_after = after_obj.authoring_source->subpaths[0];
    assert(sub_after.nodes.front().id == last_node_before);
    assert(sub_after.nodes.back().id == first_node_before);

    assert(doc.undo());
    snap = doc.current_snapshot();
    after_obj = std::get<PathObject>(*snap->get_object(obj_id));
    assert(after_obj.authoring_source->subpaths[0].nodes.front().id == first_node_before);
}

void test_convert_segment_kind() {
    Document doc("doc-convert-test");
    auto obj = make_test_open_line();
    const auto obj_id = obj.id;
    const auto sub_id = obj.authoring_source->subpaths[0].id;
    const auto seg_id = obj.authoring_source->subpaths[0].segments[0].id;
    doc.execute_command(std::make_unique<AddObjectCommand>(obj));

    // Convert Line -> CubicBezier
    assert(doc.execute_command(std::make_unique<ConvertSegmentKindCommand>(
        obj_id, sub_id, seg_id, PathSegmentKind::CubicBezier)).success);
    auto snap = doc.current_snapshot();
    auto after_obj = std::get<PathObject>(*snap->get_object(obj_id));
    assert(after_obj.authoring_source->subpaths[0].segments[0].kind == PathSegmentKind::CubicBezier);
    assert(after_obj.authoring_source->subpaths[0].nodes[0].has_outgoing_handle);
    assert(after_obj.authoring_source->subpaths[0].nodes[1].has_incoming_handle);

    // Convert CubicBezier -> Line
    assert(doc.execute_command(std::make_unique<ConvertSegmentKindCommand>(
        obj_id, sub_id, seg_id, PathSegmentKind::Line)).success);
    snap = doc.current_snapshot();
    after_obj = std::get<PathObject>(*snap->get_object(obj_id));
    assert(after_obj.authoring_source->subpaths[0].segments[0].kind == PathSegmentKind::Line);

    assert(doc.undo());
    snap = doc.current_snapshot();
    after_obj = std::get<PathObject>(*snap->get_object(obj_id));
    assert(after_obj.authoring_source->subpaths[0].segments[0].kind == PathSegmentKind::CubicBezier);
}

void test_batch_move_path_nodes() {
    Document doc("doc-batch-move-test");
    auto obj = make_test_triangle();
    const auto obj_id = obj.id;
    const auto sub_id = obj.authoring_source->subpaths[0].id;
    const auto n1_id = obj.authoring_source->subpaths[0].nodes[0].id;
    const auto n2_id = obj.authoring_source->subpaths[0].nodes[1].id;
    doc.execute_command(std::make_unique<AddObjectCommand>(obj));

    std::vector<std::pair<SubpathId, NodeId>> targets = {
        {sub_id, n1_id},
        {sub_id, n2_id}
    };
    geometry::Point2D delta{15.0, 25.0};
    assert(doc.execute_command(std::make_unique<BatchMovePathNodesCommand>(
        obj_id, targets, delta)).success);

    auto snap = doc.current_snapshot();
    auto after_obj = std::get<PathObject>(*snap->get_object(obj_id));
    const auto& nodes = after_obj.authoring_source->subpaths[0].nodes;
    assert(std::abs(nodes[0].anchor.x - 15.0) < 1e-6);
    assert(std::abs(nodes[0].anchor.y - 25.0) < 1e-6);
    assert(std::abs(nodes[1].anchor.x - 115.0) < 1e-6);
    assert(std::abs(nodes[1].anchor.y - 25.0) < 1e-6);
    assert(std::abs(nodes[2].anchor.x - 50.0) < 1e-6); // unselected unchanged

    assert(doc.undo());
    snap = doc.current_snapshot();
    after_obj = std::get<PathObject>(*snap->get_object(obj_id));
    assert(std::abs(after_obj.authoring_source->subpaths[0].nodes[0].anchor.x - 0.0) < 1e-6);
}

void test_bend_segment() {
    Document doc("doc-bend-segment-test");
    auto obj = make_test_triangle();
    const auto obj_id = obj.id;
    const auto sub_id = obj.authoring_source->subpaths[0].id;
    const auto seg_id = obj.authoring_source->subpaths[0].segments[0].id;
    doc.execute_command(std::make_unique<AddObjectCommand>(obj));

    geometry::Point2D c1{30.0, 40.0};
    geometry::Point2D c2{70.0, 40.0};
    assert(doc.execute_command(std::make_unique<BendSegmentCommand>(
        obj_id, sub_id, seg_id, c1, c2)).success);

    auto snap = doc.current_snapshot();
    auto after_obj = std::get<PathObject>(*snap->get_object(obj_id));
    const auto& sub = after_obj.authoring_source->subpaths[0];
    assert(sub.segments[0].kind == PathSegmentKind::CubicBezier);
    assert(sub.nodes[0].has_outgoing_handle);
    assert(std::abs(sub.nodes[0].outgoing_handle.x - 30.0) < 1e-6);
    assert(std::abs(sub.nodes[0].outgoing_handle.y - 40.0) < 1e-6);
    assert(sub.nodes[1].has_incoming_handle);
    assert(std::abs(sub.nodes[1].incoming_handle.x - 70.0) < 1e-6);
    assert(std::abs(sub.nodes[1].incoming_handle.y - 40.0) < 1e-6);

    // Single step undo
    assert(doc.undo());
    snap = doc.current_snapshot();
    after_obj = std::get<PathObject>(*snap->get_object(obj_id));
    assert(after_obj.authoring_source->subpaths[0].segments[0].kind == PathSegmentKind::Line);
    assert(!after_obj.authoring_source->subpaths[0].nodes[0].has_outgoing_handle);
}

int main() {
    std::cout << "[test_node_commands] Running node command tests...\n";
    test_move_node_handle_cusp_smooth_symmetric();
    test_break_path_at_node();
    test_scissors_splits_curve_and_is_one_undo_step();
    test_scissors_splits_elliptic_arc_and_preserves_geometry();
    test_reverse_subpath();
    test_convert_segment_kind();
    test_batch_move_path_nodes();
    test_bend_segment();
    std::cout << "[test_node_commands] All tests passed!\n";
    return 0;
}
