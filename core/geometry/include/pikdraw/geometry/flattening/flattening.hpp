#pragma once

#include "pikdraw/geometry/curves/path.hpp"
#include <cstddef>
#include <vector>

namespace pikdraw::geometry {

struct FlatteningOptions {
    double tolerance_mm{0.01}; // 10 micrometers
    size_t max_subdivision_depth{16};
    size_t max_output_points{1000000};
};

/// Result of path flattening: polygon or polylines
struct FlattenedSubpath {
    std::vector<Point2D> points;
    bool is_closed{false};
};

enum class FlatteningStatus {
    Success,
    InvalidOptions,
    InvalidPath,
    ToleranceNotMet,
    WorkBudgetExceeded
};

struct FlatteningResult {
    FlatteningStatus status{FlatteningStatus::InvalidPath};
    std::vector<FlattenedSubpath> subpaths;
    size_t element_index{0};
    double unmet_error_bound_mm{0.0};
};

/// Flattens source curves and analytic arcs, reporting when the requested bound cannot be met.
[[nodiscard]] FlatteningResult flatten_path_checked(
    const Path2D& path,
    const FlatteningOptions& options = FlatteningOptions{});

/// Compatibility convenience: throws std::runtime_error if checked flattening fails.
[[nodiscard]] std::vector<FlattenedSubpath> flatten_path(
    const Path2D& path,
    const FlatteningOptions& options = FlatteningOptions{}
);

} // namespace pikdraw::geometry
