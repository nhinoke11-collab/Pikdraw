#pragma once
#include "pikdraw/rendering/software_rasterizer.hpp"
#include <vector>

namespace pikdraw::rendering::raster {
using Point = geometry::Point2D;
struct Contour {
  std::vector<Point> local;
  std::vector<Point> screen;
  bool closed{false};
};
// Pixel-center rasterization, with bounded transient work and adaptive 0.25px
// curves.
bool flatten(const document::PathObject &, const Camera2D &,
             std::vector<Contour> &);
bool stroke(const std::vector<Contour> &, const document::PathObject &,
            const Camera2D &, color::DeviceRGBA, PixelBufferTarget &);
bool paint(const document::PathObject &, const Camera2D &, PixelBufferTarget &);
Point project(Point, const geometry::Transform2D &, const Camera2D &);
} // namespace pikdraw::rendering::raster
