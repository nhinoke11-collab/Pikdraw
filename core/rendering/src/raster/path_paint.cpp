#include "path_raster.hpp"
#include "pikdraw/rendering/gradient_sampler.hpp"
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
bool fill(const std::vector<Contour> &contours, geometry::WindingRule rule,
          const RasterColor &value, PixelBufferTarget &target,
          const GradientSampler *gradient = nullptr) {
  if (rule != geometry::WindingRule::NonZero &&
      rule != geometry::WindingRule::EvenOdd)
    return false;
  bool has_points = false;
  double min_y = 0.0;
  double max_y = 0.0;
  for (const auto &contour : contours) {
    for (const auto point : contour.screen) {
      if (!has_points) {
        min_y = max_y = point.y;
        has_points = true;
      } else {
        min_y = std::min(min_y, point.y);
        max_y = std::max(max_y, point.y);
      }
    }
  }
  if (!has_points)
    return true;

  // Allocate and visit scanline buckets only where this path can cover pixel
  // centers. Small objects should not allocate one vector per viewport row.
  const int first_row = bounded_ceil(min_y - 0.5, target.height);
  const int end_row = bounded_ceil(max_y - 0.5, target.height);
  if (end_row <= first_row)
    return true;
  std::vector<std::vector<Hit>> rows(
      static_cast<size_t>(end_row - first_row));
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
        rows[static_cast<size_t>(y - first_row)].push_back(
            {a.x + t * (b.x - a.x), winding});
      }
    }
  }
  for (int y = first_row; y < end_row; ++y) {
    auto &hits = rows[static_cast<size_t>(y - first_row)];
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
        if (gradient) {
          for (int col = x0; col < x1; ++col) {
            const auto sample = gradient->sample_raster_color(col, y, target);
            blend_preview(target, col, y, sample);
          }
        } else {
          for (int col = x0; col < x1; ++col)
            blend_preview(target, col, y, value);
        }
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
  if ((object.has_fill && !object.fill_gradient && !object.fill.is_valid()) ||
      (object.has_stroke && !object.stroke.is_valid()))
    return false;
  std::vector<Contour> contours;
  if (!flatten(object, camera, contours))
    return false;
  const auto convert = [preview](const color::ColorValue &value) {
    return preview ? preview->convert(value) : color::to_preview_rgba(value);
  };
  if (object.has_fill) {
    if (object.fill_gradient) {
      const auto sampler = GradientSampler::prepare(
          *object.fill_gradient, object.transform, camera, preview,
          object.appearance.fill_opacity);
      if (!sampler)
        return false;
      const RasterColor dummy{};
      if (!fill(contours, object.path.winding_rule(), dummy, target, &*sampler))
        return false;
    } else {
      const auto value = make_raster_color(convert(object.fill), target);
      if (!fill(contours, object.path.winding_rule(), value, target, nullptr))
        return false;
    }
  }
  return !object.has_stroke ||
         stroke(contours, object, camera,
                convert(object.stroke), target);
}
} // namespace pikdraw::rendering::raster
