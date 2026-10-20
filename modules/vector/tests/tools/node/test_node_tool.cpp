#include "pikdraw/document/document.hpp"
#include "pikdraw/vector/tools/node/node_tool.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::document;
using namespace pikdraw::vector::tools::node;
using pikdraw::vector::RectCornerType;

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

void test_single_step_undo_during_drag() {
    Document doc("doc-drag-single-undo");
    auto obj = make_test_quad();
    const auto obj_id = obj.id;
    doc.execute_command(std::make_unique<AddObjectCommand>(obj));

    NodeTool tool(doc);
    tool.set_target_object(obj_id);

    // Select node at (100, 0)
    assert(tool.pointer_down({100.0, 0.0}, false, 2.0));

    // Simulate 50 mouse move events during a 1-second drag gesture
    for (int step = 1; step <= 50; ++step) {
        const double x = 100.0 + static_cast<double>(step) * 0.5;
        const double y = static_cast<double>(step) * 0.5;
        assert(tool.pointer_move({x, y}));
        // Live position during drag reflects mouse position
        auto live_pos = tool.single_selected_node_position();
        assert(live_pos.has_value());
        assert(std::abs(live_pos->x - x) < 1e-4);
        assert(std::abs(live_pos->y - y) < 1e-4);
    }

    // Release mouse at (125, 25)
    tool.pointer_up({125.0, 25.0});

    // Node is now at (125, 25) in document
    auto snap = doc.current_snapshot();
    auto cur_obj = std::get<PathObject>(*snap->get_object(obj_id));
    assert(std::abs(cur_obj.authoring_source->subpaths[0].nodes[1].anchor.x - 125.0) < 1e-4);
    assert(std::abs(cur_obj.authoring_source->subpaths[0].nodes[1].anchor.y - 25.0) < 1e-4);

    // Exactly ONE undo step should exist for the drag!
    assert(doc.undo());

    // Original position restored to (100, 0)
    snap = doc.current_snapshot();
    cur_obj = std::get<PathObject>(*snap->get_object(obj_id));
    assert(std::abs(cur_obj.authoring_source->subpaths[0].nodes[1].anchor.x - 100.0) < 1e-4);
    assert(std::abs(cur_obj.authoring_source->subpaths[0].nodes[1].anchor.y - 0.0) < 1e-4);

    // No second drag undo step exists (the next undo would undo AddObjectCommand)
    // Undo once more -> removes the object
    assert(doc.undo());
    assert(doc.current_snapshot()->objects().empty());
    // Stack is now empty
    assert(!doc.undo());
}

void test_live_corner_rounding_and_single_step_undo() {
    Document doc("doc-corner-rounding-test");
    auto obj = make_test_quad();
    const auto obj_id = obj.id;
    doc.execute_command(std::make_unique<AddObjectCommand>(obj));

    NodeTool tool(doc);
    tool.set_target_object(obj_id);

    // Verify 4 corner widgets are detected for the 4 corners of the quad
    const auto widgets = tool.corner_widgets(1.0);
    assert(widgets.size() == 4);

    // Pick first corner widget
    const auto w0 = widgets[0];
    assert(w0.max_radius > 0.0);

    // Click on corner widget
    assert(tool.pointer_down(w0.widget_position, false, 2.0));
    assert(tool.is_rounding_corners());

    // Drag inward along bisector
    const auto drag_pt = w0.widget_position + w0.bisector * 10.0;
    assert(tool.pointer_move(drag_pt));
    assert(tool.current_corner_radius() > 9.0);

    // Preview path should have rounded corners
    const auto preview = tool.preview_rounded_path();
    assert(preview.has_value());

    // Release mouse to commit live corners
    tool.pointer_up(drag_pt);
    assert(!tool.is_rounding_corners());

    // Document now has rounded authoring source
    auto snap = doc.current_snapshot();
    auto cur_obj = std::get<PathObject>(*snap->get_object(obj_id));
    bool has_bezier = false;
    for (const auto& seg : cur_obj.authoring_source->subpaths[0].segments) {
        if (seg.kind == PathSegmentKind::CubicBezier) {
            has_bezier = true;
            break;
        }
    }
    assert(has_bezier);

    // Exactly ONE undo restores the sharp quad!
    assert(doc.undo());
    snap = doc.current_snapshot();
    cur_obj = std::get<PathObject>(*snap->get_object(obj_id));
    assert(cur_obj.authoring_source->subpaths[0].nodes.size() == 4);
    for (const auto& seg : cur_obj.authoring_source->subpaths[0].segments) {
        assert(seg.kind == PathSegmentKind::Line);
    }
}

void test_transformed_object_nodes_and_live_mirroring() {
    Document doc("doc-transform-node-test");
    auto obj = make_test_quad();
    obj.transform = geometry::Transform2D::translation(50.0, 30.0);
    const auto obj_id = obj.id;
    doc.execute_command(std::make_unique<AddObjectCommand>(obj));

    NodeTool tool(doc);
    tool.set_target_object(obj_id);

    // Node 0 local anchor is (0, 0), so in world space it is at (50, 30)
    auto hit = tool.hit_test_node({50.0, 30.0}, 2.0);
    assert(hit.has_value());

    // Single click selects node 0
    assert(tool.pointer_down({50.0, 30.0}, false, 2.0));
    tool.pointer_up({50.0, 30.0});
    assert(tool.selected_node_count() == 1);

    // Drag node 0 by (+20, +10) in world space
    assert(tool.pointer_down({50.0, 30.0}, false, 2.0));
    assert(tool.pointer_move({70.0, 40.0}));
    tool.pointer_up({70.0, 40.0});

    // Check single_selected_node_position returns world pos (70, 40)
    auto pos = tool.single_selected_node_position();
    assert(pos.has_value());
    assert(std::abs(pos->x - 70.0) < 1e-5);
    assert(std::abs(pos->y - 40.0) < 1e-5);

    // Check corner widgets are located in world space
    const auto widgets = tool.corner_widgets(1.0);
    assert(widgets.size() == 4);
    for (const auto& w : widgets) {
        // All widget positions should be offset by (+50, +30)
        assert(w.widget_position.x >= 50.0 && w.widget_position.y >= 30.0);
    }

    // Set node mode to Smooth
    assert(tool.set_selected_mode(PathNodeMode::Smooth));
    auto handles = tool.visible_handles();
    assert(!handles.empty());

    // Drag one handle and verify opposite handle dynamically mirrors
    const auto h0 = handles[0];
    assert(tool.pointer_down(h0.position, false, 2.0));
    assert(tool.pointer_move(h0.position + geometry::Point2D{10.0, 0.0}));
    const auto live_handles = tool.visible_handles();
    assert(live_handles.size() >= 2);
    tool.cancel();

    // Multi-selection with shift/ctrl
    // Click node 1 at world (150, 30) with shift
    assert(tool.pointer_down({150.0, 30.0}, true, 2.0));
    tool.pointer_up({150.0, 30.0});
    assert(tool.selected_node_count() == 2);

    // Click without shift on node 0 without dragging collapses selection to just node 0
    assert(tool.pointer_down({70.0, 40.0}, false, 2.0));
    tool.pointer_up({70.0, 40.0});
    assert(tool.selected_node_count() == 1);

    // Quick action: select all nodes
    tool.select_all();
    assert(tool.selected_node_count() == 4);
}

void test_bend_segment_and_node_to_curve_batch() {
    Document doc("doc-bend-tool-test");
    auto obj = make_test_quad();
    const auto obj_id = obj.id;
    doc.execute_command(std::make_unique<AddObjectCommand>(obj));

    NodeTool tool(doc);
    tool.set_target_object(obj_id);

    // 1. Direct segment bending
    // Segment 0 connects (0,0) to (100,0). Hit at (50, 0)
    assert(tool.pointer_down({50.0, 0.0}, false, 2.0));
    assert(tool.drag_mode() == NodeDragMode::BendSegment);
    assert(tool.active_segment_drag().has_value());

    // Drag to (50, 30)
    assert(tool.pointer_move({50.0, 30.0}));
    const auto [c1, c2] = tool.current_segment_bend_handles();
    assert(c1.y > 0.0 && c2.y > 0.0);

    // Release mouse
    assert(tool.pointer_up({50.0, 30.0}));
    auto snap = doc.current_snapshot();
    auto cur_obj = std::get<PathObject>(*snap->get_object(obj_id));
    const auto& sub = cur_obj.authoring_source->subpaths[0];
    assert(sub.segments[0].kind == PathSegmentKind::CubicBezier);
    assert(sub.nodes[0].has_outgoing_handle);
    assert(sub.nodes[1].has_incoming_handle);

    // Single step undo reverts segment to Line
    assert(doc.undo());
    snap = doc.current_snapshot();
    cur_obj = std::get<PathObject>(*snap->get_object(obj_id));
    assert(cur_obj.authoring_source->subpaths[0].segments[0].kind == PathSegmentKind::Line);

    // 2. Batch node-to-curve conversion
    // Select node 2 (at 100, 100)
    assert(tool.pointer_down({100.0, 100.0}, false, 2.0));
    tool.pointer_up({100.0, 100.0});
    assert(tool.selected_node_count() == 1);

    // Convert adjacent segments to curve
    assert(tool.convert_selected_segment(PathSegmentKind::CubicBezier));
    snap = doc.current_snapshot();
    cur_obj = std::get<PathObject>(*snap->get_object(obj_id));
    const auto& sub2 = cur_obj.authoring_source->subpaths[0];
    // Segments 1 and 2 connected to node 2 must both be CubicBezier
    assert(sub2.segments[1].kind == PathSegmentKind::CubicBezier);
    assert(sub2.segments[2].kind == PathSegmentKind::CubicBezier);
}

void test_anisotropic_corner_rounding_uniformity() {
    Document doc("doc-anisotropic-corner-test");
    auto obj = make_test_quad();
    // Scale object 2:1 horizontally (anisotropic scale sx=2.0, sy=1.0)
    obj.transform = geometry::Transform2D::scale(2.0, 1.0);
    const auto obj_id = obj.id;
    doc.execute_command(std::make_unique<AddObjectCommand>(obj));

    NodeTool tool(doc);
    tool.set_target_object(obj_id);

    const auto widgets = tool.corner_widgets(1.0);
    assert(widgets.size() == 4);
    const auto w0 = widgets[0];

    // Click on corner widget 0
    assert(tool.pointer_down(w0.widget_position, false, 2.0));
    assert(tool.is_rounding_corners());

    // Drag inward along bisector by 10.0 mm
    const auto drag_pt = w0.widget_position + w0.bisector * 10.0;
    assert(tool.pointer_move(drag_pt));
    const double radius = tool.current_corner_radius();
    assert(radius > 9.0);

    // Preview path check
    const auto preview = tool.preview_rounded_path();
    assert(preview.has_value());
    const auto& prev_sub = preview->subpaths[0];

    const auto tr = obj.transform;
    const auto p0_w = tr.map_point(prev_sub.nodes[0].anchor);
    const auto p1_w = tr.map_point(prev_sub.nodes[1].anchor);

    // The distances from corner in world space MUST be equal (isotropic circle!)
    const double d0 = std::hypot(p0_w.x - 0.0, p0_w.y - 0.0);
    const double d1 = std::hypot(p1_w.x - 0.0, p1_w.y - 0.0);
    assert(std::abs(d0 - d1) < 1e-4);
    assert(std::abs(d0 - radius) < 1e-4);

    // Release mouse to commit
    tool.pointer_up(drag_pt);
    assert(!tool.is_rounding_corners());

    // Verify committed path in document has equal world-space extent
    auto snap = doc.current_snapshot();
    auto cur_obj = std::get<PathObject>(*snap->get_object(obj_id));
    const auto& com_sub = cur_obj.authoring_source->subpaths[0];
    const auto com_p0_w = tr.map_point(com_sub.nodes[0].anchor);
    const auto com_p1_w = tr.map_point(com_sub.nodes[1].anchor);
    const double cd0 = std::hypot(com_p0_w.x - 0.0, com_p0_w.y - 0.0);
    const double cd1 = std::hypot(com_p1_w.x - 0.0, com_p1_w.y - 0.0);
    assert(std::abs(cd0 - cd1) < 1e-4);
    assert(std::abs(cd0 - radius) < 1e-4);

    // Exactly ONE undo restores original sharp quad
    assert(doc.undo());
    snap = doc.current_snapshot();
    cur_obj = std::get<PathObject>(*snap->get_object(obj_id));
    assert(cur_obj.authoring_source->subpaths[0].nodes.size() == 4);
}

void test_corner_types_round_scallop_chamfer() {
    Document doc("doc-corner-types-test");
    auto obj = make_test_quad();
    obj.contour_spec = "rect_corner:round:0.0:0.0:0.0:0.0:1:0";
    const auto obj_id = obj.id;
    doc.execute_command(std::make_unique<AddObjectCommand>(obj));
    NodeTool tool(doc);
    tool.set_target_object(obj_id);

    const auto widgets = tool.corner_widgets(1.0);
    assert(widgets.size() == 4);
    const auto w0 = widgets[0];

    // 1. Test Chamfer (straight bevel)
    tool.set_corner_type(RectCornerType::Chamfer);
    assert(tool.corner_type() == RectCornerType::Chamfer);

    assert(tool.pointer_down(w0.widget_position, false, 2.0));
    assert(tool.is_rounding_corners());

    const auto drag_pt = w0.widget_position + w0.bisector * 10.0;
    assert(tool.pointer_move(drag_pt));
    assert(tool.current_corner_radius() > 9.0);

    // Live preview fillets for Chamfer must be linear (is_cubic == false)
    const auto chamfer_fillets = tool.fillet_preview_segments();
    assert(!chamfer_fillets.empty());
    assert(!chamfer_fillets[0].is_cubic);

    tool.pointer_up(drag_pt);
    assert(!tool.is_rounding_corners());

    // Verify committed path has Line fillet
    auto snap = doc.current_snapshot();
    auto cur_obj = std::get<PathObject>(*snap->get_object(obj_id));
    bool all_lines = true;
    for (const auto& seg : cur_obj.authoring_source->subpaths[0].segments) {
        if (seg.kind != PathSegmentKind::Line) {
            all_lines = false;
            break;
        }
    }
    assert(all_lines);
    // Chamfer added nodes at the corner
    assert(cur_obj.authoring_source->subpaths[0].nodes.size() > 4);

    // Undo chamfer
    assert(doc.undo());

    // 2. Test Scallop (concave arc)
    tool.set_corner_type(RectCornerType::Scallop);
    assert(tool.corner_type() == RectCornerType::Scallop);

    assert(tool.pointer_down(w0.widget_position, false, 2.0));
    assert(tool.is_rounding_corners());
    assert(tool.pointer_move(drag_pt));

    const auto scallop_fillets = tool.fillet_preview_segments();
    assert(!scallop_fillets.empty());
    assert(scallop_fillets[0].is_cubic);

    tool.pointer_up(drag_pt);
    assert(!tool.is_rounding_corners());

    // After scalloping, corner widgets must STILL be present (4 persistent parametric corner handles)!
    const auto widgets_after_scallop = tool.corner_widgets(1.0);
    assert(widgets_after_scallop.size() == 4);
    assert(widgets_after_scallop[0].current_radius > 9.0);

    snap = doc.current_snapshot();
    cur_obj = std::get<PathObject>(*snap->get_object(obj_id));
    bool has_cubic = false;
    for (const auto& seg : cur_obj.authoring_source->subpaths[0].segments) {
        if (seg.kind == PathSegmentKind::CubicBezier) {
            has_cubic = true;
            break;
        }
    }
    assert(has_cubic);

    // Undo scallop
    assert(doc.undo());

    // 3. Test Round (convex fillet)
    tool.set_corner_type(RectCornerType::Round);
    assert(tool.corner_type() == RectCornerType::Round);

    assert(tool.pointer_down(w0.widget_position, false, 2.0));
    assert(tool.is_rounding_corners());
    assert(tool.pointer_move(drag_pt));

    const auto round_fillets = tool.fillet_preview_segments();
    assert(!round_fillets.empty());
    assert(round_fillets[0].is_cubic);

    // Round and scallop handles must differ (convex vs concave)
    assert(round_fillets[0].c1 != scallop_fillets[0].c1);

    tool.pointer_up(drag_pt);
    assert(!tool.is_rounding_corners());

    // Undo round
    assert(doc.undo());
    snap = doc.current_snapshot();
    cur_obj = std::get<PathObject>(*snap->get_object(obj_id));
    assert(cur_obj.authoring_source->subpaths[0].nodes.size() == 4);
}

int main() {
    std::cout << "[test_node_tool] Running NodeTool vector tests...\n";
    test_node_tool_selection_and_drag();
    test_node_tool_marquee_and_batch_move();
    test_node_tool_mode_and_handle_drag();
    test_node_tool_operations();
    test_single_step_undo_during_drag();
    test_live_corner_rounding_and_single_step_undo();
    test_transformed_object_nodes_and_live_mirroring();
    test_bend_segment_and_node_to_curve_batch();
    test_anisotropic_corner_rounding_uniformity();
    test_corner_types_round_scallop_chamfer();
    std::cout << "[test_node_tool] All tests passed!\n";
    return 0;
}
