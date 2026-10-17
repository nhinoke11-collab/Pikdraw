#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace pikdraw::rendering::effects {

/// One normalized linear-sRGB sample with premultiplied color channels.
struct LinearPremultipliedRgba {
  float r{};
  float g{};
  float b{};
  float a{};

  friend bool operator==(const LinearPremultipliedRgba &,
                         const LinearPremultipliedRgba &) = default;
};

enum class GaussianBlurStatus : std::uint8_t {
  Success,
  InvalidArgument,
  OutOfRange,
  BudgetExceeded,
  Cancelled,
  AllocationFailed,
};

/// Finite CPU-reference limits, applied before input scanning or allocation.
/// These are implementation policy, not authored document limits or a claim
/// about another application's Gaussian kernel.
struct GaussianBlurPolicy final {
  static constexpr std::size_t max_dimension = 8192;
  static constexpr std::size_t max_pixels = 4u * 1024u * 1024u;
  static constexpr double max_sigma_px = 256.0;
  static constexpr std::size_t max_kernel_taps = 2049; // ceil(4*256)*2+1
  static constexpr std::uint64_t max_work_pixel_taps = 268435456ull;
  static constexpr std::size_t max_scratch_bytes = 160u * 1024u * 1024u;
};

/// Caller-owned cancellation seam, polled during validation and each pass.
struct GaussianBlurCancellation final {
  void *context{};
  bool (*is_cancelled)(void *context) noexcept{};
};

/// Applies the authored-v1 normalized, separable Gaussian in linear sRGB.
/// Input and output have the same bounded surface; samples beyond its edges
/// are transparent zero and the kernel is never renormalized at an edge.
/// Callers that need expanded filter bounds must provide an already-padded
/// filter-local input surface and retain its origin separately.
///
/// Input channels must be finite normalized SDR premultiplied values with
/// 0 <= rgb <= alpha <= 1. On any non-success result, `output` is untouched.
/// Success atomically replaces `output`; zero sigma on both axes copies the
/// validated samples exactly.
[[nodiscard]] GaussianBlurStatus gaussian_blur_linear_srgb(
    std::span<const LinearPremultipliedRgba> input, std::size_t width,
    std::size_t height, double sigma_x_px, double sigma_y_px,
    std::vector<LinearPremultipliedRgba> &output,
    GaussianBlurCancellation cancellation = {}) noexcept;

} // namespace pikdraw::rendering::effects
