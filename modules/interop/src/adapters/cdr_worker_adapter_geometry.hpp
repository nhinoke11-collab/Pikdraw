#pragma once

#include "pikdraw/geometry/primitives/point.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace pikdraw::interop::detail {

// Corners are in the SVG rect path order after page coordinates were mapped
// into PikDraw's y-up space: top-left, top-right, bottom-right, bottom-left.
// Raster rows start at the top, while ImageObject local +Y points up, so the
// image-local origin must be the path's bottom-left corner.
inline bool CdrImagePlacementFromPageCorners(
    std::span<const geometry::Point2D> corners,
    double& width,
    double& height,
    std::array<double, 6>& transform) {
    if (corners.size() != 4) return false;
    const auto edge_x = corners[2] - corners[3];
    const auto edge_y = corners[0] - corners[3];
    width = std::hypot(edge_x.x, edge_x.y);
    height = std::hypot(edge_y.x, edge_y.y);
    if (!std::isfinite(width) || !std::isfinite(height) ||
        width <= 1.0e-9 || height <= 1.0e-9)
        return false;
    const geometry::Point2D expected_upper_right{
        corners[3].x + edge_x.x + edge_y.x,
        corners[3].y + edge_x.y + edge_y.y};
    const double closure_tolerance = (std::max)({1.0, width, height}) * 1.0e-7;
    if (std::hypot(expected_upper_right.x - corners[1].x,
                   expected_upper_right.y - corners[1].y) > closure_tolerance)
        return false;
    transform = {edge_x.x / width, edge_x.y / width,
                 edge_y.x / height, edge_y.y / height,
                 corners[3].x, corners[3].y};
    return std::all_of(transform.begin(), transform.end(), [](double value) {
        return std::isfinite(value);
    });
}

} // namespace pikdraw::interop::detail
