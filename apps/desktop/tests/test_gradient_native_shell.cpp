#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/model/object.hpp"
#include "../src/shell/native_window_menus.hpp"
#include "../src/tools/gradient_tool_controller.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <variant>

using namespace pikdraw;
using namespace pikdraw::desktop;

namespace {

document::ObjectId add_test_rect(NativeWindow& window, double x, double y, double w, double h) {
    document::PathObject obj;
    obj.id = document::generate_object_id();
    obj.name = "Test Rect";
    obj.path = geometry::Path2D::make_rect(x, y, w, h);
    obj.has_fill = true;
    obj.fill = color::RGBColor{0.1, 0.5, 0.9, 1.0};
    auto res = window.root().document().execute_command(std::make_unique<document::AddObjectCommand>(obj, 0));
    assert(res.success);
    return obj.id;
}

void select_only(NativeWindow& window, document::ObjectId id) {
    window.root().vector_controller().selection().clear();
    window.root().vector_controller().selection().select(id);
}

void test_tool_activation_and_menu_routing() {
    std::cout << "--- Shell Test 1: Tool Activation and Menu Actions ---" << std::endl;
    NativeWindow window(1440, 900);
    const auto id = add_test_rect(window, 10.0, 10.0, 80.0, 50.0);
    select_only(window, id);

    // Initial state: tool 0 (Select)
    assert(window.active_tool() == 0);

    // Activate tool 13 directly
    window.set_active_tool(13);
    assert(window.active_tool() == 13);
    assert(window.gradient_tool() != nullptr);
    assert(window.gradient_tool()->is_active());

    // Switch away to tool 1 (Rectangle)
    window.set_active_tool(1);
    assert(window.active_tool() == 1);
    assert(!window.gradient_tool()->is_active());

    // Activate via MenuActionId::ToolGradient
    window.execute_menu_action(static_cast<int>(MenuActionId::ToolGradient));
    assert(window.active_tool() == 13);
    assert(window.gradient_tool()->is_active());

    // Activate via MenuActionId::ObjectFillGradient (should also open properties panel)
    window.set_active_tool(0);
    window.execute_menu_action(static_cast<int>(MenuActionId::ObjectFillGradient));
    assert(window.active_tool() == 13);
    assert(window.gradient_tool()->is_active());
}

void test_property_bar_interactions_and_apply() {
    std::cout << "--- Shell Test 2: Property Bar Controls and Apply (AC-G09) ---" << std::endl;
    NativeWindow window(1440, 900);
    const auto id = add_test_rect(window, 0.0, 0.0, 100.0, 60.0);
    select_only(window, id);

    window.set_active_tool(13);
    assert(window.active_tool() == 13);
    auto* grad_ctrl = window.gradient_tool();
    assert(grad_ctrl != nullptr && grad_ctrl->is_active());

    const int ribbon_y = 32 + 32 + 10; // within ribbon bounds [64, 104]

    // Click Radial (x = 180)
    bool clicked = window.handle_gradient_property_click(180, ribbon_y);
    assert(clicked);
    assert(grad_ctrl->presenter().draft_paint() && grad_ctrl->presenter().draft_paint()->kind == document::GradientKind::Radial);

    // Click Linear (x = 100)
    clicked = window.handle_gradient_property_click(100, ribbon_y);
    assert(clicked);
    assert(grad_ctrl->presenter().draft_paint() && grad_ctrl->presenter().draft_paint()->kind == document::GradientKind::Linear);

    // Click Spread Repeat (x = 300)
    clicked = window.handle_gradient_property_click(300, ribbon_y);
    assert(clicked);
    assert(grad_ctrl->presenter().draft_paint() && grad_ctrl->presenter().draft_paint()->spread == document::GradientSpread::Repeat);

    // Click Spread Reflect (x = 380)
    clicked = window.handle_gradient_property_click(380, ribbon_y);
    assert(clicked);
    assert(grad_ctrl->presenter().draft_paint() && grad_ctrl->presenter().draft_paint()->spread == document::GradientSpread::Reflect);

    // Click Reverse stops (x = 450)
    clicked = window.handle_gradient_property_click(450, ribbon_y);
    assert(clicked);

    // Click Angle +45 deg (x = 600)
    const double initial_angle = grad_ctrl->presenter().geometry_projection().angle_degrees;
    clicked = window.handle_gradient_property_click(600, ribbon_y);
    assert(clicked);
    const double new_angle = grad_ctrl->presenter().geometry_projection().angle_degrees;
    assert(std::abs(new_angle - std::fmod(initial_angle + 45.0, 360.0)) < 1e-4);

    // Click Add Stop (x = 700)
    const size_t stop_count = grad_ctrl->presenter().draft_paint()->stops.size();
    clicked = window.handle_gradient_property_click(700, ribbon_y);
    assert(clicked);
    assert(grad_ctrl->presenter().draft_paint()->stops.size() == stop_count + 1);

    // Click Apply (x = 780)
    clicked = window.handle_gradient_property_click(780, ribbon_y);
    assert(clicked);

    // Verify object now has fill_gradient committed to document
    const auto snap = window.root().document().current_snapshot();
    const auto obj = snap->objects().get(id);
    assert(obj);
    const auto* path = std::get_if<document::PathObject>(&*obj);
    assert(path && path->fill_gradient != nullptr);
    assert(path->fill_gradient->stops.size() == stop_count + 1);

    // Test Undo restores original solid
    const bool undo_res = window.root().document().undo();
    assert(undo_res);
    const auto snap_undo = window.root().document().current_snapshot();
    const auto obj_undo = snap_undo->objects().get(id);
    assert(obj_undo);
    const auto* path_undo = std::get_if<document::PathObject>(&*obj_undo);
    assert(path_undo && path_undo->fill_gradient == nullptr);
}

void test_toolbox_appearance_flyout_activation() {
    std::cout << "--- Shell Test 3: Toolbox Appearance Flyout Navigation ---" << std::endl;
    NativeWindow window(1440, 900);
    const auto id = add_test_rect(window, 0.0, 0.0, 50.0, 50.0);
    select_only(window, id);

    // Open Appearance group flyout via keyboard (Tab to Appearance group, Alt+Down)
    window.handle_toolbox_key(VK_TAB); // group 1
    window.handle_toolbox_key(VK_TAB); // group 2
    window.handle_toolbox_key(VK_TAB); // group 3 (Appearance)
    assert(window.toolbox_focus_group() == 3);

    // Open flyout: Alt + Down
    window.handle_toolbox_key(VK_DOWN, true);
    assert(window.toolbox_open_group() == 3);

    // In group 3: 4 items (0: Properties, 1: Gaussian Blur, 2: Drop Shadow, 3: Gradient)
    // Press Down 3 times to navigate to item 3
    window.handle_toolbox_key(VK_DOWN, false); // item 1
    window.handle_toolbox_key(VK_DOWN, false); // item 2
    window.handle_toolbox_key(VK_DOWN, false); // item 3 (Gradient)
    assert(window.toolbox_flyout_index() == 3);

    // Press Enter to activate tool 13
    window.handle_toolbox_key(VK_RETURN, false);
    assert(window.active_tool() == 13);
    assert(window.toolbox_open_group() == 0);
}

void test_canvas_drag_gesture_commits_gradient() {
    std::cout << "--- Shell Test 4: Canvas Drag Gesture (AC-G08) ---" << std::endl;
    NativeWindow window(1440, 900);
    const auto id = add_test_rect(window, 0.0, 0.0, 100.0, 80.0);
    select_only(window, id);

    window.set_active_tool(13);
    assert(window.active_tool() == 13);

    // Pointer gesture on canvas (world mm within object: (10, 10) to (90, 70))
    // We can call handle_gradient_pointer_down, move, up
    const auto area = window.canvas_rect(1440, 900);
    // Find screen coordinate for object-local (10, 10)
    const auto start_screen = window.root().renderer().camera().world_to_screen({10.0, 10.0});
    const auto end_screen = window.root().renderer().camera().world_to_screen({90.0, 70.0});

    const int sx = area.left + static_cast<int>(std::lround(start_screen.x));
    const int sy = area.top + static_cast<int>(std::lround(start_screen.y));
    const int ex = area.left + static_cast<int>(std::lround(end_screen.x));
    const int ey = area.top + static_cast<int>(std::lround(end_screen.y));

    // Drag start
    window.handle_mouse_down(sx, sy);
    // Drag move
    window.handle_mouse_move(ex, ey);
    // Drag release (commits gesture)
    window.handle_mouse_up(ex, ey);

    // Verify object committed with gradient fill
    const auto snap = window.root().document().current_snapshot();
    const auto obj = snap->objects().get(id);
    assert(obj);
    const auto* path = std::get_if<document::PathObject>(&*obj);
    assert(path && path->fill_gradient != nullptr);
    assert(path->fill_gradient->kind == document::GradientKind::Linear);
}

} // namespace

int main() {
    std::cout << "=== Running Gradient Native Shell Tests ===" << std::endl;
    test_tool_activation_and_menu_routing();
    test_property_bar_interactions_and_apply();
    test_toolbox_appearance_flyout_activation();
    test_canvas_drag_gesture_commits_gradient();
    std::cout << "=== All Gradient Native Shell Tests Passed! ===" << std::endl;
    return 0;
}
