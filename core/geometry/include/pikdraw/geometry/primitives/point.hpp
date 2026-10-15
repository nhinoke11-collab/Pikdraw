#pragma once

#include <cmath>

namespace pikdraw::geometry {

/// 2D Point in double-precision millimeters (mm)
struct Point2D {
    double x{0.0};
    double y{0.0};

    constexpr Point2D() = default;
    constexpr Point2D(double px, double py) : x(px), y(py) {}

    [[nodiscard]] bool is_finite() const {
        return std::isfinite(x) && std::isfinite(y);
    }

    [[nodiscard]] bool approx_equal(const Point2D& other, double eps = 1e-6) const {
        return std::abs(x - other.x) <= eps && std::abs(y - other.y) <= eps;
    }

    [[nodiscard]] double distance_squared_to(const Point2D& other) const {
        const double dx = x - other.x;
        const double dy = y - other.y;
        return dx * dx + dy * dy;
    }

    [[nodiscard]] double distance_to(const Point2D& other) const {
        return std::sqrt(distance_squared_to(other));
    }

    Point2D operator+(const Point2D& o) const { return {x + o.x, y + o.y}; }
    Point2D operator-(const Point2D& o) const { return {x - o.x, y - o.y}; }
    Point2D operator*(double s) const { return {x * s, y * s}; }
    Point2D operator/(double s) const { return {x / s, y / s}; }

    bool operator==(const Point2D& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Point2D& o) const { return !(*this == o); }
};

/// Screen / Viewport render point in camera-relative local float coordinates
struct RenderPoint {
    float x{0.0f};
    float y{0.0f};
};

[[nodiscard]] inline RenderPoint to_render_local(const Point2D& pt, const Point2D& camera_origin) {
    return RenderPoint{
        static_cast<float>(pt.x - camera_origin.x),
        static_cast<float>(pt.y - camera_origin.y)
    };
}

} // namespace pikdraw::geometry
