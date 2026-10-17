#include "blend_operator.hpp"

#include <algorithm>
#include <cmath>

namespace pikdraw::rendering::effects {
namespace {

[[nodiscard]] bool is_supported_mode(document::BlendMode mode) noexcept {
  switch (mode) {
  case document::BlendMode::Normal:
  case document::BlendMode::Multiply:
  case document::BlendMode::Screen:
  case document::BlendMode::Overlay:
  case document::BlendMode::Darken:
  case document::BlendMode::Lighten:
    return true;
  }
  return false;
}

[[nodiscard]] double blend_component(document::BlendMode mode,
                                     double backdrop,
                                     double source) noexcept {
  switch (mode) {
  case document::BlendMode::Normal:
    return source;
  case document::BlendMode::Multiply:
    return backdrop * source;
  case document::BlendMode::Screen:
    return backdrop + source - backdrop * source;
  case document::BlendMode::Overlay:
    return backdrop <= 0.5
               ? 2.0 * backdrop * source
               : 1.0 - 2.0 * (1.0 - backdrop) * (1.0 - source);
  case document::BlendMode::Darken:
    return std::min(backdrop, source);
  case document::BlendMode::Lighten:
    return std::max(backdrop, source);
  }
  return 0.0;
}

[[nodiscard]] double unpremultiply(float component, float alpha) noexcept {
  return alpha > 0.0f ? static_cast<double>(component) / alpha : 0.0;
}

[[nodiscard]] float composite_component(document::BlendMode mode,
                                        float backdrop_premultiplied,
                                        float backdrop_alpha,
                                        float source_premultiplied,
                                        float source_alpha,
                                        double output_alpha) noexcept {
  const double cb = unpremultiply(backdrop_premultiplied, backdrop_alpha);
  const double cs = unpremultiply(source_premultiplied, source_alpha);
  const double ab = backdrop_alpha;
  const double as = source_alpha;
  const double blended = blend_component(mode, cb, cs);

  // C9 3.2: co = as*(1-ab)*Cs + as*ab*B(Cb,Cs) + (1-as)*ab*Cb.
  const double output = as * (1.0 - ab) * cs + as * ab * blended +
                        (1.0 - as) * ab * cb;
  // The equation is bounded by output_alpha. Clamp only floating-point drift
  // so the returned float continues to satisfy the premultiplied invariant.
  return static_cast<float>(std::clamp(output, 0.0, output_alpha));
}

} // namespace

bool is_valid_premultiplied_srgb(PremultipliedSrgbRgba pixel) noexcept {
  const auto in_unit_interval = [](float value) noexcept {
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
  };

  return in_unit_interval(pixel.a) && in_unit_interval(pixel.r) &&
         in_unit_interval(pixel.g) && in_unit_interval(pixel.b) &&
         pixel.r <= pixel.a && pixel.g <= pixel.a && pixel.b <= pixel.a;
}

std::optional<PremultipliedSrgbRgba> blend_source_over(
    document::BlendMode mode, PremultipliedSrgbRgba backdrop,
    PremultipliedSrgbRgba source) noexcept {
  if (!is_supported_mode(mode) || !is_valid_premultiplied_srgb(backdrop) ||
      !is_valid_premultiplied_srgb(source)) {
    return std::nullopt;
  }

  // Keep the alpha operation and rounding order identical to
  // PixelBufferTarget::blend_premultiplied_pixel. C9 alpha does not depend on
  // the selected color blend function, including at RGBA8 half-rounding edges.
  const float remaining_alpha = 1.0f - source.a;
  const float alpha = source.a + backdrop.a * remaining_alpha;
  const double output_alpha = alpha;
  PremultipliedSrgbRgba output;
  output.r = composite_component(mode, backdrop.r, backdrop.a, source.r,
                                 source.a, output_alpha);
  output.g = composite_component(mode, backdrop.g, backdrop.a, source.g,
                                 source.a, output_alpha);
  output.b = composite_component(mode, backdrop.b, backdrop.a, source.b,
                                 source.a, output_alpha);
  output.a = alpha;

  if (!is_valid_premultiplied_srgb(output))
    return std::nullopt;
  return output;
}

} // namespace pikdraw::rendering::effects
