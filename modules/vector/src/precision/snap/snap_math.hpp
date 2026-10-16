#pragma once

#include "pikdraw/vector/precision/snap/snap_index.hpp"

#include <optional>

namespace pikdraw::vector::precision::snap::detail {

struct SnapWorldBounds {
    double min_x{0.0};
    double min_y{0.0};
    double max_x{0.0};
    double max_y{0.0};
};

[[nodiscard]] bool finite_point(geometry::Point2D point) noexcept;
[[nodiscard]] double distance_dip(geometry::Point2D first,
                                  geometry::Point2D second) noexcept;
[[nodiscard]] std::optional<SnapWorldBounds> world_bounds_for_dip_square(
    const SnapCameraTransform& camera, geometry::Point2D center_world, double radius_dip);

} // namespace pikdraw::vector::precision::snap::detail
