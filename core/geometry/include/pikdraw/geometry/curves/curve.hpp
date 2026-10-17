#pragma once

#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <variant>
#include <vector>

namespace pikdraw::geometry {

/// Straight line segment from start to end
struct LineSegment {
    Point2D p0;
    Point2D p1;

    [[nodiscard]] Point2D point_at(double t) const {
        return {p0.x + t * (p1.x - p0.x), p0.y + t * (p1.y - p0.y)};
    }

    [[nodiscard]] Rect2D bounds() const {
        return Rect2D::from_ltrb(
            std::min(p0.x, p1.x), std::min(p0.y, p1.y),
            std::max(p0.x, p1.x), std::max(p0.y, p1.y)
        );
    }

    [[nodiscard]] Point2D derivative_at(double) const { return p1 - p0; }
};

/// Quadratic Bézier curve (p0, control p1, p2)
struct QuadraticBezier {
    Point2D p0;
    Point2D p1;
    Point2D p2;

    [[nodiscard]] Point2D point_at(double t) const {
        const Point2D p01 = p0 + (p1 - p0) * t;
        const Point2D p12 = p1 + (p2 - p1) * t;
        return p01 + (p12 - p01) * t;
    }

    [[nodiscard]] Rect2D bounds() const {
        // Tight bounds including extrema
        double min_x = std::min(p0.x, p2.x);
        double max_x = std::max(p0.x, p2.x);
        double min_y = std::min(p0.y, p2.y);
        double max_y = std::max(p0.y, p2.y);

        const double denom_x = std::fma(-2.0, p1.x, p0.x + p2.x);
        if (denom_x != 0.0) {
            const double tx = (p0.x - p1.x) / denom_x;
            if (std::isfinite(tx) && tx > 0.0 && tx < 1.0) {
                const double x = point_at(tx).x;
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
            }
        }

        const double denom_y = std::fma(-2.0, p1.y, p0.y + p2.y);
        if (denom_y != 0.0) {
            const double ty = (p0.y - p1.y) / denom_y;
            if (std::isfinite(ty) && ty > 0.0 && ty < 1.0) {
                const double y = point_at(ty).y;
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
            }
        }
        const double coordinate_scale = std::max({
            1.0, std::abs(p0.x), std::abs(p0.y), std::abs(p1.x), std::abs(p1.y),
            std::abs(p2.x), std::abs(p2.y), std::abs(min_x), std::abs(max_x),
            std::abs(min_y), std::abs(max_y)
        });
        const double padding = 64.0 * std::numeric_limits<double>::epsilon() * coordinate_scale;
        min_x = std::nextafter(min_x - padding, -std::numeric_limits<double>::infinity());
        min_y = std::nextafter(min_y - padding, -std::numeric_limits<double>::infinity());
        max_x = std::nextafter(max_x + padding, std::numeric_limits<double>::infinity());
        max_y = std::nextafter(max_y + padding, std::numeric_limits<double>::infinity());
        return Rect2D::from_ltrb(min_x, min_y, max_x, max_y);
    }

    [[nodiscard]] Point2D derivative_at(double t) const {
        return (p1 - p0) * (2.0 * (1.0 - t)) + (p2 - p1) * (2.0 * t);
    }
};

/// Cubic Bézier curve (p0, p1, p2, p3)
struct CubicBezier {
    Point2D p0;
    Point2D p1;
    Point2D p2;
    Point2D p3;

    [[nodiscard]] Point2D point_at(double t) const {
        const Point2D p01 = p0 + (p1 - p0) * t;
        const Point2D p12 = p1 + (p2 - p1) * t;
        const Point2D p23 = p2 + (p3 - p2) * t;
        const Point2D p012 = p01 + (p12 - p01) * t;
        const Point2D p123 = p12 + (p23 - p12) * t;
        return p012 + (p123 - p012) * t;
    }

    [[nodiscard]] Rect2D bounds() const;
    [[nodiscard]] Point2D derivative_at(double t) const;
};

/// Analytic elliptic arc P(t) = center + axis_u*cos(a) + axis_v*sin(a), a=start+sweep*t.
struct EllipticArc {
    Point2D center;
    Point2D axis_u;
    Point2D axis_v;
    double start_angle_rad{0.0};
    double sweep_angle_rad{0.0};

    [[nodiscard]] Point2D point_at(double t) const;
    [[nodiscard]] Point2D derivative_at(double t) const;
    [[nodiscard]] Rect2D bounds() const;
    [[nodiscard]] bool is_finite() const;
    [[nodiscard]] EllipticArc sub_arc(double t0, double t1) const;
    [[nodiscard]] EllipticArc transformed(const Transform2D& transform) const;
};

using CurveSegment = std::variant<LineSegment, QuadraticBezier, CubicBezier, EllipticArc>;

enum class GeometryOperationStatus {
    Success,
    InvalidInput,
    InvalidParameter,
    IterationLimit
};

enum class ArcResolutionKind { Arc, Line, Empty, Invalid };

struct EndpointArcInput {
    Point2D p0;
    double rx{0.0};
    double ry{0.0};
    double rotation_deg{0.0};
    bool large_arc{false};
    bool sweep{false};
    Point2D p1;
};

struct ArcResolutionResult {
    ArcResolutionKind kind{ArcResolutionKind::Invalid};
    EllipticArc arc{};
};

struct EndpointArcParameters {
    double rx{0.0};
    double ry{0.0};
    double rotation_deg{0.0};
    bool large_arc{false};
    bool sweep{false};
};

[[nodiscard]] ArcResolutionResult resolve_endpoint_arc(const EndpointArcInput& input);
[[nodiscard]] std::optional<EndpointArcParameters> encode_endpoint_arc(const EllipticArc& arc);

struct CurveSplitResult {
    GeometryOperationStatus status{GeometryOperationStatus::InvalidInput};
    std::optional<CurveSegment> first;
    std::optional<CurveSegment> second;
};

struct CurveMeasureResult {
    GeometryOperationStatus status{GeometryOperationStatus::InvalidInput};
    double length{0.0};
    double estimated_error{0.0};
};

struct NearestPointResult {
    GeometryOperationStatus status{GeometryOperationStatus::InvalidInput};
    Point2D point{};
    double parameter{0.0};
    double distance{0.0};
    double absolute_error_bound{0.0};
};

enum class IntersectionKind { None, Point, Overlap, InvalidInput };

struct SegmentIntersectionResult {
    IntersectionKind kind{IntersectionKind::None};
    Point2D first{};
    Point2D second{};
    double first_parameter{0.0};
    double second_parameter{0.0};
    double first_overlap_end_parameter{0.0};
    double second_overlap_end_parameter{0.0};
};

[[nodiscard]] Rect2D curve_bounds(const CurveSegment& curve);
[[nodiscard]] Point2D curve_point_at(const CurveSegment& curve, double t);
[[nodiscard]] Point2D curve_derivative_at(const CurveSegment& curve, double t);
[[nodiscard]] CurveSplitResult split_curve(const CurveSegment& curve, double t);
[[nodiscard]] CurveMeasureResult measure_curve_length(
    const CurveSegment& curve, double tolerance_mm = 1e-6, size_t max_depth = 20);
[[nodiscard]] NearestPointResult nearest_point_on_curve(
    const CurveSegment& curve, Point2D query, double tolerance_mm = 1e-5, size_t max_nodes = 100000);
[[nodiscard]] SegmentIntersectionResult intersect_line_segments(
    const LineSegment& first, const LineSegment& second, double tolerance_mm = 1e-9);

} // namespace pikdraw::geometry
