#include "pikdraw/raster/evaluator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace pikdraw::raster {
namespace {

constexpr uint64_t kRgbaChannels = 4;
constexpr uint32_t kCancelPixelInterval = 256;
constexpr uint64_t kMaximumClonePatchBytes = 128ull * 1024ull * 1024ull;

uint64_t sample_bytes(RasterPixelEncoding encoding) noexcept {
  switch (encoding) {
    case RasterPixelEncoding::SRgbRgba8StraightAlpha: return 1;
    case RasterPixelEncoding::SRgbRgba16LEStraightAlpha: return 2;
  }
  return 0;
}

double sample_max(RasterPixelEncoding encoding) noexcept {
  return encoding == RasterPixelEncoding::SRgbRgba8StraightAlpha
      ? 255.0 : 65535.0;
}

uint32_t read_sample(const uint8_t* source,
                     RasterPixelEncoding encoding) noexcept {
  if (encoding == RasterPixelEncoding::SRgbRgba8StraightAlpha)
    return source[0];
  return static_cast<uint32_t>(source[0]) |
      (static_cast<uint32_t>(source[1]) << 8);
}

double read_normalized(const uint8_t* source,
                       RasterPixelEncoding encoding) noexcept {
  return static_cast<double>(read_sample(source, encoding)) /
      sample_max(encoding);
}

void write_normalized(uint8_t* destination, RasterPixelEncoding encoding,
                      double value) noexcept {
  const auto maximum = static_cast<uint32_t>(sample_max(encoding));
  const auto encoded = static_cast<uint32_t>(std::clamp(
      std::llround(std::clamp(value, 0.0, 1.0) * maximum), 0ll,
      static_cast<long long>(maximum)));
  destination[0] = static_cast<uint8_t>(encoded & 0xffu);
  if (encoding == RasterPixelEncoding::SRgbRgba16LEStraightAlpha)
    destination[1] = static_cast<uint8_t>(encoded >> 8);
}

struct StepResult {
  std::optional<RasterPixelBuffer> image;
  RasterEvaluationStatus status{RasterEvaluationStatus::Success};
  std::string diagnostic;
};

RasterEvaluationResult failure(RasterEvaluationStatus status,
                               std::string diagnostic) {
  RasterEvaluationResult result;
  result.status = status;
  result.diagnostic = std::move(diagnostic);
  return result;
}

bool checked_size(uint32_t width, uint32_t height,
                  RasterPixelEncoding encoding, uint64_t& row_stride,
                  size_t& bytes) noexcept {
  const auto bytes_per_sample = sample_bytes(encoding);
  if (width == 0 || height == 0 || bytes_per_sample == 0) return false;
  row_stride = static_cast<uint64_t>(width) * kRgbaChannels * bytes_per_sample;
  if (row_stride > std::numeric_limits<uint64_t>::max() / height) return false;
  const auto total = row_stride * height;
  if (total > std::numeric_limits<size_t>::max()) return false;
  bytes = static_cast<size_t>(total);
  return true;
}

StepResult allocate_frame(uint32_t width, uint32_t height,
                          RasterPixelEncoding encoding,
                          RasterMemoryBudget& budget) {
  uint64_t row_stride = 0;
  size_t byte_count = 0;
  if (!checked_size(width, height, encoding, row_stride, byte_count))
    return {{}, RasterEvaluationStatus::InvalidGraph,
            "Raster edit output dimensions overflow the addressable buffer."};

  try {
    auto reservation = budget.try_reserve(static_cast<uint64_t>(byte_count));
    if (!reservation)
      return {{}, RasterEvaluationStatus::InsufficientMemory,
              "Raster edit output exceeds the available memory budget."};
    RasterPixelBuffer output;
    output.width = width;
    output.height = height;
    output.row_stride = row_stride;
    output.encoding = encoding;
    output.bytes.resize(byte_count);
    output.reservation = std::move(reservation);
    return {std::move(output), RasterEvaluationStatus::Success, {}};
  } catch (const std::bad_alloc&) {
    return {{}, RasterEvaluationStatus::AllocationFailure,
            "Memory allocation failed while evaluating the raster edit."};
  } catch (const std::length_error&) {
    return {{}, RasterEvaluationStatus::AllocationFailure,
            "Raster edit output exceeds the vector allocation range."};
  }
}

bool cancelled(const RasterEvaluationOptions& options) {
  return options.cancelled && options.cancelled();
}

bool cancellation_checkpoint(const RasterEvaluationOptions& options,
                             uint32_t pixel_index) {
  return pixel_index != 0 && pixel_index % kCancelPixelInterval == 0 &&
      cancelled(options);
}

bool copy_row_cancellable(uint8_t* destination, const uint8_t* source,
                           size_t byte_count,
                           const RasterEvaluationOptions& options) {
  constexpr size_t kCopyChunkBytes = 64 * 1024;
  for (size_t offset = 0; offset < byte_count;) {
    if (offset != 0 && cancelled(options)) return false;
    const auto count = (std::min)(kCopyChunkBytes, byte_count - offset);
    std::memcpy(destination + offset, source + offset, count);
    offset += count;
  }
  return true;
}

void report_row(const RasterEvaluationOptions& options, size_t node_index,
                size_t node_count, uint32_t row, uint32_t height) {
  if (options.progress)
    options.progress({node_index, node_count, row + 1, height});
}

StepResult cancelled_step() {
  return {{}, RasterEvaluationStatus::Cancelled,
          "Raster edit evaluation was cancelled."};
}

StepResult copy_source(const RasterPixelView& source,
                       RasterMemoryBudget& budget,
                       const RasterEvaluationOptions& options,
                       size_t node_count) {
  auto output_result = allocate_frame(source.width, source.height,
                                      source.encoding, budget);
  if (!output_result.image) return output_result;
  auto output = std::move(*output_result.image);
  for (uint32_t y = 0; y < source.height; ++y) {
    if (cancelled(options)) return cancelled_step();
    const auto* from = source.bytes.data() +
        static_cast<size_t>(static_cast<uint64_t>(y) * source.row_stride);
    auto* to = output.bytes.data() + static_cast<size_t>(y) * output.row_stride;
    if (!copy_row_cancellable(to, from, static_cast<size_t>(output.row_stride),
                               options))
      return cancelled_step();
    report_row(options, 0, node_count, y, source.height);
  }
  return {std::move(output), RasterEvaluationStatus::Success, {}};
}

template <typename SourceImage>
StepResult crop(const SourceImage& source,
                const document::RasterCropParams& parameters,
                RasterMemoryBudget& budget,
                const RasterEvaluationOptions& options,
                size_t node_index, size_t node_count) {
  const auto map_boundary = [](double normalized, uint32_t extent) {
    const auto rounded = std::llround(normalized * extent);
    return static_cast<uint32_t>(std::clamp<long long>(
        rounded, 0, static_cast<long long>(extent)));
  };
  auto left = map_boundary(parameters.x, source.width);
  auto top = map_boundary(parameters.y, source.height);
  auto right = map_boundary(parameters.x + parameters.width, source.width);
  auto bottom = map_boundary(parameters.y + parameters.height, source.height);
  if (right <= left) right = (std::min)(source.width, left + 1);
  if (bottom <= top) bottom = (std::min)(source.height, top + 1);
  if (right <= left || bottom <= top)
    return {{}, RasterEvaluationStatus::InvalidGraph,
            "Raster crop resolves to an empty pixel rectangle."};

  auto output_result = allocate_frame(right - left, bottom - top,
                                      source.encoding, budget);
  if (!output_result.image) return output_result;
  auto output = std::move(*output_result.image);
  const auto row_bytes = static_cast<size_t>(output.row_stride);
  const auto bytes_per_pixel = static_cast<size_t>(
      kRgbaChannels * sample_bytes(source.encoding));
  const auto x_offset = static_cast<size_t>(left) * bytes_per_pixel;
  for (uint32_t y = 0; y < output.height; ++y) {
    if (cancelled(options)) return cancelled_step();
    const auto* from = source.bytes.data() +
        static_cast<size_t>(top + y) * source.row_stride + x_offset;
    auto* to = output.bytes.data() + static_cast<size_t>(y) * output.row_stride;
    if (!copy_row_cancellable(to, from, row_bytes, options))
      return cancelled_step();
    report_row(options, node_index, node_count, y, output.height);
  }
  return {std::move(output), RasterEvaluationStatus::Success, {}};
}

StepResult flip(const RasterPixelBuffer& source,
                const document::RasterFlipParams& parameters,
                RasterMemoryBudget& budget,
                const RasterEvaluationOptions& options,
                size_t node_index, size_t node_count) {
  if (!parameters.horizontal && !parameters.vertical)
    return {source, RasterEvaluationStatus::Success, {}};
  auto output_result = allocate_frame(source.width, source.height,
                                      source.encoding, budget);
  if (!output_result.image) return output_result;
  auto output = std::move(*output_result.image);
  const auto bytes_per_pixel = static_cast<size_t>(
      kRgbaChannels * sample_bytes(source.encoding));
  for (uint32_t y = 0; y < source.height; ++y) {
    if (cancelled(options)) return cancelled_step();
    const uint32_t source_y = parameters.vertical ? source.height - 1 - y : y;
    const auto* from = source.bytes.data() +
        static_cast<size_t>(source_y) * source.row_stride;
    auto* to = output.bytes.data() + static_cast<size_t>(y) * output.row_stride;
    for (uint32_t x = 0; x < source.width; ++x) {
      if (cancellation_checkpoint(options, x)) return cancelled_step();
      const uint32_t source_x = parameters.horizontal
          ? source.width - 1 - x : x;
      std::memcpy(to + static_cast<size_t>(x) * bytes_per_pixel,
                  from + static_cast<size_t>(source_x) * bytes_per_pixel,
                  bytes_per_pixel);
    }
    report_row(options, node_index, node_count, y, source.height);
  }
  return {std::move(output), RasterEvaluationStatus::Success, {}};
}

double srgb_to_linear(double encoded) noexcept {
  return encoded <= 0.04045 ? encoded / 12.92
                            : std::pow((encoded + 0.055) / 1.055, 2.4);
}

double linear_to_srgb(double value) noexcept {
  value = std::clamp(value, 0.0, 1.0);
  return value <= 0.0031308
      ? 12.92 * value
      : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

double adjustment_mask_weight(const document::RasterAdjustmentMask* mask,
                              uint32_t width, uint32_t height,
                              uint32_t x, uint32_t y) noexcept {
  if (!mask) return 1.0;
  double coverage = 0.0;
  if (mask->kind == document::RasterAdjustmentMaskKind::Bitmap) {
    const size_t index = static_cast<size_t>(y) * width + x;
    coverage = mask->coverage[index] / 255.0;
  } else {
    const double px = static_cast<double>(x) + 0.5;
    const double py = static_cast<double>(y) + 0.5;
    const double left = mask->left * width;
    const double top = mask->top * height;
    const double right = (mask->left + mask->width) * width;
    const double bottom = (mask->top + mask->height) * height;
    if (mask->kind == document::RasterAdjustmentMaskKind::Rectangle) {
      const double dx = (std::max)({left - px, 0.0, px - right});
      const double dy = (std::max)({top - py, 0.0, py - bottom});
      const double outside = std::hypot(dx, dy);
      const double inside = (std::min)({px - left, right - px,
                                        py - top, bottom - py});
      const double signed_distance = outside > 0.0 ? -outside : inside;
      coverage = mask->feather_px <= 0.0
          ? (signed_distance >= 0.0 ? 1.0 : 0.0)
          : std::clamp(0.5 + signed_distance / mask->feather_px, 0.0, 1.0);
      coverage = coverage * coverage * (3.0 - 2.0 * coverage);
    } else if (mask->kind ==
               document::RasterAdjustmentMaskKind::LinearGradient) {
      const double dx = (std::max)({left - px, 0.0, px - right});
      const double dy = (std::max)({top - py, 0.0, py - bottom});
      const double outside = std::hypot(dx, dy);
      const double inside = (std::min)({px - left, right - px,
                                        py - top, bottom - py});
      const double signed_distance = outside > 0.0 ? -outside : inside;
      const double edge = mask->feather_px <= 0.0
          ? (signed_distance >= 0.0 ? 1.0 : 0.0)
          : std::clamp(0.5 + signed_distance / mask->feather_px, 0.0, 1.0);
      const double angle_radians = mask->gradient_angle_degrees *
          (3.14159265358979323846 / 180.0);
      const double direction_x = std::cos(angle_radians);
      const double direction_y = std::sin(angle_radians);
      const double span_x = right - left;
      const double span_y = bottom - top;
      const double minimum_projection =
          (std::min)(0.0, span_x * direction_x) +
          (std::min)(0.0, span_y * direction_y);
      const double maximum_projection =
          (std::max)(0.0, span_x * direction_x) +
          (std::max)(0.0, span_y * direction_y);
      const double projection = (px - left) * direction_x +
          (py - top) * direction_y;
      const double progress = maximum_projection > minimum_projection
          ? std::clamp((projection - minimum_projection) /
                (maximum_projection - minimum_projection), 0.0, 1.0)
          : 0.0;
      coverage = (1.0 - progress) * edge;
    } else if (mask->kind ==
               document::RasterAdjustmentMaskKind::RadialGradient) {
      const double radius_x = (right - left) * 0.5;
      const double radius_y = (bottom - top) * 0.5;
      const double normalized_radius = std::hypot(
          (px - (left + right) * 0.5) / radius_x,
          (py - (top + bottom) * 0.5) / radius_y);
      coverage = std::clamp(1.0 - normalized_radius, 0.0, 1.0);
      if (mask->feather_px > 0.0 && coverage > 0.0) {
        const double minimum_radius = (std::min)(radius_x, radius_y);
        const double exponent = 1.0 / (1.0 + mask->feather_px / minimum_radius);
        coverage = std::pow(coverage, exponent);
      }
    }
  }
  const double effective = mask->inverted ? 1.0 - coverage : coverage;
  return effective * mask->density_percent / 100.0;
}

double masked_value(double original, double adjusted, double weight) noexcept {
  return original + (adjusted - original) * weight;
}

// The evaluator owns this working frame exclusively; a cancelled result is
// discarded, so this point operation can safely avoid a second full-frame copy.
StepResult exposure(RasterPixelBuffer source,
                    const document::RasterExposureParams& parameters,
                    const document::RasterAdjustmentMask* mask,
                    const RasterEvaluationOptions& options,
                    size_t node_index, size_t node_count) {
  if (parameters.exposure_ev == 0.0)
    return {std::move(source), RasterEvaluationStatus::Success, {}};
  const double scale = std::exp2(parameters.exposure_ev);
  const auto bytes_per_sample = static_cast<size_t>(sample_bytes(source.encoding));
  for (uint32_t y = 0; y < source.height; ++y) {
    if (cancelled(options)) return cancelled_step();
    auto* row = source.bytes.data() + static_cast<size_t>(y) * source.row_stride;
    for (uint32_t x = 0; x < source.width; ++x) {
      if (cancellation_checkpoint(options, x)) return cancelled_step();
      const auto offset = static_cast<size_t>(x) * kRgbaChannels *
          bytes_per_sample;
      const double weight = adjustment_mask_weight(
          mask, source.width, source.height, x, y);
      for (size_t channel = 0; channel < 3; ++channel) {
        const auto sample_offset = offset + channel * bytes_per_sample;
        const double original = read_normalized(row + sample_offset, source.encoding);
        const double adjusted = linear_to_srgb(srgb_to_linear(original) * scale);
        write_normalized(row + sample_offset, source.encoding,
            masked_value(original, adjusted, weight));
      }
    }
    report_row(options, node_index, node_count, y, source.height);
  }
  return {std::move(source), RasterEvaluationStatus::Success, {}};
}

StepResult light(RasterPixelBuffer source,
                 const document::RasterLightParams& parameters,
                 const document::RasterAdjustmentMask* mask,
                 const RasterEvaluationOptions& options,
                 size_t node_index, size_t node_count) {
  const auto bytes_per_sample = static_cast<size_t>(sample_bytes(source.encoding));
  const double exposure_scale = std::exp2(parameters.exposure_ev);
  const double contrast_scale = std::exp2(parameters.contrast_percent / 100.0);
  const auto smoothstep = [](double low, double high, double value) noexcept {
    const double amount = std::clamp((value - low) / (high - low), 0.0, 1.0);
    return amount * amount * (3.0 - 2.0 * amount);
  };
  const auto adjust = [&](double encoded) noexcept {
    double value = srgb_to_linear(encoded) * exposure_scale;
    value = 0.18 + (value - 0.18) * contrast_scale;
    const double shadow_weight = 1.0 - smoothstep(0.02, 0.72, value);
    const double highlight_weight = smoothstep(0.28, 0.98, value);
    const double black_weight = 1.0 - smoothstep(0.0, 0.30, value);
    const double white_weight = smoothstep(0.70, 1.0, value);
    value += (parameters.shadows_percent / 100.0) * 0.18 * shadow_weight;
    value -= (parameters.highlights_percent / 100.0) * 0.18 * highlight_weight;
    value += (parameters.blacks_percent / 100.0) * 0.16 * black_weight;
    value += (parameters.whites_percent / 100.0) * 0.16 * white_weight;
    return linear_to_srgb(std::clamp(value, 0.0, 1.0));
  };
  for (uint32_t y = 0; y < source.height; ++y) {
    if (cancelled(options)) return cancelled_step();
    auto* row = source.bytes.data() + static_cast<size_t>(y) * source.row_stride;
    for (uint32_t x = 0; x < source.width; ++x) {
      if (cancellation_checkpoint(options, x)) return cancelled_step();
      const auto offset = static_cast<size_t>(x) * kRgbaChannels * bytes_per_sample;
      const double weight = adjustment_mask_weight(
          mask, source.width, source.height, x, y);
      for (size_t channel = 0; channel < 3; ++channel) {
        auto* sample = row + offset + channel * bytes_per_sample;
        const double original = read_normalized(sample, source.encoding);
        write_normalized(sample, source.encoding,
                         masked_value(original, adjust(original), weight));
      }
    }
    report_row(options, node_index, node_count, y, source.height);
  }
  return {std::move(source), RasterEvaluationStatus::Success, {}};
}

StepResult color_balance(
    RasterPixelBuffer source,
    const document::RasterColorBalanceParams& parameters,
    const document::RasterAdjustmentMask* mask,
    const RasterEvaluationOptions& options,
    size_t node_index, size_t node_count) {
  if (parameters.red_cyan_percent == 0.0 &&
      parameters.green_magenta_percent == 0.0 &&
      parameters.blue_yellow_percent == 0.0) {
    for (uint32_t y = 0; y < source.height; ++y) {
      if (cancelled(options)) return cancelled_step();
      report_row(options, node_index, node_count, y, source.height);
    }
    return {std::move(source), RasterEvaluationStatus::Success, {}};
  }
  const auto bytes_per_sample = static_cast<size_t>(sample_bytes(source.encoding));
  const auto bytes_per_pixel = static_cast<size_t>(kRgbaChannels) * bytes_per_sample;
  const auto smoothstep = [](double low, double high, double value) noexcept {
    const double t = std::clamp((value - low) / (high - low), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
  };
  constexpr std::array<double, 3> luma_weights{0.2126, 0.7152, 0.0722};
  for (uint32_t y = 0; y < source.height; ++y) {
    if (cancelled(options)) return cancelled_step();
    auto* row = source.bytes.data() + static_cast<size_t>(y) * source.row_stride;
    for (uint32_t x = 0; x < source.width; ++x) {
      if (cancellation_checkpoint(options, x)) return cancelled_step();
      auto* pixel = row + static_cast<size_t>(x) * bytes_per_pixel;
      std::array<double, 3> input{};
      for (size_t channel = 0; channel < input.size(); ++channel)
        input[channel] = srgb_to_linear(read_normalized(
            pixel + channel * bytes_per_sample, source.encoding));
      const double original_luma = input[0] * luma_weights[0] +
          input[1] * luma_weights[1] + input[2] * luma_weights[2];
      double range_weight = 0.0;
      switch (parameters.range) {
      case document::RasterTonalRange::Shadows:
        range_weight = 1.0 - smoothstep(0.05, 0.72, original_luma);
        break;
      case document::RasterTonalRange::Midtones:
        range_weight = std::clamp(1.0 - std::abs(original_luma - 0.5) / 0.5,
                                  0.0, 1.0);
        break;
      case document::RasterTonalRange::Highlights:
        range_weight = smoothstep(0.28, 0.95, original_luma);
        break;
      }
      const double red_cyan = parameters.red_cyan_percent * 0.01 * range_weight;
      const double green_magenta =
          parameters.green_magenta_percent * 0.01 * range_weight;
      const double blue_yellow =
          parameters.blue_yellow_percent * 0.01 * range_weight;
      std::array<double, 3> adjusted{
          input[0] + red_cyan * 0.25 - green_magenta * 0.125 - blue_yellow * 0.125,
          input[1] - red_cyan * 0.125 + green_magenta * 0.25 - blue_yellow * 0.125,
          input[2] - red_cyan * 0.125 - green_magenta * 0.125 + blue_yellow * 0.25};
      for (auto& value : adjusted) value = std::clamp(value, 0.0, 1.0);
      if (parameters.preserve_luminosity) {
        const double adjusted_luma = adjusted[0] * luma_weights[0] +
            adjusted[1] * luma_weights[1] + adjusted[2] * luma_weights[2];
        if (adjusted_luma > 0.0) {
          const double scale = original_luma / adjusted_luma;
          for (auto& value : adjusted)
            value = std::clamp(value * scale, 0.0, 1.0);
        }
      }
      const double mask_weight = adjustment_mask_weight(
          mask, source.width, source.height, x, y);
      for (size_t channel = 0; channel < adjusted.size(); ++channel) {
        auto* sample = pixel + channel * bytes_per_sample;
        const double original = read_normalized(sample, source.encoding);
        write_normalized(sample, source.encoding, masked_value(
            original, linear_to_srgb(adjusted[channel]), mask_weight));
      }
    }
    report_row(options, node_index, node_count, y, source.height);
  }
  return {std::move(source), RasterEvaluationStatus::Success, {}};
}

StepResult channel_mixer(RasterPixelBuffer source,
                        const document::RasterChannelMixerParams& parameters,
                        const document::RasterAdjustmentMask* mask,
                        const RasterEvaluationOptions& options,
                        size_t node_index, size_t node_count) {
  const auto bytes_per_sample = static_cast<size_t>(sample_bytes(source.encoding));
  const auto bytes_per_pixel = static_cast<size_t>(kRgbaChannels) * bytes_per_sample;
  for (uint32_t y = 0; y < source.height; ++y) {
    if (cancelled(options)) return cancelled_step();
    auto* row = source.bytes.data() + static_cast<size_t>(y) * source.row_stride;
    for (uint32_t x = 0; x < source.width; ++x) {
      if (cancellation_checkpoint(options, x)) return cancelled_step();
      auto* pixel = row + static_cast<size_t>(x) * bytes_per_pixel;
      std::array<double, 3> input{};
      for (size_t channel = 0; channel < input.size(); ++channel)
        input[channel] = srgb_to_linear(read_normalized(
            pixel + channel * bytes_per_sample, source.encoding));
      const double weight = adjustment_mask_weight(
          mask, source.width, source.height, x, y);
      for (size_t output = 0; output < input.size(); ++output) {
        double mixed = 0.0;
        for (size_t component = 0; component < input.size(); ++component)
          mixed += parameters.coefficients[output * 3 + component] * input[component];
        const auto offset = output * bytes_per_sample;
        const double original = read_normalized(pixel + offset, source.encoding);
        const double adjusted = linear_to_srgb(std::clamp(mixed, 0.0, 1.0));
        write_normalized(pixel + offset, source.encoding,
                         masked_value(original, adjusted, weight));
      }
    }
    report_row(options, node_index, node_count, y, source.height);
  }
  return {std::move(source), RasterEvaluationStatus::Success, {}};
}

StepResult clone_stamp(RasterPixelBuffer source,
                       const document::RasterCloneStampParams& parameters,
                       RasterMemoryBudget& budget,
                       const RasterEvaluationOptions& options,
                       size_t node_index, size_t node_count) {
  if (parameters.opacity_percent == 0.0)
    return {std::move(source), RasterEvaluationStatus::Success, {}};

  const double shorter_edge = static_cast<double>(
      (std::min)(source.width, source.height));
  const double radius = parameters.radius_percent * 0.01 * shorter_edge;
  const double target_x = parameters.target_x * (source.width - 1.0);
  const double target_y = parameters.target_y * (source.height - 1.0);
  const double sample_x = parameters.source_x * (source.width - 1.0);
  const double sample_y = parameters.source_y * (source.height - 1.0);
  const auto clip_x = [width = source.width](double value) {
    return static_cast<uint32_t>(std::clamp(value, 0.0,
        static_cast<double>(width - 1)));
  };
  const auto clip_y = [height = source.height](double value) {
    return static_cast<uint32_t>(std::clamp(value, 0.0,
        static_cast<double>(height - 1)));
  };
  const uint32_t target_left = clip_x(std::floor(target_x - radius));
  const uint32_t target_right = clip_x(std::ceil(target_x + radius));
  const uint32_t target_top = clip_y(std::floor(target_y - radius));
  const uint32_t target_bottom = clip_y(std::ceil(target_y + radius));
  const uint32_t patch_left = clip_x(std::floor(sample_x - radius - 1.0));
  const uint32_t patch_right = clip_x(std::ceil(sample_x + radius + 1.0));
  const uint32_t patch_top = clip_y(std::floor(sample_y - radius - 1.0));
  const uint32_t patch_bottom = clip_y(std::ceil(sample_y + radius + 1.0));
  const uint32_t patch_width = patch_right - patch_left + 1;
  const uint32_t patch_height = patch_bottom - patch_top + 1;
  uint64_t patch_stride = 0;
  size_t patch_bytes = 0;
  if (!checked_size(patch_width, patch_height, source.encoding,
                    patch_stride, patch_bytes) ||
      patch_bytes > kMaximumClonePatchBytes)
    return {{}, RasterEvaluationStatus::InsufficientMemory,
            "Clone Stamp source patch exceeds its bounded scratch-memory limit."};
  auto reservation = budget.try_reserve(patch_bytes);
  if (!reservation)
    return {{}, RasterEvaluationStatus::InsufficientMemory,
            "Clone Stamp source patch exceeds the available raster memory budget."};

  std::vector<uint8_t> source_patch;
  try {
    source_patch.resize(patch_bytes);
  } catch (const std::bad_alloc&) {
    return {{}, RasterEvaluationStatus::AllocationFailure,
            "Memory allocation failed for the Clone Stamp source patch."};
  } catch (const std::length_error&) {
    return {{}, RasterEvaluationStatus::AllocationFailure,
            "Clone Stamp source patch exceeds the vector allocation range."};
  }
  const size_t bytes_per_sample = static_cast<size_t>(sample_bytes(source.encoding));
  const size_t bytes_per_pixel = kRgbaChannels * bytes_per_sample;
  const size_t patch_row_bytes = static_cast<size_t>(patch_width) * bytes_per_pixel;
  const size_t source_x_offset = static_cast<size_t>(patch_left) * bytes_per_pixel;
  for (uint32_t row = 0; row < patch_height; ++row) {
    if (cancelled(options)) return cancelled_step();
    const auto* source_row = source.bytes.data() +
        static_cast<size_t>(patch_top + row) * source.row_stride + source_x_offset;
    auto* patch_row = source_patch.data() +
        static_cast<size_t>(row) * static_cast<size_t>(patch_stride);
    for (size_t copied = 0; copied < patch_row_bytes;) {
      if (copied != 0 && cancelled(options)) return cancelled_step();
      const size_t count = (std::min)(static_cast<size_t>(64u * 1024u), patch_row_bytes - copied);
      std::memcpy(patch_row + copied, source_row + copied, count);
      copied += count;
    }
  }

  auto patch_sample = [&](double x, double y, std::array<double, 4>& rgba) {
    if (x < 0.0 || y < 0.0 || x > source.width - 1.0 ||
        y > source.height - 1.0) return false;
    const double local_x = x - patch_left;
    const double local_y = y - patch_top;
    if (local_x < 0.0 || local_y < 0.0 ||
        local_x > patch_width - 1.0 || local_y > patch_height - 1.0)
      return false;
    const uint32_t x0 = static_cast<uint32_t>(std::floor(local_x));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(local_y));
    const uint32_t x1 = (std::min)(x0 + 1, patch_width - 1);
    const uint32_t y1 = (std::min)(y0 + 1, patch_height - 1);
    const double wx = local_x - x0;
    const double wy = local_y - y0;
    std::array<double, 4> premultiplied{};
    for (const auto [px, py, weight] : {
        std::tuple{x0, y0, (1.0 - wx) * (1.0 - wy)},
        std::tuple{x1, y0, wx * (1.0 - wy)},
        std::tuple{x0, y1, (1.0 - wx) * wy},
        std::tuple{x1, y1, wx * wy}}) {
      const size_t offset = static_cast<size_t>(py) * patch_row_bytes +
          static_cast<size_t>(px) * bytes_per_pixel;
      const double alpha = read_normalized(source_patch.data() + offset +
          3 * bytes_per_sample, source.encoding);
      for (size_t channel = 0; channel < 3; ++channel)
        premultiplied[channel] += read_normalized(source_patch.data() + offset +
            channel * bytes_per_sample, source.encoding) * alpha * weight;
      premultiplied[3] += alpha * weight;
    }
    rgba[3] = premultiplied[3];
    for (size_t channel = 0; channel < 3; ++channel)
      rgba[channel] = rgba[3] > 0.0 ? premultiplied[channel] / rgba[3] : 0.0;
    return true;
  };

  const double hardness = parameters.hardness_percent * 0.01;
  const double opacity = parameters.opacity_percent * 0.01;
  const double inner_radius = radius * hardness;
  const double soft_span = radius - inner_radius;
  for (uint32_t y = target_top; y <= target_bottom; ++y) {
    if (cancelled(options)) return cancelled_step();
    for (uint32_t x = target_left; x <= target_right; ++x) {
      if (cancellation_checkpoint(options, x)) return cancelled_step();
      const double dx = x - target_x;
      const double dy = y - target_y;
      const double distance = std::sqrt(dx * dx + dy * dy);
      if (distance > radius) continue;
      double brush_weight = 1.0;
      if (soft_span > 0.0 && distance > inner_radius) {
        const double t = std::clamp((distance - inner_radius) / soft_span, 0.0, 1.0);
        brush_weight = 1.0 - t * t * (3.0 - 2.0 * t);
      }
      const double weight = brush_weight * opacity;
      if (weight <= 0.0) continue;
      std::array<double, 4> sampled{};
      if (!patch_sample(sample_x + dx, sample_y + dy, sampled)) continue;
      auto* destination = source.bytes.data() +
          static_cast<size_t>(y) * source.row_stride +
          static_cast<size_t>(x) * bytes_per_pixel;
      const double destination_alpha = read_normalized(destination +
          3 * bytes_per_sample, source.encoding);
      const double output_alpha = sampled[3] * weight + destination_alpha * (1.0 - weight);
      for (size_t channel = 0; channel < 3; ++channel) {
        const double destination_color = read_normalized(destination +
            channel * bytes_per_sample, source.encoding);
        const double premultiplied = sampled[channel] * sampled[3] * weight +
            destination_color * destination_alpha * (1.0 - weight);
        write_normalized(destination + channel * bytes_per_sample, source.encoding,
            output_alpha > 0.0 ? premultiplied / output_alpha : 0.0);
      }
      write_normalized(destination + 3 * bytes_per_sample, source.encoding,
                       output_alpha);
    }
    report_row(options, node_index, node_count, y - target_top,
               target_bottom - target_top + 1);
  }
  return {std::move(source), RasterEvaluationStatus::Success, {}};
}

StepResult levels(RasterPixelBuffer source,
                  const document::RasterLevelsParams& parameters,
                  const document::RasterAdjustmentMask* mask,
                  const RasterEvaluationOptions& options,
                  size_t node_index, size_t node_count) {
  const auto bytes_per_sample = static_cast<size_t>(sample_bytes(source.encoding));
  const auto bytes_per_pixel = static_cast<size_t>(kRgbaChannels) * bytes_per_sample;
  const size_t first_channel = parameters.channel == document::RasterToneChannel::Composite
      ? 0 : static_cast<size_t>(parameters.channel) - 1;
  const size_t last_channel = parameters.channel == document::RasterToneChannel::Composite
      ? 3 : first_channel + 1;
  const double inverse_gamma = 1.0 / parameters.gamma;
  const double input_span = parameters.white_in - parameters.black_in;
  const double output_span = parameters.white_out - parameters.black_out;
  for (uint32_t y = 0; y < source.height; ++y) {
    if (cancelled(options)) return cancelled_step();
    auto* row = source.bytes.data() + static_cast<size_t>(y) * source.row_stride;
    for (uint32_t x = 0; x < source.width; ++x) {
      if (cancellation_checkpoint(options, x)) return cancelled_step();
      auto* pixel = row + static_cast<size_t>(x) * bytes_per_pixel;
      const double weight = adjustment_mask_weight(
          mask, source.width, source.height, x, y);
      for (size_t channel = first_channel; channel < last_channel; ++channel) {
        const auto offset = channel * bytes_per_sample;
        const double normalized = read_normalized(pixel + offset, source.encoding);
        const double input = std::clamp(
            (normalized - parameters.black_in) / input_span, 0.0, 1.0);
        const double adjusted = parameters.black_out +
            output_span * std::pow(input, inverse_gamma);
        write_normalized(pixel + offset, source.encoding,
                         masked_value(normalized, adjusted, weight));
      }
    }
    report_row(options, node_index, node_count, y, source.height);
  }
  return {std::move(source), RasterEvaluationStatus::Success, {}};
}

StepResult curves(RasterPixelBuffer source,
                  const document::RasterCurvesParams& parameters,
                  const document::RasterAdjustmentMask* mask,
                  const RasterEvaluationOptions& options,
                  size_t node_index, size_t node_count) {
  const auto bytes_per_sample = static_cast<size_t>(sample_bytes(source.encoding));
  const auto bytes_per_pixel = static_cast<size_t>(kRgbaChannels) * bytes_per_sample;
  const size_t first_channel = parameters.channel == document::RasterToneChannel::Composite
      ? 0 : static_cast<size_t>(parameters.channel) - 1;
  const size_t last_channel = parameters.channel == document::RasterToneChannel::Composite
      ? 3 : first_channel + 1;
  const auto evaluate_curve = [&](double input) {
    const double position = std::clamp(input, 0.0, 1.0) * 4.0;
    const size_t segment = (std::min)(static_cast<size_t>(position), size_t{3});
    const double amount = position - static_cast<double>(segment);
    return parameters.output_points[segment] * (1.0 - amount) +
        parameters.output_points[segment + 1] * amount;
  };
  for (uint32_t y = 0; y < source.height; ++y) {
    if (cancelled(options)) return cancelled_step();
    auto* row = source.bytes.data() + static_cast<size_t>(y) * source.row_stride;
    for (uint32_t x = 0; x < source.width; ++x) {
      if (cancellation_checkpoint(options, x)) return cancelled_step();
      auto* pixel = row + static_cast<size_t>(x) * bytes_per_pixel;
      const double weight = adjustment_mask_weight(
          mask, source.width, source.height, x, y);
      for (size_t channel = first_channel; channel < last_channel; ++channel) {
        const size_t offset = channel * bytes_per_sample;
        const double original = read_normalized(pixel + offset, source.encoding);
        write_normalized(pixel + offset, source.encoding,
            masked_value(original, evaluate_curve(original), weight));
      }
    }
    report_row(options, node_index, node_count, y, source.height);
  }
  return {std::move(source), RasterEvaluationStatus::Success, {}};
}

StepResult white_balance(RasterPixelBuffer source,
                         const document::RasterWhiteBalanceParams& parameters,
                         const document::RasterAdjustmentMask* mask,
                         const RasterEvaluationOptions& options,
                         size_t node_index, size_t node_count) {
  const auto bytes_per_sample = static_cast<size_t>(sample_bytes(source.encoding));
  const auto bytes_per_pixel = static_cast<size_t>(kRgbaChannels) * bytes_per_sample;
  const double temperature = parameters.temperature_shift_percent / 100.0;
  const double tint = parameters.tint_percent / 100.0;
  const std::array<double, 3> gains{
      std::exp2(temperature * 0.5 + tint * 0.15),
      std::exp2(-tint * 0.30),
      std::exp2(-temperature * 0.5 + tint * 0.15)};
  for (uint32_t y = 0; y < source.height; ++y) {
    if (cancelled(options)) return cancelled_step();
    auto* row = source.bytes.data() + static_cast<size_t>(y) * source.row_stride;
    for (uint32_t x = 0; x < source.width; ++x) {
      if (cancellation_checkpoint(options, x)) return cancelled_step();
      auto* pixel = row + static_cast<size_t>(x) * bytes_per_pixel;
      const double weight = adjustment_mask_weight(
          mask, source.width, source.height, x, y);
      for (size_t channel = 0; channel < gains.size(); ++channel) {
        const size_t offset = channel * bytes_per_sample;
        const double original = read_normalized(pixel + offset, source.encoding);
        const double adjusted = linear_to_srgb(srgb_to_linear(original) * gains[channel]);
        write_normalized(pixel + offset, source.encoding,
            masked_value(original, adjusted, weight));
      }
    }
    report_row(options, node_index, node_count, y, source.height);
  }
  return {std::move(source), RasterEvaluationStatus::Success, {}};
}

struct HslColor {
  double hue{0.0};
  double saturation{0.0};
  double lightness{0.0};
};

HslColor encoded_rgb_to_hsl(double red, double green, double blue) noexcept {
  const double maximum = (std::max)({red, green, blue});
  const double minimum = (std::min)({red, green, blue});
  const double delta = maximum - minimum;
  HslColor result;
  result.lightness = (maximum + minimum) * 0.5;
  if (delta <= 0.0) return result;
  result.saturation = delta / (1.0 - std::abs(2.0 * result.lightness - 1.0));
  if (maximum == red)
    result.hue = 60.0 * std::fmod((green - blue) / delta, 6.0);
  else if (maximum == green)
    result.hue = 60.0 * ((blue - red) / delta + 2.0);
  else
    result.hue = 60.0 * ((red - green) / delta + 4.0);
  if (result.hue < 0.0) result.hue += 360.0;
  return result;
}

std::array<double, 3> hsl_to_encoded_rgb(const HslColor& color) noexcept {
  const double chroma = (1.0 - std::abs(2.0 * color.lightness - 1.0)) *
      color.saturation;
  const double section = color.hue / 60.0;
  const double secondary = chroma *
      (1.0 - std::abs(std::fmod(section, 2.0) - 1.0));
  double red = 0.0, green = 0.0, blue = 0.0;
  if (section < 1.0) { red = chroma; green = secondary; }
  else if (section < 2.0) { red = secondary; green = chroma; }
  else if (section < 3.0) { green = chroma; blue = secondary; }
  else if (section < 4.0) { green = secondary; blue = chroma; }
  else if (section < 5.0) { red = secondary; blue = chroma; }
  else { red = chroma; blue = secondary; }
  const double offset = color.lightness - chroma * 0.5;
  return {red + offset, green + offset, blue + offset};
}

StepResult hsl(RasterPixelBuffer source,
               const document::RasterHslParams& parameters,
               const document::RasterAdjustmentMask* mask,
               const RasterEvaluationOptions& options,
               size_t node_index, size_t node_count) {
  const auto bytes_per_sample = static_cast<size_t>(sample_bytes(source.encoding));
  const auto bytes_per_pixel = static_cast<size_t>(kRgbaChannels) * bytes_per_sample;
  for (uint32_t y = 0; y < source.height; ++y) {
    if (cancelled(options)) return cancelled_step();
    auto* row = source.bytes.data() + static_cast<size_t>(y) * source.row_stride;
    for (uint32_t x = 0; x < source.width; ++x) {
      if (cancellation_checkpoint(options, x)) return cancelled_step();
      auto* pixel = row + static_cast<size_t>(x) * bytes_per_pixel;
      const auto color = encoded_rgb_to_hsl(
          read_normalized(pixel, source.encoding),
          read_normalized(pixel + bytes_per_sample, source.encoding),
          read_normalized(pixel + 2 * bytes_per_sample, source.encoding));
      double hue = std::fmod(color.hue + parameters.hue_degrees, 360.0);
      if (hue < 0.0) hue += 360.0;
      const double saturation = std::clamp(color.saturation *
          (1.0 + parameters.saturation_percent / 100.0), 0.0, 1.0);
      const double lightness_delta = parameters.lightness_percent / 100.0;
      const double lightness = lightness_delta >= 0.0
          ? color.lightness + (1.0 - color.lightness) * lightness_delta
          : color.lightness * (1.0 + lightness_delta);
      const auto rgb = hsl_to_encoded_rgb({hue, saturation, lightness});
      const double mask_weight = adjustment_mask_weight(
          mask, source.width, source.height, x, y);
      for (size_t channel = 0; channel < rgb.size(); ++channel)
        write_normalized(pixel + channel * bytes_per_sample,
            source.encoding, masked_value(read_normalized(
                pixel + channel * bytes_per_sample, source.encoding), rgb[channel], mask_weight));
    }
    report_row(options, node_index, node_count, y, source.height);
  }
  return {std::move(source), RasterEvaluationStatus::Success, {}};
}

StepResult vibrance(RasterPixelBuffer source,
                    const document::RasterVibranceParams& parameters,
                    const document::RasterAdjustmentMask* mask,
                    const RasterEvaluationOptions& options,
                    size_t node_index, size_t node_count) {
  if (parameters.amount_percent == 0.0)
    return {std::move(source), RasterEvaluationStatus::Success, {}};
  const auto bytes_per_sample = static_cast<size_t>(sample_bytes(source.encoding));
  const auto bytes_per_pixel = static_cast<size_t>(kRgbaChannels) * bytes_per_sample;
  const double amount = parameters.amount_percent / 100.0;
  for (uint32_t y = 0; y < source.height; ++y) {
    if (cancelled(options)) return cancelled_step();
    auto* row = source.bytes.data() + static_cast<size_t>(y) * source.row_stride;
    for (uint32_t x = 0; x < source.width; ++x) {
      if (cancellation_checkpoint(options, x)) return cancelled_step();
      auto* pixel = row + static_cast<size_t>(x) * bytes_per_pixel;
      const auto color = encoded_rgb_to_hsl(
          read_normalized(pixel, source.encoding),
          read_normalized(pixel + bytes_per_sample, source.encoding),
          read_normalized(pixel + 2 * bytes_per_sample, source.encoding));
      if (color.saturation == 0.0) {
        // Hue is undefined for neutral grays; do not invent chroma for them.
        continue;
      }
      // Positive adjustments move muted colors farther than already vivid
      // colors; negative adjustments reduce all chroma proportionally.
      const double saturation = amount >= 0.0
          ? color.saturation + (1.0 - color.saturation) * amount
          : color.saturation * (1.0 + amount);
      const auto rgb = hsl_to_encoded_rgb(
          {color.hue, std::clamp(saturation, 0.0, 1.0), color.lightness});
      const double mask_weight = adjustment_mask_weight(
          mask, source.width, source.height, x, y);
      for (size_t channel = 0; channel < rgb.size(); ++channel)
        write_normalized(pixel + channel * bytes_per_sample,
            source.encoding, masked_value(read_normalized(
                pixel + channel * bytes_per_sample, source.encoding), rgb[channel], mask_weight));
    }
    report_row(options, node_index, node_count, y, source.height);
  }
  return {std::move(source), RasterEvaluationStatus::Success, {}};
}

double selective_hue_weight(double hue_degrees,
                            document::RasterHueRange range,
                            double feather_percent) noexcept {
  constexpr double kHalfRangeDegrees = 30.0;
  const double center = static_cast<double>(static_cast<uint8_t>(range)) * 60.0;
  double distance = std::abs(hue_degrees - center);
  distance = std::fmod(distance, 360.0);
  distance = (std::min)(distance, 360.0 - distance);
  if (feather_percent <= 0.0)
    return distance < kHalfRangeDegrees ? 1.0 : 0.0;
  const double feather = kHalfRangeDegrees * feather_percent / 100.0;
  const double solid_range = kHalfRangeDegrees - feather;
  if (distance <= solid_range) return 1.0;
  if (distance >= kHalfRangeDegrees) return 0.0;
  const double t = (kHalfRangeDegrees - distance) / feather;
  return t * t * (3.0 - 2.0 * t);
}

StepResult selective_hsl(RasterPixelBuffer source,
                         const document::RasterSelectiveHslParams& parameters,
                         const document::RasterAdjustmentMask* mask,
                         const RasterEvaluationOptions& options,
                         size_t node_index, size_t node_count) {
  const auto bytes_per_sample = static_cast<size_t>(sample_bytes(source.encoding));
  const auto bytes_per_pixel = static_cast<size_t>(kRgbaChannels) * bytes_per_sample;
  for (uint32_t y = 0; y < source.height; ++y) {
    if (cancelled(options)) return cancelled_step();
    auto* row = source.bytes.data() + static_cast<size_t>(y) * source.row_stride;
    for (uint32_t x = 0; x < source.width; ++x) {
      if (cancellation_checkpoint(options, x)) return cancelled_step();
      auto* pixel = row + static_cast<size_t>(x) * bytes_per_pixel;
      const auto color = encoded_rgb_to_hsl(
          read_normalized(pixel, source.encoding),
          read_normalized(pixel + bytes_per_sample, source.encoding),
          read_normalized(pixel + 2 * bytes_per_sample, source.encoding));
      if (color.saturation == 0.0) {
        continue;
      }
      const double weight = selective_hue_weight(
          color.hue, parameters.range, parameters.feather_percent);
      if (weight == 0.0) {
        continue;
      }
      double hue = std::fmod(
          color.hue + parameters.hue_degrees * weight, 360.0);
      if (hue < 0.0) hue += 360.0;
      const double saturation = std::clamp(color.saturation *
          (1.0 + parameters.saturation_percent / 100.0 * weight), 0.0, 1.0);
      const double lightness_delta = parameters.lightness_percent / 100.0 * weight;
      const double lightness = lightness_delta >= 0.0
          ? color.lightness + (1.0 - color.lightness) * lightness_delta
          : color.lightness * (1.0 + lightness_delta);
      const auto rgb = hsl_to_encoded_rgb({hue, saturation, lightness});
      const double mask_weight = adjustment_mask_weight(
          mask, source.width, source.height, x, y);
      for (size_t channel = 0; channel < rgb.size(); ++channel)
        write_normalized(pixel + channel * bytes_per_sample,
            source.encoding, masked_value(read_normalized(
                pixel + channel * bytes_per_sample, source.encoding), rgb[channel], mask_weight));
    }
    report_row(options, node_index, node_count, y, source.height);
  }
  return {std::move(source), RasterEvaluationStatus::Success, {}};
}

StepResult rotate_right_angle(const RasterPixelBuffer& source, int angle,
                              RasterMemoryBudget& budget,
                              const RasterEvaluationOptions& options,
                              size_t node_index, size_t node_count) {
  if (angle == 0) return {source, RasterEvaluationStatus::Success, {}};
  const bool quarter_turn = angle == 90 || angle == 270;
  const uint32_t width = quarter_turn ? source.height : source.width;
  const uint32_t height = quarter_turn ? source.width : source.height;
  auto output_result = allocate_frame(width, height, source.encoding, budget);
  if (!output_result.image) return output_result;
  auto output = std::move(*output_result.image);
  for (uint32_t y = 0; y < source.height; ++y) {
    if (cancelled(options)) return cancelled_step();
    for (uint32_t x = 0; x < source.width; ++x) {
      if (cancellation_checkpoint(options, x)) return cancelled_step();
      uint32_t target_x = x;
      uint32_t target_y = y;
      if (angle == 90) {
        target_x = source.height - 1 - y;
        target_y = x;
      } else if (angle == 180) {
        target_x = source.width - 1 - x;
        target_y = source.height - 1 - y;
      } else if (angle == 270) {
        target_x = y;
        target_y = source.width - 1 - x;
      }
      const auto* from = source.bytes.data() +
          static_cast<size_t>(y) * source.row_stride +
          static_cast<size_t>(x) * kRgbaChannels * sample_bytes(source.encoding);
      auto* to = output.bytes.data() +
          static_cast<size_t>(target_y) * output.row_stride +
          static_cast<size_t>(target_x) * kRgbaChannels *
              sample_bytes(source.encoding);
      std::memcpy(to, from, static_cast<size_t>(kRgbaChannels *
          sample_bytes(source.encoding)));
    }
    report_row(options, node_index, node_count, y, source.height);
  }
  return {std::move(output), RasterEvaluationStatus::Success, {}};
}

struct LinearPremultipliedPixel {
  std::array<double, 3> rgb{};
  double alpha{0.0};
};

LinearPremultipliedPixel sample_bilinear(const RasterPixelBuffer& source,
                                         double x, double y) noexcept {
  const int64_t left = static_cast<int64_t>(std::floor(x));
  const int64_t top = static_cast<int64_t>(std::floor(y));
  const double fx = x - static_cast<double>(left);
  const double fy = y - static_cast<double>(top);
  LinearPremultipliedPixel result;
  for (int dy = 0; dy <= 1; ++dy) {
    const int64_t source_y = top + dy;
    if (source_y < 0 || source_y >= source.height) continue;
    const double wy = dy == 0 ? 1.0 - fy : fy;
    for (int dx = 0; dx <= 1; ++dx) {
      const int64_t source_x = left + dx;
      if (source_x < 0 || source_x >= source.width) continue;
      const double wx = dx == 0 ? 1.0 - fx : fx;
      const double weight = wx * wy;
      const auto* pixel = source.bytes.data() +
          static_cast<size_t>(source_y) * source.row_stride +
          static_cast<size_t>(source_x) * kRgbaChannels *
              sample_bytes(source.encoding);
      const auto bytes_per_sample = static_cast<size_t>(
          sample_bytes(source.encoding));
      const double alpha = read_normalized(
          pixel + 3 * bytes_per_sample, source.encoding);
      result.alpha += alpha * weight;
      for (size_t channel = 0; channel < 3; ++channel)
        result.rgb[channel] += srgb_to_linear(read_normalized(
            pixel + channel * bytes_per_sample, source.encoding)) *
                alpha * weight;
    }
  }
  return result;
}

StepResult rotate_arbitrary(const RasterPixelBuffer& source, double degrees,
                            RasterMemoryBudget& budget,
                            const RasterEvaluationOptions& options,
                            size_t node_index, size_t node_count) {
  const double radians = degrees * (std::acos(-1.0) / 180.0);
  const double cosine = std::cos(radians);
  const double sine = std::sin(radians);
  const double width_f = std::ceil(std::abs(source.width * cosine) +
                                   std::abs(source.height * sine));
  const double height_f = std::ceil(std::abs(source.width * sine) +
                                    std::abs(source.height * cosine));
  if (!std::isfinite(width_f) || !std::isfinite(height_f) || width_f < 1.0 ||
      height_f < 1.0 || width_f > std::numeric_limits<uint32_t>::max() ||
      height_f > std::numeric_limits<uint32_t>::max())
    return {{}, RasterEvaluationStatus::InvalidGraph,
            "Raster rotation output dimensions are outside the supported range."};
  const auto width = static_cast<uint32_t>(width_f);
  const auto height = static_cast<uint32_t>(height_f);
  auto output_result = allocate_frame(width, height, source.encoding, budget);
  if (!output_result.image) return output_result;
  auto output = std::move(*output_result.image);
  const double source_center_x = static_cast<double>(source.width) / 2.0;
  const double source_center_y = static_cast<double>(source.height) / 2.0;
  const double target_center_x = static_cast<double>(width) / 2.0;
  const double target_center_y = static_cast<double>(height) / 2.0;
  for (uint32_t y = 0; y < height; ++y) {
    if (cancelled(options)) return cancelled_step();
    for (uint32_t x = 0; x < width; ++x) {
      if (cancellation_checkpoint(options, x)) return cancelled_step();
      const double dx = static_cast<double>(x) + 0.5 - target_center_x;
      const double dy = static_cast<double>(y) + 0.5 - target_center_y;
      const double source_x = cosine * dx + sine * dy + source_center_x - 0.5;
      const double source_y = -sine * dx + cosine * dy + source_center_y - 0.5;
      const auto sample = sample_bilinear(source, source_x, source_y);
      auto* pixel = output.bytes.data() +
          static_cast<size_t>(y) * output.row_stride +
          static_cast<size_t>(x) * kRgbaChannels *
              sample_bytes(source.encoding);
      const auto bytes_per_sample = static_cast<size_t>(
          sample_bytes(source.encoding));
      const double alpha = std::clamp(sample.alpha, 0.0, 1.0);
      if (alpha > 0.0) {
        for (size_t channel = 0; channel < 3; ++channel)
          write_normalized(pixel + channel * bytes_per_sample, source.encoding,
              linear_to_srgb(sample.rgb[channel] / alpha));
        write_normalized(pixel + 3 * bytes_per_sample, source.encoding, alpha);
      }
    }
    report_row(options, node_index, node_count, y, height);
  }
  return {std::move(output), RasterEvaluationStatus::Success, {}};
}

StepResult rotate(const RasterPixelBuffer& source,
                  const document::RasterRotateParams& parameters,
                  RasterMemoryBudget& budget,
                  const RasterEvaluationOptions& options,
                  size_t node_index, size_t node_count) {
  double normalized = std::fmod(parameters.degrees, 360.0);
  if (normalized < 0.0) normalized += 360.0;
  if (normalized == 0.0)
    return {source, RasterEvaluationStatus::Success, {}};
  if (normalized == 90.0 || normalized == 180.0 || normalized == 270.0)
    return rotate_right_angle(source, static_cast<int>(normalized), budget,
                              options, node_index, node_count);
  return rotate_arbitrary(source, normalized, budget, options, node_index,
                          node_count);
}

bool valid_graph(const document::RasterEditGraph& graph,
                 std::string& diagnostic) {
  if (graph.schema_version != document::RasterEditGraph::kSchemaVersion ||
      graph.nodes.size() > document::RasterEditGraph::kMaxNodes) {
    diagnostic = "Raster edit graph schema or node count is unsupported.";
    return false;
  }
  std::unordered_set<uint64_t> ids;
  ids.reserve(graph.nodes.size());
  for (const auto& node : graph.nodes) {
    if (!node.id.valid() || node.algorithm_version != 1 ||
        !ids.insert(node.id.value).second) {
      diagnostic = "Raster edit graph contains a missing or duplicate node ID.";
      return false;
    }
    const bool valid = std::visit([](const auto& parameters) {
      using T = std::decay_t<decltype(parameters)>;
      if constexpr (std::is_same_v<T, document::RasterCropParams>) {
        return std::isfinite(parameters.x) && std::isfinite(parameters.y) &&
            std::isfinite(parameters.width) &&
            std::isfinite(parameters.height) && parameters.x >= 0.0 &&
            parameters.y >= 0.0 && parameters.width > 0.0 &&
            parameters.height > 0.0 && parameters.x + parameters.width <= 1.0 &&
            parameters.y + parameters.height <= 1.0;
      } else if constexpr (std::is_same_v<T, document::RasterFlipParams>) {
        return true;
      } else if constexpr (std::is_same_v<T, document::RasterRotateParams>) {
        return std::isfinite(parameters.degrees) &&
            std::abs(parameters.degrees) <= 360000.0;
      } else if constexpr (std::is_same_v<T, document::RasterExposureParams>) {
        return std::isfinite(parameters.exposure_ev) &&
            std::abs(parameters.exposure_ev) <= 10.0;
      } else if constexpr (std::is_same_v<T, document::RasterLevelsParams>) {
        const auto channel = static_cast<uint8_t>(parameters.channel);
        return channel <= static_cast<uint8_t>(document::RasterToneChannel::Blue) &&
            std::isfinite(parameters.black_in) &&
            std::isfinite(parameters.white_in) && std::isfinite(parameters.gamma) &&
            std::isfinite(parameters.black_out) &&
            std::isfinite(parameters.white_out) && parameters.black_in >= 0.0 &&
            parameters.white_in <= 1.0 && parameters.black_in < parameters.white_in &&
            parameters.gamma >= 0.1 && parameters.gamma <= 10.0 &&
            parameters.black_out >= 0.0 && parameters.white_out <= 1.0 &&
            parameters.black_out <= parameters.white_out;
      } else if constexpr (std::is_same_v<T, document::RasterHslParams>) {
        return std::isfinite(parameters.hue_degrees) &&
            std::abs(parameters.hue_degrees) <= 180.0 &&
            std::isfinite(parameters.saturation_percent) &&
            std::abs(parameters.saturation_percent) <= 100.0 &&
            std::isfinite(parameters.lightness_percent) &&
            std::abs(parameters.lightness_percent) <= 100.0;
      } else if constexpr (std::is_same_v<T, document::RasterCurvesParams>) {
        return static_cast<uint8_t>(parameters.channel) <=
                static_cast<uint8_t>(document::RasterToneChannel::Blue) &&
            std::all_of(parameters.output_points.begin(), parameters.output_points.end(),
                [](double value) {
                  return std::isfinite(value) && value >= 0.0 && value <= 1.0;
                });
      } else if constexpr (std::is_same_v<T, document::RasterWhiteBalanceParams>) {
        return std::isfinite(parameters.temperature_shift_percent) &&
            std::abs(parameters.temperature_shift_percent) <= 100.0 &&
            std::isfinite(parameters.tint_percent) &&
            std::abs(parameters.tint_percent) <= 100.0;
      } else if constexpr (std::is_same_v<T, document::RasterVibranceParams>) {
        return std::isfinite(parameters.amount_percent) &&
            std::abs(parameters.amount_percent) <= 100.0;
      } else if constexpr (std::is_same_v<T, document::RasterSelectiveHslParams>) {
        return static_cast<uint8_t>(parameters.range) <=
                static_cast<uint8_t>(document::RasterHueRange::Magentas) &&
            std::isfinite(parameters.hue_degrees) &&
            std::abs(parameters.hue_degrees) <= 30.0 &&
            std::isfinite(parameters.saturation_percent) &&
            std::abs(parameters.saturation_percent) <= 100.0 &&
            std::isfinite(parameters.lightness_percent) &&
            std::abs(parameters.lightness_percent) <= 100.0 &&
            std::isfinite(parameters.feather_percent) &&
            parameters.feather_percent >= 0.0 &&
            parameters.feather_percent <= 100.0;
      } else if constexpr (std::is_same_v<T, document::RasterLightParams>) {
        return std::isfinite(parameters.exposure_ev) &&
            std::abs(parameters.exposure_ev) <= 5.0 &&
            std::isfinite(parameters.contrast_percent) &&
            std::abs(parameters.contrast_percent) <= 100.0 &&
            std::isfinite(parameters.highlights_percent) &&
            std::abs(parameters.highlights_percent) <= 100.0 &&
            std::isfinite(parameters.shadows_percent) &&
            std::abs(parameters.shadows_percent) <= 100.0 &&
            std::isfinite(parameters.whites_percent) &&
            std::abs(parameters.whites_percent) <= 100.0 &&
            std::isfinite(parameters.blacks_percent) &&
            std::abs(parameters.blacks_percent) <= 100.0;
      } else if constexpr (std::is_same_v<T, document::RasterColorBalanceParams>) {
        return static_cast<uint8_t>(parameters.range) <=
                static_cast<uint8_t>(document::RasterTonalRange::Highlights) &&
            std::isfinite(parameters.red_cyan_percent) &&
            std::abs(parameters.red_cyan_percent) <= 100.0 &&
            std::isfinite(parameters.green_magenta_percent) &&
            std::abs(parameters.green_magenta_percent) <= 100.0 &&
            std::isfinite(parameters.blue_yellow_percent) &&
            std::abs(parameters.blue_yellow_percent) <= 100.0;
      } else if constexpr (std::is_same_v<T, document::RasterChannelMixerParams>) {
        return std::all_of(parameters.coefficients.begin(), parameters.coefficients.end(),
            [](double value) { return std::isfinite(value) && value >= -2.0 && value <= 2.0; });
      } else {
        return std::isfinite(parameters.source_x) &&
            std::isfinite(parameters.source_y) &&
            std::isfinite(parameters.target_x) &&
            std::isfinite(parameters.target_y) &&
            parameters.source_x >= 0.0 && parameters.source_x <= 1.0 &&
            parameters.source_y >= 0.0 && parameters.source_y <= 1.0 &&
            parameters.target_x >= 0.0 && parameters.target_x <= 1.0 &&
            parameters.target_y >= 0.0 && parameters.target_y <= 1.0 &&
            std::isfinite(parameters.radius_percent) &&
            parameters.radius_percent > 0.0 && parameters.radius_percent <= 25.0 &&
            std::isfinite(parameters.hardness_percent) &&
            parameters.hardness_percent >= 0.0 && parameters.hardness_percent <= 100.0 &&
            std::isfinite(parameters.opacity_percent) &&
            parameters.opacity_percent >= 0.0 && parameters.opacity_percent <= 100.0;
      }
    }, node.parameters);
    bool valid_mask = true;
    if (node.mask) {
      const auto& mask = *node.mask;
      const uint64_t pixel_count =
          static_cast<uint64_t>(mask.width_px) * mask.height_px;
      const bool pointwise =
          std::holds_alternative<document::RasterExposureParams>(node.parameters) ||
          std::holds_alternative<document::RasterLevelsParams>(node.parameters) ||
          std::holds_alternative<document::RasterHslParams>(node.parameters) ||
          std::holds_alternative<document::RasterCurvesParams>(node.parameters) ||
          std::holds_alternative<document::RasterWhiteBalanceParams>(node.parameters) ||
          std::holds_alternative<document::RasterVibranceParams>(node.parameters) ||
          std::holds_alternative<document::RasterSelectiveHslParams>(node.parameters) ||
          std::holds_alternative<document::RasterLightParams>(node.parameters) ||
          std::holds_alternative<document::RasterColorBalanceParams>(node.parameters) ||
          std::holds_alternative<document::RasterChannelMixerParams>(node.parameters);
      valid_mask = pointwise && mask.id != 0 && mask.width_px != 0 &&
          mask.height_px != 0 && mask.density_percent <= 100 &&
          (mask.kind == document::RasterAdjustmentMaskKind::Bitmap
              ? pixel_count == mask.coverage.size() &&
                    mask.gradient_angle_degrees == 0.0
              : (mask.kind == document::RasterAdjustmentMaskKind::Rectangle ||
                 mask.kind == document::RasterAdjustmentMaskKind::LinearGradient ||
                 mask.kind == document::RasterAdjustmentMaskKind::RadialGradient) &&
                mask.coverage.empty() && std::isfinite(mask.left) &&
                std::isfinite(mask.top) && std::isfinite(mask.width) &&
                std::isfinite(mask.height) && std::isfinite(mask.feather_px) &&
                mask.left >= 0.0 && mask.top >= 0.0 && mask.width > 0.0 &&
                mask.height > 0.0 && mask.left + mask.width <= 1.0 &&
                mask.top + mask.height <= 1.0 && mask.feather_px >= 0.0 &&
                mask.feather_px <= (std::max)(mask.width_px, mask.height_px) &&
                (mask.kind == document::RasterAdjustmentMaskKind::LinearGradient
                    ? std::isfinite(mask.gradient_angle_degrees) &&
                          std::abs(mask.gradient_angle_degrees) <= 36000.0
                    : mask.gradient_angle_degrees == 0.0));
    }
    if (!valid || !valid_mask) {
      diagnostic = "Raster edit graph contains invalid operation parameters.";
      return false;
    }
  }
  return true;
}

RasterEvaluationResult apply_graph(
    RasterPixelBuffer current, const document::RasterEditGraph& graph,
    RasterMemoryBudget& memory_budget,
    const RasterEvaluationOptions& options, size_t first_node) {
  for (size_t index = first_node; index < graph.nodes.size(); ++index) {
    if (cancelled(options)) return failure(RasterEvaluationStatus::Cancelled,
        "Raster edit evaluation was cancelled.");
    const auto& node = graph.nodes[index];
    if (!node.enabled) {
      if (options.progress)
        options.progress({index + 1, graph.nodes.size(), 0, 0});
      continue;
    }
    const bool is_identity = std::visit([](const auto& parameters) {
      using T = std::decay_t<decltype(parameters)>;
      if constexpr (std::is_same_v<T, document::RasterFlipParams>) {
        return !parameters.horizontal && !parameters.vertical;
      } else if constexpr (std::is_same_v<T, document::RasterRotateParams>) {
        return std::fmod(parameters.degrees, 360.0) == 0.0;
      } else if constexpr (std::is_same_v<T, document::RasterExposureParams>) {
        return parameters.exposure_ev == 0.0;
      } else if constexpr (std::is_same_v<T, document::RasterLevelsParams>) {
        return parameters.black_in == 0.0 && parameters.white_in == 1.0 &&
            parameters.gamma == 1.0 && parameters.black_out == 0.0 &&
            parameters.white_out == 1.0;
      } else if constexpr (std::is_same_v<T, document::RasterHslParams>) {
        return parameters.hue_degrees == 0.0 &&
            parameters.saturation_percent == 0.0 &&
            parameters.lightness_percent == 0.0;
      } else if constexpr (std::is_same_v<T, document::RasterCurvesParams>) {
        return parameters.output_points ==
            std::array<double, 5>{0.0, 0.25, 0.5, 0.75, 1.0};
      } else if constexpr (std::is_same_v<T, document::RasterWhiteBalanceParams>) {
        return parameters.temperature_shift_percent == 0.0 &&
            parameters.tint_percent == 0.0;
      } else if constexpr (std::is_same_v<T, document::RasterVibranceParams>) {
        return parameters.amount_percent == 0.0;
      } else if constexpr (std::is_same_v<T, document::RasterSelectiveHslParams>) {
        return parameters.hue_degrees == 0.0 &&
            parameters.saturation_percent == 0.0 &&
            parameters.lightness_percent == 0.0;
      } else if constexpr (std::is_same_v<T, document::RasterLightParams>) {
        return parameters.exposure_ev == 0.0 &&
            parameters.contrast_percent == 0.0 &&
            parameters.highlights_percent == 0.0 &&
            parameters.shadows_percent == 0.0 &&
            parameters.whites_percent == 0.0 &&
            parameters.blacks_percent == 0.0;
      } else if constexpr (std::is_same_v<T, document::RasterColorBalanceParams>) {
        return parameters.red_cyan_percent == 0.0 &&
            parameters.green_magenta_percent == 0.0 &&
            parameters.blue_yellow_percent == 0.0;
      } else if constexpr (std::is_same_v<T, document::RasterChannelMixerParams>) {
        return parameters.coefficients == std::array<double, 9>{
            1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
      } else if constexpr (std::is_same_v<T, document::RasterCloneStampParams>) {
        return parameters.opacity_percent == 0.0;
      }
      return false;
    }, node.parameters);
    if (is_identity) {
      if (options.progress)
        options.progress({index + 1, graph.nodes.size(), 0, 0});
      continue;
    }
    if (node.mask && (node.mask->width_px != current.width ||
                      node.mask->height_px != current.height))
      return failure(RasterEvaluationStatus::InvalidGraph,
          "Adjustment mask dimensions do not match the raster at this graph step.");
    auto step = std::visit([&](const auto& parameters) -> StepResult {
      using T = std::decay_t<decltype(parameters)>;
      if constexpr (std::is_same_v<T, document::RasterCropParams>)
        return crop(current, parameters, memory_budget, options, index + 1,
                    graph.nodes.size());
      else if constexpr (std::is_same_v<T, document::RasterFlipParams>)
        return flip(current, parameters, memory_budget, options, index + 1,
                    graph.nodes.size());
      else if constexpr (std::is_same_v<T, document::RasterRotateParams>)
        return rotate(current, parameters, memory_budget, options, index + 1,
                      graph.nodes.size());
      else if constexpr (std::is_same_v<T, document::RasterExposureParams>)
        return exposure(std::move(current), parameters,
                        node.mask ? &*node.mask : nullptr, options, index + 1,
                        graph.nodes.size());
      else if constexpr (std::is_same_v<T, document::RasterLightParams>)
        return light(std::move(current), parameters,
                     node.mask ? &*node.mask : nullptr, options, index + 1,
                     graph.nodes.size());
      else if constexpr (std::is_same_v<T, document::RasterColorBalanceParams>)
        return color_balance(std::move(current), parameters,
                     node.mask ? &*node.mask : nullptr, options, index + 1,
                     graph.nodes.size());
      else if constexpr (std::is_same_v<T, document::RasterChannelMixerParams>)
        return channel_mixer(std::move(current), parameters,
                     node.mask ? &*node.mask : nullptr, options, index + 1,
                     graph.nodes.size());
      else if constexpr (std::is_same_v<T, document::RasterCloneStampParams>)
        return clone_stamp(std::move(current), parameters, memory_budget,
                           options, index + 1, graph.nodes.size());
      else if constexpr (std::is_same_v<T, document::RasterLevelsParams>)
        return levels(std::move(current), parameters,
                      node.mask ? &*node.mask : nullptr, options, index + 1,
                      graph.nodes.size());
      else if constexpr (std::is_same_v<T, document::RasterHslParams>)
        return hsl(std::move(current), parameters,
                   node.mask ? &*node.mask : nullptr, options, index + 1,
                   graph.nodes.size());
      else if constexpr (std::is_same_v<T, document::RasterCurvesParams>)
        return curves(std::move(current), parameters,
                      node.mask ? &*node.mask : nullptr, options, index + 1,
                      graph.nodes.size());
      else if constexpr (std::is_same_v<T, document::RasterWhiteBalanceParams>)
        return white_balance(std::move(current), parameters,
                             node.mask ? &*node.mask : nullptr, options,
                             index + 1, graph.nodes.size());
      else if constexpr (std::is_same_v<T, document::RasterVibranceParams>)
        return vibrance(std::move(current), parameters,
                        node.mask ? &*node.mask : nullptr, options,
                        index + 1, graph.nodes.size());
      else
        return selective_hsl(std::move(current), parameters,
                             node.mask ? &*node.mask : nullptr, options,
                             index + 1, graph.nodes.size());
    }, node.parameters);
    if (!step.image)
      return failure(step.status, std::move(step.diagnostic));
    current = std::move(*step.image);
  }

  RasterEvaluationResult result;
  result.status = RasterEvaluationStatus::Success;
  result.image = std::move(current);
  return result;
}

}  // namespace

RasterEvaluationResult RasterEditEvaluator::evaluate(
    const RasterPixelView& source, const document::RasterEditGraph& graph,
    RasterMemoryBudget& memory_budget,
    const RasterEvaluationOptions& options) {
  if (sample_bytes(source.encoding) == 0)
    return failure(RasterEvaluationStatus::UnsupportedEncoding,
                   "This raster evaluator requires straight-alpha sRGB RGBA8 or RGBA16LE samples.");
  uint64_t minimum_stride = 0;
  size_t unused_size = 0;
  if (!checked_size(source.width, source.height, source.encoding,
                    minimum_stride, unused_size) ||
      source.row_stride < minimum_stride ||
      source.row_stride > std::numeric_limits<uint64_t>::max() / source.height ||
      source.row_stride * source.height > source.bytes.size())
    return failure(RasterEvaluationStatus::InvalidSource,
                   "Raster source dimensions, stride, or byte range are invalid.");

  std::string graph_error;
  if (!valid_graph(graph, graph_error))
    return failure(RasterEvaluationStatus::InvalidGraph,
                   std::move(graph_error));

  size_t first_node = 0;
  RasterPixelBuffer current;
  if (!graph.nodes.empty() && graph.nodes.front().enabled &&
      std::holds_alternative<document::RasterCropParams>(
          graph.nodes.front().parameters)) {
    const auto& crop_parameters =
        std::get<document::RasterCropParams>(graph.nodes.front().parameters);
    auto cropped = crop(source, crop_parameters, memory_budget, options, 1,
                        graph.nodes.size());
    if (!cropped.image)
      return failure(cropped.status, std::move(cropped.diagnostic));
    current = std::move(*cropped.image);
    first_node = 1;
  } else {
    auto current_result = copy_source(source, memory_budget, options,
                                      graph.nodes.size());
    if (!current_result.image)
      return failure(current_result.status,
                     std::move(current_result.diagnostic));
    current = std::move(*current_result.image);
  }

  return apply_graph(std::move(current), graph, memory_budget, options, first_node);
}

RasterEvaluationResult RasterEditEvaluator::evaluate_packed_rgba8(
    std::span<const uint32_t> packed_rgba8, uint32_t width, uint32_t height,
    const document::RasterEditGraph& graph,
    RasterMemoryBudget& memory_budget,
    const RasterEvaluationOptions& options) {
  const uint64_t pixel_count = static_cast<uint64_t>(width) * height;
  if (width == 0 || height == 0 || pixel_count != packed_rgba8.size() ||
      pixel_count > std::numeric_limits<size_t>::max() / 4u)
    return failure(RasterEvaluationStatus::InvalidSource,
                   "Packed RGBA8 source dimensions or byte range are invalid.");

  std::string graph_error;
  if (!valid_graph(graph, graph_error))
    return failure(RasterEvaluationStatus::InvalidGraph,
                   std::move(graph_error));

  auto frame_result = allocate_frame(
      width, height, RasterPixelEncoding::SRgbRgba8StraightAlpha, memory_budget);
  if (!frame_result.image)
    return failure(frame_result.status, std::move(frame_result.diagnostic));
  auto frame = std::move(*frame_result.image);
  for (uint32_t y = 0; y < height; ++y) {
    if (cancelled(options))
      return failure(RasterEvaluationStatus::Cancelled,
                     "Raster edit evaluation was cancelled.");
    auto* row = frame.bytes.data() + static_cast<size_t>(y) * frame.row_stride;
    const auto source_offset = static_cast<size_t>(y) * width;
    for (uint32_t x = 0; x < width; ++x) {
      if (cancellation_checkpoint(options, x))
        return failure(RasterEvaluationStatus::Cancelled,
                       "Raster edit evaluation was cancelled.");
      const uint32_t packed = packed_rgba8[source_offset + x];
      const auto offset = static_cast<size_t>(x) * 4u;
      row[offset] = static_cast<uint8_t>(packed >> 24);
      row[offset + 1] = static_cast<uint8_t>(packed >> 16);
      row[offset + 2] = static_cast<uint8_t>(packed >> 8);
      row[offset + 3] = static_cast<uint8_t>(packed);
    }
    report_row(options, 0, graph.nodes.size(), y, height);
  }
  return apply_graph(std::move(frame), graph, memory_budget, options, 0);
}

}  // namespace pikdraw::raster
