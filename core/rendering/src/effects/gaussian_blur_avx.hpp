#pragma once

#include "gaussian_blur.hpp"

namespace pikdraw::rendering::effects {

/// Convolves one axis with normalized symmetric float weights using AVX.
/// The caller must check gaussian_blur_avx_runtime_supported() first.
[[nodiscard]] GaussianBlurStatus gaussian_blur_convolve_axis_avx(
    std::span<const LinearPremultipliedRgba> source,
    std::span<LinearPremultipliedRgba> destination, std::size_t width,
    std::size_t height, std::span<const float> weights, std::size_t radius,
    bool horizontal, GaussianBlurCancellation cancellation) noexcept;

} // namespace pikdraw::rendering::effects
