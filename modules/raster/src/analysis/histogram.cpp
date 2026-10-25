#include "pikdraw/raster/analysis/histogram.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>

namespace pikdraw::raster {
namespace {

uint64_t bytes_per_sample(RasterPixelEncoding encoding) noexcept {
  switch (encoding) {
    case RasterPixelEncoding::SRgbRgba8StraightAlpha: return 1;
    case RasterPixelEncoding::SRgbRgba16LEStraightAlpha: return 2;
  }
  return 0;
}

uint32_t read_sample(const uint8_t* source,
                     RasterPixelEncoding encoding) noexcept {
  if (encoding == RasterPixelEncoding::SRgbRgba8StraightAlpha)
    return source[0];
  return static_cast<uint32_t>(source[0]) |
      (static_cast<uint32_t>(source[1]) << 8);
}

RasterHistogramResult failure(RasterHistogramStatus status,
                              const char* diagnostic) {
  RasterHistogramResult result;
  result.status = status;
  result.diagnostic = diagnostic;
  return result;
}

void discard_partial(RasterHistogramResult& result) noexcept {
  for (auto& channel : result.channels) {
    std::vector<uint64_t>().swap(channel.bins);
    channel.display_bins.fill(0);
    channel.coverage_units = 0;
    channel.covered_pixels = 0;
    channel.minimum_code = 0;
    channel.maximum_code = 0;
    channel.mean_normalized = 0.0;
    channel.clipped_low_units = 0;
    channel.clipped_high_units = 0;
  }
  result.bin_count = 0;
}

RasterHistogramResult compute_histogram_impl(
    const RasterPixelView& source,
    const RasterHistogramOptions& options,
    bool packed_rgba8_words) {
  const auto sample_bytes = bytes_per_sample(source.encoding);
  if (sample_bytes == 0 || source.width == 0 || source.height == 0)
    return failure(RasterHistogramStatus::InvalidSource,
                   "Histogram source encoding or dimensions are invalid.");
  const uint64_t bytes_per_pixel = 4u * sample_bytes;
  if (source.width > std::numeric_limits<uint64_t>::max() / bytes_per_pixel)
    return failure(RasterHistogramStatus::Overflow,
                   "Histogram row stride overflows the supported range.");
  const uint64_t minimum_stride =
      static_cast<uint64_t>(source.width) * bytes_per_pixel;
  if (source.row_stride < minimum_stride ||
      source.row_stride > std::numeric_limits<uint64_t>::max() / source.height ||
      source.row_stride * source.height > source.bytes.size())
    return failure(RasterHistogramStatus::InvalidSource,
                   "Histogram source stride or byte range is invalid.");

  const uint64_t pixel_count =
      static_cast<uint64_t>(source.width) * source.height;
  if (pixel_count > std::numeric_limits<size_t>::max())
    return failure(RasterHistogramStatus::Overflow,
                   "Histogram pixel count exceeds addressable memory.");
  if (!options.coverage.empty() && options.coverage.size() != pixel_count)
    return failure(RasterHistogramStatus::InvalidCoverage,
                   "Histogram mask coverage must contain one byte per pixel.");

  const uint32_t maximum_code = sample_bytes == 1 ? 255u : 65535u;
  const uint64_t bin_count = static_cast<uint64_t>(maximum_code) + 1u;
  if (bin_count > std::numeric_limits<uint64_t>::max() /
          (4u * sizeof(uint64_t)))
    return failure(RasterHistogramStatus::Overflow,
                   "Histogram result size overflows the supported range.");
  const uint64_t exact_result_bytes = bin_count * 4u * sizeof(uint64_t);
  const uint64_t display_result_bytes =
      4u * 256u * sizeof(uint64_t);
  if (exact_result_bytes > std::numeric_limits<uint64_t>::max() -
          display_result_bytes)
    return failure(RasterHistogramStatus::Overflow,
                   "Histogram result size overflows the supported range.");
  const uint64_t result_bytes = exact_result_bytes + display_result_bytes;
  if (result_bytes > options.maximum_result_bytes ||
      result_bytes > std::numeric_limits<size_t>::max())
    return failure(RasterHistogramStatus::InsufficientMemory,
                   "Exact channel histograms exceed the configured memory budget.");
  if (pixel_count > std::numeric_limits<uint64_t>::max() /
          (static_cast<uint64_t>(maximum_code) * 255u))
    return failure(RasterHistogramStatus::Overflow,
                   "Histogram weighted sample totals overflow 64-bit counters.");

  RasterHistogramResult result;
  result.bin_count = static_cast<uint32_t>(bin_count);
  try {
    for (auto& channel : result.channels)
      channel.bins.assign(static_cast<size_t>(bin_count), 0);
  } catch (const std::bad_alloc&) {
    discard_partial(result);
    return failure(RasterHistogramStatus::AllocationFailure,
                   "Memory allocation failed while creating channel histograms.");
  } catch (const std::length_error&) {
    discard_partial(result);
    return failure(RasterHistogramStatus::AllocationFailure,
                   "Channel histogram allocation exceeds vector limits.");
  }

  std::array<uint64_t, 4> weighted_sums{};
  std::array<bool, 4> has_covered_sample{};
  for (uint32_t y = 0; y < source.height; ++y) {
    if (options.cancelled && options.cancelled()) {
      discard_partial(result);
      result.status = RasterHistogramStatus::Cancelled;
      result.diagnostic = "Histogram calculation was cancelled.";
      return result;
    }
    const auto* row = source.bytes.data() +
        static_cast<size_t>(static_cast<uint64_t>(y) * source.row_stride);
    for (uint32_t x = 0; x < source.width; ++x) {
      const uint64_t pixel_index = static_cast<uint64_t>(y) * source.width + x;
      if ((pixel_index & 4095u) == 0u && options.cancelled &&
          options.cancelled()) {
        discard_partial(result);
        result.status = RasterHistogramStatus::Cancelled;
        result.diagnostic = "Histogram calculation was cancelled.";
        return result;
      }
      const uint32_t coverage = options.coverage.empty()
          ? 255u : options.coverage[static_cast<size_t>(pixel_index)];
      if (coverage == 0) continue;
      const auto* pixel = row + static_cast<size_t>(x) * bytes_per_pixel;
      uint32_t packed_rgba = 0;
      if (packed_rgba8_words)
        std::memcpy(&packed_rgba, pixel, sizeof(packed_rgba));
      for (size_t channel_index = 0; channel_index < result.channels.size();
           ++channel_index) {
        const uint32_t code = packed_rgba8_words
            ? static_cast<uint32_t>((packed_rgba >>
                (24u - static_cast<uint32_t>(channel_index) * 8u)) & 0xffu)
            : read_sample(pixel + channel_index * sample_bytes, source.encoding);
        auto& channel = result.channels[channel_index];
        channel.bins[code] += coverage;
        const uint32_t display_bin = sample_bytes == 1 ? code : code >> 8;
        channel.display_bins[display_bin] += coverage;
        channel.coverage_units += coverage;
        ++channel.covered_pixels;
        weighted_sums[channel_index] +=
            static_cast<uint64_t>(code) * coverage;
        channel.clipped_low_units += code == 0 ? coverage : 0u;
        channel.clipped_high_units += code == maximum_code ? coverage : 0u;
        if (!has_covered_sample[channel_index]) {
          channel.minimum_code = code;
          channel.maximum_code = code;
          has_covered_sample[channel_index] = true;
        } else {
          channel.minimum_code = (std::min)(channel.minimum_code, code);
          channel.maximum_code = (std::max)(channel.maximum_code, code);
        }
      }
    }
  }

  for (size_t channel_index = 0; channel_index < result.channels.size();
       ++channel_index) {
    auto& channel = result.channels[channel_index];
    if (channel.coverage_units != 0)
      channel.mean_normalized = static_cast<double>(weighted_sums[channel_index]) /
          (static_cast<double>(channel.coverage_units) * maximum_code);
  }
  result.status = RasterHistogramStatus::Success;
  return result;
}

}  // namespace

RasterHistogramResult compute_raster_histogram(
    const RasterPixelView& source, const RasterHistogramOptions& options) {
  return compute_histogram_impl(source, options, false);
}

RasterHistogramResult compute_raster_histogram(
    std::span<const uint32_t> packed_rgba8,
    uint32_t width,
    uint32_t height,
    const RasterHistogramOptions& options) {
  if (width == 0 || height == 0 ||
      static_cast<uint64_t>(width) * height != packed_rgba8.size())
    return failure(RasterHistogramStatus::InvalidSource,
                   "Packed histogram dimensions do not match the source pixels.");
  const auto* bytes = reinterpret_cast<const uint8_t*>(packed_rgba8.data());
  const RasterPixelView source{
      std::span<const uint8_t>(bytes, packed_rgba8.size_bytes()), width, height,
      static_cast<uint64_t>(width) * sizeof(uint32_t),
      RasterPixelEncoding::SRgbRgba8StraightAlpha};
  return compute_histogram_impl(source, options, true);
}

}  // namespace pikdraw::raster
