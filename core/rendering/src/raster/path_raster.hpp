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

/// Keep device conversion lazy: float scratch targets composite the original
/// preview sample, while the established byte target uses its exact RGBA8
/// conversion path.
struct RasterColor {
  color::PreviewRGBA preview;
  color::DeviceRGBA rgba8;
};

inline RasterColor make_raster_color(color::PreviewRGBA value,
                                     const PixelBufferTarget &target) {
  if (target.premultiplied_rgba)
    return {value, {0, 0, 0, 0}};
  return {value, value.to_device()};
}

inline void blend_preview(PixelBufferTarget &target, int x, int y,
                          const RasterColor &value) noexcept {
  if (target.premultiplied_rgba) {
    const float alpha = value.preview.a;
    target.blend_premultiplied_pixel(
        x, y, value.preview.r * alpha, value.preview.g * alpha,
        value.preview.b * alpha, alpha);
  } else {
    target.blend_pixel(x, y, value.rgba8.r, value.rgba8.g, value.rgba8.b,
                       value.rgba8.a);
  }
}

// Pixel-center rasterization, with bounded transient work and adaptive 0.25px
// curves.
bool flatten(const document::PathObject &, const Camera2D &,
             std::vector<Contour> &);
bool stroke(const std::vector<Contour> &, const document::PathObject &,
            const Camera2D &, color::PreviewRGBA, PixelBufferTarget &);
bool paint(const document::PathObject &, const Camera2D &, PixelBufferTarget &,
           const color::IccPreview *preview = nullptr);
Point project(Point, const geometry::Transform2D &, const Camera2D &);
} // namespace pikdraw::rendering::raster
