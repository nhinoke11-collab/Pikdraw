#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include "pikdraw/geometry/point.hpp"
#include "pikdraw/geometry/transform.hpp"

namespace pikdraw::geometry {

class Rect2D {
public:
    double min_x{0.0};
    double min_y{0.0};
    double max_x{0.0};
    double max_y{0.0};

    constexpr Rect2D() = default;
    constexpr Rect2D(double x0, double y0, double x1, double y1)
        : min_x(std::min(x0, x1)), min_y(std::min(y0, y1)),
          max_x(std::max(x0, x1)), max_y(std::max(y0, y1)) {}

    static constexpr Rect2D from_xywh(double x, double y, double w, double h) {
        return Rect2D(x, y, x + w, y + h);
    }

    [[nodiscard]] bool is_finite() const {
        return std::isfinite(min_x) && std::isfinite(min_y) &&
               std::isfinite(max_x) && std::isfinite(max_y);
    }

    [[nodiscard]] double width() const { return max_x - min_x; }
    [[nodiscard]] double height() const { return max_y - min_y; }
    [[nodiscard]] Point2D center() const { return {(min_x + max_x) * 0.5, (min_y + max_y) * 0.5}; }

    [[nodiscard]] bool contains(const Point2D& p) const {
        return p.x >= min_x && p.x <= max_x && p.y >= min_y && p.y <= max_y;
    }

    [[nodiscard]] bool contains(const Rect2D& o) const {
        return o.min_x >= min_x && o.max_x <= max_x && o.min_y >= min_y && o.max_y <= max_y;
    }

    [[nodiscard]] bool intersects(const Rect2D& o) const {
        return !(max_x < o.min_x || o.max_x < min_x || max_y < o.min_y || o.max_y < min_y);
    }

    [[nodiscard]] std::optional<Rect2D> intersect(const Rect2D& o) const {
        if (!intersects(o)) {
            return std::nullopt;
        }
        return Rect2D(
            std::max(min_x, o.min_x),
            std::max(min_y, o.min_y),
            std::min(max_x, o.max_x),
            std::min(max_y, o.max_y)
        );
    }

    [[nodiscard]] Rect2D unite(const Rect2D& o) const {
        return Rect2D(
            std::min(min_x, o.min_x),
            std::min(min_y, o.min_y),
            std::max(max_x, o.max_x),
            std::max(max_y, o.max_y)
        );
    }

    [[nodiscard]] Rect2D transformed(const Transform2D& t) const {
        if (!is_finite() || !t.is_finite()) throw std::invalid_argument("Invalid bounds transform");
        const Point2D p0 = t.apply({min_x, min_y});
        const Point2D p1 = t.apply({max_x, min_y});
        const Point2D p2 = t.apply({max_x, max_y});
        const Point2D p3 = t.apply({min_x, max_y});

        for (const auto& point : {p0,p1,p2,p3})
            if (!std::isfinite(point.x) || !std::isfinite(point.y)) throw std::invalid_argument("Nonfinite transformed bounds");
        const double n_min_x = std::min({p0.x, p1.x, p2.x, p3.x});
        const double n_min_y = std::min({p0.y, p1.y, p2.y, p3.y});
        const double n_max_x = std::max({p0.x, p1.x, p2.x, p3.x});
        const double n_max_y = std::max({p0.y, p1.y, p2.y, p3.y});
        return Rect2D(n_min_x, n_min_y, n_max_x, n_max_y);
    }
};

} // namespace pikdraw::geometry
