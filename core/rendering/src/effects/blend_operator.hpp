#pragma once

#include "pikdraw/document/effects/effect_stack.hpp"
#include <optional>

namespace pikdraw::rendering::effects {

/// Encoded-sRGB color and alpha in premultiplied form. Every component is in
/// [0, 1], and each color component must be no greater than alpha.
struct PremultipliedSrgbRgba {
  float r{0.0f};
  float g{0.0f};
  float b{0.0f};
  float a{0.0f};
};

/// Returns whether all channels are finite, normalized, and premultiplied.
[[nodiscard]] bool is_valid_premultiplied_srgb(
    PremultipliedSrgbRgba pixel) noexcept;

/// Composites source over backdrop using the v1 C9 blend equation in encoded
/// sRGB. Source opacity, when applicable, must already be applied to both its
/// premultiplied color and alpha. Invalid pixels and unknown modes are rejected.
[[nodiscard]] std::optional<PremultipliedSrgbRgba> blend_source_over(
    document::BlendMode mode, PremultipliedSrgbRgba backdrop,
    PremultipliedSrgbRgba source) noexcept;

} // namespace pikdraw::rendering::effects
