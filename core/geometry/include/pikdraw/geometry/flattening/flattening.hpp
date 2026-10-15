#pragma once

#include "pikdraw/geometry/curves/path.hpp"
#include <vector>

namespace pikdraw::geometry {

struct FlatteningOptions {
    double tolerance_mm{0.01}; // 10 micrometers
    size_t max_subdivision_depth{16};
};

/// Result of path flattening: polygon or polylines
struct FlattenedSubpath {
    std::vector<Point2D> points;
    bool is_closed{false};
};

/// Flattens a Path2D into discrete linear polygonal subpaths with error bounds
[[nodiscard]] std::vector<FlattenedSubpath> flatten_path(
    const Path2D& path,
    const FlatteningOptions& options = FlatteningOptions{}
);

} // namespace pikdraw::geometry
