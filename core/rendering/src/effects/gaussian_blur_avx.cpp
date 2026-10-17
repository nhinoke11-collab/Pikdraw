#include "gaussian_blur_avx.hpp"

#include <algorithm>
#include <cstring>

#ifndef PIKDRAW_RENDERING_HAS_AVX_TU
#define PIKDRAW_RENDERING_HAS_AVX_TU 0
#endif

#if PIKDRAW_RENDERING_HAS_AVX_TU
#include <immintrin.h>
#endif

namespace pikdraw::rendering::effects {
namespace {

using Pixel = LinearPremultipliedRgba;

[[nodiscard]] bool cancelled(GaussianBlurCancellation value) noexcept {
  return value.is_cancelled != nullptr &&
         value.is_cancelled(value.context);
}

[[nodiscard]] Pixel convolve_one_pixel(
    std::span<const Pixel> source, std::size_t width, std::size_t height,
    std::size_t x, std::size_t y, std::span<const float> weights,
    std::size_t radius, bool horizontal) noexcept {
  const std::size_t index = y * width + x;
  const Pixel &center = source[index];
  float r = center.r * weights[0];
  float g = center.g * weights[0];
  float b = center.b * weights[0];
  float a = center.a * weights[0];
  const std::size_t coordinate = horizontal ? x : y;
  const std::size_t extent = horizontal ? width : height;
  for (std::size_t offset = 1; offset <= radius; ++offset) {
    if (coordinate >= offset) {
      const std::size_t sample_index =
          horizontal ? index - offset : index - offset * width;
      const Pixel &sample = source[sample_index];
      r += sample.r * weights[offset];
      g += sample.g * weights[offset];
      b += sample.b * weights[offset];
      a += sample.a * weights[offset];
    }
    if (offset < extent - coordinate) {
      const std::size_t sample_index =
          horizontal ? index + offset : index + offset * width;
      const Pixel &sample = source[sample_index];
      r += sample.r * weights[offset];
      g += sample.g * weights[offset];
      b += sample.b * weights[offset];
      a += sample.a * weights[offset];
    }
  }
  a = std::clamp(a, 0.0f, 1.0f);
  return {std::clamp(r, 0.0f, a), std::clamp(g, 0.0f, a),
          std::clamp(b, 0.0f, a), a};
}

#if PIKDRAW_RENDERING_HAS_AVX_TU
using Vec8 = __m256;

[[nodiscard]] Vec8 load_two_pixels(const Pixel *pixels) noexcept {
  Vec8 result;
  std::memcpy(&result, pixels, sizeof(result));
  return result;
}

void store_two_pixels(Pixel *pixels, Vec8 value) noexcept {
  const Vec8 zero = _mm256_setzero_ps();
  Vec8 alpha = _mm256_permute_ps(value, _MM_SHUFFLE(3, 3, 3, 3));
  alpha = _mm256_min_ps(_mm256_max_ps(alpha, zero), _mm256_set1_ps(1.0f));
  const Vec8 bounded_rgb = _mm256_min_ps(_mm256_max_ps(value, zero), alpha);
  const Vec8 rgb_mask = _mm256_castsi256_ps(
      _mm256_set_epi32(0, -1, -1, -1, 0, -1, -1, -1));
  const Vec8 alpha_mask = _mm256_castsi256_ps(
      _mm256_set_epi32(-1, 0, 0, 0, -1, 0, 0, 0));
  const Vec8 bounded = _mm256_or_ps(_mm256_and_ps(bounded_rgb, rgb_mask),
                                    _mm256_and_ps(alpha, alpha_mask));
  std::memcpy(pixels, &bounded, sizeof(bounded));
}

[[nodiscard]] GaussianBlurStatus convolve_avx(
    std::span<const Pixel> source, std::span<Pixel> destination,
    std::size_t width, std::size_t height, std::span<const float> weights,
    std::size_t radius, bool horizontal,
    GaussianBlurCancellation cancellation) noexcept {
  const Vec8 zero = _mm256_setzero_ps();
  const auto status = [&]() noexcept {
    if (width == 0 || height == 0 || source.size() != width * height ||
        destination.size() != source.size() || weights.size() != radius + 1)
      return GaussianBlurStatus::InvalidArgument;

    for (std::size_t y = 0; y < height; ++y) {
      if (cancelled(cancellation))
        return GaussianBlurStatus::Cancelled;
      std::size_t x = 0;
      for (; x + 1 < width; x += 2) {
        if ((x & 0x7fu) == 0u && cancelled(cancellation))
          return GaussianBlurStatus::Cancelled;
        const std::size_t index = y * width + x;
        if (horizontal &&
            (x < radius || x + 1 + radius >= width)) {
          destination[index] = convolve_one_pixel(
              source, width, height, x, y, weights, radius, true);
          destination[index + 1] = convolve_one_pixel(
              source, width, height, x + 1, y, weights, radius, true);
          continue;
        }

        Vec8 accumulated = _mm256_mul_ps(load_two_pixels(source.data() + index),
                                         _mm256_set1_ps(weights[0]));
        for (std::size_t offset = 1; offset <= radius; ++offset) {
          Vec8 first = zero;
          Vec8 second = zero;
          if (horizontal) {
            first = load_two_pixels(source.data() + index - offset);
            second = load_two_pixels(source.data() + index + offset);
          } else {
            if (y >= offset)
              first = load_two_pixels(source.data() + index - offset * width);
            if (offset < height - y)
              second = load_two_pixels(source.data() + index + offset * width);
          }
          accumulated = _mm256_add_ps(
              accumulated,
              _mm256_mul_ps(_mm256_add_ps(first, second),
                            _mm256_set1_ps(weights[offset])));
        }
        store_two_pixels(destination.data() + index, accumulated);
      }
      if (x < width) {
        if (cancelled(cancellation))
          return GaussianBlurStatus::Cancelled;
        destination[y * width + x] = convolve_one_pixel(
            source, width, height, x, y, weights, radius, horizontal);
      }
    }
    return GaussianBlurStatus::Success;
  }();
  _mm256_zeroupper();
  return status;
}
#endif

} // namespace

GaussianBlurStatus gaussian_blur_convolve_axis_avx(
    std::span<const LinearPremultipliedRgba> source,
    std::span<LinearPremultipliedRgba> destination, std::size_t width,
    std::size_t height, std::span<const float> weights, std::size_t radius,
    bool horizontal, GaussianBlurCancellation cancellation) noexcept {
#if PIKDRAW_RENDERING_HAS_AVX_TU
  static_assert(sizeof(Pixel) == sizeof(float) * 4);
  return convolve_avx(source, destination, width, height, weights, radius,
                      horizontal, cancellation);
#else
  (void)source;
  (void)destination;
  (void)width;
  (void)height;
  (void)weights;
  (void)radius;
  (void)horizontal;
  (void)cancellation;
  return GaussianBlurStatus::InvalidArgument;
#endif
}

} // namespace pikdraw::rendering::effects
