#include "authoring_path_adapter.hpp"
#include "pikdraw/document/model/path_adapter.hpp"
#include <algorithm>
#include <cmath>

namespace pikdraw::rendering::path_adapter {
namespace {
using Point = geometry::Point2D;
constexpr size_t max_points = 200000;
constexpr double max_screen_coordinate = 1e12;

struct Builder {
  const document::PathObject &object;
  const Camera2D &camera;
  raster::Contour &contour;
  size_t &points;

  Point screen(Point point) const {
    return raster::project(point, object.transform, camera);
  }
  bool append(Point point, bool omit_endpoint = false) {
    if (omit_endpoint)
      return point.is_finite();
    const auto projected = screen(point);
    if (!point.is_finite() || !projected.is_finite() ||
        std::abs(projected.x) > max_screen_coordinate ||
        std::abs(projected.y) > max_screen_coordinate || ++points > max_points)
      return false;
    contour.local.push_back(point);
    contour.screen.push_back(projected);
    return true;
  }
  double distance_from_chord(Point point, Point start, Point end) const {
    const auto a = screen(start), b = screen(end), p = screen(point);
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double length = std::hypot(dx, dy);
    if (length <= 1e-12)
      return std::hypot(p.x - a.x, p.y - a.y);
    return std::abs((p.x - a.x) * dy - (p.y - a.y) * dx) / length;
  }
  bool cubic(Point p0, Point p1, Point p2, Point p3, int depth,
             bool omit_end) {
    const double deviation = std::max(distance_from_chord(p1, p0, p3),
                                      distance_from_chord(p2, p0, p3));
    const auto a = screen(p0), b = screen(p1), c = screen(p2), d = screen(p3);
    const double polygon = std::hypot(b.x - a.x, b.y - a.y) +
                           std::hypot(c.x - b.x, c.y - b.y) +
                           std::hypot(d.x - c.x, d.y - c.y);
    const double chord = std::hypot(d.x - a.x, d.y - a.y);
    if (!a.is_finite() || !b.is_finite() || !c.is_finite() || !d.is_finite())
      return false;
    if (deviation <= 0.25 && polygon - chord <= 0.25)
      return append(p3, omit_end);
    if (depth >= 24)
      return false;
    const auto p01 = (p0 + p1) * 0.5, p12 = (p1 + p2) * 0.5,
               p23 = (p2 + p3) * 0.5;
    const auto p012 = (p01 + p12) * 0.5, p123 = (p12 + p23) * 0.5,
               mid = (p012 + p123) * 0.5;
    return cubic(p0, p01, p012, mid, depth + 1, false) &&
           cubic(mid, p123, p23, p3, depth + 1, omit_end);
  }
  bool arc(const geometry::EllipticArc &curve, double t0, double t1,
           int depth, bool omit_end) {
    const double span = std::abs(curve.sweep_angle_rad * (t1 - t0));
    const auto p0 = curve.point_at(t0), p1 = curve.point_at(t1);
    const auto p25 = curve.point_at(t0 + (t1 - t0) * 0.25);
    const auto p50 = curve.point_at(t0 + (t1 - t0) * 0.5);
    const auto p75 = curve.point_at(t0 + (t1 - t0) * 0.75);
    const double deviation = std::max(
        {distance_from_chord(p25, p0, p1), distance_from_chord(p50, p0, p1),
         distance_from_chord(p75, p0, p1)});
    if (span <= 1.5707963267948966 && deviation <= 0.25)
      return append(p1, omit_end);
    if (depth >= 24)
      return false;
    const double mid = (t0 + t1) * 0.5;
    return arc(curve, t0, mid, depth + 1, false) &&
           arc(curve, mid, t1, depth + 1, omit_end);
  }
};
} // namespace

bool flatten_authoring_path(const document::PathObject &object,
                            const Camera2D &camera,
                            std::vector<raster::Contour> &contours,
                            std::string *out_error) {
  if (out_error)
    out_error->clear();
  const auto fail = [&](const char *message) {
    if (out_error)
      *out_error = message;
    return false;
  };
  if (!object.authoring_source)
    return fail("Path has no authoring source");
  if (const auto error = object.authoring_source->validation_error()) {
    if (out_error)
      *out_error = *error;
    return false;
  }
  if (!document::editable_path_matches_adapter(*object.authoring_source,
                                                object.path))
    return fail("Authoring source and render adapter disagree");
  if (!object.transform.is_finite() || !std::isfinite(camera.zoom()) ||
      camera.zoom() <= 0)
    return fail("Invalid transform or camera zoom");
  size_t total_points = 0;
  for (const auto &subpath : object.authoring_source->subpaths) {
    contours.push_back({});
    auto &contour = contours.back();
    Builder builder{object, camera, contour, total_points};
    if (!builder.append(subpath.nodes.front().anchor))
      return fail("Path exceeds the render point budget");
    for (size_t i = 0; i < subpath.segments.size(); ++i) {
      const auto next = (i + 1) % subpath.nodes.size();
      const auto start = subpath.nodes[i].anchor;
      const auto end = subpath.nodes[next].anchor;
      const auto &segment = subpath.segments[i];
      const bool omit_end = subpath.closed && i + 1 == subpath.segments.size();
      bool ok = false;
      switch (segment.kind) {
      case document::PathSegmentKind::Line:
        ok = builder.append(end, omit_end);
        break;
      case document::PathSegmentKind::QuadraticBezier: {
        const auto c1 = start + (segment.quadratic_control - start) * (2.0 / 3.0);
        const auto c2 = end + (segment.quadratic_control - end) * (2.0 / 3.0);
        ok = builder.cubic(start, c1, c2, end, 0, omit_end);
        break;
      }
      case document::PathSegmentKind::CubicBezier: {
        const auto handles_start = document::editable_path_handles_for(subpath, i);
        const auto handles_end = document::editable_path_handles_for(subpath, next);
        ok = builder.cubic(start, handles_start.second, handles_end.first, end,
                           0, omit_end);
        break;
      }
      case document::PathSegmentKind::EllipticArc:
        ok = builder.arc(segment.arc, 0.0, 1.0, 0, omit_end);
        break;
      }
      if (!ok)
        return fail("Curve cannot meet the bounded 0.25 px render tolerance");
    }
    contour.closed = subpath.closed;
  }
  return true;
}
} // namespace pikdraw::rendering::path_adapter
