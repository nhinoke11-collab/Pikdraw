#pragma once

#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"
#include <string_view>

namespace pikdraw::geometry {

/// Validation status code
enum class GeometryStatus {
    Ok = 0,
    NonFiniteCoordinate,
    OutOfBounds,
    DegenerateBounds,
    SingularTransform,
    InvalidParameter
};

[[nodiscard]] constexpr std::string_view status_to_string(GeometryStatus status) noexcept {
    switch (status) {
        case GeometryStatus::Ok: return "Ok";
        case GeometryStatus::NonFiniteCoordinate: return "Non-finite coordinate (NaN or Inf)";
        case GeometryStatus::OutOfBounds: return "Coordinate exceeds canvas bounds [-1000000, +1000000] mm";
        case GeometryStatus::DegenerateBounds: return "Degenerate bounds (negative or empty dimensions)";
        case GeometryStatus::SingularTransform: return "Singular non-invertible transform matrix";
        case GeometryStatus::InvalidParameter: return "Invalid geometric parameter";
    }
    return "Unknown error";
}

namespace limits {
    inline constexpr double kCanvasMinMm = -1000000.0;
    inline constexpr double kCanvasMaxMm =  1000000.0;
    inline constexpr double kPointToleranceMm = 1e-6; // 1 nm
    inline constexpr double kCollinearToleranceMm = 1e-5;
    inline constexpr double kTransformInversionToleranceMm = 1e-8;
    inline constexpr double kSingularMatrixEpsilon = 1e-12;
}

[[nodiscard]] inline GeometryStatus validate_coordinate(double val) noexcept {
    if (!std::isfinite(val)) return GeometryStatus::NonFiniteCoordinate;
    if (val < limits::kCanvasMinMm || val > limits::kCanvasMaxMm) return GeometryStatus::OutOfBounds;
    return GeometryStatus::Ok;
}

[[nodiscard]] inline GeometryStatus validate_point(const Point2D& pt) noexcept {
    const auto sx = validate_coordinate(pt.x);
    if (sx != GeometryStatus::Ok) return sx;
    return validate_coordinate(pt.y);
}

[[nodiscard]] inline GeometryStatus validate_rect(const Rect2D& r) noexcept {
    if (!r.is_finite()) return GeometryStatus::NonFiniteCoordinate;
    if (r.width < 0.0 || r.height < 0.0) return GeometryStatus::DegenerateBounds;
    const auto smin = validate_point(r.min_point());
    if (smin != GeometryStatus::Ok) return smin;
    return validate_point(r.max_point());
}

[[nodiscard]] inline GeometryStatus validate_transform(const Transform2D& t) noexcept {
    if (!t.is_finite()) return GeometryStatus::NonFiniteCoordinate;
    if (!t.is_invertible(limits::kSingularMatrixEpsilon)) return GeometryStatus::SingularTransform;
    return GeometryStatus::Ok;
}

} // namespace pikdraw::geometry
