#pragma once
#include "pikdraw/desktop/shell/native_window.hpp"
#include <array>
#include <cmath>

namespace pikdraw::desktop::selection_ui {
#ifdef _WIN32
inline RECT screen_bounds(const rendering::Camera2D &camera, const geometry::Rect2D &bounds,
                          const RECT &canvas) {
    const auto a = camera.world_to_screen({bounds.x, bounds.bottom()});
    const auto b = camera.world_to_screen({bounds.right(), bounds.y});
    const auto pixel = [](float v) { return static_cast<int>(std::lround(std::clamp(v, -1e7f, 1e7f))); };
    return {canvas.left + pixel(a.x), canvas.top + pixel(a.y), canvas.left + pixel(b.x),
            canvas.top + pixel(b.y)};
}
inline std::array<POINT, 8> handles(const RECT &r) {
    const LONG x = (r.left + r.right) / 2, y = (r.top + r.bottom) / 2;
    return {{{r.left, r.top},
             {x, r.top},
             {r.right, r.top},
             {r.right, y},
             {r.right, r.bottom},
             {x, r.bottom},
             {r.left, r.bottom},
             {r.left, y}}};
}
inline POINT rotation_handle(const RECT &r) {
    return {(r.left + r.right) / 2, r.top - 24};
}
inline POINT pivot_point(const rendering::Camera2D &camera, const geometry::Point2D &pivot, const RECT &canvas) {
    const auto screen = camera.world_to_screen({pivot.x, pivot.y});
    const auto pixel = [](float v) { return static_cast<int>(std::lround(std::clamp(v, -1e7f, 1e7f))); };
    return {canvas.left + pixel(screen.x), canvas.top + pixel(screen.y)};
}
#endif
inline geometry::Rect2D between(geometry::Point2D a, geometry::Point2D b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::abs(a.x - b.x), std::abs(a.y - b.y)};
}
} // namespace pikdraw::desktop::selection_ui
