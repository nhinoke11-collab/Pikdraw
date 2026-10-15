#pragma once

#include "pikdraw/desktop/shell/desktop_window.hpp"
#include "pikdraw/desktop/runtime/composition_root.hpp"
#include "pikdraw/desktop/workflows/document_workflow.hpp"

#include <string>
#include <memory>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace pikdraw::desktop {

enum class UILanguage {
    Vietnamese,
    English
};

/// Native Win32 window implementing the Cyber Dark Design System 2026 with full interactivity
class NativeWindow {
public:
    NativeWindow(int width = 1440, int height = 900);
    ~NativeWindow();

    bool initialize(const std::string& title = "PikDraw 2026 - Bản vẽ không tên [Vector Workspace]");
    int run();

    void capture_screenshot_bmp(const std::string& path);

    [[nodiscard]] static std::wstring utf8_to_wide(const std::string& utf8);

    [[nodiscard]] UILanguage language() const noexcept { return language_; }
    void set_language(UILanguage lang) noexcept;
    void toggle_language() noexcept;

    [[nodiscard]] int active_tool() const noexcept { return active_tool_; }
    void set_active_tool(int tool_index) noexcept;

    [[nodiscard]] int active_workspace() const noexcept { return active_workspace_; }
    void set_active_workspace(int ws_index) noexcept;

    [[nodiscard]] int active_dock_tab() const noexcept { return active_dock_tab_; }
    void set_active_dock_tab(int tab_index) noexcept;

    void zoom_in() noexcept;
    void zoom_out() noexcept;
    void trigger_export() noexcept;

    [[nodiscard]] DesktopWindow& shell() noexcept { return shell_; }
    [[nodiscard]] const DesktopWindow& shell() const noexcept { return shell_; }

    [[nodiscard]] CompositionRoot& root() noexcept { return root_; }
    [[nodiscard]] const CompositionRoot& root() const noexcept { return root_; }

    [[nodiscard]] DocumentWorkflow& workflow() noexcept { return workflow_; }
    [[nodiscard]] const DocumentWorkflow& workflow() const noexcept { return workflow_; }

#ifdef _WIN32
    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }
#endif

private:
#ifdef _WIN32
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    void paint(HDC hdc, int width, int height);
    void handle_mouse_down(int x, int y);
    void handle_mouse_move(int x, int y);
    void handle_mouse_up(int x, int y);

    HWND hwnd_{nullptr};
    HINSTANCE hinstance_{nullptr};
#endif
    int width_{1440};
    int height_{900};
    UILanguage language_{UILanguage::Vietnamese};

    // Interactive UI State
    int active_tool_{0}; // 0: Select (V), 1: Node (A), 2: Pen (P), 3: Rect (R), 4: Ellipse (E), 5: Text (T), 6: Hand (H), 7: Zoom (Z)
    int active_workspace_{0}; // 0: Vector, 1: Print, 2: Production, 3: 3D
    int active_dock_tab_{0}; // 0: Inspector, 1: Layers, 2: History
    int zoom_level_{100}; // 50, 75, 100, 125, 150, 200

    // Canvas object geometry (mm)
    double shape_x_{100.0};
    double shape_y_{150.0};
    double shape_w_{1850.0};
    double shape_h_{420.0};
    double cursor_mm_x_{100.0};
    double cursor_mm_y_{150.0};

    // Dragging state
    bool is_dragging_{false};
    int drag_start_x_{0};
    int drag_start_y_{0};
    double drag_init_shape_x_{100.0};
    double drag_init_shape_y_{150.0};

    std::string status_msg_{"Sẵn sàng | Nhấp chuột để tương tác các công cụ"};

    DesktopWindow shell_;
    CompositionRoot root_;
    DocumentWorkflow workflow_;
};

} // namespace pikdraw::desktop
