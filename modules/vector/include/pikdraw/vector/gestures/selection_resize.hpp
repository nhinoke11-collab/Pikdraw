#pragma once
#include "pikdraw/geometry/primitives/transform.hpp"

namespace pikdraw::vector {
// Handles clockwise in screen space: NW, N, NE, E, SE, S, SW, W.
// Source coordinates remain y-up. The opposite handle is the fixed anchor.
[[nodiscard]] std::optional<geometry::Transform2D> selection_resize_transform(const geometry::Rect2D &bounds,
                                                                              int handle,
                                                                              geometry::Point2D drag_delta,
                                                                              bool preserve_aspect = false);
} // namespace pikdraw::vector
