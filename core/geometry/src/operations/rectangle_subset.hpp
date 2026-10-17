#pragma once
#include "pikdraw/geometry/curves/path.hpp"
#include <optional>

namespace pikdraw::geometry::detail {

// Exact recognition only. Approximate equality could silently turn a trapezoid into a rectangle.
inline std::optional<Rect2D> axis_aligned_rectangle(const Path2D& path) {
    const auto& elements = path.elements();
    if (elements.size() != 5 || elements.front().verb != PathVerb::MoveTo ||
        elements.back().verb != PathVerb::Close) return std::nullopt;
    for (size_t i = 1; i < 4; ++i)
        if (elements[i].verb != PathVerb::LineTo) return std::nullopt;
    const auto bounds = path.bounds();
    if (!bounds.is_finite() || bounds.is_empty()) return std::nullopt;
    unsigned corners = 0;
    for (size_t i = 0; i < 4; ++i) {
        const auto point = elements[i].pt;
        const auto next = elements[(i + 1) % 4].pt;
        if (!point.is_finite() ||
            !((point.x == next.x) != (point.y == next.y))) return std::nullopt;
        if (point.x != bounds.left() && point.x != bounds.right()) return std::nullopt;
        if (point.y != bounds.top() && point.y != bounds.bottom()) return std::nullopt;
        const unsigned corner = (point.x == bounds.right() ? 1u : 0u) |
                                (point.y == bounds.bottom() ? 2u : 0u);
        if ((corners & (1u << corner)) != 0) return std::nullopt;
        corners |= 1u << corner;
    }
    return corners == 15 ? std::optional<Rect2D>(bounds) : std::nullopt;
}
}
