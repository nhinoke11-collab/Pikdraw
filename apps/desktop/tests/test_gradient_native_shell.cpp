#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/model/object.hpp"
#include "../src/shell/native_window_menus.hpp"
#include "../src/shell/native_window_gradient_properties_paint.hpp"
#include "../src/tools/gradient_tool_controller.hpp"

#include <cassert>
#include <cmath>
#include <cstdlib>
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

    // 1. Topbar Ribbon: Radial (x = 180)
    bool clicked = window.handle_gradient_property_click(180, ribbon_y);
    assert(clicked);
    assert(grad_ctrl->presenter().draft_paint() && grad_ctrl->presenter().draft_paint()->kind == document::GradientKind::Radial);

    // 2. Topbar Ribbon: Linear (x = 100)
    clicked = window.handle_gradient_property_click(100, ribbon_y);
    assert(clicked);
    assert(grad_ctrl->presenter().draft_paint() && grad_ctrl->presenter().draft_paint()->kind == document::GradientKind::Linear);

    // 3. Topbar Ribbon: Reverse stops (x = 250)
    const uint64_t orig_stop0_id = grad_ctrl->presenter().draft_paint()->stops.front().id;
    clicked = window.handle_gradient_property_click(250, ribbon_y);
    assert(clicked);
    assert(grad_ctrl->presenter().draft_paint()->stops.back().id == orig_stop0_id);

    // 4. Topbar Ribbon: Properties Jump (x = 350)
    window.set_active_dock_tab(1);
    clicked = window.handle_gradient_property_click(350, ribbon_y);
    assert(clicked);
    assert(window.active_dock_tab() == 0);

    // 5. Properties Dock: Spread Repeat & Reflect
    constexpr int inspector_left = 1440 - 280;
    constexpr int panel_left = inspector_left + GradientPropertiesLayout::content_left;
    constexpr int panel_top = 38 + 28 + 36 + 36;
    const int panel_width = GradientPropertiesLayout::content_width;
    const int spread_width = (panel_width - 12) / 3;

    clicked = window.handle_gradient_property_click(panel_left + spread_width + 8,
                                                   panel_top + GradientPropertiesLayout::spread_top + 10);
    assert(clicked);
    assert(grad_ctrl->presenter().draft_paint() && grad_ctrl->presenter().draft_paint()->spread == document::GradientSpread::Repeat);

    clicked = window.handle_gradient_property_click(panel_left + (spread_width + 6) * 2 + 8,
                                                   panel_top + GradientPropertiesLayout::spread_top + 10);
    assert(clicked);
    assert(grad_ctrl->presenter().draft_paint() && grad_ctrl->presenter().draft_paint()->spread == document::GradientSpread::Reflect);

    // 6. Properties Dock: Add Stop
    const size_t stop_count = grad_ctrl->presenter().draft_paint()->stops.size();
    const int stop_action_width = (panel_width - 18) / 4;
    clicked = window.handle_gradient_property_click(panel_left + (stop_action_width + 6) * 1 + 10,
                                                   panel_top + GradientPropertiesLayout::stop_actions_top + 10);
    assert(clicked);
    assert(grad_ctrl->presenter().draft_paint()->stops.size() == stop_count + 1);

    // 7. Properties Dock: Apply
    clicked = window.handle_gradient_property_click(panel_left + 20,
                                                   panel_top + GradientPropertiesLayout::actions_top + 10);
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

    // The current Appearance group also includes Contour before Gradient.
    // Traverse every preceding item so keyboard routing remains covered.
    window.handle_toolbox_key(VK_DOWN, false); // item 1
    window.handle_toolbox_key(VK_DOWN, false); // item 2
    window.handle_toolbox_key(VK_DOWN, false); // item 3 (Contour)
    window.handle_toolbox_key(VK_DOWN, false); // item 4 (Gradient)
    assert(window.toolbox_flyout_index() == 4);

    // Press Enter to activate tool 13
    window.handle_toolbox_key(VK_RETURN, false);
    assert(window.active_tool() == 13);
    assert(window.toolbox_open_group() == 0);

    // Also test direct toolbox button click on row 10 (Gradient, y = 550)
    window.set_active_tool(0);
    window.handle_mouse_down(26, 550);
    assert(window.active_tool() == 13);
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

void test_properties_stop_and_geometry_draft() {
    std::cout << "--- Shell Test 5: Gradient Properties draft editing ---" << std::endl;
    NativeWindow window(1440, 900);
    const auto id = add_test_rect(window, 5.0, 8.0, 90.0, 55.0);
    select_only(window, id);
    window.set_active_tool(13);
    auto* controller = window.gradient_tool();
    assert(controller && controller->active());
    if (const char* evidence_path = std::getenv("PIKDRAW_GRADIENT_PROPERTIES_SCREENSHOT"))
        window.capture_screenshot_bmp(evidence_path);
    const auto capture_layout = [&](const char* variable, int width, int height, UILanguage language) {
        const char* evidence_path = std::getenv(variable);
        if (!evidence_path) return;
        NativeWindow specimen(width, height);
        specimen.set_language(language);
        const auto specimen_id = add_test_rect(specimen, 0.0, 0.0, 60.0, 45.0);
        select_only(specimen, specimen_id);
        specimen.set_active_tool(13);
        specimen.capture_screenshot_bmp(evidence_path);
    };
    capture_layout("PIKDRAW_GRADIENT_PROPERTIES_1366_VI", 1366, 768, UILanguage::Vietnamese);
    capture_layout("PIKDRAW_GRADIENT_PROPERTIES_1920_EN", 1920, 1080, UILanguage::English);
    const auto revision = window.root().document().revision();
    constexpr int inspector_left = 1440 - 280;
    constexpr int panel_left = inspector_left + 14;
    constexpr int panel_top = 38 + 28 + 36 + 36;
    const int input_x = panel_left + 124;
    const auto enter_value = [&](const std::wstring& value) {
        const size_t existing = window.numeric_input_buffer().size();
        for (size_t i = 0; i < existing; ++i) window.handle_numeric_char_input(8);
        for (const wchar_t ch : value) window.handle_numeric_char_input(ch);
        window.handle_numeric_char_input(13);
    };

    assert(window.handle_gradient_property_click(input_x, panel_top + GradientPropertiesLayout::offset_top + 12));
    assert(window.active_numeric_field() == NativeWindow::NumericFieldId::GradientStopOffset);
    enter_value(L"25");
    assert(controller->presenter().is_dirty());
    assert(window.root().document().revision() == revision);
    const auto* draft = controller->presenter().draft_paint();
    assert(draft && std::abs(draft->stops.front().offset - 0.25) < 1e-6);

    assert(window.handle_gradient_property_click(input_x, panel_top + GradientPropertiesLayout::opacity_top + 12));
    assert(window.active_numeric_field() == NativeWindow::NumericFieldId::GradientStopOpacity);
    enter_value(L"80");
    assert(std::abs(controller->presenter().draft_paint()->stops.front().opacity - 0.8) < 1e-6);

    assert(window.handle_gradient_property_click(input_x, panel_top + GradientPropertiesLayout::midpoint_top + 12));
    assert(window.active_numeric_field() == NativeWindow::NumericFieldId::GradientStopMidpoint);
    enter_value(L"60");
    assert(std::abs(controller->presenter().draft_paint()->stops.front().midpoint_to_next - 0.6) < 1e-6);

    assert(window.handle_gradient_property_click(input_x, panel_top + GradientPropertiesLayout::angle_top + 12));
    assert(window.active_numeric_field() == NativeWindow::NumericFieldId::GradientAngle);
    enter_value(L"35");
    assert(std::abs(controller->presenter().geometry_projection().angle_degrees - 35.0) < 1e-6);
    assert(window.root().document().revision() == revision);

    assert(window.handle_gradient_property_click(panel_left + 10, panel_top + GradientPropertiesLayout::actions_top + 12));
    assert(!controller->presenter().is_dirty());
    const auto applied_snapshot = window.root().document().current_snapshot();
    const auto applied_object = applied_snapshot->objects().get(id);
    assert(applied_object);
    const auto* applied_path = std::get_if<document::PathObject>(&*applied_object);
    assert(applied_path && applied_path->fill_gradient);
    assert(std::abs(applied_path->fill_gradient->stops.front().offset - 0.25) < 1e-6);
    assert(std::abs(applied_path->fill_gradient->stops.front().opacity - 0.8) < 1e-6);
    assert(std::abs(applied_path->fill_gradient->stops.front().midpoint_to_next - 0.6) < 1e-6);
    assert(std::abs(project_gradient_geometry(applied_path->fill_gradient->kind,
        applied_path->fill_gradient->gradient_to_object).angle_degrees - 35.0) < 1e-5);

    assert(window.handle_gradient_property_click(input_x, panel_top + GradientPropertiesLayout::offset_top + 12));
    enter_value(L"35");
    assert(controller->presenter().is_dirty());
    assert(window.handle_gradient_property_click(panel_left + 140, panel_top + GradientPropertiesLayout::actions_top + 12));
    assert(!controller->presenter().is_dirty());
    const auto cancelled_snapshot = window.root().document().current_snapshot();
    const auto cancelled_object = cancelled_snapshot->objects().get(id);
    const auto* cancelled_path = cancelled_object
        ? std::get_if<document::PathObject>(&*cancelled_object) : nullptr;
    assert(cancelled_path && cancelled_path->fill_gradient);
    assert(std::abs(cancelled_path->fill_gradient->stops.front().offset - 0.25) < 1e-6);
}

void test_properties_stop_list_controls() {
    std::cout << "--- Shell Test 6: Gradient Properties stop list controls ---" << std::endl;
    NativeWindow window(1440, 900);
    const auto id = add_test_rect(window, 0.0, 0.0, 60.0, 45.0);
    select_only(window, id);
    window.set_active_tool(13);
    auto* controller = window.gradient_tool();
    assert(controller && controller->presenter().draft_paint());
    constexpr int panel_left = 1440 - 280 + 14;
    constexpr int panel_top = 38 + 28 + 36 + 36;
    const size_t initial_count = controller->presenter().draft_paint()->stops.size();
    const uint64_t initial_selected = controller->presenter().selected_stop_id();

    assert(window.handle_gradient_property_click(panel_left + 74, panel_top + GradientPropertiesLayout::stop_actions_top + 12));
    const auto* after_add = controller->presenter().draft_paint();
    assert(after_add && after_add->stops.size() == initial_count + 1);
    const uint64_t inserted_id = controller->presenter().selected_stop_id();
    assert(inserted_id != 0 && inserted_id != initial_selected);
    assert(std::abs(after_add->stops[1].offset - 0.5) < 1e-6);

    assert(window.handle_gradient_property_click(panel_left + 202, panel_top + GradientPropertiesLayout::stop_actions_top + 12));
    assert(controller->presenter().selected_stop_id() == after_add->stops.back().id);
    assert(window.handle_gradient_property_click(panel_left + 138, panel_top + GradientPropertiesLayout::stop_actions_top + 12));
    assert(controller->presenter().draft_paint()->stops.size() == initial_count);
    assert(!controller->presenter().has_draft() || controller->presenter().is_dirty());
    assert(window.handle_gradient_property_click(panel_left + 140, panel_top + GradientPropertiesLayout::actions_top + 12));
    assert(!controller->presenter().is_dirty());
}

void test_properties_ramp_and_presets() {
    std::cout << "--- Shell Test 7: Gradient Ramp, Presets, Reverse & Distribute ---" << std::endl;
    NativeWindow window(1440, 900);
    const auto id = add_test_rect(window, 0.0, 0.0, 60.0, 45.0);
    select_only(window, id);
    window.set_active_tool(13);
    auto* controller = window.gradient_tool();
    assert(controller && controller->presenter().draft_paint());
    constexpr int panel_left = 1440 - 280 + 14;
    constexpr int panel_top = 38 + 28 + 36 + 36;

    // 1. Click preset (Gold preset = index 0)
    assert(window.handle_gradient_property_click(panel_left + 10, panel_top + GradientPropertiesLayout::presets_top + 10));
    assert(controller->presenter().is_dirty());
    const auto* gold_draft = controller->presenter().draft_paint();
    assert(gold_draft && gold_draft->stops.size() == 3);

    // 2. Click Reverse button (half width left of align_top)
    assert(window.handle_gradient_property_click(panel_left + 30, panel_top + GradientPropertiesLayout::align_top + 10));
    const auto* rev_draft = controller->presenter().draft_paint();
    assert(rev_draft && rev_draft->stops.size() == 3);

    // 3. Click Distribute button (half width right of align_top)
    assert(window.handle_gradient_property_click(panel_left + 160, panel_top + GradientPropertiesLayout::align_top + 10));
    const auto* dist_draft = controller->presenter().draft_paint();
    assert(dist_draft && dist_draft->stops.size() == 3);
    assert(std::abs(dist_draft->stops[0].offset - 0.0) < 1e-6);
    assert(std::abs(dist_draft->stops[1].offset - 0.50) < 1e-6);
    assert(std::abs(dist_draft->stops[2].offset - 1.00) < 1e-6);

    // 4. Cancel
    assert(window.handle_gradient_property_click(panel_left + 140, panel_top + GradientPropertiesLayout::actions_top + 12));
    assert(!controller->presenter().is_dirty());
}

} // namespace

int main() {
    std::cout << "=== Running Gradient Native Shell Tests ===" << std::endl;
    test_tool_activation_and_menu_routing();
    test_property_bar_interactions_and_apply();
    test_toolbox_appearance_flyout_activation();
    test_canvas_drag_gesture_commits_gradient();
    test_properties_stop_and_geometry_draft();
    test_properties_stop_list_controls();
    test_properties_ramp_and_presets();
    std::cout << "=== All Gradient Native Shell Tests Passed! ===" << std::endl;
    return 0;
}
