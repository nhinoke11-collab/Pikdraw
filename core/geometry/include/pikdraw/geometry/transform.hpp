#pragma once

#include <array>
#include <cmath>
#include <optional>
#include "pikdraw/geometry/point.hpp"

namespace pikdraw::geometry {

// 2D Affine Transformation
// Matrix layout: [a, b, c, d, tx, ty]
// Transformation formula:
//   x' = a * x + c * y + tx
//   y' = b * x + d * y + ty
class Transform2D {
public:
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
        return Transform2D{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    }

    static constexpr Transform2D translation(double tx, double ty) {
        return Transform2D{1.0, 0.0, 0.0, 1.0, tx, ty};
    }

    static constexpr Transform2D scale(double sx, double sy) {
        return Transform2D{sx, 0.0, 0.0, sy, 0.0, 0.0};
    }

    static Transform2D rotation_radians(double rad) {
        const double cos_r = std::cos(rad);
        const double sin_r = std::sin(rad);
        return Transform2D{cos_r, sin_r, -sin_r, cos_r, 0.0, 0.0};
    }

    [[nodiscard]] bool is_finite() const {
        return std::isfinite(a) && std::isfinite(b) &&
               std::isfinite(c) && std::isfinite(d) &&
               std::isfinite(tx) && std::isfinite(ty);
    }

    [[nodiscard]] Point2D apply(const Point2D& p) const {
        return Point2D{
            a * p.x + c * p.y + tx,
            b * p.x + d * p.y + ty
        };
    }

    [[nodiscard]] double determinant() const {
        return a * d - b * c;
    }

    [[nodiscard]] bool is_invertible(double eps = 1e-12) const {
        return inverse(eps).has_value();
    }

    [[nodiscard]] std::optional<Transform2D> inverse(double eps = 1e-12) const {
        const double det = determinant();
        if (!is_finite() || !std::isfinite(det) || !std::isfinite(eps) || eps<0 || std::abs(det) <= eps) {
            return std::nullopt;
        }
        const double inv_det = 1.0 / det;
        const Transform2D candidate{
            d * inv_det,
            -b * inv_det,
            -c * inv_det,
            a * inv_det,
            (c * ty - d * tx) * inv_det,
            (b * tx - a * ty) * inv_det
        };
        return candidate.is_finite() ? std::optional<Transform2D>(candidate) : std::nullopt;
    }

    // compose(parent, local): applies local first, then parent.
    // v' = Parent(Local(v))
    [[nodiscard]] static Transform2D compose(const Transform2D& parent, const Transform2D& local) {
        return Transform2D{
            parent.a * local.a + parent.c * local.b,
            parent.b * local.a + parent.d * local.b,
            parent.a * local.c + parent.c * local.d,
            parent.b * local.c + parent.d * local.d,
            parent.a * local.tx + parent.c * local.ty + parent.tx,
            parent.b * local.tx + parent.d * local.ty + parent.ty
        };
    }

    [[nodiscard]] bool approx_equal(const Transform2D& o, double eps = 1e-9) const {
        return std::abs(a - o.a) <= eps && std::abs(b - o.b) <= eps &&
               std::abs(c - o.c) <= eps && std::abs(d - o.d) <= eps &&
               std::abs(tx - o.tx) <= eps && std::abs(ty - o.ty) <= eps;
    }
};

} // namespace pikdraw::geometry
