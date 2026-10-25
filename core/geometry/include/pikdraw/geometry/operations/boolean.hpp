#pragma once

#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/geometry/curves/path.hpp"
#include <cstddef>
#include <optional>
#include <vector>

namespace pikdraw::geometry {

enum class BooleanOp {
    Union,          // Weld / Unite
    Intersection,   // Intersect
    Difference,     // Front minus back (Subject minus Clip)
    Xor,            // Exclude (Symmetric difference)
    BackMinusFront, // Back minus front (Clip minus Subject)
    Boundary,       // Outer boundary perimeter
    Simplify        // Sliced pieces
};

enum class BooleanStatus {
    Success,
    Empty,
    InvalidInput,
    Unsupported,
    ToleranceNotMet,
    WorkBudgetExceeded,
    NoChange
};

struct BooleanOptions {
    // General curves are flattened before polygon clipping; rectangle fast path is exact.
    double flatten_tolerance_mm{0.01};
    double quantization_grid_mm{0.000001};
    size_t max_subdivision_depth{16};
    size_t max_output_points{250000};
};

struct BooleanResult {
    bool success{false};
    BooleanStatus status{BooleanStatus::InvalidInput};
    std::vector<Rect2D> rectangles;
    Path2D path;
    // Disconnected filled regions. Each component keeps its hole contours in
    // the same non-zero-winding path.
    std::vector<Path2D> components;
    // Individual boundary loops; callers must not treat these as filled
    // components because some loops are holes.
    std::vector<Path2D> sub_paths;
    const char* error_message{nullptr};
    // Flatten + half-cell quantization bound; zero for exact rectangle operations.
    double approximation_error_bound_mm{0.0};
};

/// Computes Boolean operations on axis-aligned rectangles
[[nodiscard]] BooleanResult boolean_rectangles(
    const Rect2D& subject,
    const Rect2D& clip,
    BooleanOp op
);

/// Computes Boolean operations on general filled paths. Exact axis-aligned
/// rectangles retain the analytical fast path; other paths are flattened with
/// the requested error budget and clipped on a checked local integer grid.
[[nodiscard]] BooleanResult boolean_paths(
    const Path2D& subject,
    const Path2D& clip,
    BooleanOp op,
    const BooleanOptions& options = {}
);

/// Splits an axis-aligned rectangle by a line segment (p1 -> p2).
/// Returns true and 2 split polygons/paths if the line bisects the rectangle.
[[nodiscard]] std::pair<bool, std::vector<Path2D>> split_rect_by_line(
    const Rect2D& rect,
    const Point2D& p1,
    const Point2D& p2
);

} // namespace pikdraw::geometry
