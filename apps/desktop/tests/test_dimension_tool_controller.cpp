#include "pikdraw/document/document.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "../src/tools/dimension_tool_controller.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::desktop::tools;

int main() {
    std::cout << "=== Running PikDraw Desktop Dimension Tool Controller Tests ===" << std::endl;

    document::Document doc("test-desktop-dimension");

    // Add a source box: (0, 0) to (80, 50)
    document::PathObject box;
    box.id = document::generate_object_id();
    box.name = "Test Box";
    box.path = geometry::Path2D::make_rect(0.0, 0.0, 80.0, 50.0);
    doc.execute_command(std::make_unique<document::AddObjectCommand>(box, 0));

    DimensionToolController controller(doc);
    assert(!controller.active());

    // 1. Activation
    controller.activate(document::DimensionKind::Aligned);
    assert(controller.active());
    assert(controller.state() == DimensionSessionState::Idle);
    assert(!controller.has_preview());

    // 2. Interactive Aligned Dimension Creation
    // Step 1: Click at (0, 0)
    bool p1 = controller.handle_pointer_press({0.0, 0.0}, box.id, 0);
    assert(p1);
    assert(controller.state() == DimensionSessionState::PickSecond);

    // Step 2: Click at (80, 0)
    bool p2 = controller.handle_pointer_press({80.0, 0.0}, box.id, 1);
    assert(p2);
    assert(controller.state() == DimensionSessionState::Placement);
    assert(controller.has_preview());
    assert(std::abs(controller.preview_layout().raw_value - 80.0) < 1e-6);

    // Step 3: Move placement offset to (40, -20)
    controller.handle_pointer_move({40.0, -20.0});
    assert(controller.has_preview());

    // Step 4: Final click at (40, -20) commits the dimension!
    bool p3 = controller.handle_pointer_press({40.0, -20.0});
    assert(p3);
    assert(controller.state() == DimensionSessionState::Idle);
    assert(!controller.has_preview());
    // Document now has 2 objects (box + dimension)
    assert(doc.page().layers[0].object_ids.size() == 2);

    // 3. Test Undo/Redo
    bool undo_ok = doc.undo();
    assert(undo_ok);
    assert(doc.page().layers[0].object_ids.size() == 1);

    bool redo_ok = doc.redo();
    assert(redo_ok);
    assert(doc.page().layers[0].object_ids.size() == 2);

    // 4. Test Escape handling
    controller.activate(document::DimensionKind::Linear);
    controller.set_linear_orientation(geometry::LinearOrientation::Vertical);
    assert(controller.linear_orientation() == geometry::LinearOrientation::Vertical);

    controller.handle_pointer_press({0.0, 0.0});
    assert(controller.state() == DimensionSessionState::PickSecond);

    controller.handle_pointer_press({0.0, 50.0});
    assert(controller.state() == DimensionSessionState::Placement);
    assert(controller.has_preview());

    // Esc cancels placement -> returns to PickSecond
    bool esc1 = controller.handle_escape();
    assert(esc1);
    assert(controller.state() == DimensionSessionState::PickSecond);
    assert(!controller.has_preview());

    // Esc again -> returns to Idle
    bool esc2 = controller.handle_escape();
    assert(esc2);
    assert(controller.state() == DimensionSessionState::Idle);

    // Esc when Idle -> returns false
    bool esc3 = controller.handle_escape();
    assert(!esc3);

    // 5. Test Smart Snap & Ortho
    controller.activate(document::DimensionKind::Aligned);
    // Near box corner (0, 0)
    controller.update_snap({2.0, 1.0}, 1.0, false);
    assert(controller.snap_active());
    assert(std::abs(controller.snap_point().x - 0.0) < 1e-4);
    assert(std::abs(controller.snap_point().y - 0.0) < 1e-4);

    // Near midpoint of top edge (40, 0)
    controller.update_snap({39.5, 1.5}, 1.0, false);
    assert(controller.snap_active());
    assert(controller.snap_type() == DimensionSnapType::Midpoint);
    assert(std::abs(controller.snap_point().x - 40.0) < 1e-4);
    assert(std::abs(controller.snap_point().y - 0.0) < 1e-4);

    // Near center (40, 25)
    controller.update_snap({41.0, 24.5}, 1.0, false);
    assert(controller.snap_active());
    assert(controller.snap_type() == DimensionSnapType::Center);
    assert(std::abs(controller.snap_point().x - 40.0) < 1e-4);
    assert(std::abs(controller.snap_point().y - 25.0) < 1e-4);

    // Ortho constraint
    controller.update_snap({60.0, 10.0}, 1.0, true);
    assert(controller.ortho_active());

    // 6. Test Snap-assisted pointer press
    controller.update_snap({1.5, 0.5}, 1.0, false);
    assert(controller.snap_active());
    controller.handle_pointer_press({1.5, 0.5});
    assert(controller.state() == DimensionSessionState::PickSecond);
    assert(std::abs(controller.first_point().x - 0.0) < 1e-4);
    assert(std::abs(controller.first_point().y - 0.0) < 1e-4);

    controller.deactivate();
    assert(!controller.active());

    std::cout << "All Desktop Dimension Tool Controller Tests passed successfully!" << std::endl;
    return 0;
}
