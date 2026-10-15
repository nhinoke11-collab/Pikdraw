#pragma once

#include "pikdraw/rendering/types.hpp"
#include <algorithm>

namespace pikdraw::rendering {

class Camera2D {
public:
    static constexpr double MinZoom = 1e-6;
    static constexpr double MaxZoom = 1e6;
    static constexpr double MaxCoordMm = 1e7;
    static constexpr int32_t MaxViewportDim = 16384;

    Camera2D() noexcept = default;
    Camera2D(Point2D center, double initial_zoom, int32_t width, int32_t height) noexcept;

    [[nodiscard]] Point2D center() const noexcept { return center_mm_; }
    [[nodiscard]] double zoom() const noexcept { return zoom_; }
    [[nodiscard]] int32_t viewport_width() const noexcept { return viewport_width_px_; }
    [[nodiscard]] int32_t viewport_height() const noexcept { return viewport_height_px_; }

    bool set_center(Point2D new_center) noexcept;
    bool set_zoom(double new_zoom) noexcept;
    bool set_viewport_size(int32_t width, int32_t height) noexcept;

    [[nodiscard]] bool world_to_local_relative(const Point2D& world_pt, Point2F& out) const noexcept;
    [[nodiscard]] bool world_to_screen(const Point2D& world_pt, Point2F& out) const noexcept;
    [[nodiscard]] bool screen_to_world(const Point2F& screen_pt, Point2D& out) const noexcept;
    [[nodiscard]] Rect2D visible_world_bounds() const noexcept;

private:
    Point2D center_mm_{0.0, 0.0};
    double zoom_{1.0};
    int32_t viewport_width_px_{800};
    int32_t viewport_height_px_{600};
};

} // namespace pikdraw::rendering
