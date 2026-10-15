#include "pikdraw/rendering/camera.hpp"

namespace pikdraw::rendering {

Camera2D::Camera2D(Point2D center, double initial_zoom, int32_t width, int32_t height) noexcept {
    if (center.is_finite() && std::abs(center.x) <= MaxCoordMm && std::abs(center.y) <= MaxCoordMm) {
        center_mm_ = center;
    }
    if (std::isfinite(initial_zoom) && initial_zoom > 0.0) {
        zoom_ = std::clamp(initial_zoom, MinZoom, MaxZoom);
    }
    if (width > 0 && width <= MaxViewportDim) {
        viewport_width_px_ = width;
    }
    if (height > 0 && height <= MaxViewportDim) {
        viewport_height_px_ = height;
    }
}

bool Camera2D::set_center(Point2D new_center) noexcept {
    if (!new_center.is_finite() || std::abs(new_center.x) > MaxCoordMm || std::abs(new_center.y) > MaxCoordMm) {
        return false;
    }
    center_mm_ = new_center;
    return true;
}

bool Camera2D::set_zoom(double new_zoom) noexcept {
    if (!std::isfinite(new_zoom) || new_zoom <= 0.0) {
        return false;
    }
    zoom_ = std::clamp(new_zoom, MinZoom, MaxZoom);
    return true;
}

bool Camera2D::set_viewport_size(int32_t width, int32_t height) noexcept {
    if (width <= 0 || width > MaxViewportDim || height <= 0 || height > MaxViewportDim) {
        return false;
    }
    viewport_width_px_ = width;
    viewport_height_px_ = height;
    return true;
}

bool Camera2D::world_to_local_relative(const Point2D& world_pt, Point2F& out) const noexcept {
    if (!world_pt.is_finite() || std::abs(world_pt.x) > MaxCoordMm || std::abs(world_pt.y) > MaxCoordMm) {
        return false;
    }
    const double dx = world_pt.x - center_mm_.x;
    const double dy = world_pt.y - center_mm_.y;
    out = Point2F{ static_cast<float>(dx), static_cast<float>(dy) };
    return true;
}

bool Camera2D::world_to_screen(const Point2D& world_pt, Point2F& out) const noexcept {
    if (!world_pt.is_finite() || std::abs(world_pt.x) > MaxCoordMm || std::abs(world_pt.y) > MaxCoordMm) {
        return false;
    }
    const double dx = world_pt.x - center_mm_.x;
    const double dy = world_pt.y - center_mm_.y;
    const float half_w = static_cast<float>(viewport_width_px_) * 0.5f;
    const float half_h = static_cast<float>(viewport_height_px_) * 0.5f;
    out = Point2F{
        static_cast<float>(dx * zoom_) + half_w,
        static_cast<float>(dy * zoom_) + half_h
    };
    return true;
}

bool Camera2D::screen_to_world(const Point2F& screen_pt, Point2D& out) const noexcept {
    if (!screen_pt.is_finite() || zoom_ <= 0.0 || viewport_width_px_ <= 0 || viewport_height_px_ <= 0) {
        return false;
    }
    const double half_w = static_cast<double>(viewport_width_px_) * 0.5;
    const double half_h = static_cast<double>(viewport_height_px_) * 0.5;
    const double dx = (static_cast<double>(screen_pt.x) - half_w) / zoom_;
    const double dy = (static_cast<double>(screen_pt.y) - half_h) / zoom_;
    const Point2D candidate{ center_mm_.x + dx, center_mm_.y + dy };
    if (!candidate.is_finite()) return false;
    out = candidate;
    return true;
}

Rect2D Camera2D::visible_world_bounds() const noexcept {
    Point2D top_left{0.0, 0.0};
    Point2D bottom_right{0.0, 0.0};
    if (!screen_to_world(Point2F{0.0f, 0.0f}, top_left) || !screen_to_world(Point2F{
        static_cast<float>(viewport_width_px_),
        static_cast<float>(viewport_height_px_)
    }, bottom_right)) return {};
    return Rect2D{
        top_left.x,
        top_left.y,
        std::abs(bottom_right.x - top_left.x),
        std::abs(bottom_right.y - top_left.y)
    };
}

} // namespace pikdraw::rendering
