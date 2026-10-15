#include "path_raster.hpp"
#include <algorithm>
#include <cmath>

namespace pikdraw::rendering::raster {
Point project(Point local, const geometry::Transform2D &transform,
              const Camera2D &camera) {
  const auto world = transform.map_point(local);
  return {(world.x - camera.center().x) * camera.zoom() + camera.width() * 0.5,
          (camera.center().y - world.y) * camera.zoom() +
              camera.height() * 0.5};
}

namespace {
constexpr size_t kMaxPoints = 200000;
constexpr double kMaxScreenCoordinate = 1e12;
bool acceptable(Point p) {
  return p.is_finite() && std::abs(p.x) <= kMaxScreenCoordinate &&
         std::abs(p.y) <= kMaxScreenCoordinate;
}
struct Flattener {
  const document::PathObject &object;
  const Camera2D &camera;
  size_t points{0};
  Point screen(Point p) const { return project(p, object.transform, camera); }
  bool append(Contour &contour, Point p) {
    const auto s = screen(p);
    if (!p.is_finite() || !acceptable(s) || ++points > kMaxPoints)
      return false;
    contour.local.push_back(p);
    contour.screen.push_back(s);
    return true;
  }
  double distance_from_chord(Point p, Point a, Point b) const {
    const auto delta = b - a;
    const double length = std::hypot(delta.x, delta.y);
    if (length < 1e-12)
      return std::hypot(p.x - a.x, p.y - a.y);
    return std::abs((p.x - a.x) * delta.y - (p.y - a.y) * delta.x) / length;
  }
  bool cubic(Contour &contour, Point a, Point b, Point c, Point d,
             int depth = 0) {
    const auto sa = screen(a), sb = screen(b), sc = screen(c), sd = screen(d);
    if (!acceptable(sa) || !acceptable(sb) || !acceptable(sc) ||
        !acceptable(sd))
      return false;
    const double polygon_length = std::hypot(sb.x - sa.x, sb.y - sa.y) +
                                  std::hypot(sc.x - sb.x, sc.y - sb.y) +
                                  std::hypot(sd.x - sc.x, sd.y - sc.y);
    const double chord = std::hypot(sd.x - sa.x, sd.y - sa.y);
    if (distance_from_chord(sb, sa, sd) <= 0.25 &&
        distance_from_chord(sc, sa, sd) <= 0.25 &&
        polygon_length - chord <= 0.25) {
      return append(contour, d);
    }
    if (depth >= 24)
      return false; // Never silently use an inaccurate LOD.
    const auto ab = (a + b) * 0.5, bc = (b + c) * 0.5, cd = (c + d) * 0.5;
    const auto abc = (ab + bc) * 0.5, bcd = (bc + cd) * 0.5,
               mid = (abc + bcd) * 0.5;
    return cubic(contour, a, ab, abc, mid, depth + 1) &&
           cubic(contour, mid, bcd, cd, d, depth + 1);
  }
};
} // namespace

bool flatten(const document::PathObject &object, const Camera2D &camera,
             std::vector<Contour> &contours) {
  if (!object.transform.is_finite() || !std::isfinite(object.stroke_width_mm) ||
      object.stroke_width_mm < 0 || object.path.size() > kMaxPoints)
    return false;
  Flattener builder{object, camera};
  Point current{}, start{};
  bool active = false;
  for (const auto &element : object.path.elements()) {
    if (element.verb == geometry::PathVerb::MoveTo) {
      contours.push_back({});
      if (!builder.append(contours.back(), element.pt))
        return false;
      current = start = element.pt;
      active = true;
      continue;
    }
    if (!active)
      return false;
    auto &contour = contours.back();
    switch (element.verb) {
    case geometry::PathVerb::LineTo:
      if (!builder.append(contour, element.pt))
        return false;
      break;
    case geometry::PathVerb::QuadTo:
      if (!builder.cubic(contour, current,
                         current + (element.ctrl1 - current) * (2.0 / 3.0),
                         element.pt +
                             (element.ctrl1 - element.pt) * (2.0 / 3.0),
                         element.pt))
        return false;
      break;
    case geometry::PathVerb::CubicTo:
      if (!builder.cubic(contour, current, element.ctrl1, element.ctrl2,
                         element.pt))
        return false;
      break;
    case geometry::PathVerb::Close:
      contour.closed = true;
      current = start;
      active = false;
      continue;
    default:
      return false; // Arc conversion requires its own verified implementation.
    }
    current = element.pt;
  }
  return true;
}
} // namespace pikdraw::rendering::raster
