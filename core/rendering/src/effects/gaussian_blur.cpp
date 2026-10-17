#include "gaussian_blur.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>

namespace pikdraw::rendering::effects {
namespace {

using Pixel = LinearPremultipliedRgba;

struct AxisKernel final {
  std::size_t radius{};
  std::size_t taps{};
  std::vector<double> weights;
};

[[nodiscard]] bool checked_add(std::size_t a, std::size_t b,
                               std::size_t &result) noexcept {
  if (b > std::numeric_limits<std::size_t>::max() - a)
    return false;
  result = a + b;
  return true;
}

[[nodiscard]] bool checked_multiply(std::size_t a, std::size_t b,
                                    std::size_t &result) noexcept {
  if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a)
    return false;
  result = a * b;
  return true;
}

[[nodiscard]] bool is_cancelled(
    GaussianBlurCancellation cancellation) noexcept {
  return cancellation.is_cancelled != nullptr &&
         cancellation.is_cancelled(cancellation.context);
}

[[nodiscard]] bool is_valid_pixel(const Pixel &pixel) noexcept {
  return std::isfinite(pixel.r) && std::isfinite(pixel.g) &&
         std::isfinite(pixel.b) && std::isfinite(pixel.a) && pixel.a >= 0.0f &&
         pixel.a <= 1.0f && pixel.r >= 0.0f && pixel.g >= 0.0f &&
         pixel.b >= 0.0f && pixel.r <= pixel.a && pixel.g <= pixel.a &&
         pixel.b <= pixel.a;
}

[[nodiscard]] GaussianBlurStatus axis_kernel_shape(double sigma_px,
                                                   AxisKernel &kernel) noexcept {
  if (sigma_px == 0.0) {
    kernel.radius = 0;
    kernel.taps = 1;
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
  std::size_t twice_radius = 0;
  if (!checked_multiply(kernel.radius, 2, twice_radius) ||
      !checked_add(twice_radius, 1, kernel.taps) ||
      kernel.taps > GaussianBlurPolicy::max_kernel_taps)
    return GaussianBlurStatus::OutOfRange;
  return GaussianBlurStatus::Success;
}

[[nodiscard]] GaussianBlurStatus make_kernel(double sigma_px,
                                             AxisKernel &kernel,
                                             GaussianBlurCancellation cancel)
    noexcept {
  if (sigma_px == 0.0)
    return GaussianBlurStatus::Success;

  try {
    kernel.weights.resize(kernel.taps);
  } catch (const std::bad_alloc &) {
    return GaussianBlurStatus::AllocationFailed;
  } catch (const std::length_error &) {
    return GaussianBlurStatus::AllocationFailed;
  }

  double sum = 0.0;
  const auto radius = static_cast<std::int64_t>(kernel.radius);
  for (std::size_t index = 0; index < kernel.taps; ++index) {
    if ((index & 0xffu) == 0u && is_cancelled(cancel))
      return GaussianBlurStatus::Cancelled;
    const auto offset = static_cast<std::int64_t>(index) - radius;
    const double normalized = static_cast<double>(offset) / sigma_px;
    const double weight = std::exp(-0.5 * normalized * normalized);
    if (!std::isfinite(weight) || weight < 0.0)
      return GaussianBlurStatus::InvalidArgument;
    kernel.weights[index] = weight;
    sum += weight;
  }
  if (!std::isfinite(sum) || sum <= 0.0)
    return GaussianBlurStatus::InvalidArgument;

  for (double &weight : kernel.weights)
    weight /= sum;
  return GaussianBlurStatus::Success;
}

[[nodiscard]] Pixel bounded_premultiplied(double r, double g, double b,
                                          double a) noexcept {
  const float alpha = static_cast<float>(std::clamp(a, 0.0, 1.0));
  return {std::clamp(static_cast<float>(r), 0.0f, alpha),
          std::clamp(static_cast<float>(g), 0.0f, alpha),
          std::clamp(static_cast<float>(b), 0.0f, alpha), alpha};
}

[[nodiscard]] GaussianBlurStatus convolve_horizontal(
    std::span<const Pixel> source, std::span<Pixel> destination,
    std::size_t width, std::size_t height, const AxisKernel &kernel,
    GaussianBlurCancellation cancel) noexcept {
  std::uint64_t work = 0;
  const auto signed_width = static_cast<std::int64_t>(width);
  const auto radius = static_cast<std::int64_t>(kernel.radius);
  for (std::size_t y = 0; y < height; ++y) {
    if (is_cancelled(cancel))
      return GaussianBlurStatus::Cancelled;
    const std::size_t row = y * width;
    for (std::size_t x = 0; x < width; ++x) {
      double r = 0.0;
      double g = 0.0;
      double b = 0.0;
      double a = 0.0;
      for (std::size_t tap = 0; tap < kernel.taps; ++tap) {
        if ((work & 0xfffu) == 0u && is_cancelled(cancel))
          return GaussianBlurStatus::Cancelled;
        ++work;
        const auto sample_x = static_cast<std::int64_t>(x) +
                              static_cast<std::int64_t>(tap) - radius;
        if (sample_x < 0 || sample_x >= signed_width)
          continue;
        const Pixel &sample = source[row + static_cast<std::size_t>(sample_x)];
        const double weight = kernel.weights[tap];
        r += static_cast<double>(sample.r) * weight;
        g += static_cast<double>(sample.g) * weight;
        b += static_cast<double>(sample.b) * weight;
        a += static_cast<double>(sample.a) * weight;
      }
      destination[row + x] = bounded_premultiplied(r, g, b, a);
    }
  }
  return GaussianBlurStatus::Success;
}

[[nodiscard]] GaussianBlurStatus convolve_vertical(
    std::span<const Pixel> source, std::span<Pixel> destination,
    std::size_t width, std::size_t height, const AxisKernel &kernel,
    GaussianBlurCancellation cancel) noexcept {
  std::uint64_t work = 0;
  const auto signed_height = static_cast<std::int64_t>(height);
  const auto radius = static_cast<std::int64_t>(kernel.radius);
  for (std::size_t y = 0; y < height; ++y) {
    if (is_cancelled(cancel))
      return GaussianBlurStatus::Cancelled;
    for (std::size_t x = 0; x < width; ++x) {
      double r = 0.0;
      double g = 0.0;
      double b = 0.0;
      double a = 0.0;
      for (std::size_t tap = 0; tap < kernel.taps; ++tap) {
        if ((work & 0xfffu) == 0u && is_cancelled(cancel))
          return GaussianBlurStatus::Cancelled;
        ++work;
        const auto sample_y = static_cast<std::int64_t>(y) +
                              static_cast<std::int64_t>(tap) - radius;
        if (sample_y < 0 || sample_y >= signed_height)
          continue;
        const Pixel &sample =
            source[static_cast<std::size_t>(sample_y) * width + x];
        const double weight = kernel.weights[tap];
        r += static_cast<double>(sample.r) * weight;
        g += static_cast<double>(sample.g) * weight;
        b += static_cast<double>(sample.b) * weight;
        a += static_cast<double>(sample.a) * weight;
      }
      destination[y * width + x] = bounded_premultiplied(r, g, b, a);
    }
  }
  return GaussianBlurStatus::Success;
}

} // namespace

GaussianBlurStatus gaussian_blur_linear_srgb(
    std::span<const LinearPremultipliedRgba> input, std::size_t width,
    std::size_t height, double sigma_x_px, double sigma_y_px,
    std::vector<LinearPremultipliedRgba> &output,
    GaussianBlurCancellation cancellation) noexcept {
  if (width == 0 || height == 0)
    return GaussianBlurStatus::InvalidArgument;
  if (width > GaussianBlurPolicy::max_dimension ||
      height > GaussianBlurPolicy::max_dimension)
    return GaussianBlurStatus::BudgetExceeded;
  if (width > std::numeric_limits<std::size_t>::max() / height)
    return GaussianBlurStatus::BudgetExceeded;
  const std::size_t pixel_count = width * height;
  if (pixel_count > GaussianBlurPolicy::max_pixels)
    return GaussianBlurStatus::BudgetExceeded;
  if (input.size() != pixel_count)
    return GaussianBlurStatus::InvalidArgument;

  AxisKernel x_kernel;
  AxisKernel y_kernel;
  auto status = axis_kernel_shape(sigma_x_px, x_kernel);
  if (status != GaussianBlurStatus::Success)
    return status;
  status = axis_kernel_shape(sigma_y_px, y_kernel);
  if (status != GaussianBlurStatus::Success)
    return status;

  const bool blur_x = sigma_x_px > 0.0;
  const bool blur_y = sigma_y_px > 0.0;
  const std::size_t active_passes = static_cast<std::size_t>(blur_x) +
                                    static_cast<std::size_t>(blur_y);
  const std::size_t x_work_taps = blur_x ? x_kernel.taps : 1;
  const std::size_t y_work_taps = blur_y ? y_kernel.taps : 1;
  std::size_t taps_per_pixel = 0;
  std::size_t estimated_work = 0;
  if (!checked_add(x_work_taps, y_work_taps, taps_per_pixel) ||
      !checked_multiply(pixel_count, taps_per_pixel, estimated_work) ||
      estimated_work > GaussianBlurPolicy::max_work_pixel_taps)
    return GaussianBlurStatus::BudgetExceeded;

  std::size_t surface_bytes = 0;
  std::size_t scratch_bytes = 0;
  if (!checked_multiply(pixel_count, sizeof(Pixel), surface_bytes) ||
      !checked_multiply(surface_bytes, active_passes == 2 ? 2 : 1,
                        scratch_bytes))
    return GaussianBlurStatus::BudgetExceeded;
  std::size_t kernel_taps = 0;
  if ((blur_x && !checked_add(kernel_taps, x_kernel.taps, kernel_taps)) ||
      (blur_y && !checked_add(kernel_taps, y_kernel.taps, kernel_taps)))
    return GaussianBlurStatus::BudgetExceeded;
  std::size_t kernel_bytes = 0;
  if (!checked_multiply(kernel_taps, sizeof(double), kernel_bytes) ||
      !checked_add(scratch_bytes, kernel_bytes, scratch_bytes) ||
      scratch_bytes > GaussianBlurPolicy::max_scratch_bytes)
    return GaussianBlurStatus::BudgetExceeded;

  if (is_cancelled(cancellation))
    return GaussianBlurStatus::Cancelled;
  for (std::size_t index = 0; index < input.size(); ++index) {
    if ((index & 0x3ffu) == 0u && is_cancelled(cancellation))
      return GaussianBlurStatus::Cancelled;
    if (!is_valid_pixel(input[index]))
      return GaussianBlurStatus::OutOfRange;
  }

  status = make_kernel(sigma_x_px, x_kernel, cancellation);
  if (status != GaussianBlurStatus::Success)
    return status;
  status = make_kernel(sigma_y_px, y_kernel, cancellation);
  if (status != GaussianBlurStatus::Success)
    return status;

  try {
    std::vector<Pixel> staged(pixel_count);
    if (!blur_x && !blur_y) {
      for (std::size_t index = 0; index < pixel_count; ++index) {
        if ((index & 0x3ffu) == 0u && is_cancelled(cancellation))
          return GaussianBlurStatus::Cancelled;
        staged[index] = input[index];
      }
    } else if (blur_x && blur_y) {
      std::vector<Pixel> horizontal(pixel_count);
      status = convolve_horizontal(input, horizontal, width, height, x_kernel,
                                   cancellation);
      if (status != GaussianBlurStatus::Success)
        return status;
      status = convolve_vertical(horizontal, staged, width, height, y_kernel,
                                 cancellation);
      if (status != GaussianBlurStatus::Success)
        return status;
    } else if (blur_x) {
      status = convolve_horizontal(input, staged, width, height, x_kernel,
                                   cancellation);
      if (status != GaussianBlurStatus::Success)
        return status;
    } else {
      status = convolve_vertical(input, staged, width, height, y_kernel,
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
}

} // namespace pikdraw::rendering::effects
