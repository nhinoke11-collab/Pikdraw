#pragma once

#include "pikdraw/geometry/curves/curve.hpp"
#include "pikdraw/geometry/primitives/point.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace pikdraw::geometry {

enum class CurveQueryStatus : uint8_t {
    ExactForPrimitive,
    BoundedApproximation,
    Overlap,
    NoSolution,
    Indeterminate,
    BudgetExceeded,
    InvalidInput
};

struct IntersectionPoint {
    Point2D point{};
    double first_parameter{0.0};
    double second_parameter{0.0};
    double residual_mm{0.0}; // ||P1(t1) - P2(t2)||
};

struct CurveIntersectionResult {
    CurveQueryStatus status{CurveQueryStatus::NoSolution};
    std::vector<IntersectionPoint> points;
    std::optional<std::pair<double, double>> first_overlap_interval;
    std::optional<std::pair<double, double>> second_overlap_interval;
};

struct RelationPoint {
    Point2D point{};
    double parameter{0.0};
    double residual_mm{0.0}; // |(C(t)-A)·C'(t)| / ||C'(t)|| or |(C(t)-A)×C'(t)| / ||C'(t)||
};

struct RelationQueryResult {
    CurveQueryStatus status{CurveQueryStatus::NoSolution};
    std::vector<RelationPoint> points;
};

struct HalfLengthResult {
    CurveQueryStatus status{CurveQueryStatus::NoSolution};
    Point2D point{};
    double parameter{0.5};
    double total_length{0.0};
    double half_length{0.0};
    double residual_mm{0.0};
};

struct QuadrantPointsResult {
    CurveQueryStatus status{CurveQueryStatus::NoSolution};
    std::vector<RelationPoint> points;
};

struct CurveQueryOptions {
    double tolerance_mm{1e-3};      // 0.001 mm
    double max_residual_mm{1e-4};   // 0.0001 mm
    size_t max_subdivision_depth{32};
    size_t max_work_units{100000};
};

[[nodiscard]] CurveIntersectionResult find_curve_intersections(
    const CurveSegment& first, const CurveSegment& second,
    const CurveQueryOptions& options = {});

[[nodiscard]] RelationQueryResult find_perpendicular_points(
    const CurveSegment& curve, Point2D external_anchor,
    const CurveQueryOptions& options = {});

[[nodiscard]] RelationQueryResult find_tangent_points(
    const CurveSegment& curve, Point2D external_anchor,
    const CurveQueryOptions& options = {});

[[nodiscard]] HalfLengthResult find_half_curve_length(
    const CurveSegment& curve,
    const CurveQueryOptions& options = {});

[[nodiscard]] QuadrantPointsResult find_quadrant_points(
    const EllipticArc& arc);

[[nodiscard]] Point2D find_analytic_center(
    const EllipticArc& arc) noexcept;

} // namespace pikdraw::geometry
