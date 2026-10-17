#include "drop_shadow.hpp"
#include "gaussian_blur_simd.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>

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

[[nodiscard]] bool checked_add_u64(std::uint64_t left, std::uint64_t right,
                                   std::uint64_t &result) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left)
    return false;
  result = left + right;
  return true;
}

[[nodiscard]] bool checked_multiply_u64(std::uint64_t left,
                                        std::uint64_t right,
                                        std::uint64_t &result) noexcept {
  if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
    return false;
  result = left * right;
  return true;
}

template <class T>
[[nodiscard]] std::size_t vector_capacity_bytes(
    const std::vector<T> &buffer) noexcept {
  if (buffer.capacity() > std::numeric_limits<std::size_t>::max() / sizeof(T))
    return std::numeric_limits<std::size_t>::max();
  return buffer.capacity() * sizeof(T);
}

[[nodiscard]] std::size_t saturated_add(std::size_t left,
                                         std::size_t right) noexcept {
  return right > std::numeric_limits<std::size_t>::max() - left
             ? std::numeric_limits<std::size_t>::max()
             : left + right;
}

[[nodiscard]] GaussianBlurStatus sigma_taps(double sigma_px,
                                             std::size_t &tap_count) noexcept {
  if (!std::isfinite(sigma_px) || sigma_px < 0.0)
    return GaussianBlurStatus::InvalidArgument;
  if (sigma_px > GaussianBlurPolicy::max_sigma_px)
    return GaussianBlurStatus::OutOfRange;

  if (sigma_px == 0.0) {
    tap_count = 1;
    return GaussianBlurStatus::Success;
  }

  const double support = std::ceil(4.0 * sigma_px);
  const std::size_t max_radius =
      (GaussianBlurPolicy::max_kernel_taps - 1) / 2;
  if (!std::isfinite(support) || support < 1.0 ||
      support > static_cast<double>(max_radius))
    return GaussianBlurStatus::OutOfRange;

  const auto radius = static_cast<std::size_t>(support);
  std::size_t twice_radius = 0;
  if (!checked_multiply(radius, 2, twice_radius) ||
      !checked_add(twice_radius, 1, tap_count) ||
      tap_count > GaussianBlurPolicy::max_kernel_taps)
    return GaussianBlurStatus::OutOfRange;
  return GaussianBlurStatus::Success;
}

[[nodiscard]] bool is_cancelled(
    GaussianBlurCancellation cancellation) noexcept {
  return cancellation.is_cancelled != nullptr &&
         cancellation.is_cancelled(cancellation.context);
}

[[nodiscard]] GaussianBlurStatus validate_parameters(
    const DropShadowParameters &parameters, std::size_t &x_taps,
    std::size_t &y_taps) noexcept {
  if (!std::isfinite(parameters.offset_x_px) ||
      !std::isfinite(parameters.offset_y_px))
    return GaussianBlurStatus::InvalidArgument;

  auto status = sigma_taps(parameters.sigma_x_px, x_taps);
  if (status != GaussianBlurStatus::Success)
    return status;
  status = sigma_taps(parameters.sigma_y_px, y_taps);
  if (status != GaussianBlurStatus::Success)
    return status;

  const float values[]{parameters.color_r_linear_srgb,
                       parameters.color_g_linear_srgb,
                       parameters.color_b_linear_srgb,
                       parameters.color_alpha, parameters.opacity};
  for (const float value : values) {
    if (!std::isfinite(value))
      return GaussianBlurStatus::InvalidArgument;
    if (value < 0.0f || value > 1.0f)
      return GaussianBlurStatus::OutOfRange;
  }
  return GaussianBlurStatus::Success;
}

[[nodiscard]] bool preflight_budget(std::size_t pixel_count,
                                    std::size_t x_taps,
                                    std::size_t y_taps) noexcept {
  std::uint64_t taps_per_pixel = 0;
  std::uint64_t estimated_work = 0;
  std::uint64_t pixels = static_cast<std::uint64_t>(pixel_count);
  if (!checked_add_u64(static_cast<std::uint64_t>(x_taps),
                       static_cast<std::uint64_t>(y_taps), taps_per_pixel) ||
      !checked_add_u64(taps_per_pixel, 5, taps_per_pixel) ||
      !checked_multiply_u64(pixels, taps_per_pixel, estimated_work) ||
      estimated_work > DropShadowPolicy::max_work_units)
    return false;

  std::size_t one_surface_bytes = 0;
  std::size_t mask_and_gaussian_bytes = 0;
  std::size_t two_surfaces_bytes = 0;
  if (!checked_multiply(pixel_count, sizeof(Pixel), one_surface_bytes) ||
      !checked_add(one_surface_bytes, GaussianBlurPolicy::max_scratch_bytes,
                   mask_and_gaussian_bytes) ||
      !checked_multiply(one_surface_bytes, 2, two_surfaces_bytes))
    return false;
  const std::size_t peak_bytes =
      std::max(mask_and_gaussian_bytes, two_surfaces_bytes);
  if (peak_bytes > DropShadowPolicy::max_scratch_bytes)
    return false;

  std::vector<Pixel> surface_probe;
  return pixel_count <= surface_probe.max_size();
}

/// Returns a transparent sample if either bilinear neighbor pair falls
/// outside the finite filter-local surface. Coordinates are kept in double
/// until the bounded floor value can safely be converted to an integer.
[[nodiscard]] double sample_bilinear_zero_exterior(
    std::span<const Pixel> source, std::size_t width, std::size_t height,
    double sample_x, double sample_y) noexcept {
  if (!std::isfinite(sample_x) || !std::isfinite(sample_y))
    return 0.0;

  const double x0_double = std::floor(sample_x);
  const double y0_double = std::floor(sample_y);
  if (x0_double < -1.0 || y0_double < -1.0 ||
      x0_double >= static_cast<double>(width) ||
      y0_double >= static_cast<double>(height))
    return 0.0;

  const auto x0 = static_cast<std::int64_t>(x0_double);
  const auto y0 = static_cast<std::int64_t>(y0_double);
  const double fraction_x = sample_x - x0_double;
  const double fraction_y = sample_y - y0_double;
  const double weight_x0 = 1.0 - fraction_x;
  const double weight_y0 = 1.0 - fraction_y;

  double alpha = 0.0;
  const auto add_sample = [&](std::int64_t x, std::int64_t y,
                              double weight) noexcept {
    if (weight == 0.0 || x < 0 || y < 0 ||
        x >= static_cast<std::int64_t>(width) ||
        y >= static_cast<std::int64_t>(height))
      return;
    const auto index = static_cast<std::size_t>(y) * width +
                       static_cast<std::size_t>(x);
    alpha += static_cast<double>(source[index].a) * weight;
  };

  add_sample(x0, y0, weight_x0 * weight_y0);
  add_sample(x0 + 1, y0, fraction_x * weight_y0);
  add_sample(x0, y0 + 1, weight_x0 * fraction_y);
  add_sample(x0 + 1, y0 + 1, fraction_x * fraction_y);
  return std::clamp(alpha, 0.0, 1.0);
}

} // namespace

GaussianBlurStatus drop_shadow_linear_srgb(
    std::span<const float> input_alpha, std::size_t width,
    std::size_t height, const DropShadowParameters &parameters,
    std::vector<LinearPremultipliedRgba> &output,
    GaussianBlurCancellation cancellation,
    std::size_t *out_peak_scratch_capacity_bytes) noexcept {
  if (out_peak_scratch_capacity_bytes)
    *out_peak_scratch_capacity_bytes = 0;
  if (width == 0 || height == 0)
    return GaussianBlurStatus::InvalidArgument;
  if (width > DropShadowPolicy::max_dimension ||
      height > DropShadowPolicy::max_dimension)
    return GaussianBlurStatus::BudgetExceeded;
  if (width > std::numeric_limits<std::size_t>::max() / height)
    return GaussianBlurStatus::BudgetExceeded;

  const std::size_t pixel_count = width * height;
  if (pixel_count > DropShadowPolicy::max_pixels)
    return GaussianBlurStatus::BudgetExceeded;
  if (input_alpha.size() != pixel_count)
    return GaussianBlurStatus::InvalidArgument;

  std::size_t x_taps = 0;
  std::size_t y_taps = 0;
  const auto parameter_status =
      validate_parameters(parameters, x_taps, y_taps);
  if (parameter_status != GaussianBlurStatus::Success)
    return parameter_status;
  if (!preflight_budget(pixel_count, x_taps, y_taps))
    return GaussianBlurStatus::BudgetExceeded;
  if (is_cancelled(cancellation))
    return GaussianBlurStatus::Cancelled;

  for (std::size_t index = 0; index < pixel_count; ++index) {
    if ((index & 0x3ffu) == 0u && is_cancelled(cancellation))
      return GaussianBlurStatus::Cancelled;
    const float alpha = input_alpha[index];
    if (!std::isfinite(alpha) || alpha < 0.0f || alpha > 1.0f)
      return GaussianBlurStatus::OutOfRange;
  }

  try {
    std::vector<Pixel> alpha_surface(pixel_count);
    for (std::size_t index = 0; index < pixel_count; ++index) {
      if ((index & 0x3ffu) == 0u && is_cancelled(cancellation))
        return GaussianBlurStatus::Cancelled;
      const float alpha = input_alpha[index];
      alpha_surface[index] = {0.0f, 0.0f, 0.0f, alpha};
    }

    std::vector<Pixel> blurred_alpha;
    std::size_t gaussian_scratch_capacity_bytes = 0;
    const auto blur_status = gaussian_blur_linear_srgb_sse2(
        alpha_surface, width, height, parameters.sigma_x_px,
        parameters.sigma_y_px, blurred_alpha, cancellation,
        &gaussian_scratch_capacity_bytes);
    std::size_t blur_peak_capacity_bytes = saturated_add(
        vector_capacity_bytes(alpha_surface),
        vector_capacity_bytes(blurred_alpha));
    blur_peak_capacity_bytes = saturated_add(
        blur_peak_capacity_bytes, gaussian_scratch_capacity_bytes);
    if (out_peak_scratch_capacity_bytes) {
      *out_peak_scratch_capacity_bytes = std::max(
          *out_peak_scratch_capacity_bytes, blur_peak_capacity_bytes);
    }
    if (blur_status != GaussianBlurStatus::Success)
      return blur_status;

    std::vector<Pixel>().swap(alpha_surface);
    std::vector<Pixel> staged(pixel_count);
    if (out_peak_scratch_capacity_bytes) {
      const auto staged_peak_capacity_bytes = saturated_add(
          vector_capacity_bytes(blurred_alpha),
          vector_capacity_bytes(staged));
      *out_peak_scratch_capacity_bytes = std::max(
          *out_peak_scratch_capacity_bytes, staged_peak_capacity_bytes);
    }
    const double tint_alpha = static_cast<double>(parameters.color_alpha) *
                              static_cast<double>(parameters.opacity);
    const double red = parameters.color_r_linear_srgb;
    const double green = parameters.color_g_linear_srgb;
    const double blue = parameters.color_b_linear_srgb;

    for (std::size_t y = 0; y < height; ++y) {
      if (is_cancelled(cancellation))
        return GaussianBlurStatus::Cancelled;
      for (std::size_t x = 0; x < width; ++x) {
        const std::size_t index = y * width + x;
        if ((index & 0x3ffu) == 0u && is_cancelled(cancellation))
          return GaussianBlurStatus::Cancelled;

        // Local +Y is up, whereas raster row indices increase downwards.
        const double source_x = static_cast<double>(x) -
                                parameters.offset_x_px;
        const double source_y = static_cast<double>(y) +
                                parameters.offset_y_px;
        const double mask_alpha = sample_bilinear_zero_exterior(
            blurred_alpha, width, height, source_x, source_y);
        const float alpha = static_cast<float>(mask_alpha * tint_alpha);
        staged[index] = {static_cast<float>(red * alpha),
                         static_cast<float>(green * alpha),
                         static_cast<float>(blue * alpha), alpha};
      }
    }

    if (is_cancelled(cancellation))
      return GaussianBlurStatus::Cancelled;
    if (out_peak_scratch_capacity_bytes) {
      const auto staged_peak_capacity_bytes = saturated_add(
          vector_capacity_bytes(blurred_alpha),
          vector_capacity_bytes(staged));
      *out_peak_scratch_capacity_bytes = std::max(
          *out_peak_scratch_capacity_bytes, staged_peak_capacity_bytes);
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
