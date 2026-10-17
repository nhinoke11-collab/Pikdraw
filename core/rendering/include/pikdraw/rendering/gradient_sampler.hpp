#pragma once

#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/document/model/gradient_paint.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"
#include "pikdraw/rendering/camera.hpp"
#include "pikdraw/rendering/frame_target.hpp"
#include <optional>
#include <vector>

namespace pikdraw::rendering {

namespace raster {
struct RasterColor;
}

/// Prepared stop with straight encoded-sRGB color components and effective alpha.
struct PreparedGradientStop {
  double offset{0.0};
  double midpoint{0.5};
  float r{0.0f};
  float g{0.0f};
  float b{0.0f};
  float a{1.0f}; // Straight alpha: source_alpha * stop.opacity
};

/// CPU software sampler for Linear and Radial gradients according to C9 and Roadmap.
/// Operates in EncodedSRGBStraightAlpha mode, with piecewise-linear midpoint remap,
/// Pad/Repeat/Reflect spread modes, and analytic inverse camera-object-gradient transform.
class GradientSampler {
public:
  /// Prepare a sampler from paint, object transform, camera, preview color profile, and fill opacity.
  /// Prevalidates payload, inverts affine matrices once, and converts stops to straight sRGB.
  [[nodiscard]] static std::optional<GradientSampler> prepare(
      const document::GradientPaint &paint,
      const geometry::Transform2D &object_transform,
      const Camera2D &camera,
      const color::IccPreview *preview = nullptr,
      double fill_opacity = 1.0);

  /// Sample straight sRGB color at pixel center (col + 0.5, row + 0.5).
  [[nodiscard]] color::PreviewRGBA sample_straight(int col, int row) const noexcept;

  /// Sample directly converted to RasterColor for the given target (device RGBA8 or float scratch).
  [[nodiscard]] raster::RasterColor sample_raster_color(
      int col, int row, const PixelBufferTarget &target) const noexcept;

  /// Sample straight color directly by parameter t in [0.0, 1.0] (applies midpoint and stop interpolation).
  [[nodiscard]] color::PreviewRGBA sample_at_t(double t) const noexcept;

  /// Map screen point (x, y) to normalized gradient space q.
  [[nodiscard]] geometry::Point2D screen_to_q(double screen_x, double screen_y) const noexcept;

  /// Map point q in gradient space to scalar parameter t before spread.
  [[nodiscard]] double q_to_t(geometry::Point2D q) const noexcept;

  /// Apply spread mode to scalar parameter t, returning wrapped value in [0.0, 1.0].
  [[nodiscard]] static double apply_spread(double t, document::GradientSpread spread) noexcept;

  [[nodiscard]] document::GradientKind kind() const noexcept { return kind_; }
  [[nodiscard]] document::GradientSpread spread() const noexcept { return spread_; }
  [[nodiscard]] double fill_opacity() const noexcept { return fill_opacity_; }
  [[nodiscard]] const std::vector<PreparedGradientStop> &stops() const noexcept { return stops_; }
  [[nodiscard]] const geometry::Transform2D &screen_to_q_transform() const noexcept { return screen_to_q_; }

private:
  GradientSampler(document::GradientKind kind,
                  document::GradientSpread spread,
                  double fill_opacity,
                  geometry::Transform2D screen_to_q,
                  std::vector<PreparedGradientStop> stops) noexcept
      : kind_(kind),
        spread_(spread),
        fill_opacity_(fill_opacity),
        screen_to_q_(screen_to_q),
        stops_(std::move(stops)) {}

  document::GradientKind kind_{document::GradientKind::Linear};
  document::GradientSpread spread_{document::GradientSpread::Pad};
  double fill_opacity_{1.0};
  geometry::Transform2D screen_to_q_{geometry::Transform2D::identity()};
  std::vector<PreparedGradientStop> stops_;
};

} // namespace pikdraw::rendering
