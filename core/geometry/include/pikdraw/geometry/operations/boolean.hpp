#pragma once

#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/geometry/curves/path.hpp"
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

struct BooleanResult {
    bool success{false};
    std::vector<Rect2D> rectangles;
    Path2D path;
    std::vector<Path2D> sub_paths;
    const char* error_message{nullptr};
};

/// Computes Boolean operations on axis-aligned rectangles
[[nodiscard]] BooleanResult boolean_rectangles(
    const Rect2D& subject,
    const Rect2D& clip,
    BooleanOp op
);

/// Computes Boolean operations on general paths
[[nodiscard]] BooleanResult boolean_paths(
    const Path2D& subject,
    const Path2D& clip,
    BooleanOp op
);

/// Splits an axis-aligned rectangle by a line segment (p1 -> p2).
/// Returns true and 2 split polygons/paths if the line bisects the rectangle.
[[nodiscard]] std::pair<bool, std::vector<Path2D>> split_rect_by_line(
    const Rect2D& rect,
    const Point2D& p1,
    const Point2D& p2
);

} // namespace pikdraw::geometry
