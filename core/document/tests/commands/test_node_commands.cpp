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
    assert(after_obj.authoring_source->subpaths[0].segments.size() == 2);

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

int main() {
    std::cout << "[test_node_commands] Running node command tests...\n";
    test_move_node_handle_cusp_smooth_symmetric();
    test_break_path_at_node();
    test_reverse_subpath();
    test_convert_segment_kind();
    test_batch_move_path_nodes();
    std::cout << "[test_node_commands] All tests passed!\n";
    return 0;
}
