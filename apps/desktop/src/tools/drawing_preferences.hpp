#pragma once

#include "pikdraw/vector/precision/snap/snap_config.hpp"

#include <filesystem>
#include <string>

namespace pikdraw::desktop::tools {

struct DrawingPreferences {
    std::string profile{"PikDraw"};
    bool snap_master{true};
    bool endpoint{true};
    bool source_node{true};
    bool line_midpoint{true};
    bool grid{true};
    vector::precision::snap::DirectionMode direction{
        vector::precision::snap::DirectionMode::Free};
    double polar_step_degrees{45.0};
};

struct PreferencesLoadResult {
    DrawingPreferences value;
    bool loaded{false};
    std::string diagnostic;
};

[[nodiscard]] std::filesystem::path drawing_preferences_path();
[[nodiscard]] PreferencesLoadResult load_drawing_preferences(
    const std::filesystem::path& path);
[[nodiscard]] bool save_drawing_preferences(
    const std::filesystem::path& path, const DrawingPreferences& value,
    std::string* diagnostic = nullptr);
[[nodiscard]] bool apply_drawing_preferences(
    const DrawingPreferences& value,
    vector::precision::snap::SnapConfiguration& configuration);

} // namespace pikdraw::desktop::tools
