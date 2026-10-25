#pragma once

#include "pikdraw/vector/precision/snap/snap_config.hpp"
#include "pikdraw/geometry/units/length_unit.hpp"

#include <filesystem>
#include <array>
#include <cmath>
#include <string>

namespace pikdraw::desktop::tools {

struct DrawingPreferences {
    std::string profile{"PikDraw"};
    bool snap_master{true};
    bool snap_objects{true};
    bool smart_guides{true};
    bool smart_spacing{true};
    bool snap_page{true};
    bool snap_guides{false};
    bool artboard_overview{false};
    bool endpoint{true};
    bool source_node{true};
    bool line_midpoint{true};
    bool grid{true};
    std::array<bool, vector::precision::snap::kSnapGroupCount> snap_groups{
        true, true, true, true, true, true};
    std::array<bool, vector::precision::snap::kSnapOptionCount> snap_options = [] {
        std::array<bool, vector::precision::snap::kSnapOptionCount> options{};
        using vector::precision::snap::SnapOption;
        options[static_cast<size_t>(SnapOption::Endpoint)] = true;
        options[static_cast<size_t>(SnapOption::SourceNode)] = true;
        options[static_cast<size_t>(SnapOption::LineMidpoint)] = true;
        options[static_cast<size_t>(SnapOption::CurrentLayerOnly)] = true;
        return options;
    }();
    double snap_acquire_radius_dip{8.0};
    double snap_release_radius_dip{12.0};
    vector::precision::snap::DirectionMode direction{
        vector::precision::snap::DirectionMode::Free};
    double polar_step_degrees{45.0};
    double grid_spacing_mm{10.0};
    geometry::units::LengthUnit default_length_unit{geometry::units::LengthUnit::Millimeter};
};

inline constexpr double kMinimumGridSpacingMm = 0.001;
inline constexpr double kMaximumGridSpacingMm = 1'000'000'000.0;

[[nodiscard]] inline bool is_valid_grid_spacing_mm(double value) noexcept {
    return std::isfinite(value) && value >= kMinimumGridSpacingMm &&
        value <= kMaximumGridSpacingMm;
}

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
