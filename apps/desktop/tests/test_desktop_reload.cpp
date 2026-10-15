#include "pikdraw/desktop/development/ui_reload_controller.hpp"
#include "pikdraw/desktop/shell/desktop_window.hpp"
#include <iostream>
#include <cassert>

using namespace pikdraw::desktop;

void test_50_reloads_no_handler_leak() {
    DesktopWindow window;
    auto& controller = window.reload_controller();

    const uint64_t initial_gen = controller.current_generation();
    assert(initial_gen == 1);

    const std::string valid_qss = "QMainWindow { background-color: #070b10; }";
    const std::string valid_manifest = "{\"panel\": \"topbar\", \"class\": \"TopbarWidget\"}";

    // Execute 50 consecutive reloads
    for (int i = 0; i < 50; ++i) {
        auto report = window.trigger_f5_reload(valid_qss, valid_manifest);
        assert(report.status == ReloadStatus::Success);
        assert(report.ui_generation == initial_gen + i + 1);

        // Verify active handler count remains bounded (exactly 6 standard panel handlers)
        assert(report.active_handlers == 6);
    }

    assert(controller.reload_count() == 50);
    assert(controller.current_generation() == initial_gen + 50);
    assert(controller.active_handler_count() == 6);

    std::cout << "[PASS] test_50_reloads_no_handler_leak\n";
}

void test_malformed_qss_and_manifest_rollback() {
    DesktopWindow window;
    auto& controller = window.reload_controller();
    const uint64_t gen_before = controller.current_generation();

    // 1. Malformed QSS (unbalanced braces)
    const std::string broken_qss = "QMainWindow { background-color: #070b10; "; // missing closing brace
    const std::string valid_manifest = "{\"panel\": \"topbar\", \"class\": \"TopbarWidget\"}";

    auto report1 = window.trigger_f5_reload(broken_qss, valid_manifest);
    assert(report1.status == ReloadStatus::Rollback);
    assert(report1.ui_generation == gen_before); // Generation did not increment
    assert(controller.current_generation() == gen_before);

    // 2. Unauthorized foreign widget in manifest
    const std::string valid_qss = "QMainWindow { background-color: #070b10; }";
    const std::string malicious_manifest = "{\"panel\": \"topbar\", \"class\": \"MaliciousPluginWidget\"}";

    auto report2 = window.trigger_f5_reload(valid_qss, malicious_manifest);
    assert(report2.status == ReloadStatus::Rollback);
    assert(report2.ui_generation == gen_before);
    assert(controller.current_generation() == gen_before);

    std::cout << "[PASS] test_malformed_qss_and_manifest_rollback\n";
}

void test_state_preservation_across_reload() {
    DesktopWindow window;

    // Mutate state before reload
    window.tool_strip().set_active_tool(ToolMode::Ellipse);
    window.workspace_state().switch_workspace(WorkspaceType::InAn);
    window.status_bar().set_cursor_position(125.0, 340.0);
    window.status_bar().set_zoom_ratio(2.0);

    // Reload UI
    auto report = window.trigger_f5_reload();
    assert(report.status == ReloadStatus::Success);

    // Invariants preserved: tool mode, workspace, cursor, zoom
    assert(window.tool_strip().active_tool() == ToolMode::Ellipse);
    assert(window.workspace_state().active_workspace() == WorkspaceType::InAn);
    assert(window.status_bar().cursor_x() == 125.0);
    assert(window.status_bar().cursor_y() == 340.0);
    assert(window.status_bar().zoom_ratio() == 2.0);

    std::cout << "[PASS] test_state_preservation_across_reload\n";
}

int main() {
    std::cout << "=== Running Desktop Reload Tests ===\n";
    test_50_reloads_no_handler_leak();
    test_malformed_qss_and_manifest_rollback();
    test_state_preservation_across_reload();
    std::cout << "All Desktop Reload Tests PASSED.\n";
    return 0;
}
