#include "gaussian_blur_simd.hpp"
#include "gaussian_blur_avx.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

#ifndef PIKDRAW_RENDERING_HAS_AVX_TU
#define PIKDRAW_RENDERING_HAS_AVX_TU 0
#endif

#if defined(_M_X64) || defined(_M_IX86)
#include <intrin.h>
#elif defined(__i386__) || defined(__x86_64__)
#include <cpuid.h>
#endif

#if defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2) ||             \
    defined(__SSE2__)
#define PIKDRAW_EFFECTS_HAS_SSE2 1
#include <emmintrin.h>
#else
#define PIKDRAW_EFFECTS_HAS_SSE2 0
#endif

namespace pikdraw::rendering::effects {
namespace {

using Pixel = LinearPremultipliedRgba;

[[nodiscard]] bool checked_add(std::size_t left, std::size_t right,
                               std::size_t &result) noexcept {
  if (right > std::numeric_limits<std::size_t>::max() - left)
    return false;
  result = left + right;
  return true;
}

[[nodiscard]] bool checked_multiply(std::size_t left, std::size_t right,
                                    std::size_t &result) noexcept {
  if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left)
    return false;
  result = left * right;
  return true;
}

template <class T>
[[nodiscard]] std::size_t vector_capacity_bytes(
    const std::vector<T> &buffer) noexcept {
  std::size_t bytes = 0;
  return checked_multiply(buffer.capacity(), sizeof(T), bytes)
             ? bytes
             : std::numeric_limits<std::size_t>::max();
}

[[nodiscard]] std::size_t saturated_add(std::size_t left,
                                        std::size_t right) noexcept {
  return right > std::numeric_limits<std::size_t>::max() - left
             ? std::numeric_limits<std::size_t>::max()
             : left + right;
}

[[nodiscard]] bool cancelled(GaussianBlurCancellation value) noexcept {
  return value.is_cancelled != nullptr &&
         value.is_cancelled(value.context);
}

[[nodiscard]] bool valid_pixel(const Pixel &pixel) noexcept {
  return std::isfinite(pixel.r) && std::isfinite(pixel.g) &&
         std::isfinite(pixel.b) && std::isfinite(pixel.a) && pixel.a >= 0.0f &&
         pixel.a <= 1.0f && pixel.r >= 0.0f && pixel.g >= 0.0f &&
         pixel.b >= 0.0f && pixel.r <= pixel.a && pixel.g <= pixel.a &&
         pixel.b <= pixel.a;
}

struct SymmetricKernel final {
  std::size_t radius{};
  // Index zero is the center weight. Every later weight is used for the
  // symmetric pair at +/- index.
  std::vector<float> weights;
};

[[nodiscard]] GaussianBlurStatus kernel_shape(double sigma_px,
                                               SymmetricKernel &kernel) {
  if (sigma_px == 0.0) {
    kernel.radius = 0;
    return GaussianBlurStatus::Success;
  }
  if (!std::isfinite(sigma_px) || sigma_px < 0.0)
    return GaussianBlurStatus::InvalidArgument;
  if (sigma_px > GaussianBlurPolicy::max_sigma_px)
    return GaussianBlurStatus::OutOfRange;

  const double support = std::ceil(4.0 * sigma_px);
  if (!std::isfinite(support) || support < 1.0 ||
      support > static_cast<double>((GaussianBlurPolicy::max_kernel_taps - 1) /
                                    2))
    return GaussianBlurStatus::OutOfRange;
  kernel.radius = static_cast<std::size_t>(support);
  return GaussianBlurStatus::Success;
}

[[nodiscard]] GaussianBlurStatus make_kernel(
    double sigma_px, SymmetricKernel &kernel,
    GaussianBlurCancellation cancellation) {
  const auto shape = kernel_shape(sigma_px, kernel);
  if (shape != GaussianBlurStatus::Success)
    return shape;
  try {
    kernel.weights.resize(kernel.radius + 1);
  } catch (const std::bad_alloc &) {
    return GaussianBlurStatus::AllocationFailed;
  } catch (const std::length_error &) {
    return GaussianBlurStatus::AllocationFailed;
  }
  if (kernel.radius == 0) {
    kernel.weights[0] = 1.0f;
    return GaussianBlurStatus::Success;
  }

  double normalization = 0.0;
  for (std::size_t offset = 0; offset <= kernel.radius; ++offset) {
    if ((offset & 0xffu) == 0u && cancelled(cancellation))
      return GaussianBlurStatus::Cancelled;
    const double normalized = static_cast<double>(offset) / sigma_px;
    const double weight = std::exp(-0.5 * normalized * normalized);
    if (!std::isfinite(weight) || weight < 0.0)
      return GaussianBlurStatus::InvalidArgument;
    kernel.weights[offset] = static_cast<float>(weight);
    normalization += weight * (offset == 0 ? 1.0 : 2.0);
  }
  if (!std::isfinite(normalization) || normalization <= 0.0)
    return GaussianBlurStatus::InvalidArgument;

  for (float &weight : kernel.weights)
    weight = static_cast<float>(static_cast<double>(weight) / normalization);

  // Normalize once more after float quantization so the SIMD pass preserves
  // constant colors and alpha without accumulating a measurable bias.
  double float_sum = kernel.weights[0];
  for (std::size_t offset = 1; offset < kernel.weights.size(); ++offset)
    float_sum += 2.0 * static_cast<double>(kernel.weights[offset]);
  if (!std::isfinite(float_sum) || float_sum <= 0.0)
    return GaussianBlurStatus::InvalidArgument;
  const float correction = static_cast<float>(1.0 / float_sum);
  for (float &weight : kernel.weights)
    weight *= correction;
  return GaussianBlurStatus::Success;
}

#if PIKDRAW_EFFECTS_HAS_SSE2
using Vec4 = __m128;

[[nodiscard]] Vec4 load_pixel(const Pixel &pixel) noexcept {
  Vec4 value;
  std::memcpy(&value, &pixel, sizeof(value));
  return value;
}

void store_pixel(Pixel &pixel, Vec4 value) noexcept {
  const Vec4 zero = _mm_setzero_ps();
  Vec4 alpha = _mm_shuffle_ps(value, value, _MM_SHUFFLE(3, 3, 3, 3));
  alpha = _mm_min_ps(_mm_max_ps(alpha, zero), _mm_set1_ps(1.0f));
  const Vec4 bounded_rgb = _mm_min_ps(_mm_max_ps(value, zero), alpha);
  const Vec4 rgb_mask = _mm_castsi128_ps(_mm_set_epi32(0, -1, -1, -1));
  const Vec4 alpha_mask = _mm_castsi128_ps(_mm_set_epi32(-1, 0, 0, 0));
  const Vec4 bounded = _mm_or_ps(_mm_and_ps(bounded_rgb, rgb_mask),
                                 _mm_and_ps(alpha, alpha_mask));
  std::memcpy(&pixel, &bounded, sizeof(bounded));
}

[[nodiscard]] GaussianBlurStatus convolve_axis(
    std::span<const Pixel> source, std::span<Pixel> destination,
    std::size_t width, std::size_t height, const SymmetricKernel &kernel,
    bool horizontal, GaussianBlurCancellation cancellation) noexcept {
  static const bool use_avx = gaussian_blur_avx_runtime_supported();
  if (use_avx)
    return gaussian_blur_convolve_axis_avx(
        source, destination, width, height, kernel.weights, kernel.radius,
        horizontal, cancellation);

  const Vec4 zero = _mm_setzero_ps();
  const std::size_t outer = horizontal ? height : width;
  const std::size_t inner = horizontal ? width : height;
  for (std::size_t outer_index = 0; outer_index < outer; ++outer_index) {
    if (cancelled(cancellation))
      return GaussianBlurStatus::Cancelled;
    for (std::size_t inner_index = 0; inner_index < inner; ++inner_index) {
      if ((inner_index & 0x3fu) == 0u && cancelled(cancellation))
        return GaussianBlurStatus::Cancelled;
      const std::size_t x = horizontal ? inner_index : outer_index;
      const std::size_t y = horizontal ? outer_index : inner_index;
      const std::size_t index = y * width + x;
      Vec4 accumulated = _mm_mul_ps(load_pixel(source[index]),
                                    _mm_set1_ps(kernel.weights[0]));
      for (std::size_t offset = 1; offset <= kernel.radius; ++offset) {
        Vec4 first = zero;
        Vec4 second = zero;
        if (inner_index >= offset) {
          const std::size_t neighbor = horizontal
              ? index - offset
              : index - offset * width;
          first = load_pixel(source[neighbor]);
        }
        if (offset < inner - inner_index) {
          const std::size_t neighbor = horizontal
              ? index + offset
              : index + offset * width;
          second = load_pixel(source[neighbor]);
        }
        const Vec4 pair = _mm_add_ps(first, second);
        accumulated = _mm_add_ps(
            accumulated,
            _mm_mul_ps(pair, _mm_set1_ps(kernel.weights[offset])));
      }
      store_pixel(destination[index], accumulated);
    }
  }
  return GaussianBlurStatus::Success;
}
#endif

} // namespace

bool gaussian_blur_avx_runtime_supported() noexcept {
#if PIKDRAW_EFFECTS_HAS_SSE2 && PIKDRAW_RENDERING_HAS_AVX_TU && \
    defined(_MSC_VER) && \
    (defined(_M_X64) || defined(_M_IX86))
  int registers[4]{};
  __cpuidex(registers, 0, 0);
  if (registers[0] < 1)
    return false;
  __cpuidex(registers, 1, 0);
  constexpr int osxsave_bit = 1 << 27;
  constexpr int avx_bit = 1 << 28;
  if ((registers[2] & (osxsave_bit | avx_bit)) !=
      (osxsave_bit | avx_bit))
    return false;
  return (_xgetbv(0) & 0x6) == 0x6;
#elif PIKDRAW_EFFECTS_HAS_SSE2 && PIKDRAW_RENDERING_HAS_AVX_TU && \
    (defined(__i386__) || defined(__x86_64__)) && \
    (defined(__GNUC__) || defined(__clang__))
  __builtin_cpu_init();
  return __builtin_cpu_supports("avx");
#else
  return false;
#endif
}

GaussianBlurStatus gaussian_blur_linear_srgb_sse2(
    std::span<const LinearPremultipliedRgba> input, std::size_t width,
    std::size_t height, double sigma_x_px, double sigma_y_px,
    std::vector<LinearPremultipliedRgba> &output,
    GaussianBlurCancellation cancellation,
    std::size_t *out_peak_scratch_capacity_bytes) noexcept {
  if (out_peak_scratch_capacity_bytes)
    *out_peak_scratch_capacity_bytes = 0;
#if !PIKDRAW_EFFECTS_HAS_SSE2
  return gaussian_blur_linear_srgb(input, width, height, sigma_x_px,
                                   sigma_y_px, output, cancellation);
#else
  static_assert(sizeof(Pixel) == sizeof(float) * 4);
  if (width == 0 || height == 0)
    return GaussianBlurStatus::InvalidArgument;
  if (width > GaussianBlurPolicy::max_dimension ||
      height > GaussianBlurPolicy::max_dimension ||
      width > std::numeric_limits<std::size_t>::max() / height)
    return GaussianBlurStatus::BudgetExceeded;
  const std::size_t pixel_count = width * height;
  if (pixel_count > GaussianBlurPolicy::max_pixels)
    return GaussianBlurStatus::BudgetExceeded;
  if (input.size() != pixel_count)
    return GaussianBlurStatus::InvalidArgument;

  SymmetricKernel x_kernel;
  SymmetricKernel y_kernel;
  auto status = kernel_shape(sigma_x_px, x_kernel);
  if (status != GaussianBlurStatus::Success)
    return status;
  status = kernel_shape(sigma_y_px, y_kernel);
  if (status != GaussianBlurStatus::Success)
    return status;

  const bool blur_x = sigma_x_px > 0.0;
  const bool blur_y = sigma_y_px > 0.0;
  const std::uint64_t x_taps =
      blur_x ? static_cast<std::uint64_t>(x_kernel.radius) * 2 + 1 : 1;
  const std::uint64_t y_taps =
      blur_y ? static_cast<std::uint64_t>(y_kernel.radius) * 2 + 1 : 1;
  const std::uint64_t work =
      static_cast<std::uint64_t>(pixel_count) * (x_taps + y_taps);
  if (work > GaussianBlurPolicy::max_work_pixel_taps)
    return GaussianBlurStatus::BudgetExceeded;

  std::size_t surface_bytes = 0;
  std::size_t scratch_bytes = 0;
  if (!checked_multiply(pixel_count, sizeof(Pixel), surface_bytes) ||
      !checked_multiply(surface_bytes, blur_x && blur_y ? 2 : 1,
                        scratch_bytes))
    return GaussianBlurStatus::BudgetExceeded;
  std::size_t kernel_taps = 0;
  if ((blur_x && !checked_add(kernel_taps,
                              static_cast<std::size_t>(x_taps), kernel_taps)) ||
      (blur_y && !checked_add(kernel_taps,
                              static_cast<std::size_t>(y_taps), kernel_taps)))
    return GaussianBlurStatus::BudgetExceeded;
  std::size_t kernel_bytes = 0;
  if (!checked_multiply(kernel_taps, sizeof(double), kernel_bytes) ||
      !checked_add(scratch_bytes, kernel_bytes, scratch_bytes) ||
      scratch_bytes > GaussianBlurPolicy::max_scratch_bytes)
    return GaussianBlurStatus::BudgetExceeded;

  if (cancelled(cancellation))
    return GaussianBlurStatus::Cancelled;
  for (std::size_t index = 0; index < input.size(); ++index) {
    if ((index & 0x3ffu) == 0u && cancelled(cancellation))
      return GaussianBlurStatus::Cancelled;
    if (!valid_pixel(input[index]))
      return GaussianBlurStatus::OutOfRange;
  }

  try {
    status = make_kernel(sigma_x_px, x_kernel, cancellation);
    if (status != GaussianBlurStatus::Success)
      return status;
    status = make_kernel(sigma_y_px, y_kernel, cancellation);
    if (status != GaussianBlurStatus::Success)
      return status;

    std::vector<Pixel> staged(pixel_count);
    const auto record_scratch = [&](std::size_t surface_bytes) {
      if (!out_peak_scratch_capacity_bytes)
        return;
      std::size_t capacity = saturated_add(
          vector_capacity_bytes(x_kernel.weights),
          vector_capacity_bytes(y_kernel.weights));
      capacity = saturated_add(capacity, surface_bytes);
      *out_peak_scratch_capacity_bytes =
          std::max(*out_peak_scratch_capacity_bytes, capacity);
    };
    record_scratch(vector_capacity_bytes(staged));
    if (!blur_x && !blur_y) {
      for (std::size_t index = 0; index < pixel_count; ++index) {
        if ((index & 0x3ffu) == 0u && cancelled(cancellation))
          return GaussianBlurStatus::Cancelled;
        staged[index] = input[index];
      }
    } else if (blur_x && blur_y) {
      std::vector<Pixel> horizontal(pixel_count);
      record_scratch(saturated_add(vector_capacity_bytes(staged),
                                   vector_capacity_bytes(horizontal)));
      status = convolve_axis(input, horizontal, width, height, x_kernel, true,
                             cancellation);
      if (status != GaussianBlurStatus::Success)
        return status;
      status = convolve_axis(horizontal, staged, width, height, y_kernel,
                             false, cancellation);
      if (status != GaussianBlurStatus::Success)
        return status;
    } else if (blur_x) {
      status = convolve_axis(input, staged, width, height, x_kernel, true,
                             cancellation);
      if (status != GaussianBlurStatus::Success)
        return status;
    } else {
      status = convolve_axis(input, staged, width, height, y_kernel, false,
                             cancellation);
      if (status != GaussianBlurStatus::Success)
        return status;
    }
    output.swap(staged);
  } catch (const std::bad_alloc &) {
    return GaussianBlurStatus::AllocationFailed;
  } catch (const std::length_error &) {
    return GaussianBlurStatus::AllocationFailed;
  }
  return GaussianBlurStatus::Success;
#endif
}

} // namespace pikdraw::rendering::effects
