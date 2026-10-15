#pragma once

#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/geometry/curves/path.hpp"
#include <optional>
#include <vector>

namespace pikdraw::geometry {

enum class BooleanOp {
    Union,
    Intersection,
    Difference,
    Xor
};

struct BooleanResult {
    bool success{false};
    std::vector<Rect2D> rectangles;
    Path2D path;
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

} // namespace pikdraw::geometry
