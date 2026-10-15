#include "path_raster.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace pikdraw::rendering::raster {
namespace {
constexpr size_t kWorkBudget = 20000000;
struct Hit {
  double x;
  int winding;
};
int bounded_ceil(double value, int limit) {
  return static_cast<int>(
      std::ceil(std::clamp(value, 0.0, static_cast<double>(limit))));
}
void blend(PixelBufferTarget &target, int x, int y, color::DeviceRGBA value) {
  target.blend_pixel(x, y, value.r, value.g, value.b, value.a);
}

bool fill(const std::vector<Contour> &contours, geometry::WindingRule rule,
          color::DeviceRGBA value, PixelBufferTarget &target) {
  if (rule != geometry::WindingRule::NonZero &&
      rule != geometry::WindingRule::EvenOdd)
    return false;
  std::vector<std::vector<Hit>> rows(static_cast<size_t>(target.height));
  size_t work = 0;
  for (const auto &contour : contours) {
    const auto &points = contour.screen;
    // Filling implicitly closes open subpaths; stroking below does not.
    for (size_t i = 0; i < points.size(); ++i) {
      auto a = points[i], b = points[(i + 1) % points.size()];
      if (a.y == b.y)
        continue;
      const int winding = a.y < b.y ? 1 : -1;
      if (a.y > b.y)
        std::swap(a, b);
      const int y0 = bounded_ceil(a.y - 0.5, target.height);
      const int y1 = bounded_ceil(b.y - 0.5, target.height);
      work += static_cast<size_t>(y1 - y0);
      if (work > kWorkBudget)
        return false;
      for (int y = y0; y < y1; ++y) {
        const double t = (static_cast<double>(y) + 0.5 - a.y) / (b.y - a.y);
        rows[static_cast<size_t>(y)].push_back(
            {a.x + t * (b.x - a.x), winding});
      }
    }
  }
  for (int y = 0; y < target.height; ++y) {
    auto &hits = rows[static_cast<size_t>(y)];
    std::sort(hits.begin(), hits.end(),
              [](const Hit &a, const Hit &b) { return a.x < b.x; });
    int winding = 0;
    double previous = 0;
    size_t i = 0;
    while (i < hits.size()) {
      const double x = hits[i].x;
      const bool inside = rule == geometry::WindingRule::EvenOdd
                              ? winding % 2 != 0
                              : winding != 0;
      if (inside) {
        const int x0 = bounded_ceil(previous - 0.5, target.width);
        const int x1 = bounded_ceil(x - 0.5, target.width);
        for (int col = x0; col < x1; ++col)
          blend(target, col, y, value);
      }
      while (i < hits.size() && hits[i].x == x)
        winding += hits[i++].winding;
      previous = x;
    }
  }
  return true;
}

} // namespace

bool paint(const document::PathObject &object, const Camera2D &camera,
           PixelBufferTarget &target, const color::IccPreview *preview) {
  if ((object.has_fill && !object.fill.is_valid()) ||
      (object.has_stroke && !object.stroke.is_valid()))
    return false;
  std::vector<Contour> contours;
  if (!flatten(object, camera, contours))
    return false;
  const auto convert = [preview](const color::ColorValue &value) {
    return (preview ? preview->convert(value) : color::to_preview_rgba(value)).to_device();
  };
  if (object.has_fill &&
      !fill(contours, object.path.winding_rule(),
            convert(object.fill), target))
    return false;
  return !object.has_stroke ||
         stroke(contours, object, camera,
                convert(object.stroke), target);
}
} // namespace pikdraw::rendering::raster
