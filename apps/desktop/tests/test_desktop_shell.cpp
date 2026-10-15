#include "pikdraw/desktop/shell/shell_geometry.hpp"
#include "pikdraw/desktop/shell/workspace_state.hpp"
#include "pikdraw/desktop/shell/status_bar_model.hpp"
#include "pikdraw/desktop/shell/desktop_window.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace pikdraw::desktop;

void test_shell_geometry_resolutions_and_dpi() {
    // 1920x1080 @ 100% DPI
    auto area1080 = ShellGeometry::compute_canvas_area(1920.0, 1080.0, 1.0, true, 300.0, 52.0);
    assert(area1080.x == 52.0);
    assert(area1080.y == 72.0); // 38 + 34
    assert(area1080.width == 1920.0 - 52.0 - 300.0);
    assert(area1080.height == 1080.0 - 72.0 - 26.0);

    // 1366x768 @ 100% DPI
    auto area768 = ShellGeometry::compute_canvas_area(1366.0, 768.0, 1.0, true, 280.0, 48.0);
    assert(area768.x == 48.0);
    assert(area768.y == 72.0);
    assert(area768.width == 1366.0 - 48.0 - 280.0);
    assert(area768.height == 768.0 - 72.0 - 26.0);

    // DPI 150% scaling
    auto areaDpi150 = ShellGeometry::compute_canvas_area(1920.0, 1080.0, 1.5, true, 300.0, 52.0);
    assert(areaDpi150.x == 52.0 * 1.5);
    assert(areaDpi150.y == (38.0 + 34.0) * 1.5);
    assert(areaDpi150.width == 1920.0 - (52.0 + 300.0) * 1.5);

    // DPI 200% scaling
    auto areaDpi200 = ShellGeometry::compute_canvas_area(3840.0, 2160.0, 2.0, true, 300.0, 52.0);
    assert(areaDpi200.x == 104.0);
    assert(areaDpi200.y == 144.0);

    std::cout << "[PASS] test_shell_geometry_resolutions_and_dpi\n";
}

void test_workspace_state_and_capabilities() {
    WorkspaceStateManager mgr;
    assert(mgr.active_workspace() == WorkspaceType::Vector);
    assert(mgr.active_info().capability == WorkspaceCapability::Operational);

    // Verify 4 workspaces exist
    const auto& all = mgr.all_workspaces();
    assert(all.size() == 4);

    // Verify non-vector workspaces report M2 placeholder capability
    bool found_inan = false;
    bool found_thicong = false;
    bool found_3d = false;

    for (const auto& ws : all) {
        if (ws.type == WorkspaceType::InAn) {
            found_inan = true;
            assert(ws.capability == WorkspaceCapability::PlaceholderM2);
        } else if (ws.type == WorkspaceType::ThiCong) {
            found_thicong = true;
            assert(ws.capability == WorkspaceCapability::PlaceholderM2);
        } else if (ws.type == WorkspaceType::Scene3D) {
            found_3d = true;
            assert(ws.capability == WorkspaceCapability::PlaceholderM2);
        }
    }

    assert(found_inan && found_thicong && found_3d);

    // Switch workspace
    assert(mgr.switch_workspace(WorkspaceType::InAn));
    assert(mgr.active_workspace() == WorkspaceType::InAn);
    assert(mgr.active_info().title_vi == "In ấn");

    std::cout << "[PASS] test_workspace_state_and_capabilities\n";
}

void test_status_bar_metrics_and_vietnamese_labels() {
    StatusBarModel status;
    status.set_cursor_position(20000.0, 1000.0);
    status.set_zoom_ratio(1.25);
    status.set_selection_count(3);
    status.set_document_state(42, true);

    std::string coords = status.format_coordinates_vi();
    assert(coords.find("20000.00 mm") != std::string::npos);
    assert(coords.find("1000.00 mm") != std::string::npos);

    std::string zoom = status.format_zoom_vi();
    assert(zoom == "125%");

    std::string sel = status.format_selection_vi();
    assert(sel == "Đã chọn 3 đối tượng");

    std::string rev = status.format_document_revision_vi();
    assert(rev.find("Rev 42") != std::string::npos);
    assert(rev.find("Chưa lưu") != std::string::npos);

    std::cout << "[PASS] test_status_bar_metrics_and_vietnamese_labels\n";
}

int main() {
    std::cout << "=== Running Desktop Shell Tests ===\n";
    test_shell_geometry_resolutions_and_dpi();
    test_workspace_state_and_capabilities();
    test_status_bar_metrics_and_vietnamese_labels();
    std::cout << "All Desktop Shell Tests PASSED.\n";
    return 0;
}
