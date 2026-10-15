#include "pikdraw/desktop/shell/native_window.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::cout << "PikDraw Professional Vector Editor 2026 (Milestone M1 Native Shell)\n";

    bool screenshot_mode = false;
    std::string screenshot_path;
    bool headless = false;
    int timeout_ms = 0;
    pikdraw::desktop::UILanguage initial_lang = pikdraw::desktop::UILanguage::Vietnamese;
    pikdraw::desktop::ThemeMode initial_theme = pikdraw::desktop::ThemeMode::CyberDark;
    bool open_settings = false;
    int settings_tab = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--screenshot" && i + 1 < argc) {
            screenshot_mode = true;
            screenshot_path = argv[++i];
        } else if (arg == "--headless") {
            headless = true;
        } else if (arg == "--timeout" && i + 1 < argc) {
            timeout_ms = std::stoi(argv[++i]) * 1000;
        } else if (arg == "--lang" && i + 1 < argc) {
            std::string l = argv[++i];
            initial_lang = (l == "en" || l == "EN") ?
                pikdraw::desktop::UILanguage::English :
                pikdraw::desktop::UILanguage::Vietnamese;
        } else if (arg == "--theme" && i + 1 < argc) {
            std::string t = argv[++i];
            initial_theme = (t == "light" || t == "LIGHT" || t == "Light") ?
                pikdraw::desktop::ThemeMode::Light :
                pikdraw::desktop::ThemeMode::CyberDark;
        } else if (arg == "--settings" || arg == "--settings-modal") {
            open_settings = true;
        } else if (arg == "--settings-tab" && i + 1 < argc) {
            open_settings = true;
            settings_tab = std::stoi(argv[++i]);
        }
    }

    pikdraw::desktop::NativeWindow window(1440, 900);
    window.set_language(initial_lang);
    window.set_theme(initial_theme);
    if (open_settings) {
        window.open_settings_modal();
        window.set_settings_tab(settings_tab);
    }

    const auto area = window.shell().canvas_area();
    std::cout << "Shell Initialized:\n"
              << "  Canvas Viewport Area: " << area.width << " x " << area.height
              << " (Offset X=" << area.x << ", Y=" << area.y << ")\n"
              << "  Registered Actions: " << window.shell().action_dispatcher().action_count() << "\n"
              << "  Active Workspace: " << window.shell().workspace_state().active_info().title_vi << "\n"
              << "  Status: " << window.shell().status_bar().format_coordinates_vi() << "\n";

    if (screenshot_mode) {
        window.capture_screenshot_bmp(screenshot_path);
        std::cout << "Screenshot captured to: " << screenshot_path << "\n";
        return 0;
    }

    if (headless) {
        return 0;
    }

    if (!window.initialize("PikDraw 2026 - Bản vẽ không tên [Vector Workspace]")) {
        std::cerr << "Failed to create native desktop window.\n";
        return 1;
    }

#ifdef _WIN32
    HWND console_hwnd = GetConsoleWindow();
    if (console_hwnd) {
        ShowWindow(console_hwnd, SW_HIDE);
    }

    if (timeout_ms > 0 && window.hwnd()) {
        SetTimer(window.hwnd(), 1001, timeout_ms, [](HWND hwnd, UINT, UINT_PTR, DWORD) {
            DestroyWindow(hwnd);
        });
    }
#endif

    return window.run();
}
