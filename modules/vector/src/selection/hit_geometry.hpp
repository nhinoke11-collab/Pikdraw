#pragma once
#include "pikdraw/geometry/flattening/flattening.hpp"
#include <optional>
namespace pikdraw::vector {
// Interaction approximation only: refuse if depth/point budgets cannot meet tolerance.
std::optional<std::vector<geometry::FlattenedSubpath>> hit_polylines(const geometry::Path2D &path,
                                                                     double tolerance);
} // namespace pikdraw::vector
