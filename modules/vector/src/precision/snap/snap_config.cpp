#include "pikdraw/vector/precision/snap/snap_config.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pikdraw::vector::precision::snap {
namespace {

constexpr size_t index_of(SnapOption option) noexcept {
    return static_cast<size_t>(option);
}

constexpr size_t index_of(SnapGroup group) noexcept {
    return static_cast<size_t>(group);
}

bool valid_group(SnapGroup group) noexcept {
    return index_of(group) < kSnapGroupCount;
}

bool valid_option(SnapOption option) noexcept {
    return index_of(option) < kSnapOptionCount;
}

std::array<bool, kSnapGroupCount> default_groups() {
    std::array<bool, kSnapGroupCount> groups{};
    groups.fill(true);
    return groups;
}

std::array<bool, kSnapOptionCount> default_options() {
    std::array<bool, kSnapOptionCount> options{};
    options[index_of(SnapOption::Endpoint)] = true;
    options[index_of(SnapOption::SourceNode)] = true;
    options[index_of(SnapOption::LineMidpoint)] = true;
    options[index_of(SnapOption::CurrentLayerOnly)] = true;
    return options;
}

std::array<int, kSnapOptionCount> default_priorities() {
    std::array<int, kSnapOptionCount> priorities{};
    priorities.fill(1000);
    priorities[index_of(SnapOption::Endpoint)] = 0;
    priorities[index_of(SnapOption::SourceNode)] = 1;
    priorities[index_of(SnapOption::LineMidpoint)] = 2;
    priorities[index_of(SnapOption::Intersection)] = 3;
    priorities[index_of(SnapOption::AnalyticCenter)] = 4;
    priorities[index_of(SnapOption::Quadrant)] = 5;
    priorities[index_of(SnapOption::BezierTMidpoint)] = 6;
    priorities[index_of(SnapOption::HalfCurveLength)] = 7;
    priorities[index_of(SnapOption::Perpendicular)] = 8;
    priorities[index_of(SnapOption::Tangent)] = 9;
    priorities[index_of(SnapOption::Nearest)] = 10;
    priorities[index_of(SnapOption::ReferenceTracking)] = 11;
    priorities[index_of(SnapOption::Grid)] = 12;
    return priorities;
}

} // namespace

SnapGroup group_for_option(SnapOption option) noexcept {
    switch (option) {
    case SnapOption::Endpoint:
    case SnapOption::SourceNode:
    case SnapOption::LineMidpoint:
    case SnapOption::AnalyticCenter:
    case SnapOption::Quadrant:
    case SnapOption::Nearest:
    case SnapOption::BezierTMidpoint:
    case SnapOption::HalfCurveLength:
        return SnapGroup::ObjectPoints;
    case SnapOption::Intersection:
    case SnapOption::Perpendicular:
    case SnapOption::Tangent:
    case SnapOption::Parallel:
    case SnapOption::Extension:
    case SnapOption::ReferenceTracking:
        return SnapGroup::AutomaticRelations;
    case SnapOption::PageEdge:
    case SnapOption::PageCenter:
    case SnapOption::Grid:
    case SnapOption::Guide:
    case SnapOption::ConstructionGeometry:
        return SnapGroup::PageGridGuidesConstruction;
    case SnapOption::Markers:
    case SnapOption::SnapTooltips:
    case SnapOption::TrackingLines:
        return SnapGroup::Display;
    case SnapOption::CurrentLayerOnly:
    case SnapOption::LockedAsReference:
    case SnapOption::SelfPath:
    case SnapOption::ControlHandles:
    case SnapOption::HiddenLayers:
        return SnapGroup::TargetPolicy;
    case SnapOption::BboxEdge:
    case SnapOption::BboxCenter:
    case SnapOption::PencilAutoClose:
        return SnapGroup::OptionalOffByDefault;
    case SnapOption::Count:
        break;
    }
    return SnapGroup::Count;
}

bool is_supported_snap_candidate(SnapOption option) noexcept {
    switch (option) {
    case SnapOption::Endpoint:
    case SnapOption::SourceNode:
    case SnapOption::LineMidpoint:
    case SnapOption::AnalyticCenter:
    case SnapOption::Quadrant:
    case SnapOption::Nearest:
    case SnapOption::BezierTMidpoint:
    case SnapOption::HalfCurveLength:
    case SnapOption::Intersection:
    case SnapOption::Perpendicular:
    case SnapOption::Tangent:
    case SnapOption::ReferenceTracking:
    case SnapOption::Grid:
        return true;
    default:
        return false;
    }
}

bool is_source_geometry_candidate(SnapOption option) noexcept {
    switch (option) {
    case SnapOption::Endpoint:
    case SnapOption::SourceNode:
    case SnapOption::LineMidpoint:
    case SnapOption::AnalyticCenter:
    case SnapOption::Quadrant:
    case SnapOption::Nearest:
    case SnapOption::BezierTMidpoint:
    case SnapOption::HalfCurveLength:
    case SnapOption::Intersection:
    case SnapOption::Perpendicular:
    case SnapOption::Tangent:
        return true;
    default:
        return false;
    }
}

double normalize_angle_degrees(double angle_degrees) noexcept {
    if (!std::isfinite(angle_degrees)) return angle_degrees;
    double normalized = std::fmod(angle_degrees, 360.0);
    if (normalized < 0.0) normalized += 360.0;
    return normalized == 0.0 ? 0.0 : normalized;
}

std::optional<std::string> validate_custom_angles(const std::vector<double>& angles_degrees) {
    if (angles_degrees.empty()) return "Custom angle list must contain at least one angle";
    if (angles_degrees.size() > kMaxCustomPolarAngles)
        return "Custom angle list exceeds its bounded entry count";
    std::vector<double> normalized;
    normalized.reserve(angles_degrees.size());
    for (const double angle : angles_degrees) {
        if (!std::isfinite(angle)) return "Custom angles must be finite";
        normalized.push_back(normalize_angle_degrees(angle));
    }
    std::sort(normalized.begin(), normalized.end());
    if (std::adjacent_find(normalized.begin(), normalized.end()) != normalized.end())
        return "Custom angles must be unique after normalization";
    return std::nullopt;
}

bool is_valid_preset_step(double step_degrees) noexcept {
    return step_degrees == 15.0 || step_degrees == 30.0 ||
           step_degrees == 45.0 || step_degrees == 90.0;
}

bool effective_group_enabled(const SnapConfigSnapshot& config, std::string_view tool,
                             SnapGroup group) noexcept {
    if (!valid_group(group)) return false;
    const auto found = config.tool_overrides.find(tool);
    if (found != config.tool_overrides.end() && found->second.groups[index_of(group)])
        return *found->second.groups[index_of(group)];
    return config.groups[index_of(group)];
}

bool effective_option_enabled(const SnapConfigSnapshot& config, std::string_view tool,
                              SnapOption option) noexcept {
    if (!valid_option(option)) return false;
    const SnapGroup group = group_for_option(option);
    if (!effective_group_enabled(config, tool, group)) return false;
    const auto found = config.tool_overrides.find(tool);
    if (found != config.tool_overrides.end() && found->second.options[index_of(option)])
        return *found->second.options[index_of(option)];
    return config.options[index_of(option)];
}

int effective_tool_priority(const SnapConfigSnapshot& config, std::string_view tool,
                            SnapOption option) noexcept {
    if (!valid_option(option)) return std::numeric_limits<int>::max();
    const auto found = config.tool_overrides.find(tool);
    if (found != config.tool_overrides.end() && found->second.priorities[index_of(option)])
        return *found->second.priorities[index_of(option)];
    return 0;
}

int effective_kind_priority(const SnapConfigSnapshot& config, SnapOption option) noexcept {
    return valid_option(option) ? config.configured_priorities[index_of(option)]
                                : std::numeric_limits<int>::max();
}

SnapConfiguration::SnapConfiguration() {
    state_.groups = default_groups();
    state_.options = default_options();
    state_.configured_priorities = default_priorities();
}

bool SnapConfiguration::bump_generation() noexcept {
    if (state_.generation == std::numeric_limits<uint64_t>::max()) return false;
    ++state_.generation;
    return true;
}

bool SnapConfiguration::set_master_enabled(bool enabled) noexcept {
    if (state_.master_enabled == enabled) return true;
    if (!bump_generation()) return false;
    state_.master_enabled = enabled;
    return true;
}

bool SnapConfiguration::toggle_master() noexcept {
    return set_master_enabled(!state_.master_enabled);
}

bool SnapConfiguration::set_group_enabled(SnapGroup group, bool enabled) noexcept {
    if (!valid_group(group)) return false;
    bool& value = state_.groups[index_of(group)];
    if (value == enabled) return true;
    if (!bump_generation()) return false;
    value = enabled;
    return true;
}

bool SnapConfiguration::set_option_enabled(SnapOption option, bool enabled) noexcept {
    if (!valid_option(option)) return false;
    bool& value = state_.options[index_of(option)];
    if (value == enabled) return true;
    if (!bump_generation()) return false;
    value = enabled;
    return true;
}

bool SnapConfiguration::set_all_group_options(SnapGroup group, bool enabled) noexcept {
    if (!valid_group(group)) return false;
    bool changed = false;
    for (size_t i = 0; i < kSnapOptionCount; ++i) {
        if (group_for_option(static_cast<SnapOption>(i)) == group &&
            state_.options[i] != enabled) {
            changed = true;
        }
    }
    if (!changed) return true;
    if (!bump_generation()) return false;
    for (size_t i = 0; i < kSnapOptionCount; ++i) {
        if (group_for_option(static_cast<SnapOption>(i)) == group)
            state_.options[i] = enabled;
    }
    return true;
}

bool SnapConfiguration::set_option_priority(SnapOption option, int priority) noexcept {
    if (!valid_option(option)) return false;
    int& value = state_.configured_priorities[index_of(option)];
    if (value == priority) return true;
    if (!bump_generation()) return false;
    value = priority;
    return true;
}

bool SnapConfiguration::set_tool_override(std::string tool, SnapToolOverride value) {
    if (tool.empty() || tool.size() > 128) return false;
    const auto existing = state_.tool_overrides.find(tool);
    if (existing != state_.tool_overrides.end() && existing->second == value) return true;
    if (!bump_generation()) return false;
    state_.tool_overrides.insert_or_assign(std::move(tool), std::move(value));
    return true;
}

bool SnapConfiguration::clear_tool_override(std::string_view tool) noexcept {
    const auto found = state_.tool_overrides.find(tool);
    if (found == state_.tool_overrides.end()) return true;
    if (!bump_generation()) return false;
    state_.tool_overrides.erase(found);
    return true;
}

bool SnapConfiguration::set_hysteresis(double acquire_radius_dip,
                                       double release_radius_dip) noexcept {
    if (!std::isfinite(acquire_radius_dip) || !std::isfinite(release_radius_dip) ||
        acquire_radius_dip < 1.0 || acquire_radius_dip > 64.0 ||
        release_radius_dip < 1.0 || release_radius_dip > 64.0 ||
        release_radius_dip < acquire_radius_dip)
        return false;
    if (state_.acquire_radius_dip == acquire_radius_dip &&
        state_.release_radius_dip == release_radius_dip)
        return true;
    if (!bump_generation()) return false;
    state_.acquire_radius_dip = acquire_radius_dip;
    state_.release_radius_dip = release_radius_dip;
    return true;
}

bool SnapConfiguration::set_grid(SnapGridSettings grid) noexcept {
    if (!std::isfinite(grid.origin_x_mm) || !std::isfinite(grid.origin_y_mm) ||
        !std::isfinite(grid.spacing_mm) || grid.spacing_mm <= 0.0 ||
        grid.max_cells_per_query == 0 || grid.max_cells_per_query > 65536)
        return false;
    if (state_.grid.origin_x_mm == grid.origin_x_mm &&
        state_.grid.origin_y_mm == grid.origin_y_mm &&
        state_.grid.spacing_mm == grid.spacing_mm &&
        state_.grid.max_cells_per_query == grid.max_cells_per_query)
        return true;
    if (!bump_generation()) return false;
    state_.grid = grid;
    return true;
}

bool SnapConfiguration::set_direction_mode(DirectionMode mode) noexcept {
    if (mode > DirectionMode::Polar) return false;
    if (state_.direction.mode == mode) return true;
    if (!bump_generation()) return false;
    state_.direction.mode = mode;
    return true;
}

bool SnapConfiguration::toggle_direction_mode(DirectionMode mode) noexcept {
    if (mode == DirectionMode::Free || mode > DirectionMode::Polar) return false;
    return set_direction_mode(state_.direction.mode == mode ? DirectionMode::Free : mode);
}

bool SnapConfiguration::set_angle_reference(AngleReference reference) noexcept {
    if (reference > AngleReference::PreviousSegment) return false;
    if (state_.direction.reference == reference) return true;
    if (!bump_generation()) return false;
    state_.direction.reference = reference;
    return true;
}

bool SnapConfiguration::set_polar_preset(double step_degrees) noexcept {
    if (!is_valid_preset_step(step_degrees)) return false;
    const auto& old = state_.direction.polar;
    if (old.kind == PolarAngleKind::PresetStep && old.step_degrees == step_degrees) return true;
    if (!bump_generation()) return false;
    state_.direction.polar.kind = PolarAngleKind::PresetStep;
    state_.direction.polar.step_degrees = step_degrees;
    return true;
}

bool SnapConfiguration::set_custom_polar_angles(std::vector<double> angles_degrees,
                                                std::string* error) {
    if (const auto validation = validate_custom_angles(angles_degrees)) {
        if (error) *error = *validation;
        return false;
    }
    for (double& angle : angles_degrees) angle = normalize_angle_degrees(angle);
    std::sort(angles_degrees.begin(), angles_degrees.end());
    const auto& old = state_.direction.polar;
    if (old.kind == PolarAngleKind::CustomList && old.custom_angles_degrees == angles_degrees)
        return true;
    if (!bump_generation()) return false;
    state_.direction.polar.kind = PolarAngleKind::CustomList;
    state_.direction.polar.custom_angles_degrees = std::move(angles_degrees);
    if (error) error->clear();
    return true;
}

} // namespace pikdraw::vector::precision::snap
