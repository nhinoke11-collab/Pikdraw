#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pikdraw::vector::precision::snap {

enum class SnapGroup : uint8_t {
    ObjectPoints,
    AutomaticRelations,
    PageGridGuidesConstruction,
    Display,
    TargetPolicy,
    OptionalOffByDefault,
    Count
};

enum class SnapOption : uint8_t {
    Endpoint,
    SourceNode,
    LineMidpoint,
    AnalyticCenter,
    Quadrant,
    Nearest,
    BezierTMidpoint,
    HalfCurveLength,
    Intersection,
    Perpendicular,
    Tangent,
    Parallel,
    Extension,
    ReferenceTracking,
    PageEdge,
    PageCenter,
    Grid,
    Guide,
    ConstructionGeometry,
    Markers,
    SnapTooltips,
    TrackingLines,
    CurrentLayerOnly,
    LockedAsReference,
    SelfPath,
    ControlHandles,
    HiddenLayers,
    BboxEdge,
    BboxCenter,
    PencilAutoClose,
    Count
};

inline constexpr size_t kSnapOptionCount = static_cast<size_t>(SnapOption::Count);
inline constexpr size_t kSnapGroupCount = static_cast<size_t>(SnapGroup::Count);
inline constexpr size_t kMaxCustomPolarAngles = 1024;
inline constexpr double kPolarAcquireRadiusDip = 6.0;
inline constexpr double kPolarReleaseRadiusDip = 10.0;
inline constexpr double kPolarMinimumRayLengthDip = 16.0;

enum class DirectionMode : uint8_t { Free, Ortho, Polar };
enum class AngleReference : uint8_t { WorldX, ConstructionFrameX, PreviousSegment };
enum class PolarAngleKind : uint8_t { PresetStep, CustomList };

struct PolarAngleSettings {
    PolarAngleKind kind{PolarAngleKind::PresetStep};
    double step_degrees{45.0};
    std::vector<double> custom_angles_degrees;
};

struct SnapDirectionSettings {
    DirectionMode mode{DirectionMode::Free};
    AngleReference reference{AngleReference::ConstructionFrameX};
    PolarAngleSettings polar;
};

struct SnapGridSettings {
    double origin_x_mm{0.0};
    double origin_y_mm{0.0};
    double spacing_mm{10.0};
    size_t max_cells_per_query{4096};
};

struct SnapToolOverride {
    std::array<std::optional<bool>, kSnapGroupCount> groups{};
    std::array<std::optional<bool>, kSnapOptionCount> options{};
    std::array<std::optional<int>, kSnapOptionCount> priorities{};
    friend bool operator==(const SnapToolOverride&, const SnapToolOverride&) = default;
};

struct SnapConfigSnapshot {
    uint64_t generation{1};
    bool master_enabled{true};
    std::array<bool, kSnapGroupCount> groups{};
    std::array<bool, kSnapOptionCount> options{};
    std::array<int, kSnapOptionCount> configured_priorities{};
    std::map<std::string, SnapToolOverride, std::less<>> tool_overrides;
    double acquire_radius_dip{8.0};
    double release_radius_dip{12.0};
    SnapGridSettings grid;
    SnapDirectionSettings direction;
};

[[nodiscard]] SnapGroup group_for_option(SnapOption option) noexcept;
[[nodiscard]] bool is_supported_snap_candidate(SnapOption option) noexcept;
[[nodiscard]] bool is_source_geometry_candidate(SnapOption option) noexcept;
[[nodiscard]] double normalize_angle_degrees(double angle_degrees) noexcept;
[[nodiscard]] std::optional<std::string> validate_custom_angles(
    const std::vector<double>& angles_degrees);
[[nodiscard]] bool is_valid_preset_step(double step_degrees) noexcept;
[[nodiscard]] bool effective_group_enabled(
    const SnapConfigSnapshot& config, std::string_view tool, SnapGroup group) noexcept;
[[nodiscard]] bool effective_option_enabled(
    const SnapConfigSnapshot& config, std::string_view tool, SnapOption option) noexcept;
[[nodiscard]] int effective_tool_priority(
    const SnapConfigSnapshot& config, std::string_view tool, SnapOption option) noexcept;
[[nodiscard]] int effective_kind_priority(
    const SnapConfigSnapshot& config, SnapOption option) noexcept;

class SnapConfiguration {
public:
    SnapConfiguration();

    [[nodiscard]] SnapConfigSnapshot snapshot() const { return state_; }
    bool set_master_enabled(bool enabled) noexcept;
    bool toggle_master() noexcept;
    bool set_group_enabled(SnapGroup group, bool enabled) noexcept;
    bool set_option_enabled(SnapOption option, bool enabled) noexcept;
    bool set_all_group_options(SnapGroup group, bool enabled) noexcept;
    bool set_option_priority(SnapOption option, int priority) noexcept;
    bool set_tool_override(std::string tool, SnapToolOverride value);
    bool clear_tool_override(std::string_view tool) noexcept;
    bool set_hysteresis(double acquire_radius_dip, double release_radius_dip) noexcept;
    bool set_grid(SnapGridSettings grid) noexcept;
    bool set_direction_mode(DirectionMode mode) noexcept;
    bool toggle_direction_mode(DirectionMode mode) noexcept;
    bool set_angle_reference(AngleReference reference) noexcept;
    bool set_polar_preset(double step_degrees) noexcept;
    bool set_custom_polar_angles(std::vector<double> angles_degrees,
                                 std::string* error = nullptr);

private:
    bool bump_generation() noexcept;
    SnapConfigSnapshot state_;
};

} // namespace pikdraw::vector::precision::snap
