#include "path_raster.hpp"
#include <algorithm>
#include <cmath>

namespace pikdraw::rendering::raster {
namespace {
constexpr size_t kWorkBudget = 20000000;
int clipped(double value, int limit) {
  return static_cast<int>(
      std::ceil(std::clamp(value, 0.0, static_cast<double>(limit))));
}
double distance_squared(Point p, Point a, Point b) {
  const auto delta = b - a;
  const double length2 = delta.x * delta.x + delta.y * delta.y;
  const double t =
      length2 > 0 ? std::clamp(((p.x - a.x) * delta.x + (p.y - a.y) * delta.y) /
                                   length2,
                               0.0, 1.0)
                  : 0;
  return p.distance_squared_to(a + delta * t);
}
struct Coverage {
  int left{0};
  int top{0};
  size_t width{0};
  std::vector<uint8_t> mask;
  std::vector<size_t> covered;

  Coverage(const std::vector<Contour> &contours, double radius,
           const PixelBufferTarget &target) {
    double min_x = target.width, min_y = target.height, max_x = 0, max_y = 0;
    for (const auto &contour : contours) {
      for (auto p : contour.screen) {
        min_x = std::min(min_x, p.x - radius);
        min_y = std::min(min_y, p.y - radius);
        max_x = std::max(max_x, p.x + radius);
        max_y = std::max(max_y, p.y + radius);
      }
    }
    left = clipped(min_x - 0.5, target.width);
    top = clipped(min_y - 0.5, target.height);
    const int right = clipped(max_x - 0.5, target.width);
    const int bottom = clipped(max_y - 0.5, target.height);
    if (right > left && bottom > top) {
      width = static_cast<size_t>(right - left);
      mask.resize(width * static_cast<size_t>(bottom - top));
    }
  }
  void mark(int x, int y) {
    const size_t index =
        static_cast<size_t>(y - top) * width + static_cast<size_t>(x - left);
    if (!mask[index]) {
      mask[index] = 1;
      covered.push_back(index);
    }
  }
  void blend_into(PixelBufferTarget &target, color::DeviceRGBA value) const {
    // Union coverage: joins/self-overlaps composite once per source object.
    for (auto index : covered) {
      target.blend_pixel(left + static_cast<int>(index % width),
                         top + static_cast<int>(index / width), value.r,
                         value.g, value.b, value.a);
    }
  }
};

struct StrokeSampler {
  const Camera2D &camera;
  const geometry::Transform2D &inverse;
  double radius;
  double screen_radius;
  size_t work{0};
  bool segment(Point local_a, Point local_b, Point a, Point b,
               Coverage &coverage, const PixelBufferTarget &target) {
    const int x0 =
        clipped(std::min(a.x, b.x) - screen_radius - 0.5, target.width);
    const int x1 =
        clipped(std::max(a.x, b.x) + screen_radius - 0.5, target.width);
    const int y0 =
        clipped(std::min(a.y, b.y) - screen_radius - 0.5, target.height);
    const int y1 =
        clipped(std::max(a.y, b.y) + screen_radius - 0.5, target.height);
    work += static_cast<size_t>(x1 - x0) * static_cast<size_t>(y1 - y0);
    if (work > kWorkBudget)
      return false;
    for (int y = y0; y < y1; ++y) {
      for (int x = x0; x < x1; ++x) {
        const Point world{camera.center().x +
                              (x + 0.5 - camera.width() * 0.5) / camera.zoom(),
                          camera.center().y -
                              (y + 0.5 - camera.height() * 0.5) /
                                  camera.zoom()};
        if (distance_squared(inverse.map_point(world), local_a, local_b) <=
            radius * radius) {
          coverage.mark(x, y);
        }
      }
    }
    return true;
  }
};
} // namespace

bool stroke(const std::vector<Contour> &contours,
            const document::PathObject &object, const Camera2D &camera,
            color::DeviceRGBA value, PixelBufferTarget &target) {
  if (object.stroke_width_mm == 0 || value.a == 0)
    return true;
  const auto inverse = object.transform.inverted();
  if (!inverse || !inverse->is_finite())
    return false;
  const auto &t = object.transform;
  const double radius = object.stroke_width_mm * 0.5;
  const double screen_radius =
      radius * camera.zoom() *
      std::hypot(std::hypot(t.a, t.b), std::hypot(t.c, t.d));
  if (!std::isfinite(screen_radius))
    return false;
  Coverage coverage{contours, screen_radius, target};
  if (coverage.mask.empty())
    return true;
  StrokeSampler sampler{camera, *inverse, radius, screen_radius};
  for (const auto &contour : contours) {
    const size_t count = contour.local.size();
    if (count < 2)
      continue;
    const size_t segments = contour.closed ? count : count - 1;
    for (size_t i = 0; i < segments; ++i) {
      const size_t j = (i + 1) % count;
      if (!sampler.segment(contour.local[i], contour.local[j],
                           contour.screen[i], contour.screen[j], coverage,
                           target))
        return false;
    }
  }
  coverage.blend_into(target, value);
  return true;
}
} // namespace pikdraw::rendering::raster
