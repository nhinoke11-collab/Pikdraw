#include "skia_internal.hpp"

namespace pikdraw::rendering::skia {

std::vector<SkiaPathConverter::PathSegment> SkiaPathConverter::convert(
    const geometry::Path2D &path,
    double camera_center_x,
    double camera_center_y) {
  std::vector<PathSegment> segments;
  segments.reserve(path.elements().size());

  for (const auto &el : path.elements()) {
    PathSegment seg;
    seg.verb = el.verb;
    seg.pt = camera_relative_point(el.pt.x, el.pt.y,
                                   camera_center_x, camera_center_y);
    seg.ctrl1 = camera_relative_point(el.ctrl1.x, el.ctrl1.y,
                                      camera_center_x, camera_center_y);
    seg.ctrl2 = camera_relative_point(el.ctrl2.x, el.ctrl2.y,
                                      camera_center_x, camera_center_y);
    segments.push_back(seg);
  }

  return segments;
}

} // namespace pikdraw::rendering::skia
