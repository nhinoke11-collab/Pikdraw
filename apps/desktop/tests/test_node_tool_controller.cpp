#include "pikdraw/document/document.hpp"
#include "../src/tools/node_tool_controller.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::document;
using namespace pikdraw::desktop::tools;

static PathObject make_controller_test_triangle() {
    PathObject obj;
    obj.id = generate_object_id();
    EditablePath path;
    EditableSubpath sub;
    sub.id = generate_path_element_id<SubpathId>();
    sub.closed = true;

    PathNode n1{generate_path_element_id<NodeId>(), {10.0, 10.0}, PathNodeMode::Corner};
    PathNode n2{generate_path_element_id<NodeId>(), {80.0, 10.0}, PathNodeMode::Corner};
    PathNode n3{generate_path_element_id<NodeId>(), {45.0, 70.0}, PathNodeMode::Corner};
    sub.nodes = {n1, n2, n3};

    for (int i = 0; i < 3; ++i) {
        sub.segments.push_back({generate_path_element_id<SegmentId>(), PathSegmentKind::Line});
    }

    path.subpaths.push_back(std::move(sub));
    obj.set_authoring_source(std::move(path));
    return obj;
}

int main() {
    std::cout << "[test_node_tool_controller] Running controller integration tests...\n";

    Document doc("doc-controller-test");
    auto obj = make_controller_test_triangle();
    const auto obj_id = obj.id;
    doc.execute_command(std::make_unique<AddObjectCommand>(obj));

    NodeToolController controller(doc);
    assert(!controller.active());

    // Activate with object
    assert(controller.activate(obj_id));
    assert(controller.active());
    assert(controller.target_object() == obj_id);

    // Select node at (10, 10)
    assert(controller.pointer_down({9.5, 10.5}, false, 2.0));
    controller.pointer_up({9.5, 10.5});
    assert(controller.selected_node_count() == 1);
    auto pos = controller.single_selected_node_position();
    assert(pos.has_value());
    assert(std::abs(pos->x - 10.0) < 1e-6);

    // Drag node
    assert(controller.pointer_down({10.0, 10.0}, false, 2.0));
    assert(controller.pointer_move({15.0, 20.0}));
    controller.pointer_up({15.0, 20.0});

    pos = controller.single_selected_node_position();
    assert(pos.has_value());
    assert(std::abs(pos->x - 15.0) < 1e-6);
    assert(std::abs(pos->y - 20.0) < 1e-6);

    // Mode operations
    assert(controller.set_selected_mode(PathNodeMode::Smooth));
    auto mode = controller.selected_nodes_mode();
    assert(mode.has_value() && *mode == PathNodeMode::Smooth);
    assert(!controller.visible_handles().empty());

    // Marquee query
    assert(!controller.is_marquee());

    // Deactivate
    controller.deactivate();
    assert(!controller.active());

    std::cout << "[test_node_tool_controller] All tests passed!\n";
    return 0;
}
