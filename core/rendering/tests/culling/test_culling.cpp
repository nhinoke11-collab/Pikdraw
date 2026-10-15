#include "pikdraw/rendering/culling.hpp"
#include "pikdraw/document/document.hpp"
#include <cassert>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::rendering;
using namespace pikdraw::document;

void test_spatial_culling() {
    Document doc("doc-cull-test");

    // Object 1: inside visible bounds [0, 0] to [100, 100]
    PathObject p1;
    p1.id = generate_object_id();
    p1.name = "Inside";
    p1.path.move_to({10, 10});
    p1.path.line_to({50, 50});
    doc.execute_command(std::make_unique<AddObjectCommand>(p1));

    // Object 2: far outside visible bounds at [5000, 5000]
    PathObject p2;
    p2.id = generate_object_id();
    p2.name = "Outside";
    p2.path.move_to({5000, 5000});
    p2.path.line_to({5100, 5100});
    doc.execute_command(std::make_unique<AddObjectCommand>(p2));

    // Object 3: inside bounds but invisible
    PathObject p3;
    p3.id = generate_object_id();
    p3.name = "Hidden";
    p3.visible = false;
    p3.path.move_to({20, 20});
    p3.path.line_to({40, 40});
    doc.execute_command(std::make_unique<AddObjectCommand>(p3));

    auto snap = doc.current_snapshot();
    assert(snap->object_count() == 3);

    geometry::Rect2D view_bounds{0, 0, 200, 200};
    auto culled = SpatialCuller::query_visible(*snap, view_bounds);

    assert(culled.size() == 1);
    assert(culled[0].id == p1.id);
}

int main() {
    std::cout << "[test_culling] Running spatial culling tests...\n";
    test_spatial_culling();
    std::cout << "[test_culling] All tests passed!\n";
    return 0;
}
