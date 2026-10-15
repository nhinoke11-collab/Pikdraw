#pragma once

#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pikdraw::rendering {

struct ScreenPoint {
  float x{0.0f};
  float y{0.0f};
};

/// 2D Viewport Camera mapping double-precision millimeters (world) to screen
/// pixels
class Camera2D {
public:
  static constexpr double kMinZoom = 0.01;
  static constexpr double kMaxZoom = 1000.0;

  Camera2D(geometry::Point2D center_mm = {10000.0, 500.0}, double zoom = 0.05,
           int32_t width_px = 1920, int32_t height_px = 1080) noexcept
      : center_mm_(center_mm.is_finite() ? center_mm : geometry::Point2D{}),
        zoom_(std::isfinite(zoom) ? std::clamp(zoom, kMinZoom, kMaxZoom) : 1.0),
        width_px_(std::max(1, width_px)), height_px_(std::max(1, height_px)) {}

  [[nodiscard]] geometry::Point2D center() const noexcept { return center_mm_; }
  [[nodiscard]] double zoom() const noexcept { return zoom_; }
  [[nodiscard]] int32_t width() const noexcept { return width_px_; }
  [[nodiscard]] int32_t height() const noexcept { return height_px_; }

  void set_center(geometry::Point2D center) noexcept {
    if (center.is_finite())
      center_mm_ = center;
  }

  void set_zoom(double zoom) noexcept {
    if (std::isfinite(zoom))
      zoom_ = std::clamp(zoom, kMinZoom, kMaxZoom);
  }

  void set_viewport_size(int32_t width, int32_t height) noexcept {
    width_px_ = std::max(1, width);
    height_px_ = std::max(1, height);
  }

  void pan_pixels(double delta_px_x, double delta_px_y) noexcept {
    if (!std::isfinite(delta_px_x) || !std::isfinite(delta_px_y))
      return;
    set_center({center_mm_.x - delta_px_x / zoom_,
                center_mm_.y + delta_px_y / zoom_}); // +Y up
  }

  void zoom_at(double factor, float focus_screen_x,
               float focus_screen_y) noexcept {
    if (!std::isfinite(factor) || factor <= 0.0 ||
        !std::isfinite(focus_screen_x) || !std::isfinite(focus_screen_y))
      return;
    auto world_before = screen_to_world({focus_screen_x, focus_screen_y});
    set_zoom(zoom_ * factor);
    auto world_after = screen_to_world({focus_screen_x, focus_screen_y});
    set_center({center_mm_.x + world_before.x - world_after.x,
                center_mm_.y + world_before.y - world_after.y});
  }

  /// Converts world mm point to screen coordinates.
  /// Camera origin is subtracted in double precision before conversion to local
  /// float.
  [[nodiscard]] ScreenPoint
  world_to_screen(const geometry::Point2D &world_pt) const noexcept {
    const double rel_x = (world_pt.x - center_mm_.x) * zoom_;
    const double rel_y = (center_mm_.y - world_pt.y) * zoom_;
    return ScreenPoint{static_cast<float>(rel_x + width_px_ * 0.5),
                       static_cast<float>(rel_y + height_px_ * 0.5)};
  }

  /// Converts screen pixel point back to world coordinates in double mm
  [[nodiscard]] geometry::Point2D
  screen_to_world(const ScreenPoint &screen_pt) const noexcept {
    const double rel_x = static_cast<double>(screen_pt.x) - width_px_ * 0.5;
    const double rel_y = static_cast<double>(screen_pt.y) - height_px_ * 0.5;
    return geometry::Point2D{center_mm_.x + rel_x / zoom_,
                             center_mm_.y - rel_y / zoom_};
  }

  /// Returns the world bounding box in millimeters currently visible in the
  /// viewport
  [[nodiscard]] geometry::Rect2D visible_world_bounds() const noexcept {
    auto top_left = screen_to_world({0.0f, 0.0f});
    auto bottom_right = screen_to_world(
        {static_cast<float>(width_px_), static_cast<float>(height_px_)});
    double min_x = std::min(top_left.x, bottom_right.x);
    double max_x = std::max(top_left.x, bottom_right.x);
    double min_y = std::min(top_left.y, bottom_right.y);
    double max_y = std::max(top_left.y, bottom_right.y);
    return geometry::Rect2D::from_ltrb(min_x, min_y, max_x, max_y);
  }

private:
  geometry::Point2D center_mm_{10000.0, 500.0};
  double zoom_{0.05};
  int32_t width_px_{1920};
  int32_t height_px_{1080};
};

} // namespace pikdraw::rendering
