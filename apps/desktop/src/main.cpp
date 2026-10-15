#include "pikdraw/desktop/shell/desktop_window.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    std::cout << "PikDraw Professional Vector Editor 2026 (Milestone M1 Native Shell)\n";

    pikdraw::desktop::DesktopWindow window(1920.0, 1080.0, 1.0);
    const auto area = window.canvas_area();

    std::cout << "Shell Initialized:\n"
              << "  Canvas Viewport Area: " << area.width << " x " << area.height
              << " (Offset X=" << area.x << ", Y=" << area.y << ")\n"
              << "  Registered Actions: " << window.action_dispatcher().action_count() << "\n"
              << "  Active Workspace: " << window.workspace_state().active_info().title_vi << "\n"
              << "  Status: " << window.status_bar().format_coordinates_vi() << "\n";

    return 0;
}
