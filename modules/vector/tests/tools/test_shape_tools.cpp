#include "pikdraw/vector/vector_controller.hpp"
#include <cassert>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::vector;
using namespace pikdraw::document;

void test_shape_creation_and_deletion() {
    Document doc("doc-shapes-test");
    VectorController controller;

    // Create Rectangle tool action
    controller.set_mode(ToolMode::Rectangle);
    auto res1 = controller.on_pointer_up(geometry::Point2D{100, 200}, doc);
    assert(res1.success);
    assert(doc.revision() == 1);
    assert(doc.current_snapshot()->object_count() == 1);
    assert(controller.selection().count() == 1);

    // Create Ellipse tool action
    controller.set_mode(ToolMode::Ellipse);
    auto res2 = controller.on_pointer_up(geometry::Point2D{400, 500}, doc);
    assert(res2.success);
    assert(doc.revision() == 2);
    assert(doc.current_snapshot()->object_count() == 2);

    // Delete selected object
    auto del_res = controller.delete_selected(doc);
    assert(del_res.success);
    assert(doc.revision() == 3);
    assert(doc.current_snapshot()->object_count() == 1);

    // Undo delete
    doc.undo();
    assert(doc.revision() == 4);
    assert(doc.current_snapshot()->object_count() == 2);
}

int main() {
    std::cout << "[test_shape_tools] Running shape tools tests...\n";
    test_shape_creation_and_deletion();
    std::cout << "[test_shape_tools] All tests passed!\n";
    return 0;
}
