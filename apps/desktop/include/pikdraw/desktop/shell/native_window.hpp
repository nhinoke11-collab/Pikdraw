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

/// Native Win32 window implementing the Cyber Dark Design System 2026
class NativeWindow {
public:
    NativeWindow(int width = 1440, int height = 900);
    ~NativeWindow();

    bool initialize(const std::string& title = "PikDraw 2026 - Bản vẽ không tên [Vector Workspace]");
    int run();

    void capture_screenshot_bmp(const std::string& path);

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

    HWND hwnd_{nullptr};
    HINSTANCE hinstance_{nullptr};
#endif
    int width_{1440};
    int height_{900};
    DesktopWindow shell_;
    CompositionRoot root_;
    DocumentWorkflow workflow_;
};

} // namespace pikdraw::desktop
