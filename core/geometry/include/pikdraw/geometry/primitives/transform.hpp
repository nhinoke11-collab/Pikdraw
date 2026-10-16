#pragma once

#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include <cmath>
#include <optional>

namespace pikdraw::geometry {

/// 2D Affine Transformation Matrix according to Contract C1:
/// [ a   c   tx ]
/// [ b   d   ty ]
/// [ 0   0   1  ]
struct Transform2D {
    double a{1.0};
    double b{0.0};
    double c{0.0};
    double d{1.0};
    double tx{0.0};
    double ty{0.0};

    constexpr Transform2D() = default;
    constexpr Transform2D(double ma, double mb, double mc, double md, double mtx, double mty)
        : a(ma), b(mb), c(mc), d(md), tx(mtx), ty(mty) {}

    static constexpr Transform2D identity() {
        return Transform2D{};
    }

    static Transform2D translation(double dx, double dy) {
        return Transform2D{1.0, 0.0, 0.0, 1.0, dx, dy};
    }

    static Transform2D scale(double sx, double sy) {
        return Transform2D{sx, 0.0, 0.0, sy, 0.0, 0.0};
    }

    static Transform2D rotation_radians(double angle_rad) {
        const double cos_a = std::cos(angle_rad);
        const double sin_a = std::sin(angle_rad);
        return Transform2D{cos_a, sin_a, -sin_a, cos_a, 0.0, 0.0};
    }

    static Transform2D rotation_degrees(double angle_deg) {
        return rotation_radians(angle_deg * (3.14159265358979323846 / 180.0));
    }

    [[nodiscard]] double determinant() const {
        return a * d - b * c;
    }

    [[nodiscard]] bool is_finite() const {
        return std::isfinite(a) && std::isfinite(b) &&
               std::isfinite(c) && std::isfinite(d) &&
               std::isfinite(tx) && std::isfinite(ty);
    }

    [[nodiscard]] bool is_invertible(double eps = 1e-12) const {
        return std::abs(determinant()) > eps;
    }

    [[nodiscard]] std::optional<Transform2D> inverted(double eps = 1e-12) const {
        const double det = determinant();
        if (std::abs(det) <= eps) return std::nullopt;
        const double inv_det = 1.0 / det;
        return Transform2D{
             d * inv_det,
            -b * inv_det,
            -c * inv_det,
             a * inv_det,
            (c * ty - d * tx) * inv_det,
            (b * tx - a * ty) * inv_det
        };
    }

    [[nodiscard]] Point2D map_point(const Point2D& p) const {
        return Point2D{
            a * p.x + c * p.y + tx,
            b * p.x + d * p.y + ty
        };
    }

    [[nodiscard]] Point2D map_vector(const Point2D& v) const {
        return Point2D{a * v.x + c * v.y, b * v.x + d * v.y};
    }

    [[nodiscard]] Rect2D map_rect(const Rect2D& r) const {
        const Point2D p1 = map_point({r.x, r.y});
        const Point2D p2 = map_point({r.right(), r.y});
        const Point2D p3 = map_point({r.right(), r.bottom()});
        const Point2D p4 = map_point({r.x, r.bottom()});

        const double min_x = std::min({p1.x, p2.x, p3.x, p4.x});
        const double max_x = std::max({p1.x, p2.x, p3.x, p4.x});
        const double min_y = std::min({p1.y, p2.y, p3.y, p4.y});
        const double max_y = std::max({p1.y, p2.y, p3.y, p4.y});

        return Rect2D::from_ltrb(min_x, min_y, max_x, max_y);
    }

    Transform2D operator*(const Transform2D& rhs) const {
        return Transform2D{
            a * rhs.a + c * rhs.b,
            b * rhs.a + d * rhs.b,
            a * rhs.c + c * rhs.d,
            b * rhs.c + d * rhs.d,
            a * rhs.tx + c * rhs.ty + tx,
            b * rhs.tx + d * rhs.ty + ty
        };
    }
};

} // namespace pikdraw::geometry
