#pragma once

#include "pikdraw/color/values/color_value.hpp"
#include "pikdraw/document/model/object_id.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace pikdraw::document {

using EffectInstanceId = uint64_t;
inline constexpr EffectInstanceId kInvalidEffectInstanceId = 0;
inline constexpr uint32_t kAppearanceSchemaVersion = 1;
inline constexpr uint32_t kAppearanceSchemaMinor = 0;
inline constexpr uint32_t kBasicEffectNodeSchemaVersion = 1;
inline constexpr uint32_t kBasicEffectNodeSchemaMinor = 0;
inline constexpr uint32_t kDropShadowBlendSchemaMinor = 1;
inline constexpr size_t kMaxEffectsPerObject = 256;
inline constexpr size_t kMaxEffectsPerDocument = 100'000;

/// Blend IDs are stable document semantics, never localized labels or vendor enum values.
enum class BlendMode : uint8_t {
  Normal = 0,
  Multiply = 1,
  Screen = 2,
  Overlay = 3,
  Darken = 4,
  Lighten = 5,
};

struct GaussianBlurEffect {
  double sigma_x_mm{0.0};
  double sigma_y_mm{0.0};
};

struct DropShadowEffect {
  double dx_mm{2.0};
  double dy_mm{-2.0};
  double sigma_x_mm{1.0};
  double sigma_y_mm{1.0};
  color::ColorValue color{color::RGBColor{0.0, 0.0, 0.0, 1.0}};
  double opacity{0.5};
  BlendMode blend_mode{BlendMode::Normal};
};

using EffectParameters = std::variant<GaussianBlurEffect, DropShadowEffect>;

struct EffectNode {
  EffectInstanceId id{kInvalidEffectInstanceId};
  uint32_t schema_version{kBasicEffectNodeSchemaVersion};
  uint32_t schema_minor{kBasicEffectNodeSchemaMinor};
  bool enabled{true};
  EffectParameters parameters{GaussianBlurEffect{}};
};

/// Authored v1 is object-local mm, single-input and ordered. Render caches and pixel halos
/// are derived state and do not belong in this source value.
struct Appearance {
  uint32_t schema_version{kAppearanceSchemaVersion};
  uint32_t schema_minor{kAppearanceSchemaMinor};
  double object_opacity{1.0};
  double fill_opacity{1.0};
  double stroke_opacity{1.0};
  BlendMode blend_mode{BlendMode::Normal};
  std::vector<EffectNode> effects;
};

[[nodiscard]] inline EffectInstanceId generate_effect_instance_id() noexcept {
  // Share the process-wide stable ID allocator with document and path source IDs.
  return generate_object_id();
}

[[nodiscard]] constexpr std::string_view effect_type_id(
    const EffectParameters &parameters) noexcept {
  if (std::holds_alternative<GaussianBlurEffect>(parameters))
    return "pikdraw.effect.gaussian-blur";
  return "pikdraw.effect.drop-shadow";
}

} // namespace pikdraw::document
