#pragma once

#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/geometry/curves/path.hpp"

namespace pikdraw::geometry {

enum class JoinType {
    Miter,
    Round,
    Bevel
};

struct OffsetOptions {
    double distance_mm{0.0};
    JoinType join_type{JoinType::Miter};
    double miter_limit{4.0};
    double arc_tolerance_mm{0.01};
};

struct OffsetResult {
    bool success{false};
    Path2D path;
    const char* error_message{nullptr};
};

/// Computes an offset / dilation of a rectangle
[[nodiscard]] Rect2D offset_rect(const Rect2D& rect, double distance_mm);

/// Computes an offset of a path
[[nodiscard]] OffsetResult offset_path(const Path2D& path, const OffsetOptions& options);

} // namespace pikdraw::geometry
