#pragma once

#include "pikdraw/color/values/color_value.hpp"
#include "pikdraw/document/model/object_id.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
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
inline constexpr uint32_t kExtendedEffectNodeSchemaMinor = 2;
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

struct FeatherEffect {
  double radius_mm{0.5};
};

struct OuterGlowEffect {
  double radius_mm{1.0};
  color::ColorValue color{color::RGBColor{1.0, 0.9, 0.2, 1.0}};
  double opacity{0.75};
};

struct InnerGlowEffect {
  double radius_mm{1.0};
  color::ColorValue color{color::RGBColor{1.0, 1.0, 1.0, 1.0}};
  double opacity{0.75};
};

struct InnerShadowEffect {
  double dx_mm{2.0};
  double dy_mm{-2.0};
  double sigma_x_mm{1.0};
  double sigma_y_mm{1.0};
  color::ColorValue color{color::RGBColor{0.0, 0.0, 0.0, 1.0}};
  double opacity{0.5};
  BlendMode blend_mode{BlendMode::Normal};
};

struct ColorOverlayEffect {
  color::ColorValue color{color::RGBColor{1.0, 0.0, 0.0, 1.0}};
  double opacity{1.0};
  BlendMode blend_mode{BlendMode::Normal};
};

using EffectParameters = std::variant<
    GaussianBlurEffect,
    DropShadowEffect,
    FeatherEffect,
    OuterGlowEffect,
    InnerGlowEffect,
    InnerShadowEffect,
    ColorOverlayEffect>;

[[nodiscard]] constexpr uint32_t effect_node_schema_minor(
    const EffectParameters &parameters) noexcept {
  return std::visit([](const auto &effect) constexpr -> uint32_t {
    using T = std::decay_t<decltype(effect)>;
    if constexpr (std::is_same_v<T, DropShadowEffect>)
      return effect.blend_mode == BlendMode::Normal
                 ? kBasicEffectNodeSchemaMinor
                 : kDropShadowBlendSchemaMinor;
    else if constexpr (std::is_same_v<T, FeatherEffect> ||
                       std::is_same_v<T, OuterGlowEffect> ||
                       std::is_same_v<T, InnerGlowEffect> ||
                       std::is_same_v<T, InnerShadowEffect> ||
                       std::is_same_v<T, ColorOverlayEffect>)
      return kExtendedEffectNodeSchemaMinor;
    return kBasicEffectNodeSchemaMinor;
  }, parameters);
}

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
  return std::visit([](const auto &effect) -> std::string_view {
    using T = std::decay_t<decltype(effect)>;
    if constexpr (std::is_same_v<T, GaussianBlurEffect>)
      return "pikdraw.effect.gaussian-blur";
    else if constexpr (std::is_same_v<T, DropShadowEffect>)
      return "pikdraw.effect.drop-shadow";
    else if constexpr (std::is_same_v<T, FeatherEffect>)
      return "pikdraw.effect.feather";
    else if constexpr (std::is_same_v<T, OuterGlowEffect>)
      return "pikdraw.effect.outer-glow";
    else if constexpr (std::is_same_v<T, InnerGlowEffect>)
      return "pikdraw.effect.inner-glow";
    else if constexpr (std::is_same_v<T, InnerShadowEffect>)
      return "pikdraw.effect.inner-shadow";
    else if constexpr (std::is_same_v<T, ColorOverlayEffect>)
      return "pikdraw.effect.color-overlay";
    return "";
  }, parameters);
}

} // namespace pikdraw::document
