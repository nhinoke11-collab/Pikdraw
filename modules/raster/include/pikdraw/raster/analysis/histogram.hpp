#pragma once

#include "pikdraw/raster/evaluator.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace pikdraw::raster {

struct RasterHistogramChannel {
  // One exact bucket per integer sample code (256 for U8, 65536 for U16).
  // Counts are coverage-weighted in 1/255-pixel units.
  std::vector<uint64_t> bins;
  // A compact 256-bin view for UI display. U16 codes are grouped by their
  // high byte; exact values remain available in bins.
  std::array<uint64_t, 256> display_bins{};
  uint64_t coverage_units{0};
  uint64_t covered_pixels{0};
  uint32_t minimum_code{0};
  uint32_t maximum_code{0};
  double mean_normalized{0.0};
  uint64_t clipped_low_units{0};
  uint64_t clipped_high_units{0};
};

struct RasterHistogramOptions {
  // Empty means every pixel has full coverage. Otherwise one U8 coverage
  // value per pixel, applied to all four channels without changing alpha.
  std::span<const uint8_t> coverage{};
  uint64_t maximum_result_bytes{8u * 1024u * 1024u};
  std::function<bool()> cancelled;
};

enum class RasterHistogramStatus : uint8_t {
  Success,
  InvalidSource,
  InvalidCoverage,
  InsufficientMemory,
  Overflow,
  Cancelled,
  AllocationFailure
};

struct RasterHistogramResult {
  RasterHistogramStatus status{RasterHistogramStatus::InvalidSource};
  std::array<RasterHistogramChannel, 4> channels;
  uint32_t bin_count{0};
  std::string diagnostic;

  [[nodiscard]] bool succeeded() const noexcept {
    return status == RasterHistogramStatus::Success;
  }
};

/// Computes exact integer-code channel histograms from straight-alpha sRGB
/// RGBA8 or RGBA16LE samples. A mask contributes fractional pixel coverage;
/// cancellation clears partial results before returning.
[[nodiscard]] RasterHistogramResult compute_raster_histogram(
    const RasterPixelView& source,
    const RasterHistogramOptions& options = {});

/// Computes the same exact RGBA8 histogram directly from ImageObject's packed
/// RRGGBBAA words without allocating an interleaved byte copy.
[[nodiscard]] RasterHistogramResult compute_raster_histogram(
    std::span<const uint32_t> packed_rgba8,
    uint32_t width,
    uint32_t height,
    const RasterHistogramOptions& options = {});

}  // namespace pikdraw::raster
