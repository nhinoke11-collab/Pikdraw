#pragma once

#include "gaussian_blur.hpp"

namespace pikdraw::rendering::effects {

/// Reports whether the optional AVX translation unit can run on this CPU and
/// whether the operating system has enabled XMM/YMM state management.
[[nodiscard]] bool gaussian_blur_avx_runtime_supported() noexcept;

/// SSE2 implementation of the authored-v1 Gaussian contract. On platforms
/// without SSE2, this entry point falls back to the scalar reference routine.
/// When AVX is available, convolution dispatches to the AVX implementation.
/// Optional scratch telemetry reports the peak vector capacity owned by this
/// call; caller input/output storage and allocator metadata are excluded.
[[nodiscard]] GaussianBlurStatus gaussian_blur_linear_srgb_sse2(
    std::span<const LinearPremultipliedRgba> input, std::size_t width,
    std::size_t height, double sigma_x_px, double sigma_y_px,
    std::vector<LinearPremultipliedRgba> &output,
    GaussianBlurCancellation cancellation = {},
    std::size_t *out_peak_scratch_capacity_bytes = nullptr) noexcept;

} // namespace pikdraw::rendering::effects
