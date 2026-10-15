#pragma once

#include <cstdint>
#include <cmath>
#include <array>

namespace pikdraw::rendering {

struct Point2D {
    double x{0.0};
    double y{0.0};

    [[nodiscard]] bool is_finite() const noexcept {
        return std::isfinite(x) && std::isfinite(y);
    }
};

struct Point2F {
    float x{0.0f};
    float y{0.0f};

    [[nodiscard]] bool is_finite() const noexcept {
        return std::isfinite(x) && std::isfinite(y);
    }
};

struct Rect2D {
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};

    [[nodiscard]] bool is_finite() const noexcept {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(width) && std::isfinite(height);
    }

    [[nodiscard]] constexpr bool contains(const Point2D& pt) const noexcept {
        return pt.x >= x && pt.x <= (x + width) && pt.y >= y && pt.y <= (y + height);
    }

    [[nodiscard]] constexpr bool intersects(const Rect2D& other) const noexcept {
        return !(x + width < other.x || other.x + other.width < x ||
                 y + height < other.y || other.y + other.height < y);
    }
};

struct Transform2D {
    double a{1.0};
    double b{0.0};
    double c{0.0};
    double d{1.0};
    double tx{0.0};
    double ty{0.0};

    [[nodiscard]] static constexpr Transform2D identity() noexcept {
        return Transform2D{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    }

    [[nodiscard]] static constexpr Transform2D translation(double tx, double ty) noexcept {
        return Transform2D{1.0, 0.0, 0.0, 1.0, tx, ty};
    }

    [[nodiscard]] static constexpr Transform2D scale(double sx, double sy) noexcept {
        return Transform2D{sx, 0.0, 0.0, sy, 0.0, 0.0};
    }

    [[nodiscard]] static Transform2D rotation(double radians) noexcept {
        const double cos_val = std::cos(radians);
        const double sin_val = std::sin(radians);
        return Transform2D{cos_val, sin_val, -sin_val, cos_val, 0.0, 0.0};
    }

    [[nodiscard]] bool is_finite() const noexcept {
        return std::isfinite(a) && std::isfinite(b) && std::isfinite(c) &&
               std::isfinite(d) && std::isfinite(tx) && std::isfinite(ty);
    }

    [[nodiscard]] Point2D apply(const Point2D& pt) const noexcept {
        return Point2D{
            a * pt.x + c * pt.y + tx,
            b * pt.x + d * pt.y + ty
        };
    }

    [[nodiscard]] static Transform2D compose(const Transform2D& parent, const Transform2D& local) noexcept {
        return Transform2D{
            parent.a * local.a + parent.c * local.b,
            parent.b * local.a + parent.d * local.b,
            parent.a * local.c + parent.c * local.d,
            parent.b * local.c + parent.d * local.d,
            parent.a * local.tx + parent.c * local.ty + parent.tx,
            parent.b * local.tx + parent.d * local.ty + parent.ty
        };
    }

    [[nodiscard]] bool invert(Transform2D& out) const noexcept {
        if (!is_finite()) return false;
        const double det = a * d - b * c;
        if (!std::isfinite(det) || std::abs(det) < 1e-15) {
            return false;
        }
        const double inv_det = 1.0 / det;
        Transform2D candidate{d * inv_det, -b * inv_det, -c * inv_det, a * inv_det,
            (c * ty - d * tx) * inv_det, (b * tx - a * ty) * inv_det};
        if (!candidate.is_finite()) return false;
        out = candidate;
        return true;
    }
};

struct ColorRGBA8 {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
    uint8_t a{255};
};

} // namespace pikdraw::rendering
