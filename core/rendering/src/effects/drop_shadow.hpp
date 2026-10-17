#pragma once

#include "gaussian_blur.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace pikdraw::rendering::effects {

/// Drop-shadow tint already converted to unassociated linear-sRGB by the
/// caller. Offsets and Gaussian sigmas are expressed in the local filter
/// surface's pixel axes; positive local Y maps to raster-up.
struct DropShadowParameters final {
  double offset_x_px{};
  double offset_y_px{};
  double sigma_x_px{};
  double sigma_y_px{};
  float color_r_linear_srgb{};
  float color_g_linear_srgb{};
  float color_b_linear_srgb{};
  float color_alpha{1.0f};
  float opacity{1.0f};
};

/// Finite policy for the reference CPU shadow branch. The input alpha span and
/// the caller's existing output storage are not scratch owned by this call.
struct DropShadowPolicy final {
  static constexpr std::size_t max_dimension = GaussianBlurPolicy::max_dimension;
  static constexpr std::size_t max_pixels = GaussianBlurPolicy::max_pixels;
  static constexpr std::uint64_t max_work_units =
      GaussianBlurPolicy::max_work_pixel_taps;
  // At the peak, the mask coexists with Gaussian's bounded scratch. After
  // blur, the mask is released before the translated output is staged.
  static constexpr std::size_t max_scratch_bytes =
      GaussianBlurPolicy::max_scratch_bytes +
      max_pixels * sizeof(LinearPremultipliedRgba);
};

/// Builds a separate premultiplied linear-sRGB shadow branch from the alpha
/// entering this effect node. The local translation uses deterministic
/// bilinear sampling: output raster sample (x,y) reads the blurred mask at
/// (x-offset_x_px, y+offset_y_px), so positive local Y moves raster-up.
/// Samples outside the supplied filter-local surface are transparent zero.
/// The caller is responsible for supplying padded filter-local bounds when
/// needed and compositing this branch below the unmodified node input.
///
/// For each sample, shadow alpha is blurred-mask alpha times shadow opacity
/// times source color alpha; RGB is the linear-sRGB tint premultiplied by that
/// result. A failure leaves `output` untouched. The input alpha values and
/// parameters must remain immutable for the duration of the call.
[[nodiscard]] GaussianBlurStatus drop_shadow_linear_srgb(
    std::span<const float> input_alpha, std::size_t width,
    std::size_t height, const DropShadowParameters &parameters,
    std::vector<LinearPremultipliedRgba> &output,
    GaussianBlurCancellation cancellation = {},
    std::size_t *out_peak_scratch_capacity_bytes = nullptr) noexcept;

} // namespace pikdraw::rendering::effects
