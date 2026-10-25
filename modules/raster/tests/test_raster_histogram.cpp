#include "pikdraw/raster/analysis/histogram.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace pikdraw::raster;

namespace {

void check(bool condition, const char* expression) {
  if (condition) return;
  std::cerr << "[FAIL] " << expression << '\n';
  std::exit(1);
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression)

void u8_histogram_applies_mask_and_reports_channel_statistics() {
  const std::array<uint8_t, 8> pixels{
      0, 255, 128, 255,
      255, 0, 128, 128};
  const std::array<uint8_t, 2> mask{255, 128};
  RasterHistogramOptions options;
  options.coverage = mask;
  const auto result = compute_raster_histogram(
      {pixels, 2, 1, 8, RasterPixelEncoding::SRgbRgba8StraightAlpha}, options);
  CHECK(result.succeeded());
  CHECK(result.bin_count == 256);
  const auto& red = result.channels[0];
  CHECK(red.covered_pixels == 2 && red.coverage_units == 383);
  CHECK(red.bins[0] == 255 && red.bins[255] == 128);
  CHECK(red.minimum_code == 0 && red.maximum_code == 255);
  CHECK(red.clipped_low_units == 255 && red.clipped_high_units == 128);
  CHECK(std::abs(red.mean_normalized - (128.0 / 383.0)) < 1e-12);
  CHECK(result.channels[1].bins[255] == 255 &&
         result.channels[1].bins[0] == 128);
  CHECK(result.channels[2].bins[128] == 383);
  CHECK(result.channels[3].bins[255] == 255 &&
         result.channels[3].bins[128] == 128);
  CHECK(result.channels[3].display_bins[255] == 255 &&
         result.channels[3].display_bins[128] == 128);
}

void u16_histogram_keeps_each_code_in_its_own_bin() {
  const std::array<uint8_t, 16> pixels{
      1, 0, 0xfe, 0xff, 0x34, 0x12, 0x11, 0x11,
      0xfe, 0xff, 1, 0, 0x34, 0x12, 0x22, 0x22};
  const auto result = compute_raster_histogram(
      {pixels, 2, 1, 16, RasterPixelEncoding::SRgbRgba16LEStraightAlpha});
  CHECK(result.succeeded());
  CHECK(result.bin_count == 65536);
  const auto& red = result.channels[0];
  CHECK(red.bins[1] == 255 && red.bins[65534] == 255);
  CHECK(red.minimum_code == 1 && red.maximum_code == 65534);
  CHECK(red.covered_pixels == 2 && red.coverage_units == 510);
  CHECK(result.channels[1].bins[1] == 255 &&
         result.channels[1].bins[65534] == 255);
  CHECK(result.channels[0].display_bins[0x00] == 255 &&
         result.channels[0].display_bins[0xff] == 255);
  CHECK(result.channels[3].display_bins[0x11] == 255 &&
         result.channels[3].display_bins[0x22] == 255);
}

void packed_rgba8_histogram_reads_logical_channel_order() {
  const std::array<uint32_t, 2> pixels{0x123456ffu, 0xff000080u};
  const auto result = compute_raster_histogram(pixels, 2, 1);
  CHECK(result.succeeded());
  CHECK(result.channels[0].bins[0x12] == 255 &&
         result.channels[0].bins[0xff] == 255);
  CHECK(result.channels[1].bins[0x34] == 255 &&
         result.channels[1].bins[0x00] == 255);
  CHECK(result.channels[2].bins[0x56] == 255 &&
         result.channels[2].bins[0x00] == 255);
  CHECK(result.channels[3].bins[0xff] == 255 &&
         result.channels[3].bins[0x80] == 255);
  CHECK(result.channels[3].display_bins[0xff] == 255 &&
         result.channels[3].display_bins[0x80] == 255);
  CHECK(result.channels[0].display_bins[0x12] == 255);
  CHECK(compute_raster_histogram(pixels, 3, 1).status ==
         RasterHistogramStatus::InvalidSource);
}

void invalid_inputs_budgets_and_cancellation_are_reported() {
  const std::array<uint8_t, 8> pixels{};
  RasterHistogramOptions bad_mask;
  const std::array<uint8_t, 1> short_mask{255};
  bad_mask.coverage = short_mask;
  auto result = compute_raster_histogram(
      {pixels, 2, 1, 8, RasterPixelEncoding::SRgbRgba8StraightAlpha}, bad_mask);
  CHECK(result.status == RasterHistogramStatus::InvalidCoverage);

  RasterHistogramOptions no_memory;
  no_memory.maximum_result_bytes = 128;
  result = compute_raster_histogram(
      {pixels, 2, 1, 8, RasterPixelEncoding::SRgbRgba8StraightAlpha}, no_memory);
  CHECK(result.status == RasterHistogramStatus::InsufficientMemory);

  RasterHistogramOptions cancelled;
  cancelled.cancelled = [] { return true; };
  result = compute_raster_histogram(
      {pixels, 2, 1, 8, RasterPixelEncoding::SRgbRgba8StraightAlpha}, cancelled);
  CHECK(result.status == RasterHistogramStatus::Cancelled);
  CHECK(result.bin_count == 0);
  for (const auto& channel : result.channels) CHECK(channel.bins.empty());
}

}  // namespace

int main() {
  u8_histogram_applies_mask_and_reports_channel_statistics();
  u16_histogram_keeps_each_code_in_its_own_bin();
  packed_rgba8_histogram_reads_logical_channel_order();
  invalid_inputs_budgets_and_cancellation_are_reported();
  return 0;
}

#undef CHECK
