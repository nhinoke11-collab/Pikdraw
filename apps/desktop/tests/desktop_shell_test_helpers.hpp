#pragma once
#include "pikdraw/desktop/shell/shell_geometry.hpp"
#include "pikdraw/desktop/shell/workspace_state.hpp"
#include "pikdraw/desktop/shell/status_bar_model.hpp"
#include "pikdraw/desktop/shell/desktop_window.hpp"
#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/commands/appearance_commands.hpp"
#include "pikdraw/typography/typography.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include "pikdraw/vector/gestures/selection_resize.hpp"
#include "../src/shell/native_window_menus.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <cassert>
#include <cmath>
#include <string_view>

using namespace pikdraw::desktop;



inline std::filesystem::path shell_screenshot_path(const char* filename) {
    const auto directory = std::filesystem::temp_directory_path() / "pikdraw-desktop-shell-test-20261017";
    std::filesystem::create_directories(directory);
    return directory / filename;
}

inline void capture_shell_screenshot(NativeWindow& window, const char* filename) {
    window.capture_screenshot_bmp(shell_screenshot_path(filename).string());
}



