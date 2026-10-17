#include "pikdraw/document/document.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "../src/tools/contour_tool_controller.hpp"
#include <cassert>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::desktop::tools;

int main() {
    std::cout << "=== Running PikDraw Desktop Contour Tool Controller Tests ===" << std::endl;

    document::Document doc("test-desktop-contour");

    document::PathObject obj;
    obj.id = document::generate_object_id();
    obj.name = "Desktop Box";
    obj.path = geometry::Path2D::make_rect(0.0, 0.0, 80.0, 60.0);
    doc.execute_command(std::make_unique<document::AddObjectCommand>(obj, 0));

    ContourToolController controller(doc);
    assert(!controller.active());

    bool ok = controller.activate(obj.id);
    assert(ok);
    assert(controller.active());
    assert(controller.target_object() == obj.id);

    controller.set_direction(geometry::ContourDirection::Outside);
    controller.set_spacing_mm(5.0);
    controller.set_steps(2);
    controller.set_join_type(geometry::JoinType::Miter);

    assert(controller.has_preview());
    const auto& res = controller.preview_result();
    assert(res.steps.size() == 2);
    assert(res.steps[0].combined_path.bounds().width == 90.0);
    assert(res.steps[1].combined_path.bounds().width == 100.0);

    bool applied = controller.apply();
    assert(applied);

    bool broken = controller.break_apart();
    assert(broken);
    assert(doc.page().layers[0].object_ids.size() == 3);

    controller.deactivate();
    assert(!controller.active());

    std::cout << "All PikDraw Desktop Contour Tool Controller Tests passed!" << std::endl;
    return 0;
}
