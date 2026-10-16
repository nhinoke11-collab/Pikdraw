#pragma once

#include "pikdraw/vector/precision/snap/snap_index.hpp"

#include <optional>

namespace pikdraw::vector::precision::snap {

struct DirectionLockState {
    double world_angle_degrees{0.0};
    geometry::Point2D anchor_world;
    SnapQueryStamp stamp;
};

struct DirectionSnapResult {
    SnapQueryStatus status{SnapQueryStatus::Success};
    SnapQueryStamp stamp;
    DirectionMode mode{DirectionMode::Free};
    bool constrained{false};
    double world_angle_degrees{0.0};
    double perpendicular_distance_dip{0.0};
    double ray_length_dip{0.0};
    geometry::Point2D world_point;
    std::optional<DirectionLockState> next_lock;
};

[[nodiscard]] DirectionSnapResult constrain_direction(
    const SnapQueryStamp& stamp, geometry::Point2D anchor_world,
    geometry::Point2D pointer_world, double reference_angle_degrees,
    const SnapCameraTransform& camera, const SnapConfigSnapshot& config,
    const std::optional<DirectionLockState>& previous_lock = std::nullopt);

} // namespace pikdraw::vector::precision::snap
