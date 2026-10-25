#include "object_compositor_internal.hpp"

#include "blend_operator.hpp"
#include "drop_shadow.hpp"
#include "gaussian_blur.hpp"
#include "gaussian_blur_simd.hpp"
#include "../raster/path_raster.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace pikdraw::rendering::effects {
namespace {
bool expand_rect(geometry::Rect2D &rect, double x_margin, double y_margin) {
  if (!finite_rect(rect) || !std::isfinite(x_margin) ||
      !std::isfinite(y_margin) || x_margin < 0.0 || y_margin < 0.0)
    return false;
  const double left = rect.x - x_margin;
  const double top = rect.y - y_margin;
  const double right = rect.right() + x_margin;
  const double bottom = rect.bottom() + y_margin;
  const double width = right - left;
  const double height = bottom - top;
  if (!std::isfinite(left) || !std::isfinite(top) ||
      !std::isfinite(width) || !std::isfinite(height) || width <= 0.0 ||
      height <= 0.0)
    return false;
  rect = {left, top, width, height};
  return finite_nonempty_rect(rect);
}

bool translate_rect(geometry::Rect2D &rect, double dx, double dy) {
  if (!finite_nonempty_rect(rect) || !std::isfinite(dx) || !std::isfinite(dy))
    return false;
  rect.x += dx;
  rect.y += dy;
  return finite_nonempty_rect(rect);
}

bool union_rects(const geometry::Rect2D &first,
                 const geometry::Rect2D &second,
                 geometry::Rect2D &result) {
  if (!finite_nonempty_rect(first) || !finite_nonempty_rect(second))
    return false;
  const double left = std::min(first.x, second.x);
  const double top = std::min(first.y, second.y);
  const double right = std::max(first.right(), second.right());
  const double bottom = std::max(first.bottom(), second.bottom());
  result = {left, top, right - left, bottom - top};
  return finite_nonempty_rect(result);
}

bool gaussian_support(double sigma_mm, double density, double &radius_mm,
                      uint64_t &taps) {
  if (!std::isfinite(sigma_mm) || sigma_mm < 0.0 || !std::isfinite(density) ||
      density <= 0.0)
    return false;
  const double sigma_px = sigma_mm * density;
  if (!std::isfinite(sigma_px) ||
      sigma_px > GaussianBlurPolicy::max_sigma_px)
    return false;
  if (sigma_px == 0.0) {
    radius_mm = 0.0;
    taps = 1;
    return true;
  }
  const double radius_px = std::ceil(4.0 * sigma_px);
  if (!std::isfinite(radius_px) || radius_px < 0.0 ||
      radius_px > static_cast<double>(
                      (GaussianBlurPolicy::max_kernel_taps - 1) / 2))
    return false;
  radius_mm = radius_px / density;
  taps = static_cast<uint64_t>(radius_px) * 2 + 1;
  return std::isfinite(radius_mm);
}

} // namespace

BlendPixel read_target_pixel(const PixelBufferTarget &target, int32_t x,
                             int32_t y) noexcept {
  if (target.premultiplied_rgba) {
    const size_t stride =
        static_cast<size_t>(target.stride_bytes / sizeof(float));
    const auto *pixel = target.premultiplied_rgba +
                        static_cast<size_t>(y) * stride +
                        static_cast<size_t>(x) * 4;
    return {pixel[0], pixel[1], pixel[2], pixel[3]};
  }

  const auto *pixel = target.byte_pixel(x, y);
  const float alpha = static_cast<float>(pixel[3]) / 255.0f;
  return {static_cast<float>(pixel[target.channel_index(0)]) / 255.0f * alpha,
          static_cast<float>(pixel[1]) / 255.0f * alpha,
          static_cast<float>(pixel[target.channel_index(2)]) / 255.0f * alpha, alpha};
}

void write_target_pixel(PixelBufferTarget &target, int32_t x, int32_t y,
                        BlendPixel pixel) noexcept {
  if (target.premultiplied_rgba) {
    const size_t stride =
        static_cast<size_t>(target.stride_bytes / sizeof(float));
    auto *destination = target.premultiplied_rgba +
                        static_cast<size_t>(y) * stride +
                        static_cast<size_t>(x) * 4;
    destination[0] = pixel.r;
    destination[1] = pixel.g;
    destination[2] = pixel.b;
    destination[3] = pixel.a;
    return;
  }

  auto *destination = target.byte_pixel(x, y);
  const float inverse_alpha = pixel.a > 0.0f ? 1.0f / pixel.a : 0.0f;
  const float straight[3]{pixel.r * inverse_alpha, pixel.g * inverse_alpha,
                          pixel.b * inverse_alpha};
  for (size_t channel = 0; channel < 3; ++channel) {
    destination[target.channel_index(channel)] = static_cast<uint8_t>(std::lround(
        std::clamp(straight[channel], 0.0f, 1.0f) * 255.0f));
  }
  destination[3] = static_cast<uint8_t>(
      std::lround(std::clamp(pixel.a, 0.0f, 1.0f) * 255.0f));
}

color::ColorValue scale_alpha(const color::ColorValue &source,
                              double opacity) {
  auto result = std::visit(
      [opacity](auto value) -> color::ColorValue {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, color::RGBColor> ||
                      std::is_same_v<T, color::CMYKColor> ||
                      std::is_same_v<T, color::GrayscaleColor>) {
          value.a *= opacity;
        } else {
          value.alternate_rgb.a *= opacity;
        }
        return color::ColorValue{std::move(value)};
      },
      source.payload());
  result.set_overprint(source.overprint());
  return result;
}

bool valid_effect_node(const document::EffectNode &node) {
  if (node.id == document::kInvalidEffectInstanceId ||
      node.schema_version != document::kBasicEffectNodeSchemaVersion ||
      node.schema_minor > document::kExtendedEffectNodeSchemaMinor)
    return false;
  return std::visit(
      [&node](const auto &effect) {
        using T = std::decay_t<decltype(effect)>;
        if constexpr (std::is_same_v<T, document::GaussianBlurEffect>) {
          return node.schema_minor == document::kBasicEffectNodeSchemaMinor &&
                 std::isfinite(effect.sigma_x_mm) &&
                 std::isfinite(effect.sigma_y_mm) && effect.sigma_x_mm >= 0.0 &&
                 effect.sigma_y_mm >= 0.0 &&
                 effect.sigma_x_mm <= geometry::limits::kCanvasMaxMm &&
                 effect.sigma_y_mm <= geometry::limits::kCanvasMaxMm;
        } else if constexpr (std::is_same_v<T, document::DropShadowEffect>) {
          return std::isfinite(effect.dx_mm) &&
                 std::isfinite(effect.dy_mm) &&
                 std::abs(effect.dx_mm) <= geometry::limits::kCanvasMaxMm &&
                 std::abs(effect.dy_mm) <= geometry::limits::kCanvasMaxMm &&
                 std::isfinite(effect.sigma_x_mm) &&
                 std::isfinite(effect.sigma_y_mm) && effect.sigma_x_mm >= 0.0 &&
                 effect.sigma_y_mm >= 0.0 &&
                 effect.sigma_x_mm <= geometry::limits::kCanvasMaxMm &&
                 effect.sigma_y_mm <= geometry::limits::kCanvasMaxMm &&
                 effect.color.is_valid() && unit_interval(effect.opacity) &&
                 blend_source_over(effect.blend_mode, {}, {}).has_value() &&
                 (node.schema_minor >= document::kDropShadowBlendSchemaMinor ||
                  effect.blend_mode == document::BlendMode::Normal);
        } else if constexpr (std::is_same_v<T, document::FeatherEffect>) {
          return std::isfinite(effect.radius_mm) && effect.radius_mm >= 0.0 &&
                 effect.radius_mm <= geometry::limits::kCanvasMaxMm;
        } else if constexpr (std::is_same_v<T, document::OuterGlowEffect>) {
          return std::isfinite(effect.radius_mm) && effect.radius_mm >= 0.0 &&
                 effect.radius_mm <= geometry::limits::kCanvasMaxMm &&
                 effect.color.is_valid() && unit_interval(effect.opacity);
        } else if constexpr (std::is_same_v<T, document::InnerGlowEffect>) {
          return std::isfinite(effect.radius_mm) && effect.radius_mm >= 0.0 &&
                 effect.radius_mm <= geometry::limits::kCanvasMaxMm &&
                 effect.color.is_valid() && unit_interval(effect.opacity);
        } else if constexpr (std::is_same_v<T, document::InnerShadowEffect>) {
          return std::isfinite(effect.dx_mm) &&
                 std::isfinite(effect.dy_mm) &&
                 std::abs(effect.dx_mm) <= geometry::limits::kCanvasMaxMm &&
                 std::abs(effect.dy_mm) <= geometry::limits::kCanvasMaxMm &&
                 std::isfinite(effect.sigma_x_mm) &&
                 std::isfinite(effect.sigma_y_mm) && effect.sigma_x_mm >= 0.0 &&
                 effect.sigma_y_mm >= 0.0 &&
                 effect.sigma_x_mm <= geometry::limits::kCanvasMaxMm &&
                 effect.sigma_y_mm <= geometry::limits::kCanvasMaxMm &&
                 effect.color.is_valid() && unit_interval(effect.opacity) &&
                 blend_source_over(effect.blend_mode, {}, {}).has_value();
        } else if constexpr (std::is_same_v<T, document::ColorOverlayEffect>) {
          return effect.color.is_valid() && unit_interval(effect.opacity) &&
                 blend_source_over(effect.blend_mode, {}, {}).has_value();
        }
        return false;
      },
      node.parameters);
}

bool valid_appearance(const document::Appearance &appearance) {
  if (appearance.schema_version != document::kAppearanceSchemaVersion ||
      appearance.schema_minor != document::kAppearanceSchemaMinor ||
      !unit_interval(appearance.object_opacity) ||
      !unit_interval(appearance.fill_opacity) ||
      !unit_interval(appearance.stroke_opacity) ||
      appearance.effects.size() > document::kMaxEffectsPerObject ||
      !blend_source_over(appearance.blend_mode, {}, {}))
    return false;

  for (size_t index = 0; index < appearance.effects.size(); ++index) {
    const auto &node = appearance.effects[index];
    if (!valid_effect_node(node))
      return false;
    if (node.enabled) {
      if (const auto *shadow =
              std::get_if<document::DropShadowEffect>(&node.parameters);
          shadow && shadow->blend_mode != document::BlendMode::Normal) {
        for (size_t later = index + 1; later < appearance.effects.size(); ++later) {
          if (appearance.effects[later].enabled)
            return false;
        }
      }
    }
    for (size_t previous = 0; previous < index; ++previous) {
      if (appearance.effects[previous].id == node.id)
        return false;
    }
  }
  return true;
}

/// Largest singular value of the object's local-to-world linear transform.
/// Sampling at this density avoids undersampling any direction before the
/// transformed local surface is mapped into the viewport.
bool max_linear_scale(const geometry::Transform2D &transform, double &scale) {
  if (!transform.is_finite() || !transform.is_invertible())
    return false;
  const double trace = transform.a * transform.a + transform.b * transform.b +
                       transform.c * transform.c + transform.d * transform.d;
  const double determinant = transform.determinant();
  double discriminant = trace * trace - 4.0 * determinant * determinant;
  if (!std::isfinite(trace) || !std::isfinite(determinant) ||
      !std::isfinite(discriminant))
    return false;
  discriminant = std::max(0.0, discriminant);
  const double eigenvalue = (trace + std::sqrt(discriminant)) * 0.5;
  scale = std::sqrt(eigenvalue);
  return std::isfinite(scale) && scale > 0.0;
}

bool effect_output_bounds(const document::PathObject &path,
                          const document::Appearance &appearance,
                          double density, geometry::Rect2D &bounds,
                          uint64_t &work_per_pixel) {
  bounds = path.path.bounds();
  if (!finite_rect(bounds) || !std::isfinite(path.stroke_width_mm) ||
      path.stroke_width_mm < 0.0)
    return false;
  if (path.has_stroke && path.stroke_width_mm > 0.0 &&
      !expand_rect(bounds, path.stroke_width_mm * 0.5,
                   path.stroke_width_mm * 0.5))
    return false;

  work_per_pixel = 0;
  for (const auto &node : appearance.effects) {
    if (!node.enabled)
      continue;
    const bool applied = std::visit(
        [&](const auto &effect) {
          using T = std::decay_t<decltype(effect)>;
          double radius_x = 0.0;
          double radius_y = 0.0;
          uint64_t taps_x = 0;
          uint64_t taps_y = 0;
          if constexpr (std::is_same_v<T, document::GaussianBlurEffect>) {
            if (!gaussian_support(effect.sigma_x_mm, density, radius_x, taps_x) ||
                !gaussian_support(effect.sigma_y_mm, density, radius_y, taps_y))
              return false;
            if (!expand_rect(bounds, radius_x, radius_y))
              return false;
            work_per_pixel += taps_x + taps_y;
            return work_per_pixel <= kMaxEffectJobWorkUnits;
          } else if constexpr (std::is_same_v<T, document::DropShadowEffect>) {
            if (!gaussian_support(effect.sigma_x_mm, density, radius_x, taps_x) ||
                !gaussian_support(effect.sigma_y_mm, density, radius_y, taps_y))
              return false;
            auto shadow = bounds;
            if (!expand_rect(shadow, radius_x, radius_y) ||
                !translate_rect(shadow, effect.dx_mm, effect.dy_mm))
              return false;
            geometry::Rect2D combined;
            if (!union_rects(bounds, shadow, combined))
              return false;
            bounds = combined;
            work_per_pixel += taps_x + taps_y + 5;
            return work_per_pixel <= kMaxEffectJobWorkUnits;
          } else if constexpr (std::is_same_v<T, document::FeatherEffect>) {
            // Feather blurs the alpha mask with sigma = radius / 2. Keep the
            // source surface padded by the full kernel support so the edge
            // result does not depend on the allocation boundary, and account
            // for the actual separable taps before allocating the surface.
            if (!gaussian_support(effect.radius_mm * 0.5, density,
                                  radius_x, taps_x) ||
                !gaussian_support(effect.radius_mm * 0.5, density,
                                  radius_y, taps_y) ||
                !expand_rect(bounds, radius_x, radius_y))
              return false;
            work_per_pixel += taps_x + taps_y;
            return work_per_pixel <= kMaxEffectJobWorkUnits;
          } else if constexpr (std::is_same_v<T, document::OuterGlowEffect>) {
            if (!gaussian_support(effect.radius_mm, density, radius_x, taps_x))
              return false;
            if (!expand_rect(bounds, radius_x, radius_x))
              return false;
            work_per_pixel += taps_x * 2 + 5;
            return work_per_pixel <= kMaxEffectJobWorkUnits;
          } else if constexpr (std::is_same_v<T, document::InnerGlowEffect>) {
            work_per_pixel += 15;
            return work_per_pixel <= kMaxEffectJobWorkUnits;
          } else if constexpr (std::is_same_v<T, document::InnerShadowEffect>) {
            work_per_pixel += 20;
            return work_per_pixel <= kMaxEffectJobWorkUnits;
          } else if constexpr (std::is_same_v<T, document::ColorOverlayEffect>) {
            work_per_pixel += 2;
            return work_per_pixel <= kMaxEffectJobWorkUnits;
          }
        },
        node.parameters);
    if (!applied)
      return false;
  }
  return finite_rect(bounds);
}

bool make_local_surface(const geometry::Rect2D &output_bounds, double density,
                        LocalSurface &surface) {
  if (!finite_nonempty_rect(output_bounds) || !std::isfinite(density) ||
      density <= 0.0)
    return false;
  const double margin = 1.0 / density;
  const double requested_width = output_bounds.width + 2.0 * margin;
  const double requested_height = output_bounds.height + 2.0 * margin;
  const double width_px = std::ceil(requested_width * density);
  const double height_px = std::ceil(requested_height * density);
  if (!std::isfinite(margin) || !std::isfinite(width_px) ||
      !std::isfinite(height_px) || width_px < 1.0 || height_px < 1.0 ||
      width_px > static_cast<double>(GaussianBlurPolicy::max_dimension) ||
      height_px > static_cast<double>(GaussianBlurPolicy::max_dimension))
    return false;
  const auto width = static_cast<uint64_t>(width_px);
  const auto height = static_cast<uint64_t>(height_px);
  if (width > std::numeric_limits<uint64_t>::max() / height)
    return false;
  const uint64_t pixels = width * height;
  if (pixels > kMaxEffectSurfacePixels ||
      pixels > std::numeric_limits<size_t>::max() / sizeof(FilterPixel) ||
      pixels > kMaxEffectJobScratchBytes / kEffectScratchBytesPerPixel)
    return false;

  const double surface_width = width_px / density;
  const double surface_height = height_px / density;
  const double left = output_bounds.x - margin;
  const double bottom = output_bounds.y - margin;
  if (!std::isfinite(surface_width) || !std::isfinite(surface_height) ||
      !std::isfinite(left) || !std::isfinite(bottom))
    return false;
  surface = {{left, bottom, surface_width, surface_height},
             static_cast<int32_t>(width), static_cast<int32_t>(height), density};
  return finite_nonempty_rect(surface.bounds);
}

bool screen_bounds(const geometry::Rect2D &world_bounds,
                   const Camera2D &camera, const PixelBufferTarget &target,
                   int32_t &left, int32_t &top, int32_t &width,
                   int32_t &height) {
  left = top = width = height = 0;
  if (!finite_nonempty_rect(world_bounds))
    return false;
  const std::array<geometry::Point2D, 4> corners{{
      {world_bounds.left(), world_bounds.top()},
      {world_bounds.right(), world_bounds.top()},
      {world_bounds.left(), world_bounds.bottom()},
      {world_bounds.right(), world_bounds.bottom()}}};
  double min_x = std::numeric_limits<double>::infinity();
  double min_y = std::numeric_limits<double>::infinity();
  double max_x = -std::numeric_limits<double>::infinity();
  double max_y = -std::numeric_limits<double>::infinity();
  for (const auto corner : corners) {
    const auto point = camera.world_to_screen(corner);
    if (!std::isfinite(point.x) || !std::isfinite(point.y))
      return false;
    min_x = std::min(min_x, static_cast<double>(point.x));
    min_y = std::min(min_y, static_cast<double>(point.y));
    max_x = std::max(max_x, static_cast<double>(point.x));
    max_y = std::max(max_y, static_cast<double>(point.y));
  }
  if (max_x <= -1.0 || max_y <= -1.0 || min_x >= target.width + 1.0 ||
      min_y >= target.height + 1.0)
    return true;
  const double clipped_left = std::floor(std::max(0.0, min_x - 1.0));
  const double clipped_top = std::floor(std::max(0.0, min_y - 1.0));
  const double clipped_right = std::ceil(std::min(
      static_cast<double>(target.width), max_x + 1.0));
  const double clipped_bottom = std::ceil(std::min(
      static_cast<double>(target.height), max_y + 1.0));
  if (clipped_right <= clipped_left || clipped_bottom <= clipped_top)
    return true;
  left = static_cast<int32_t>(clipped_left);
  top = static_cast<int32_t>(clipped_top);
  width = static_cast<int32_t>(clipped_right - clipped_left);
  height = static_cast<int32_t>(clipped_bottom - clipped_top);
  return true;
}

bool encoded_to_linear(const std::vector<float> &source,
                       std::vector<FilterPixel> &destination) {
  if (source.size() % 4 != 0)
    return false;
  destination.resize(source.size() / 4);
  for (size_t index = 0; index < destination.size(); ++index) {
    const float alpha = source[index * 4 + 3];
    const BlendPixel encoded{source[index * 4], source[index * 4 + 1],
                             source[index * 4 + 2], alpha};
    if (!is_valid_premultiplied_srgb(encoded))
      return false;
    if (alpha <= 0.0f) {
      destination[index] = {};
      continue;
    }
    destination[index] = {
        srgb_to_linear(encoded.r / alpha) * alpha,
        srgb_to_linear(encoded.g / alpha) * alpha,
        srgb_to_linear(encoded.b / alpha) * alpha, alpha};
  }
  return true;
}

bool linear_to_encoded(const FilterPixel &source, BlendPixel &destination) {
  if (!std::isfinite(source.r) || !std::isfinite(source.g) ||
      !std::isfinite(source.b) || !unit_interval(source.a) || source.r < 0.0f ||
      source.g < 0.0f || source.b < 0.0f || source.r > source.a + 1e-5f ||
      source.g > source.a + 1e-5f || source.b > source.a + 1e-5f)
    return false;
  if (source.a <= 0.0f) {
    destination = {};
    return true;
  }
  destination = {linear_to_srgb(std::clamp(source.r / source.a, 0.0f, 1.0f)) *
                     source.a,
                 linear_to_srgb(std::clamp(source.g / source.a, 0.0f, 1.0f)) *
                     source.a,
                 linear_to_srgb(std::clamp(source.b / source.a, 0.0f, 1.0f)) *
                     source.a,
                 source.a};
  return is_valid_premultiplied_srgb(destination);
}

FilterPixel sample_linear_surface(std::span<const FilterPixel> pixels,
                                  int32_t width, int32_t height, double x,
                                  double y) noexcept {
  if (!std::isfinite(x) || !std::isfinite(y) || x < -1.0 || y < -1.0 ||
      x >= width || y >= height)
    return {};
  const double floor_x = std::floor(x);
  const double floor_y = std::floor(y);
  const auto x0 = static_cast<int64_t>(floor_x);
  const auto y0 = static_cast<int64_t>(floor_y);
  const double fx = x - floor_x;
  const double fy = y - floor_y;
  FilterPixel result{};
  const auto add = [&](int64_t sx, int64_t sy, double weight) {
    if (weight == 0.0 || sx < 0 || sy < 0 || sx >= width || sy >= height)
      return;
    const auto &pixel = pixels[static_cast<size_t>(sy) *
                                   static_cast<size_t>(width) +
                               static_cast<size_t>(sx)];
    const float amount = static_cast<float>(weight);
    result.r += pixel.r * amount;
    result.g += pixel.g * amount;
    result.b += pixel.b * amount;
    result.a += pixel.a * amount;
  };
  add(x0, y0, (1.0 - fx) * (1.0 - fy));
  add(x0 + 1, y0, fx * (1.0 - fy));
  add(x0, y0 + 1, (1.0 - fx) * fy);
  add(x0 + 1, y0 + 1, fx * fy);
  return result;
}

bool is_default_appearance(const document::Appearance &appearance) {
  return appearance.object_opacity == 1.0 &&
         appearance.fill_opacity == 1.0 &&
         appearance.stroke_opacity == 1.0 &&
         appearance.blend_mode == document::BlendMode::Normal &&
         std::none_of(appearance.effects.begin(), appearance.effects.end(),
                      [](const auto &node) { return node.enabled; });
}

bool screen_bounds(const document::PathObject &path, const Camera2D &camera,
                   const PixelBufferTarget &target, int32_t &left,
                   int32_t &top, int32_t &width, int32_t &height) {
  auto bounds = path.bounds();
  if (!bounds.is_finite())
    return false;
  if (path.has_stroke) {
    if (!std::isfinite(path.stroke_width_mm) || path.stroke_width_mm < 0.0)
      return false;
    const auto &transform = path.transform;
    const double scale = std::sqrt(transform.a * transform.a +
                                    transform.b * transform.b +
                                    transform.c * transform.c +
                                    transform.d * transform.d);
    const double expansion = path.stroke_width_mm * 0.5 * scale;
    if (!std::isfinite(expansion))
      return false;
    bounds.x -= expansion;
    bounds.y -= expansion;
    bounds.width += 2.0 * expansion;
    bounds.height += 2.0 * expansion;
  }
  // A centerline can have zero width or height before its stroke is applied.
  // Only treat it as empty after expanding the stroke bounds.
  if (!bounds.is_finite() || !std::isfinite(bounds.right()) ||
      !std::isfinite(bounds.bottom()))
    return false;
  if (bounds.is_empty())
    return true;

  const std::array<geometry::Point2D, 4> corners{{
      {bounds.left(), bounds.top()}, {bounds.right(), bounds.top()},
      {bounds.left(), bounds.bottom()}, {bounds.right(), bounds.bottom()}}};
  double min_x = std::numeric_limits<double>::infinity();
  double min_y = std::numeric_limits<double>::infinity();
  double max_x = -std::numeric_limits<double>::infinity();
  double max_y = -std::numeric_limits<double>::infinity();
  for (const auto corner : corners) {
    const auto screen = camera.world_to_screen(corner);
    if (!std::isfinite(screen.x) || !std::isfinite(screen.y))
      return false;
    min_x = std::min(min_x, static_cast<double>(screen.x));
    min_y = std::min(min_y, static_cast<double>(screen.y));
    max_x = std::max(max_x, static_cast<double>(screen.x));
    max_y = std::max(max_y, static_cast<double>(screen.y));
  }
  if (max_x <= 0.0 || max_y <= 0.0 || min_x >= target.width ||
      min_y >= target.height)
    return true;

  const auto pixel_edge = [](double value, int32_t limit) {
    return static_cast<int32_t>(
        std::floor(std::clamp(value, 0.0, static_cast<double>(limit))));
  };
  left = pixel_edge(min_x - 1.0, target.width);
  top = pixel_edge(min_y - 1.0, target.height);
  const auto right = pixel_edge(std::ceil(max_x + 1.0), target.width);
  const auto bottom = pixel_edge(std::ceil(max_y + 1.0), target.height);
  width = right - left;
  height = bottom - top;
  return true;
}

bool has_enabled_effects(const document::Appearance &appearance) {
  return std::any_of(appearance.effects.begin(), appearance.effects.end(),
                     [](const auto &node) { return node.enabled; });
}


} // namespace pikdraw::rendering::effects
