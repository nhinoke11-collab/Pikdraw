#include "pikdraw/document/document.hpp"
#include "pikdraw/vector/tools/node/node_tool.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::document;
using namespace pikdraw::vector::tools::node;

static PathObject make_test_quad() {
    PathObject obj;
    obj.id = generate_object_id();
    EditablePath path;
    EditableSubpath sub;
    sub.id = generate_path_element_id<SubpathId>();
    sub.closed = true;

    PathNode n1{generate_path_element_id<NodeId>(), {0.0, 0.0}, PathNodeMode::Corner};
    PathNode n2{generate_path_element_id<NodeId>(), {100.0, 0.0}, PathNodeMode::Corner};
    PathNode n3{generate_path_element_id<NodeId>(), {100.0, 100.0}, PathNodeMode::Corner};
    PathNode n4{generate_path_element_id<NodeId>(), {0.0, 100.0}, PathNodeMode::Corner};
    sub.nodes = {n1, n2, n3, n4};

    for (int i = 0; i < 4; ++i) {
        sub.segments.push_back({generate_path_element_id<SegmentId>(), PathSegmentKind::Line});
    }

    path.subpaths.push_back(std::move(sub));
    obj.set_authoring_source(std::move(path));
    return obj;
}

void test_node_tool_selection_and_drag() {
    Document doc("doc-tool-test");
    auto obj = make_test_quad();
    const auto obj_id = obj.id;
    doc.execute_command(std::make_unique<AddObjectCommand>(obj));

    NodeTool tool(doc);
    tool.set_target_object(obj_id);
    assert(tool.selected_node_count() == 0);

    // Click near (100, 0)
    assert(tool.pointer_down({99.5, 0.5}, false, 2.0));
    assert(tool.selected_node_count() == 1);
    auto pos = tool.single_selected_node_position();
    assert(pos.has_value());
    assert(std::abs(pos->x - 100.0) < 1e-6);
    assert(std::abs(pos->y - 0.0) < 1e-6);

    // Drag node to (110, 10)
    assert(tool.pointer_move({109.5, 10.5}));
    tool.pointer_up({109.5, 10.5});

    pos = tool.single_selected_node_position();
    assert(pos.has_value());
    assert(std::abs(pos->x - 110.0) < 1e-6);
    assert(std::abs(pos->y - 10.0) < 1e-6);

    // Undo
    assert(doc.undo());
    pos = tool.single_selected_node_position();
    assert(pos.has_value());
    assert(std::abs(pos->x - 100.0) < 1e-6);
}

void test_node_tool_marquee_and_batch_move() {
    Document doc("doc-marquee-test");
    auto obj = make_test_quad();
    const auto obj_id = obj.id;
    doc.execute_command(std::make_unique<AddObjectCommand>(obj));

    NodeTool tool(doc);
    tool.set_target_object(obj_id);

    // Drag marquee covering (0,0) and (100,0)
    assert(tool.pointer_down({-10.0, -10.0}, false, 2.0));
    assert(tool.is_marquee());
    assert(tool.pointer_move({110.0, 10.0}));
    tool.pointer_up({110.0, 10.0});

    assert(tool.selected_node_count() == 2);

    // Batch move by (5, -5)
    assert(tool.move_selected_delta({5.0, -5.0}));
    auto snap = doc.current_snapshot();
    auto cur_obj = std::get<PathObject>(*snap->get_object(obj_id));
    const auto& nodes = cur_obj.authoring_source->subpaths[0].nodes;
    assert(std::abs(nodes[0].anchor.x - 5.0) < 1e-6);
    assert(std::abs(nodes[0].anchor.y - (-5.0)) < 1e-6);
    assert(std::abs(nodes[1].anchor.x - 105.0) < 1e-6);
    assert(std::abs(nodes[1].anchor.y - (-5.0)) < 1e-6);
}

void test_node_tool_mode_and_handle_drag() {
    Document doc("doc-handle-tool-test");
    auto obj = make_test_quad();
    const auto obj_id = obj.id;
    doc.execute_command(std::make_unique<AddObjectCommand>(obj));

    NodeTool tool(doc);
    tool.set_target_object(obj_id);

    // Select node at (0, 0)
    assert(tool.pointer_down({0.0, 0.0}, false, 2.0));
    tool.pointer_up({0.0, 0.0});
    assert(tool.selected_node_count() == 1);

    // Change mode to Smooth
    assert(tool.set_selected_mode(PathNodeMode::Smooth));
    auto mode = tool.selected_nodes_mode();
    assert(mode.has_value() && *mode == PathNodeMode::Smooth);

    // Verify handles are visible
    auto handles = tool.visible_handles();
    assert(!handles.empty());

    // Drag one handle
    const auto h0 = handles[0];
    assert(tool.pointer_down(h0.position, false, 2.0));
    assert(tool.pointer_move(h0.position + geometry::Point2D{10.0, 10.0}));
    tool.pointer_up(h0.position + geometry::Point2D{10.0, 10.0});

    // Check handle updated
    handles = tool.visible_handles();
    assert(!handles.empty());
    assert(handles[0].position.distance_to(h0.position + geometry::Point2D{10.0, 10.0}) < 1e-5);
}

void test_node_tool_operations() {
    Document doc("doc-tool-ops");
    auto obj = make_test_quad();
    const auto obj_id = obj.id;
    doc.execute_command(std::make_unique<AddObjectCommand>(obj));

    NodeTool tool(doc);
    tool.set_target_object(obj_id);

    // Select all
    tool.select_all();
    assert(tool.selected_node_count() == 4);

    // Add node at midpoint of segment
    assert(tool.pointer_down({50.0, 0.0}, false, 3.0));
    tool.pointer_up({50.0, 0.0});
    assert(tool.add_node_at_midpoint());

    auto snap = doc.current_snapshot();
    auto cur_obj = std::get<PathObject>(*snap->get_object(obj_id));
    assert(cur_obj.authoring_source->subpaths[0].nodes.size() == 5);

    // Break at node
    assert(tool.pointer_down({0.0, 0.0}, false, 2.0));
    tool.pointer_up({0.0, 0.0});
    assert(tool.break_selected());
    snap = doc.current_snapshot();
    cur_obj = std::get<PathObject>(*snap->get_object(obj_id));
    assert(!cur_obj.authoring_source->subpaths[0].closed);

    // Close
    assert(tool.close_selected());
    snap = doc.current_snapshot();
    cur_obj = std::get<PathObject>(*snap->get_object(obj_id));
    assert(cur_obj.authoring_source->subpaths[0].closed);
}

int main() {
    std::cout << "[test_node_tool] Running NodeTool vector tests...\n";
    test_node_tool_selection_and_drag();
    test_node_tool_marquee_and_batch_move();
    test_node_tool_mode_and_handle_drag();
    test_node_tool_operations();
    std::cout << "[test_node_tool] All tests passed!\n";
    return 0;
}
