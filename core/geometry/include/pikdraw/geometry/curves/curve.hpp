#pragma once

#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
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
};

/// Quadratic Bézier curve (p0, control p1, p2)
struct QuadraticBezier {
    Point2D p0;
    Point2D p1;
    Point2D p2;

    [[nodiscard]] Point2D point_at(double t) const {
        const double u = 1.0 - t;
        return {
            u * u * p0.x + 2.0 * u * t * p1.x + t * t * p2.x,
            u * u * p0.y + 2.0 * u * t * p1.y + t * t * p2.y
        };
    }

    [[nodiscard]] Rect2D bounds() const {
        // Tight bounds including extrema
        double min_x = std::min(p0.x, p2.x);
        double max_x = std::max(p0.x, p2.x);
        double min_y = std::min(p0.y, p2.y);
        double max_y = std::max(p0.y, p2.y);

        const double denom_x = p0.x - 2.0 * p1.x + p2.x;
        if (std::abs(denom_x) > 1e-12) {
            const double tx = (p0.x - p1.x) / denom_x;
            if (tx > 0.0 && tx < 1.0) {
                const double x = point_at(tx).x;
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
            }
        }

        const double denom_y = p0.y - 2.0 * p1.y + p2.y;
        if (std::abs(denom_y) > 1e-12) {
            const double ty = (p0.y - p1.y) / denom_y;
            if (ty > 0.0 && ty < 1.0) {
                const double y = point_at(ty).y;
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
            }
        }
        return Rect2D::from_ltrb(min_x, min_y, max_x, max_y);
    }
};

/// Cubic Bézier curve (p0, p1, p2, p3)
struct CubicBezier {
    Point2D p0;
    Point2D p1;
    Point2D p2;
    Point2D p3;

    [[nodiscard]] Point2D point_at(double t) const {
        const double u = 1.0 - t;
        const double u2 = u * u;
        const double u3 = u2 * u;
        const double t2 = t * t;
        const double t3 = t2 * t;

        return {
            u3 * p0.x + 3.0 * u2 * t * p1.x + 3.0 * u * t2 * p2.x + t3 * p3.x,
            u3 * p0.y + 3.0 * u2 * t * p1.y + 3.0 * u * t2 * p2.y + t3 * p3.y
        };
    }

    [[nodiscard]] Rect2D bounds() const;
};

/// Elliptic Arc segment
struct EllipticArc {
    Point2D p0;
    double rx{0.0};
    double ry{0.0};
    double x_axis_rotation{0.0};
    bool large_arc{false};
    bool sweep{false};
    Point2D p1;

    [[nodiscard]] Rect2D bounds() const;
};

using CurveSegment = std::variant<LineSegment, QuadraticBezier, CubicBezier, EllipticArc>;

} // namespace pikdraw::geometry
