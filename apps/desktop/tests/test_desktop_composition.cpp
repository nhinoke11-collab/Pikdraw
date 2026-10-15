#include "pikdraw/desktop/runtime/composition_root.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include <iostream>
#include <cassert>

using namespace pikdraw::desktop;

void test_composition_root_lifecycle() {
    CompositionRoot root;
    assert(!root.is_initialized());

    root.initialize(1920.0, 1080.0, 1.0);
    assert(root.is_initialized());

    // Verify owned services are instantiated
    assert(root.document().current_snapshot()->revision() == 0);
    assert(root.vector_controller().mode() == pikdraw::vector::ToolMode::Select);
    assert(root.window().window_width() == 1920.0);
    assert(root.window().window_height() == 1080.0);

    // Verify actions wired to services
    auto& dispatcher = root.window().action_dispatcher();
    assert(dispatcher.action_count() >= 18);

    // Test tool switching via action dispatcher
    assert(dispatcher.dispatch(actions::ActionId::kToolRectangle));
    assert(root.window().tool_strip().active_tool() == ToolMode::Rectangle);
    assert(root.vector_controller().mode() == pikdraw::vector::ToolMode::Rectangle);

    // Clean teardown
    root.shutdown();
    assert(!root.is_initialized());

    std::cout << "[PASS] test_composition_root_lifecycle\n";
}

int main() {
    std::cout << "=== Running Desktop Composition Tests ===\n";
    test_composition_root_lifecycle();
    std::cout << "All Desktop Composition Tests PASSED.\n";
    return 0;
}
