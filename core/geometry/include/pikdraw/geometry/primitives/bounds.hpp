#pragma once

#include "pikdraw/geometry/primitives/point.hpp"
#include <algorithm>
#include <cmath>

namespace pikdraw::geometry {

/// Axis-aligned bounding box in millimeters (mm)
struct Rect2D {
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};

    constexpr Rect2D() = default;
    constexpr Rect2D(double px, double py, double pw, double ph)
        : x(px), y(py), width(pw), height(ph) {}

    static Rect2D from_ltrb(double left, double top, double right, double bottom) {
        return Rect2D{left, top, right - left, bottom - top};
    }

    [[nodiscard]] double left() const { return x; }
    [[nodiscard]] double top() const { return y; }
    [[nodiscard]] double right() const { return x + width; }
    [[nodiscard]] double bottom() const { return y + height; }

    [[nodiscard]] Point2D min_point() const { return {x, y}; }
    [[nodiscard]] Point2D max_point() const { return {x + width, y + height}; }
    [[nodiscard]] Point2D center() const { return {x + width * 0.5, y + height * 0.5}; }

    [[nodiscard]] bool is_empty() const {
        return width <= 0.0 || height <= 0.0;
    }

    [[nodiscard]] bool is_finite() const {
        return std::isfinite(x) && std::isfinite(y) &&
               std::isfinite(width) && std::isfinite(height);
    }

    [[nodiscard]] bool contains(const Point2D& pt) const {
        return pt.x >= x && pt.x <= (x + width) &&
               pt.y >= y && pt.y <= (y + height);
    }

    [[nodiscard]] bool intersects(const Rect2D& other) const {
        return !(other.x > right() || other.right() < x ||
                 other.y > bottom() || other.bottom() < y);
    }

    [[nodiscard]] Rect2D united(const Rect2D& other) const {
        if (is_empty()) return other;
        if (other.is_empty()) return *this;
        const double l = std::min(x, other.x);
        const double t = std::min(y, other.y);
        const double r = std::max(right(), other.right());
        const double b = std::max(bottom(), other.bottom());
        return from_ltrb(l, t, r, b);
    }

    [[nodiscard]] Rect2D intersected(const Rect2D& other) const {
        if (!intersects(other)) return Rect2D{};
        const double l = std::max(x, other.x);
        const double t = std::max(y, other.y);
        const double r = std::min(right(), other.right());
        const double b = std::min(bottom(), other.bottom());
        return from_ltrb(l, t, r, b);
    }

    [[nodiscard]] bool approx_equal(const Rect2D& other, double eps = 1e-6) const {
        return std::abs(x - other.x) <= eps &&
               std::abs(y - other.y) <= eps &&
               std::abs(width - other.width) <= eps &&
               std::abs(height - other.height) <= eps;
    }
};

using Bounds2D = Rect2D;

} // namespace pikdraw::geometry
