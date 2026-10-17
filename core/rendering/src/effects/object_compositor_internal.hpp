#pragma once

#include "object_compositor.hpp"
#include "blend_operator.hpp"
#include "drop_shadow.hpp"
#include "gaussian_blur.hpp"
#include "gaussian_blur_simd.hpp"
#include "../raster/path_raster.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace pikdraw::rendering::effects {

constexpr uint64_t kMaxSourceSurfacePixels = 4ull * 1024ull * 1024ull;
constexpr uint64_t kMaxEffectSurfacePixels = 1024ull * 1024ull;
constexpr uint64_t kMaxEffectJobScratchBytes = 128ull * 1024ull * 1024ull;
constexpr uint64_t kEffectScratchBytesPerPixel = 68;
constexpr uint64_t kMaxCachedEffectOutputPixels = 256ull * 1024ull;
constexpr uint64_t kMaxEffectJobWorkUnits =
    GaussianBlurPolicy::max_work_pixel_taps;

using BlendPixel = PremultipliedSrgbRgba;
using FilterPixel = LinearPremultipliedRgba;

template <class T>
[[nodiscard]] inline size_t vector_capacity_bytes(const std::vector<T> &buffer) noexcept {
  if (buffer.capacity() > std::numeric_limits<size_t>::max() / sizeof(T))
    return std::numeric_limits<size_t>::max();
  return buffer.capacity() * sizeof(T);
}

[[nodiscard]] inline size_t saturated_add(size_t left, size_t right) noexcept {
  return right > std::numeric_limits<size_t>::max() - left
             ? std::numeric_limits<size_t>::max()
             : left + right;
}

inline bool unit_interval(double value) {
  return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

inline float srgb_to_linear(float value) noexcept {
  return value <= 0.04045f ? value / 12.92f
                           : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

inline float linear_to_srgb(float value) noexcept {
  return value <= 0.0031308f ? value * 12.92f
                             : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

inline bool finite_nonempty_rect(const geometry::Rect2D &rect) {
  return rect.is_finite() && rect.width > 0.0 && rect.height > 0.0 &&
         std::isfinite(rect.right()) && std::isfinite(rect.bottom());
}

inline bool finite_rect(const geometry::Rect2D &rect) {
  return rect.is_finite() && rect.width >= 0.0 && rect.height >= 0.0 &&
         std::isfinite(rect.right()) && std::isfinite(rect.bottom());
}

struct LocalSurface final {
  geometry::Rect2D bounds;
  int32_t width{};
  int32_t height{};
  double density{};
};

BlendPixel read_target_pixel(const PixelBufferTarget &target, int32_t x,
                             int32_t y) noexcept;
void write_target_pixel(PixelBufferTarget &target, int32_t x, int32_t y,
                        BlendPixel pixel) noexcept;

color::ColorValue scale_alpha(const color::ColorValue &source, double opacity);

bool valid_effect_node(const document::EffectNode &node);
bool valid_appearance(const document::Appearance &appearance);
bool is_default_appearance(const document::Appearance &appearance);
bool has_enabled_effects(const document::Appearance &appearance);

bool max_linear_scale(const geometry::Transform2D &transform, double &scale);
bool effect_output_bounds(const document::PathObject &path,
                          const document::Appearance &appearance,
                          double density, geometry::Rect2D &bounds,
                          uint64_t &work_per_pixel);

bool make_local_surface(const geometry::Rect2D &output_bounds, double density,
                        LocalSurface &surface);

bool screen_bounds(const geometry::Rect2D &world_bounds,
                   const Camera2D &camera, const PixelBufferTarget &target,
                   int32_t &left, int32_t &top, int32_t &width,
                   int32_t &height);
bool screen_bounds(const document::PathObject &path, const Camera2D &camera,
                   const PixelBufferTarget &target, int32_t &left,
                   int32_t &top, int32_t &width, int32_t &height);

bool encoded_to_linear(const std::vector<float> &source,
                       std::vector<FilterPixel> &destination);
bool linear_to_encoded(const FilterPixel &source, BlendPixel &destination);

FilterPixel sample_linear_surface(std::span<const FilterPixel> pixels,
                                  int32_t width, int32_t height, double x,
                                  double y) noexcept;

} // namespace pikdraw::rendering::effects
