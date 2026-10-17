#include "pikdraw/document/document.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/vector/tools/contour/contour_tool.hpp"
#include <cassert>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::vector::tools::contour;

int main() {
    std::cout << "=== Running PikDraw Vector Contour Tool Tests ===" << std::endl;

    document::Document doc("test-tool-doc");

    document::PathObject obj;
    obj.id = document::generate_object_id();
    obj.name = "Test Box";
    obj.path = geometry::Path2D::make_rect(0.0, 0.0, 100.0, 100.0);
    doc.execute_command(std::make_unique<document::AddObjectCommand>(obj, 0));

    ContourTool tool(doc);
    tool.set_target_object(obj.id);
    tool.set_direction(geometry::ContourDirection::Outside);
    tool.set_spacing_mm(10.0);
    tool.set_steps(2);
    tool.set_join_type(geometry::JoinType::Miter);

    assert(tool.has_preview());
    const auto& prev = tool.preview_result();
    assert(prev.steps.size() == 2);
    assert(prev.steps[0].combined_path.bounds().width == 120.0);
    assert(prev.steps[1].combined_path.bounds().width == 140.0);

    bool applied = tool.apply();
    assert(applied);

    auto attached = document::get_object_contour(doc, obj.id);
    assert(attached.has_value());
    assert(attached->steps == 2);

    bool broken = tool.break_apart();
    assert(broken);
    assert(!document::get_object_contour(doc, obj.id).has_value());
    assert(doc.page().layers[0].object_ids.size() == 3);

    std::cout << "All PikDraw Vector Contour Tool Tests passed!" << std::endl;
    return 0;
}
