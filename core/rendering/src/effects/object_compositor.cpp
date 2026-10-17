#include "object_compositor.hpp"
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
constexpr uint64_t kMaxSourceSurfacePixels = 4ull * 1024ull * 1024ull;
constexpr uint64_t kMaxEffectSurfacePixels = 1024ull * 1024ull;
constexpr uint64_t kMaxEffectJobScratchBytes = 128ull * 1024ull * 1024ull;
constexpr uint64_t kEffectScratchBytesPerPixel = 68;
// A bounded output cache avoids repeating screen/world transforms and filter
// sampling for ordinary effect bounds. Larger outputs keep the two-pass path
// without allocating a second surface-sized buffer.
constexpr uint64_t kMaxCachedEffectOutputPixels = 256ull * 1024ull;
constexpr uint64_t kMaxEffectJobWorkUnits =
    GaussianBlurPolicy::max_work_pixel_taps;

using BlendPixel = PremultipliedSrgbRgba;
using FilterPixel = LinearPremultipliedRgba;

template <class T>
[[nodiscard]] size_t vector_capacity_bytes(const std::vector<T> &buffer) noexcept {
  if (buffer.capacity() > std::numeric_limits<size_t>::max() / sizeof(T))
    return std::numeric_limits<size_t>::max();
  return buffer.capacity() * sizeof(T);
}

[[nodiscard]] size_t saturated_add(size_t left, size_t right) noexcept {
  return right > std::numeric_limits<size_t>::max() - left
             ? std::numeric_limits<size_t>::max()
             : left + right;
}

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

  const auto *pixel = target.pixels +
                      static_cast<size_t>(y) *
                          static_cast<size_t>(target.stride_bytes) +
                      static_cast<size_t>(x) * 4;
  const float alpha = static_cast<float>(pixel[3]) / 255.0f;
  return {static_cast<float>(pixel[0]) / 255.0f * alpha,
          static_cast<float>(pixel[1]) / 255.0f * alpha,
          static_cast<float>(pixel[2]) / 255.0f * alpha, alpha};
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

  auto *destination = target.pixels +
                      static_cast<size_t>(y) *
                          static_cast<size_t>(target.stride_bytes) +
                      static_cast<size_t>(x) * 4;
  const float inverse_alpha = pixel.a > 0.0f ? 1.0f / pixel.a : 0.0f;
  const float straight[3]{pixel.r * inverse_alpha, pixel.g * inverse_alpha,
                          pixel.b * inverse_alpha};
  for (size_t channel = 0; channel < 3; ++channel) {
    destination[channel] = static_cast<uint8_t>(std::lround(
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

bool unit_interval(double value) {
  return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

bool valid_effect_node(const document::EffectNode &node) {
  if (node.id == document::kInvalidEffectInstanceId ||
      node.schema_version != document::kBasicEffectNodeSchemaVersion ||
      node.schema_minor > document::kDropShadowBlendSchemaMinor)
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
        } else {
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
                 (node.schema_minor == document::kDropShadowBlendSchemaMinor ||
                  effect.blend_mode == document::BlendMode::Normal);
        }
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

bool finite_nonempty_rect(const geometry::Rect2D &rect) {
  return rect.is_finite() && rect.width > 0.0 && rect.height > 0.0 &&
         std::isfinite(rect.right()) && std::isfinite(rect.bottom());
}

bool finite_rect(const geometry::Rect2D &rect) {
  return rect.is_finite() && rect.width >= 0.0 && rect.height >= 0.0 &&
         std::isfinite(rect.right()) && std::isfinite(rect.bottom());
}

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
          if (!gaussian_support(effect.sigma_x_mm, density, radius_x, taps_x) ||
              !gaussian_support(effect.sigma_y_mm, density, radius_y, taps_y))
            return false;
          if constexpr (std::is_same_v<T, document::GaussianBlurEffect>) {
            if (!expand_rect(bounds, radius_x, radius_y))
              return false;
            work_per_pixel += taps_x + taps_y;
            return work_per_pixel <= kMaxEffectJobWorkUnits;
          } else {
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
          }
        },
        node.parameters);
    if (!applied)
      return false;
  }
  return finite_rect(bounds);
}

struct LocalSurface final {
  geometry::Rect2D bounds;
  int32_t width{};
  int32_t height{};
  double density{};
};

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

float srgb_to_linear(float value) noexcept {
  return value <= 0.04045f ? value / 12.92f
                           : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float linear_to_srgb(float value) noexcept {
  return value <= 0.0031308f ? value * 12.92f
                             : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
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

bool compose_shadow_under_input(FilterPixel &input,
                                const FilterPixel &shadow) noexcept {
  if (!std::isfinite(input.r) || !std::isfinite(input.g) ||
      !std::isfinite(input.b) || !unit_interval(input.a) ||
      !std::isfinite(shadow.r) || !std::isfinite(shadow.g) ||
      !std::isfinite(shadow.b) || !unit_interval(shadow.a) ||
      input.r < 0.0f || input.g < 0.0f || input.b < 0.0f ||
      shadow.r < 0.0f || shadow.g < 0.0f || shadow.b < 0.0f ||
      input.r > input.a + 1e-5f || input.g > input.a + 1e-5f ||
      input.b > input.a + 1e-5f || shadow.r > shadow.a + 1e-5f ||
      shadow.g > shadow.a + 1e-5f || shadow.b > shadow.a + 1e-5f)
    return false;
  const float remaining = 1.0f - input.a;
  input = {input.r + shadow.r * remaining,
           input.g + shadow.g * remaining,
           input.b + shadow.b * remaining,
           input.a + shadow.a * remaining};
  input.a = std::clamp(input.a, 0.0f, 1.0f);
  input.r = std::clamp(input.r, 0.0f, input.a);
  input.g = std::clamp(input.g, 0.0f, input.a);
  input.b = std::clamp(input.b, 0.0f, input.a);
  return true;
}

bool render_effect_stack(const document::PathObject &path,
                         const document::Appearance &appearance,
                         const Camera2D &camera, PixelBufferTarget &target,
                         const color::IccPreview *preview,
                         size_t *out_peak_gaussian_blur_scratch_capacity_bytes,
                         size_t *out_peak_drop_shadow_scratch_capacity_bytes) {
  if (path.path.elements().empty())
    return true;
  const auto source_bounds = path.path.bounds();
  if (!finite_rect(source_bounds) || !std::isfinite(path.stroke_width_mm) ||
      path.stroke_width_mm < 0.0)
    return false;
  if ((source_bounds.width <= 0.0 || source_bounds.height <= 0.0) &&
      (!path.has_stroke || path.stroke_width_mm <= 0.0))
    return true;

  double transform_scale = 0.0;
  if (!max_linear_scale(path.transform, transform_scale))
    return false;
  const double density = camera.zoom() * transform_scale;
  if (!std::isfinite(density) || density <= 0.0)
    return false;

  geometry::Rect2D output_bounds;
  uint64_t work_per_pixel = 0;
  if (!effect_output_bounds(path, appearance, density, output_bounds,
                            work_per_pixel))
    return false;
  if (!finite_nonempty_rect(output_bounds))
    return true;

  LocalSurface surface;
  if (!make_local_surface(output_bounds, density, surface))
    return false;
  const uint64_t pixel_count = static_cast<uint64_t>(surface.width) *
                               static_cast<uint64_t>(surface.height);
  if (work_per_pixel != 0 &&
      pixel_count > kMaxEffectJobWorkUnits / work_per_pixel)
    return false;

  const auto world_output_bounds = path.transform.map_rect(surface.bounds);
  if (!finite_nonempty_rect(world_output_bounds))
    return false;
  int32_t target_left = 0;
  int32_t target_top = 0;
  int32_t target_width = 0;
  int32_t target_height = 0;
  if (!screen_bounds(world_output_bounds, camera, target, target_left,
                     target_top, target_width, target_height))
    return false;
  if (target_width <= 0 || target_height <= 0)
    return true;

  std::vector<float> source_pixels(static_cast<size_t>(pixel_count) * 4, 0.0f);
  PixelBufferTarget source_target;
  source_target.width = surface.width;
  source_target.height = surface.height;
  source_target.stride_bytes =
      surface.width * 4 * static_cast<int32_t>(sizeof(float));
  source_target.premultiplied_rgba = source_pixels.data();
  if (!source_target.is_valid())
    return false;

  const auto local_center = surface.bounds.center();
  const Camera2D local_camera(local_center, surface.density, surface.width,
                              surface.height);
  auto source_path = path;
  source_path.transform = geometry::Transform2D::identity();
  source_path.fill = scale_alpha(path.fill, appearance.fill_opacity);
  source_path.stroke = scale_alpha(path.stroke, appearance.stroke_opacity);
  if (!raster::paint(source_path, local_camera, source_target, preview))
    return false;

  std::vector<FilterPixel> current;
  if (!encoded_to_linear(source_pixels, current) ||
      current.size() != static_cast<size_t>(pixel_count))
    return false;

  const auto record_gaussian_peak = [&](size_t additional_bytes) {
    if (!out_peak_gaussian_blur_scratch_capacity_bytes)
      return;
    size_t live_bytes = saturated_add(vector_capacity_bytes(source_pixels),
                                      vector_capacity_bytes(current));
    live_bytes = saturated_add(live_bytes, additional_bytes);
    *out_peak_gaussian_blur_scratch_capacity_bytes = std::max(
        *out_peak_gaussian_blur_scratch_capacity_bytes, live_bytes);
  };
  const auto record_drop_shadow_peak = [&](size_t alpha_bytes,
                                           size_t branch_bytes) {
    if (!out_peak_drop_shadow_scratch_capacity_bytes)
      return;
    size_t live_bytes = saturated_add(vector_capacity_bytes(source_pixels),
                                      vector_capacity_bytes(current));
    live_bytes = saturated_add(live_bytes, alpha_bytes);
    live_bytes = saturated_add(live_bytes, branch_bytes);
    *out_peak_drop_shadow_scratch_capacity_bytes = std::max(
        *out_peak_drop_shadow_scratch_capacity_bytes, live_bytes);
  };

  std::vector<FilterPixel> deferred_shadow;
  document::BlendMode deferred_shadow_blend = document::BlendMode::Normal;
  bool has_deferred_shadow = false;
  for (const auto &node : appearance.effects) {
    if (!node.enabled)
      continue;
    const bool applied = std::visit(
        [&](const auto &effect) {
          using T = std::decay_t<decltype(effect)>;
          const double sigma_x_px = effect.sigma_x_mm * surface.density;
          const double sigma_y_px = effect.sigma_y_mm * surface.density;
          if constexpr (std::is_same_v<T, document::GaussianBlurEffect>) {
            std::vector<FilterPixel> output;
            size_t internal_scratch_capacity_bytes = 0;
            if (gaussian_blur_linear_srgb_sse2(
                    current, static_cast<size_t>(surface.width),
                    static_cast<size_t>(surface.height), sigma_x_px,
                    sigma_y_px, output, {},
                    &internal_scratch_capacity_bytes) !=
                GaussianBlurStatus::Success)
              return false;
            record_gaussian_peak(internal_scratch_capacity_bytes);
            record_gaussian_peak(vector_capacity_bytes(output));
            current.swap(output);
            record_gaussian_peak(vector_capacity_bytes(output));
            return true;
          } else {
            std::vector<float> alpha(current.size());
            for (size_t index = 0; index < current.size(); ++index)
              alpha[index] = current[index].a;
            const auto tint = preview ? preview->convert(effect.color)
                                      : color::to_preview_rgba(effect.color);
            if (!unit_interval(tint.r) || !unit_interval(tint.g) ||
                !unit_interval(tint.b) || !unit_interval(tint.a))
              return false;
            DropShadowParameters parameters;
            parameters.offset_x_px = effect.dx_mm * surface.density;
            parameters.offset_y_px = effect.dy_mm * surface.density;
            parameters.sigma_x_px = sigma_x_px;
            parameters.sigma_y_px = sigma_y_px;
            parameters.color_r_linear_srgb = srgb_to_linear(tint.r);
            parameters.color_g_linear_srgb = srgb_to_linear(tint.g);
            parameters.color_b_linear_srgb = srgb_to_linear(tint.b);
            parameters.color_alpha = tint.a;
            parameters.opacity = static_cast<float>(effect.opacity);
            std::vector<FilterPixel> shadow;
            size_t branch_scratch_capacity_bytes = 0;
            const auto shadow_status = drop_shadow_linear_srgb(
                    alpha, static_cast<size_t>(surface.width),
                    static_cast<size_t>(surface.height), parameters,
                    shadow, {}, &branch_scratch_capacity_bytes);
            record_drop_shadow_peak(vector_capacity_bytes(alpha),
                                    branch_scratch_capacity_bytes);
            if (shadow_status != GaussianBlurStatus::Success ||
                shadow.size() != current.size())
              return false;
            if (effect.blend_mode != document::BlendMode::Normal) {
              const auto node_position = std::find_if(
                  appearance.effects.begin(), appearance.effects.end(),
                  [&node](const document::EffectNode &candidate) {
                    return candidate.id == node.id;
                  });
              if (node_position == appearance.effects.end() ||
                  std::any_of(std::next(node_position), appearance.effects.end(),
                      [](const document::EffectNode &candidate) {
                        return candidate.enabled;
                      }))
                return false;
              deferred_shadow = std::move(shadow);
              deferred_shadow_blend = effect.blend_mode;
              has_deferred_shadow = true;
              return true;
            }
            for (size_t index = 0; index < current.size(); ++index) {
              if (!compose_shadow_under_input(current[index], shadow[index]))
                return false;
            }
            return true;
          }
        },
        node.parameters);
    if (!applied)
      return false;
  }

  const auto inverse = path.transform.inverted();
  if (!inverse || !inverse->is_finite())
    return false;
  const auto sample_surface_at = [&](const std::vector<FilterPixel> &pixels,
                                     int32_t x, int32_t y,
                                     BlendPixel &pixel) -> bool {
    const auto world = camera.screen_to_world(
        {static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f});
    const auto local = inverse->map_point(world);
    if (!local.is_finite())
      return false;
    const double sample_x =
        (local.x - surface.bounds.x) * surface.density - 0.5;
    const double sample_y =
        (surface.bounds.bottom() - local.y) * surface.density - 0.5;
    return linear_to_encoded(sample_linear_surface(
        pixels, surface.width, surface.height, sample_x, sample_y), pixel);
  };
  const auto sample_output = [&](int32_t x, int32_t y,
                                 BlendPixel &pixel) -> bool {
    if (!sample_surface_at(current, x, y, pixel)) return false;
    if (has_deferred_shadow) return true;
    const float opacity = static_cast<float>(appearance.object_opacity);
    pixel.r *= opacity;
    pixel.g *= opacity;
    pixel.b *= opacity;
    pixel.a *= opacity;
    return is_valid_premultiplied_srgb(pixel);
  };
  const auto resolve_output_pixel = [&](int32_t x, int32_t y,
                                        BlendPixel &output) -> bool {
    BlendPixel source_pixel;
    if (!sample_output(x, y, source_pixel)) return false;
    BlendPixel shadow_pixel{};
    if (has_deferred_shadow &&
        !sample_surface_at(deferred_shadow, x, y, shadow_pixel))
      return false;
    if (!has_deferred_shadow && source_pixel.a <= 0.0f) {
      output = {};
      return true;
    }
    if (has_deferred_shadow && source_pixel.a <= 0.0f &&
        shadow_pixel.a <= 0.0f) {
      output = {};
      return true;
    }
    const auto backdrop = read_target_pixel(target, x, y);
    if (!is_valid_premultiplied_srgb(backdrop)) return false;
    if (has_deferred_shadow) {
      const auto shadowed = blend_source_over(
          deferred_shadow_blend, backdrop, shadow_pixel);
      if (!shadowed) return false;
      const auto grouped = blend_source_over(
          appearance.blend_mode, *shadowed, source_pixel);
      if (!grouped) return false;
      const float opacity = static_cast<float>(appearance.object_opacity);
      const float remaining = 1.0f - opacity;
      output = {backdrop.r * remaining + grouped->r * opacity,
                backdrop.g * remaining + grouped->g * opacity,
                backdrop.b * remaining + grouped->b * opacity,
                backdrop.a * remaining + grouped->a * opacity};
      return is_valid_premultiplied_srgb(output);
    }
    if (appearance.blend_mode == document::BlendMode::Normal) {
      output = source_pixel;
      return true;
    }
    const auto composed = blend_source_over(
        appearance.blend_mode, backdrop, source_pixel);
    if (!composed) return false;
    output = *composed;
    return true;
  };

  const uint64_t target_pixel_count =
      static_cast<uint64_t>(target_width) *
      static_cast<uint64_t>(target_height);
  const bool cache_output =
      target_pixel_count <= kMaxCachedEffectOutputPixels;
  std::vector<BlendPixel> staged_output;
  if (cache_output)
    staged_output.resize(static_cast<size_t>(target_pixel_count));

  // Validate every destination sample before the first write. The filter
  // helpers and local source surface are private until this loop succeeds.
  // For bounded outputs, stage the exact source or composed pixel so the
  // write pass does not repeat transforms, sampling, or blend evaluation.
  for (int32_t y = target_top; y < target_top + target_height; ++y) {
    for (int32_t x = target_left; x < target_left + target_width; ++x) {
      BlendPixel output_pixel;
      if (!resolve_output_pixel(x, y, output_pixel)) return false;
      if (output_pixel.a <= 0.0f) continue;
      if (cache_output) {
        const size_t output_index =
            static_cast<size_t>(y - target_top) *
                static_cast<size_t>(target_width) +
            static_cast<size_t>(x - target_left);
        staged_output[output_index] = output_pixel;
      }
    }
  }

  for (int32_t y = target_top; y < target_top + target_height; ++y) {
    for (int32_t x = target_left; x < target_left + target_width; ++x) {
      if (cache_output) {
        const size_t output_index =
            static_cast<size_t>(y - target_top) *
                static_cast<size_t>(target_width) +
            static_cast<size_t>(x - target_left);
        const auto pixel = staged_output[output_index];
        if (pixel.a <= 0.0f)
          continue;
        if (!has_deferred_shadow &&
            appearance.blend_mode == document::BlendMode::Normal) {
          target.blend_premultiplied_pixel(x, y, pixel.r, pixel.g, pixel.b,
                                           pixel.a);
        } else {
          write_target_pixel(target, x, y, pixel);
        }
        continue;
      }

      BlendPixel output_pixel;
      if (!resolve_output_pixel(x, y, output_pixel) || output_pixel.a <= 0.0f)
        continue;
      if (!has_deferred_shadow &&
          appearance.blend_mode == document::BlendMode::Normal) {
        target.blend_premultiplied_pixel(
            x, y, output_pixel.r, output_pixel.g, output_pixel.b,
            output_pixel.a);
      } else {
        write_target_pixel(target, x, y, output_pixel);
      }
    }
  }
  return true;
}

} // namespace

bool render_path_with_appearance(const document::PathObject &path,
                                 const document::Appearance &appearance,
                                 const Camera2D &camera,
                                 PixelBufferTarget &target,
                                 const color::IccPreview *preview,
                                 size_t *out_peak_gaussian_blur_scratch_capacity_bytes,
                                 size_t *out_peak_drop_shadow_scratch_capacity_bytes) {
  if (out_peak_gaussian_blur_scratch_capacity_bytes)
    *out_peak_gaussian_blur_scratch_capacity_bytes = 0;
  if (out_peak_drop_shadow_scratch_capacity_bytes)
    *out_peak_drop_shadow_scratch_capacity_bytes = 0;
  if (!target.is_valid() || !valid_appearance(appearance))
    return false;
  if (is_default_appearance(appearance))
    return raster::paint(path, camera, target, preview);
  if (appearance.object_opacity == 0.0)
    return true;
  if (has_enabled_effects(appearance)) {
    try {
      return render_effect_stack(
          path, appearance, camera, target, preview,
          out_peak_gaussian_blur_scratch_capacity_bytes,
          out_peak_drop_shadow_scratch_capacity_bytes);
    } catch (const std::bad_alloc &) {
      return false;
    } catch (const std::length_error &) {
      return false;
    }
  }

  int32_t left = 0;
  int32_t top = 0;
  int32_t width = 0;
  int32_t height = 0;
  if (!screen_bounds(path, camera, target, left, top, width, height))
    return false;
  if (width <= 0 || height <= 0)
    return true;
  const auto pixel_count = static_cast<uint64_t>(width) *
                           static_cast<uint64_t>(height);
  if (pixel_count > kMaxSourceSurfacePixels ||
      pixel_count > std::numeric_limits<size_t>::max() / (4 * sizeof(float)))
    return false;

  try {
    std::vector<float> source_pixels(static_cast<size_t>(pixel_count) * 4, 0.0f);
    PixelBufferTarget source_target;
    source_target.width = width;
    source_target.height = height;
    source_target.stride_bytes = width * 4 * static_cast<int32_t>(sizeof(float));
    source_target.premultiplied_rgba = source_pixels.data();
    if (!source_target.is_valid())
      return false;

    const auto center = camera.screen_to_world(
        {static_cast<float>(left) + static_cast<float>(width) * 0.5f,
         static_cast<float>(top) + static_cast<float>(height) * 0.5f});
    const Camera2D source_camera(center, camera.zoom(), width, height);
    auto source = path;
    source.fill = scale_alpha(path.fill, appearance.fill_opacity);
    source.stroke = scale_alpha(path.stroke, appearance.stroke_opacity);
    if (!raster::paint(source, source_camera, source_target, preview))
      return false;

    const float object_opacity = static_cast<float>(appearance.object_opacity);
    const size_t source_stride =
        static_cast<size_t>(source_target.stride_bytes / sizeof(float));

    // Validate every source/backdrop pair before the first write for both
    // Normal and non-Normal compositing. Float targets are caller-owned and
    // may contain malformed samples even when their buffer shape is valid.
    for (int32_t y = 0; y < height; ++y) {
      const auto *row = source_pixels.data() +
                        static_cast<size_t>(y) * source_stride;
      for (int32_t x = 0; x < width; ++x) {
        const auto *pixel = row + static_cast<size_t>(x) * 4;
        const BlendPixel source_pixel{pixel[0] * object_opacity,
                                      pixel[1] * object_opacity,
                                      pixel[2] * object_opacity,
                                      pixel[3] * object_opacity};
        if (!is_valid_premultiplied_srgb(source_pixel))
          return false;
        if (source_pixel.a <= 0.0f)
          continue;
        const auto backdrop = read_target_pixel(target, left + x, top + y);
        if (!is_valid_premultiplied_srgb(backdrop))
          return false;
        if (appearance.blend_mode != document::BlendMode::Normal &&
            !blend_source_over(appearance.blend_mode, backdrop, source_pixel))
          return false;
      }
    }

    if (appearance.blend_mode != document::BlendMode::Normal) {
      for (int32_t y = 0; y < height; ++y) {
        const auto *row = source_pixels.data() +
                          static_cast<size_t>(y) * source_stride;
        for (int32_t x = 0; x < width; ++x) {
          const auto *pixel = row + static_cast<size_t>(x) * 4;
          const BlendPixel source_pixel{pixel[0] * object_opacity,
                                        pixel[1] * object_opacity,
                                        pixel[2] * object_opacity,
                                        pixel[3] * object_opacity};
          if (source_pixel.a <= 0.0f)
            continue;
          const auto composed = blend_source_over(
              appearance.blend_mode,
              read_target_pixel(target, left + x, top + y), source_pixel);
          if (!composed)
            return false;
          write_target_pixel(target, left + x, top + y, *composed);
        }
      }
      return true;
    }

    for (int32_t y = 0; y < height; ++y) {
      const auto *row = source_pixels.data() +
                        static_cast<size_t>(y) * source_stride;
      for (int32_t x = 0; x < width; ++x) {
        const auto *pixel = row + static_cast<size_t>(x) * 4;
        const float alpha = pixel[3] * object_opacity;
        if (alpha <= 0.0f)
          continue;
        target.blend_premultiplied_pixel(
            left + x, top + y, pixel[0] * object_opacity,
            pixel[1] * object_opacity, pixel[2] * object_opacity, alpha);
      }
    }
  } catch (const std::bad_alloc &) {
    return false;
  } catch (const std::length_error &) {
    return false;
  }
  return true;
}

} // namespace pikdraw::rendering::effects
