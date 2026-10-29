#include "pikdraw/raster/evaluator.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using namespace pikdraw::raster;
using pikdraw::document::RasterEditGraph;
using pikdraw::document::RasterEditNode;
using pikdraw::document::RasterEditNodeId;

RasterEditNode node(uint64_t id,
                    pikdraw::document::RasterEditParameters parameters) {
  return {RasterEditNodeId{id}, true, std::move(parameters)};
}

RasterEvaluationResult evaluate(const std::vector<uint8_t>& bytes,
                                uint32_t width, uint32_t height,
                                RasterEditGraph graph,
                                uint64_t memory_bytes = 1024 * 1024) {
  RasterMemoryBudget budget(memory_bytes);
  const RasterPixelView source{bytes, width, height,
      static_cast<uint64_t>(width) * 4,
      RasterPixelEncoding::SRgbRgba8StraightAlpha};
  return RasterEditEvaluator::evaluate(source, graph, budget);
}

void append_u16_le(std::vector<uint8_t>& bytes, uint16_t value) {
  bytes.push_back(static_cast<uint8_t>(value & 0xffu));
  bytes.push_back(static_cast<uint8_t>(value >> 8));
}

uint16_t read_u16_le(const std::vector<uint8_t>& bytes, size_t offset) {
  return static_cast<uint16_t>(bytes[offset]) |
      static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

uint16_t expected_u16_exposure(uint16_t value, double ev) {
  const double encoded = static_cast<double>(value) / 65535.0;
  const double linear = encoded <= 0.04045
      ? encoded / 12.92
      : std::pow((encoded + 0.055) / 1.055, 2.4);
  const double adjusted = std::clamp(linear * std::exp2(ev), 0.0, 1.0);
  const double result = adjusted <= 0.0031308
      ? 12.92 * adjusted
      : 1.055 * std::pow(adjusted, 1.0 / 2.4) - 0.055;
  return static_cast<uint16_t>(std::clamp(
      std::llround(result * 65535.0), 0ll, 65535ll));
}

void crop_then_flip_uses_pixel_edges_and_graph_order() {
  // 3 x 2 pixels: each color identifies its original source coordinate.
  const std::vector<uint8_t> source{
      10, 0, 0, 255, 20, 0, 0, 255, 30, 0, 0, 255,
      40, 0, 0, 255, 50, 0, 0, 255, 60, 0, 0, 255};
  RasterEditGraph graph;
  graph.nodes.push_back(node(1, pikdraw::document::RasterCropParams{
      1.0 / 3.0, 0.0, 2.0 / 3.0, 1.0}));
  graph.nodes.push_back(node(2, pikdraw::document::RasterFlipParams{true, false}));

  auto result = evaluate(source, 3, 2, std::move(graph));
  assert(result.succeeded());
  assert(result.image.width == 2 && result.image.height == 2);
  assert(result.image.bytes[0] == 30 && result.image.bytes[4] == 20);
  assert(result.image.bytes[8] == 60 && result.image.bytes[12] == 50);
  assert(result.image.bytes[3] == 255 && result.image.bytes[15] == 255);
  assert(source[0] == 10 && source[8] == 30);  // source remains immutable
}

void crop_then_point_edits_fit_crop_only_memory_budget() {
  const std::vector<uint8_t> source{
      10, 0, 0, 255, 20, 0, 0, 255, 30, 0, 0, 255, 40, 0, 0, 255,
      0xee, 0xee, 0xee, 0xee,
      50, 0, 0, 255, 60, 0, 0, 255, 128, 0, 0, 255, 80, 0, 0, 255,
      0xee, 0xee, 0xee, 0xee};
  // The final four bytes in each row are padding and must not enter the crop.
  RasterEditGraph graph;
  graph.nodes.push_back(node(41, pikdraw::document::RasterCropParams{
      0.5, 0.5, 0.25, 0.5}));
  graph.nodes.push_back(node(43,
      pikdraw::document::RasterExposureParams{1.0}));
  graph.nodes.push_back(node(45,
      pikdraw::document::RasterLevelsParams{
          pikdraw::document::RasterToneChannel::Composite,
          0.0, 1.0, 2.0, 0.0, 1.0}));
  RasterMemoryBudget budget(4);
  const RasterPixelView view{source, 4, 2, 20,
      RasterPixelEncoding::SRgbRgba8StraightAlpha};

  const auto result = RasterEditEvaluator::evaluate(view, graph, budget);
  assert(result.succeeded());
  assert(result.image.width == 1 && result.image.height == 1);
  assert(result.image.bytes == std::vector<uint8_t>({212, 0, 0, 255}));
  assert(budget.reserved_bytes() == 4);
  assert(source[28] == 128);  // the immutable source remains untouched

  std::vector<uint8_t> source16;
  for (uint16_t pixel = 1; pixel <= 8; ++pixel) {
    append_u16_le(source16, static_cast<uint16_t>(pixel * 1000));
    append_u16_le(source16, static_cast<uint16_t>(pixel * 1000 + 1));
    append_u16_le(source16, static_cast<uint16_t>(pixel * 1000 + 2));
    append_u16_le(source16, static_cast<uint16_t>(pixel * 1000 + 3));
    if (pixel == 4 || pixel == 8)
      for (int padding = 0; padding < 4; ++padding)
        append_u16_le(source16, 0xeeee);
  }
  RasterEditGraph graph16;
  graph16.nodes.push_back(node(42, pikdraw::document::RasterCropParams{
      0.5, 0.5, 0.25, 0.5}));
  graph16.nodes.push_back(node(44,
      pikdraw::document::RasterExposureParams{1.0}));
  RasterMemoryBudget budget16(8);
  const RasterPixelView view16{source16, 4, 2, 40,
      RasterPixelEncoding::SRgbRgba16LEStraightAlpha};
  const auto result16 = RasterEditEvaluator::evaluate(view16, graph16, budget16);
  assert(result16.succeeded());
  assert(result16.image.width == 1 && result16.image.height == 1);
  assert(read_u16_le(result16.image.bytes, 0) ==
      expected_u16_exposure(7000, 1.0));
  assert(read_u16_le(result16.image.bytes, 2) ==
      expected_u16_exposure(7001, 1.0));
  assert(read_u16_le(result16.image.bytes, 4) ==
      expected_u16_exposure(7002, 1.0));
  assert(read_u16_le(result16.image.bytes, 6) == 7003);
  assert(budget16.reserved_bytes() == 8);
}

void exposure_is_linear_light_and_preserves_alpha() {
  const std::vector<uint8_t> source{128, 64, 32, 73};
  RasterEditGraph graph;
  graph.nodes.push_back(node(1, pikdraw::document::RasterExposureParams{1.0}));
  auto result = evaluate(source, 1, 1, std::move(graph));
  assert(result.succeeded());
  assert(result.image.bytes[0] == 176);
  assert(result.image.bytes[1] == 90);
  assert(result.image.bytes[2] == 47);
  assert(result.image.bytes[3] == 73);
  assert(source[0] == 128);
}

void exposure_mask_limits_adjustment_and_rejects_wrong_graph_dimensions() {
  const std::vector<uint8_t> source{
      64, 64, 64, 31, 64, 64, 64, 223};
  RasterEditGraph graph;
  auto local = node(711, pikdraw::document::RasterExposureParams{2.0});
  local.mask = pikdraw::document::RasterAdjustmentMask{
      91, 2, 1, 100, false, {255, 0}};
  graph.nodes.push_back(std::move(local));
  const auto result = evaluate(source, 2, 1, graph);
  assert(result.succeeded());
  assert(result.image.bytes[0] > source[0]);
  assert(result.image.bytes[4] == source[4]);
  assert(result.image.bytes[3] == source[3]);
  assert(result.image.bytes[7] == source[7]);
  graph.nodes.front().mask->density_percent = 50;
  const auto reduced_density = evaluate(source, 2, 1, graph);
  assert(reduced_density.succeeded());
  assert(reduced_density.image.bytes[0] > source[0]);
  assert(reduced_density.image.bytes[0] < result.image.bytes[0]);

  RasterEditGraph rectangle_graph;
  auto rectangle = node(713,
      pikdraw::document::RasterExposureParams{2.0});
  rectangle.mask = pikdraw::document::RasterAdjustmentMask{
      92, 2, 1, 100, false, {}};
  rectangle.mask->kind = pikdraw::document::RasterAdjustmentMaskKind::Rectangle;
  rectangle.mask->left = 0.0;
  rectangle.mask->top = 0.0;
  rectangle.mask->width = 0.5;
  rectangle.mask->height = 1.0;
  rectangle_graph.nodes.push_back(std::move(rectangle));
  const auto rectangular = evaluate(source, 2, 1, rectangle_graph);
  assert(rectangular.succeeded());
  assert(rectangular.image.bytes[0] > source[0]);
  assert(rectangular.image.bytes[4] == source[4]);
  rectangle_graph.nodes.front().mask->inverted = true;
  const auto inverted = evaluate(source, 2, 1, rectangle_graph);
  assert(inverted.succeeded());
  assert(inverted.image.bytes[0] == source[0]);
  assert(inverted.image.bytes[4] > source[4]);

  const std::vector<uint8_t> gradient_source(3 * 3 * 4, 64);
  RasterEditGraph linear_graph;
  auto linear = node(714, pikdraw::document::RasterExposureParams{2.0});
  linear.mask = pikdraw::document::RasterAdjustmentMask{93, 3, 3, 100, false, {}};
  linear.mask->kind = pikdraw::document::RasterAdjustmentMaskKind::LinearGradient;
  linear.mask->left = 0.0;
  linear.mask->top = 0.0;
  linear.mask->width = 1.0;
  linear.mask->height = 1.0;
  linear_graph.nodes.push_back(std::move(linear));
  const auto linear_result = evaluate(gradient_source, 3, 3, linear_graph);
  assert(linear_result.succeeded());
  assert(linear_result.image.bytes[0] > linear_result.image.bytes[4]);
  assert(linear_result.image.bytes[4] > linear_result.image.bytes[8]);
  linear_graph.nodes.front().mask->gradient_angle_degrees = 90.0;
  const auto vertical_result = evaluate(gradient_source, 3, 3, linear_graph);
  assert(vertical_result.succeeded());
  assert(vertical_result.image.bytes[0] > vertical_result.image.bytes[24]);
  linear_graph.nodes.front().mask->gradient_angle_degrees =
      std::numeric_limits<double>::quiet_NaN();
  const auto invalid_angle = evaluate(gradient_source, 3, 3, linear_graph);
  assert(invalid_angle.status == RasterEvaluationStatus::InvalidGraph);

  RasterEditGraph radial_graph;
  auto radial = node(715, pikdraw::document::RasterExposureParams{2.0});
  radial.mask = pikdraw::document::RasterAdjustmentMask{94, 3, 3, 100, false, {}};
  radial.mask->kind = pikdraw::document::RasterAdjustmentMaskKind::RadialGradient;
  radial.mask->left = 0.0;
  radial.mask->top = 0.0;
  radial.mask->width = 1.0;
  radial.mask->height = 1.0;
  radial_graph.nodes.push_back(std::move(radial));
  const auto radial_result = evaluate(gradient_source, 3, 3, radial_graph);
  assert(radial_result.succeeded());
  const size_t center = (1 * 3 + 1) * 4;
  const size_t edge = (1 * 3) * 4;
  const size_t corner = 0;
  assert(radial_result.image.bytes[center] > radial_result.image.bytes[edge]);
  assert(radial_result.image.bytes[edge] > radial_result.image.bytes[corner]);
  assert(radial_result.image.bytes[corner] >= gradient_source[corner]);

  graph.nodes.front().mask->width_px = 2;
  graph.nodes.front().mask->height_px = 1;
  graph.nodes.front().mask->coverage = {255, 0};
  graph.nodes.insert(graph.nodes.begin(), node(712,
      pikdraw::document::RasterCropParams{0.0, 0.0, 0.5, 1.0}));
  const auto mismatched = evaluate(source, 2, 1, graph);
  assert(mismatched.status == RasterEvaluationStatus::InvalidGraph);
}

void packed_rgba8_evaluation_preserves_channel_order_and_alpha() {
  const std::vector<uint32_t> packed{0x8040207fu};
  RasterEditGraph graph;
  graph.nodes.push_back(node(46,
      pikdraw::document::RasterExposureParams{1.0}));
  RasterMemoryBudget budget(4);
  const auto result = RasterEditEvaluator::evaluate_packed_rgba8(
      packed, 1, 1, graph, budget);
  assert(result.succeeded());
  assert(result.image.bytes == std::vector<uint8_t>({176, 90, 47, 127}));
  assert(budget.reserved_bytes() == 4);
  assert(packed[0] == 0x8040207fu);

  RasterMemoryBudget invalid_budget(16);
  const auto invalid = RasterEditEvaluator::evaluate_packed_rgba8(
      packed, 2, 1, {}, invalid_budget);
  assert(invalid.status == RasterEvaluationStatus::InvalidSource);
  assert(invalid_budget.reserved_bytes() == 0);
}

void levels_respects_range_channel_alpha_and_u16_precision() {
  using pikdraw::document::RasterLevelsParams;
  using pikdraw::document::RasterToneChannel;
  const std::vector<uint8_t> source{0, 128, 255, 73};
  RasterEditGraph graph;
  graph.nodes.push_back(node(12, RasterLevelsParams{
      RasterToneChannel::Red, 0.25, 0.75, 1.0, 0.1, 0.9}));
  const auto adjusted = evaluate(source, 1, 1, std::move(graph));
  assert(adjusted.succeeded());
  assert(adjusted.image.bytes[0] == 26); // black input maps to black output
  assert(adjusted.image.bytes[1] == 128);
  assert(adjusted.image.bytes[2] == 255);
  assert(adjusted.image.bytes[3] == 73);

  std::vector<uint8_t> source16;
  for (const auto value : {uint16_t{10000}, uint16_t{10001}}) {
    append_u16_le(source16, value);
    append_u16_le(source16, 32000);
    append_u16_le(source16, 50000);
    append_u16_le(source16, 23117);
  }
  RasterEditGraph graph16;
  graph16.nodes.push_back(node(13, RasterLevelsParams{
      RasterToneChannel::Red, 0.0, 1.0, 2.0, 0.0, 1.0}));
  RasterMemoryBudget budget(1024);
  const RasterPixelView view{source16, 2, 1, 16,
      RasterPixelEncoding::SRgbRgba16LEStraightAlpha};
  const auto adjusted16 = RasterEditEvaluator::evaluate(view, graph16, budget);
  assert(adjusted16.succeeded());
  assert(read_u16_le(adjusted16.image.bytes, 0) !=
         read_u16_le(adjusted16.image.bytes, 8));
  assert(read_u16_le(adjusted16.image.bytes, 2) == 32000 &&
         read_u16_le(adjusted16.image.bytes, 4) == 50000 &&
         read_u16_le(adjusted16.image.bytes, 6) == 23117);
  assert(read_u16_le(adjusted16.image.bytes, 10) == 32000 &&
         read_u16_le(adjusted16.image.bytes, 12) == 50000 &&
         read_u16_le(adjusted16.image.bytes, 14) == 23117);
}

void levels_identity_is_bit_exact_and_invalid_parameters_are_rejected() {
  using pikdraw::document::RasterLevelsParams;
  const std::vector<uint8_t> source{1, 2, 3, 4, 5, 6, 7, 8};
  RasterEditGraph identity;
  identity.nodes.push_back(node(14, RasterLevelsParams{}));
  const auto unchanged = evaluate(source, 2, 1, std::move(identity));
  assert(unchanged.succeeded() && unchanged.image.bytes == source);

  RasterEditGraph invalid;
  invalid.nodes.push_back(node(15, RasterLevelsParams{
      pikdraw::document::RasterToneChannel::Composite,
      0.8, 0.2, 1.0, 0.0, 1.0}));
  const auto rejected = evaluate(source, 2, 1, std::move(invalid));
  assert(rejected.status == RasterEvaluationStatus::InvalidGraph);
}

void light_adjustment_targets_tones_preserves_alpha_and_validates_u16() {
  using pikdraw::document::RasterLightParams;
  const std::vector<uint8_t> source{
      0, 0, 0, 91, 128, 128, 128, 143, 255, 255, 255, 207};
  RasterEditGraph graph;
  graph.nodes.push_back(node(70, RasterLightParams{
      0.0, 0.0, 100.0, 100.0, 0.0, 0.0}));
  const auto adjusted = evaluate(source, 3, 1, std::move(graph));
  assert(adjusted.succeeded());
  assert(adjusted.image.bytes[0] > source[0]);
  assert(adjusted.image.bytes[8] < source[8]);
  assert(adjusted.image.bytes[3] == source[3] &&
         adjusted.image.bytes[7] == source[7] &&
         adjusted.image.bytes[11] == source[11]);

  RasterEditGraph masked_graph;
  auto masked_light = node(74, RasterLightParams{0.0, 0.0, 0.0,
      100.0, 0.0, 0.0});
  masked_light.mask = pikdraw::document::RasterAdjustmentMask{
      75, 3, 1, 100, false, {0, 255, 255}};
  masked_graph.nodes.push_back(masked_light);
  const auto masked = evaluate(source, 3, 1, std::move(masked_graph));
  assert(masked.succeeded());
  assert(std::equal(source.begin(), source.begin() + 4,
                    masked.image.bytes.begin()));
  assert(masked.image.bytes[4] > source[4] &&
         masked.image.bytes[7] == source[7]);

  RasterEditGraph identity;
  identity.nodes.push_back(node(71, RasterLightParams{}));
  const auto unchanged = evaluate(source, 3, 1, std::move(identity));
  assert(unchanged.succeeded() && unchanged.image.bytes == source);

  std::vector<uint8_t> source16;
  for (const auto value : {uint16_t{12000}, uint16_t{12001}}) {
    append_u16_le(source16, value);
    append_u16_le(source16, value);
    append_u16_le(source16, value);
    append_u16_le(source16, uint16_t{32123});
  }
  RasterEditGraph graph16;
  graph16.nodes.push_back(node(72, RasterLightParams{0.5, 20.0, 0.0,
      25.0, 0.0, 0.0}));
  RasterMemoryBudget budget(1024);
  const RasterPixelView view{source16, 2, 1, 16,
      RasterPixelEncoding::SRgbRgba16LEStraightAlpha};
  const auto adjusted16 = RasterEditEvaluator::evaluate(view, graph16, budget);
  assert(adjusted16.succeeded());
  assert(read_u16_le(adjusted16.image.bytes, 0) !=
         read_u16_le(adjusted16.image.bytes, 8));
  assert(read_u16_le(adjusted16.image.bytes, 6) == 32123 &&
         read_u16_le(adjusted16.image.bytes, 14) == 32123);

  RasterEditGraph invalid;
  invalid.nodes.push_back(node(73, RasterLightParams{5.01, 0.0, 0.0,
      0.0, 0.0, 0.0}));
  const auto rejected = evaluate(source, 3, 1, std::move(invalid));
  assert(rejected.status == RasterEvaluationStatus::InvalidGraph);
}

void channel_mixer_uses_linear_rgb_preserves_alpha_and_validates_matrix() {
  using pikdraw::document::RasterChannelMixerParams;
  const std::vector<uint8_t> source{255, 0, 0, 73, 0, 255, 0, 91};
  RasterEditGraph graph;
  RasterChannelMixerParams mixer;
  mixer.coefficients = {0.0, 0.0, 1.0,
                        1.0, 0.0, 0.0,
                        0.0, 1.0, 0.0};
  graph.nodes.push_back(node(76, mixer));
  const auto result = evaluate(source, 2, 1, std::move(graph));
  assert(result.succeeded());
  assert((std::vector<uint8_t>(result.image.bytes.begin(), result.image.bytes.begin() + 4) ==
          std::vector<uint8_t>{0, 255, 0, 73}));
  assert((std::vector<uint8_t>(result.image.bytes.begin() + 4, result.image.bytes.end()) ==
          std::vector<uint8_t>{0, 0, 255, 91}));

  RasterEditGraph identity;
  identity.nodes.push_back(node(77, RasterChannelMixerParams{}));
  const auto unchanged = evaluate(source, 2, 1, std::move(identity));
  assert(unchanged.succeeded() && unchanged.image.bytes == source);

  RasterEditGraph masked_graph;
  auto masked_node = node(79, mixer);
  masked_node.mask = pikdraw::document::RasterAdjustmentMask{
      80, 2, 1, 100, false, {0, 255}};
  masked_graph.nodes.push_back(masked_node);
  const auto masked = evaluate(source, 2, 1, std::move(masked_graph));
  assert(masked.succeeded());
  assert(std::equal(source.begin(), source.begin() + 4, masked.image.bytes.begin()));
  assert(masked.image.bytes[4] == 0 && masked.image.bytes[5] == 0 &&
         masked.image.bytes[6] == 255 && masked.image.bytes[7] == 91);

  std::vector<uint8_t> source16;
  for (const auto value : {uint16_t{65535}, uint16_t{0}, uint16_t{0}, uint16_t{40001}})
    append_u16_le(source16, value);
  RasterEditGraph graph16;
  graph16.nodes.push_back(node(80, mixer));
  RasterMemoryBudget budget(64);
  const RasterPixelView view{source16, 1, 1, 8,
      RasterPixelEncoding::SRgbRgba16LEStraightAlpha};
  const auto adjusted16 = RasterEditEvaluator::evaluate(view, graph16, budget);
  assert(adjusted16.succeeded());
  assert(read_u16_le(adjusted16.image.bytes, 0) == 0 &&
         read_u16_le(adjusted16.image.bytes, 2) == 65535 &&
         read_u16_le(adjusted16.image.bytes, 6) == 40001);

  auto invalid_mixer = RasterChannelMixerParams{};
  invalid_mixer.coefficients[0] = 2.01;
  RasterEditGraph invalid;
  invalid.nodes.push_back(node(78, invalid_mixer));
  const auto rejected = evaluate(source, 2, 1, std::move(invalid));
  assert(rejected.status == RasterEvaluationStatus::InvalidGraph);
}

void color_balance_targets_tonal_range_preserves_alpha_and_mask() {
  using pikdraw::document::RasterAdjustmentMask;
  using pikdraw::document::RasterColorBalanceParams;
  using pikdraw::document::RasterTonalRange;
  const std::vector<uint8_t> source{
      32, 32, 32, 71, 128, 128, 128, 119, 240, 240, 240, 203};
  RasterEditGraph shadows;
  shadows.nodes.push_back(node(81, RasterColorBalanceParams{
      RasterTonalRange::Shadows, 100.0, 0.0, 0.0, true}));
  const auto adjusted = evaluate(source, 3, 1, std::move(shadows));
  assert(adjusted.succeeded());
  assert(adjusted.image.bytes[0] > source[0]);
  assert(adjusted.image.bytes[3] == source[3]);
  assert(adjusted.image.bytes[8] == source[8]);
  assert(adjusted.image.bytes[11] == source[11]);

  RasterEditGraph masked;
  RasterEditNode masked_node{RasterEditNodeId{82}, true,
      RasterColorBalanceParams{RasterTonalRange::Midtones,
          50.0, -25.0, 10.0, false}};
  RasterAdjustmentMask mask;
  mask.id = 7001;
  mask.width_px = 2;
  mask.height_px = 1;
  mask.coverage = {255, 0};
  masked_node.mask = mask;
  masked.nodes.push_back(std::move(masked_node));
  const std::vector<uint8_t> pair{128, 128, 128, 17, 128, 128, 128, 29};
  const auto masked_result = evaluate(pair, 2, 1, std::move(masked));
  assert(masked_result.succeeded());
  assert(masked_result.image.bytes[0] != pair[0]);
  assert(masked_result.image.bytes[4] == pair[4]);
  assert(masked_result.image.bytes[3] == pair[3] &&
         masked_result.image.bytes[7] == pair[7]);

  const std::vector<uint8_t> source16{
      0x00, 0x40, 0x00, 0x40, 0x00, 0x40, 0x5a, 0x5a};
  RasterEditGraph graph16;
  graph16.nodes.push_back(node(83, RasterColorBalanceParams{
      RasterTonalRange::Midtones, 60.0, 0.0, 0.0, false}));
  RasterMemoryBudget budget16(128);
  const RasterPixelView view16{source16, 1, 1, 8,
      RasterPixelEncoding::SRgbRgba16LEStraightAlpha};
  const auto result16 = RasterEditEvaluator::evaluate(view16, graph16, budget16);
  assert(result16.succeeded());
  assert(read_u16_le(result16.image.bytes, 0) > 0x4000);
  assert(read_u16_le(result16.image.bytes, 6) == 0x5a5a);

  RasterEditGraph invalid;
  invalid.nodes.push_back(node(84, RasterColorBalanceParams{
      static_cast<RasterTonalRange>(3), 0.0, 0.0, 0.0, true}));
  assert(evaluate(source, 3, 1, std::move(invalid)).status ==
      RasterEvaluationStatus::InvalidGraph);

  RasterEditGraph identity;
  identity.nodes.push_back(node(85, RasterColorBalanceParams{}));
  const auto unchanged = evaluate(source, 3, 1, std::move(identity));
  assert(unchanged.succeeded() && unchanged.image.bytes == source);
}

void clone_stamp_freezes_source_and_blends_u16_alpha() {
  using pikdraw::document::RasterCloneStampParams;
  std::vector<uint8_t> source(8u * 8u * 4u);
  for (uint32_t y = 0; y < 8; ++y) {
    for (uint32_t x = 0; x < 8; ++x) {
      const size_t offset = (static_cast<size_t>(y) * 8 + x) * 4;
      source[offset] = static_cast<uint8_t>(x * 20);
      source[offset + 1] = static_cast<uint8_t>(y * 20);
      source[offset + 2] = 15;
      source[offset + 3] = 200;
    }
  }
  RasterEditGraph graph;
  graph.nodes.push_back(node(79, RasterCloneStampParams{
      2.0 / 7.0, 4.0 / 7.0, 3.0 / 7.0, 4.0 / 7.0,
      25.0, 100.0, 100.0}));
  const auto result = evaluate(source, 8, 8, std::move(graph));
  assert(result.succeeded());
  const size_t destination_x3 = (4u * 8u + 3u) * 4u;
  const size_t destination_x4 = (4u * 8u + 4u) * 4u;
  assert(result.image.bytes[destination_x3] == 40);
  assert(result.image.bytes[destination_x4] == 60);
  assert(result.image.bytes[destination_x4 + 3] == 200);
  assert(source[destination_x3] == 60 && source[destination_x4] == 80);

  std::vector<uint8_t> source16;
  source16.reserve(3u * 3u * 8u);
  for (uint32_t y = 0; y < 3; ++y) {
    for (uint32_t x = 0; x < 3; ++x) {
      const uint16_t value = x == 0 && y == 1 ? 10000 : 20000;
      append_u16_le(source16, value);
      append_u16_le(source16, value);
      append_u16_le(source16, value);
      append_u16_le(source16, 40000);
    }
  }
  RasterEditGraph graph16;
  graph16.nodes.push_back(node(80, RasterCloneStampParams{
      0.0, 0.5, 0.5, 0.5, 25.0, 100.0, 50.0}));
  RasterMemoryBudget budget(4096);
  const auto result16 = RasterEditEvaluator::evaluate(
      {source16, 3, 3, 24, RasterPixelEncoding::SRgbRgba16LEStraightAlpha},
      graph16, budget);
  assert(result16.succeeded());
  const size_t center = (1u * 3u + 1u) * 8u;
  assert(read_u16_le(result16.image.bytes, center) == 15000);
  assert(read_u16_le(result16.image.bytes, center + 6) == 40000);

  auto invalid = RasterCloneStampParams{};
  invalid.radius_percent = 25.01;
  RasterEditGraph invalid_graph;
  invalid_graph.nodes.push_back(node(81, invalid));
  const auto rejected = evaluate(source, 8, 8, std::move(invalid_graph));
  assert(rejected.status == RasterEvaluationStatus::InvalidGraph);
}

void raster_eraser_changes_alpha_non_destructively_with_u8_and_u16_coverage() {
  using pikdraw::document::RasterEraserParams;
  std::vector<uint8_t> source(5u * 5u * 4u);
  for (size_t offset = 0; offset < source.size(); offset += 4) {
    source[offset] = 100;
    source[offset + 1] = 50;
    source[offset + 2] = 25;
    source[offset + 3] = 255;
  }
  const auto original = source;
  RasterEditGraph graph;
  RasterEraserParams eraser;
  eraser.diameter_px = 2.0;
  eraser.points = {{0.5, 0.0}, {0.5, 1.0}};
  graph.nodes.push_back(node(86, eraser));
  const auto result = evaluate(source, 5, 5, std::move(graph));
  assert(result.succeeded());
  assert(source == original);
  const size_t center = (2u * 5u + 2u) * 4u;
  const size_t edge = (2u * 5u + 1u) * 4u;
  const size_t outside = (2u * 5u) * 4u;
  assert(result.image.bytes[center] == 0 && result.image.bytes[center + 1] == 0 &&
      result.image.bytes[center + 2] == 0 && result.image.bytes[center + 3] == 0);
  assert(result.image.bytes[edge] == 100 && result.image.bytes[edge + 1] == 50 &&
      result.image.bytes[edge + 2] == 25 && result.image.bytes[edge + 3] >= 126 &&
      result.image.bytes[edge + 3] <= 129);
  assert(result.image.bytes[outside + 3] == 255);

  std::vector<uint8_t> source16;
  for (size_t index = 0; index < 9; ++index) {
    append_u16_le(source16, 10000);
    append_u16_le(source16, 20000);
    append_u16_le(source16, 30000);
    append_u16_le(source16, 40000);
  }
  RasterEditGraph graph16;
  RasterEraserParams eraser16;
  eraser16.diameter_px = 2.0;
  eraser16.points = {{0.5, 0.0}, {0.5, 1.0}};
  graph16.nodes.push_back(node(87, eraser16));
  RasterMemoryBudget budget(4096);
  const auto result16 = RasterEditEvaluator::evaluate(
      {source16, 3, 3, 24,
       RasterPixelEncoding::SRgbRgba16LEStraightAlpha}, graph16, budget);
  assert(result16.succeeded());
  const size_t center16 = (1u * 3u + 1u) * 8u;
  const size_t edge16 = (1u * 3u) * 8u;
  assert(read_u16_le(result16.image.bytes, center16) == 0 &&
      read_u16_le(result16.image.bytes, center16 + 6) == 0);
  assert(read_u16_le(result16.image.bytes, edge16) == 10000 &&
      read_u16_le(result16.image.bytes, edge16 + 2) == 20000 &&
      read_u16_le(result16.image.bytes, edge16 + 6) >= 19999 &&
      read_u16_le(result16.image.bytes, edge16 + 6) <= 20001);

  RasterEditGraph invalid;
  RasterEraserParams invalid_params;
  invalid_params.points.push_back({std::numeric_limits<double>::quiet_NaN(), 0.5});
  invalid.nodes.push_back(node(88, invalid_params));
  assert(evaluate(source, 5, 5, std::move(invalid)).status ==
      RasterEvaluationStatus::InvalidGraph);
}

void hsl_rotates_hue_preserves_alpha_and_respects_u16_and_gray() {
  using pikdraw::document::RasterHslParams;
  const auto check = [](bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
  };
  const std::vector<uint8_t> source{
      255, 0, 0, 73, 128, 128, 128, 91};
  RasterEditGraph graph;
  graph.nodes.push_back(node(16, RasterHslParams{120.0, 0.0, 0.0}));
  const auto result = evaluate(source, 2, 1, std::move(graph));
  check(result.succeeded(), "HSL evaluation should succeed");
  check(result.image.bytes[0] == 0 && result.image.bytes[1] == 255 &&
        result.image.bytes[2] == 0 && result.image.bytes[3] == 73,
        "120 degree hue shift should turn red to green and preserve alpha");
  check(result.image.bytes[4] == 128 && result.image.bytes[5] == 128 &&
        result.image.bytes[6] == 128 && result.image.bytes[7] == 91,
        "Hue adjustment must leave neutral gray unchanged");

  std::vector<uint8_t> source16;
  for (const auto value : {uint16_t{65535}, uint16_t{0}, uint16_t{0}, uint16_t{32769}})
    append_u16_le(source16, value);
  RasterEditGraph graph16;
  graph16.nodes.push_back(node(17, RasterHslParams{120.0, 0.0, 0.0}));
  RasterMemoryBudget budget(1024);
  const RasterPixelView view{source16, 1, 1, 8,
      RasterPixelEncoding::SRgbRgba16LEStraightAlpha};
  const auto result16 = RasterEditEvaluator::evaluate(view, graph16, budget);
  check(result16.succeeded(), "U16 HSL evaluation should succeed");
  check(read_u16_le(result16.image.bytes, 0) == 0 &&
        read_u16_le(result16.image.bytes, 2) == 65535 &&
        read_u16_le(result16.image.bytes, 4) == 0 &&
        read_u16_le(result16.image.bytes, 6) == 32769,
        "U16 HSL should preserve full-depth primary values and alpha");
}

void hsl_identity_is_bit_exact_and_out_of_range_values_are_rejected() {
  const auto check = [](bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
  };
  const std::vector<uint8_t> source{1, 2, 3, 4, 5, 6, 7, 8};
  RasterEditGraph identity;
  identity.nodes.push_back(node(18, pikdraw::document::RasterHslParams{}));
  const auto unchanged = evaluate(source, 2, 1, std::move(identity));
  check(unchanged.succeeded() && unchanged.image.bytes == source,
        "Identity HSL must preserve the source bit-for-bit");
  RasterEditGraph invalid;
  invalid.nodes.push_back(node(19,
      pikdraw::document::RasterHslParams{181.0, 0.0, 0.0}));
  const auto rejected = evaluate(source, 2, 1, std::move(invalid));
  check(rejected.status == RasterEvaluationStatus::InvalidGraph,
        "Out-of-range HSL values must be rejected");
}

void curves_adjust_selected_channel_preserve_alpha_and_keep_u16_precision() {
  using pikdraw::document::RasterCurvesParams;
  using pikdraw::document::RasterToneChannel;
  const std::vector<uint8_t> source{64, 128, 128, 73};
  RasterCurvesParams params;
  params.channel = RasterToneChannel::Red;
  params.output_points = {0.0, 0.125, 0.5, 0.75, 1.0};
  RasterEditGraph graph;
  graph.nodes.push_back(node(23, params));
  const auto adjusted = evaluate(source, 1, 1, std::move(graph));
  assert(adjusted.succeeded());
  assert(adjusted.image.bytes[0] == 32);
  assert(adjusted.image.bytes[1] == 128 &&
         adjusted.image.bytes[2] == 128 &&
         adjusted.image.bytes[3] == 73);

  std::vector<uint8_t> source16;
  for (const auto red : {uint16_t{16384}, uint16_t{16385}}) {
    append_u16_le(source16, red);
    append_u16_le(source16, 32000);
    append_u16_le(source16, 50000);
    append_u16_le(source16, 23117);
  }
  RasterCurvesParams curve16;
  curve16.channel = RasterToneChannel::Composite;
  curve16.output_points = {0.0, 0.0, 1.0, 1.0, 1.0};
  RasterEditGraph graph16;
  graph16.nodes.push_back(node(24, curve16));
  RasterMemoryBudget budget(1024);
  const RasterPixelView view{source16, 2, 1, 16,
      RasterPixelEncoding::SRgbRgba16LEStraightAlpha};
  const auto adjusted16 = RasterEditEvaluator::evaluate(view, graph16, budget);
  assert(adjusted16.succeeded());
  assert(read_u16_le(adjusted16.image.bytes, 0) !=
         read_u16_le(adjusted16.image.bytes, 8));
  assert(read_u16_le(adjusted16.image.bytes, 6) == 23117 &&
         read_u16_le(adjusted16.image.bytes, 14) == 23117);
}

void curves_identity_is_bit_exact_and_invalid_points_are_rejected() {
  const std::vector<uint8_t> source{1, 2, 3, 4, 5, 6, 7, 8};
  RasterEditGraph identity;
  identity.nodes.push_back(node(25, pikdraw::document::RasterCurvesParams{}));
  const auto unchanged = evaluate(source, 2, 1, std::move(identity));
  assert(unchanged.succeeded() && unchanged.image.bytes == source);

  RasterEditGraph invalid;
  auto params = pikdraw::document::RasterCurvesParams{};
  params.output_points[2] = 1.01;
  invalid.nodes.push_back(node(26, params));
  const auto rejected = evaluate(source, 2, 1, std::move(invalid));
  assert(rejected.status == RasterEvaluationStatus::InvalidGraph);
}

void white_balance_uses_linear_rgb_preserves_alpha_and_validates_ranges() {
  using pikdraw::document::RasterWhiteBalanceParams;
  const std::vector<uint8_t> source{128, 128, 128, 73};
  RasterEditGraph warm;
  warm.nodes.push_back(node(27, RasterWhiteBalanceParams{100.0, 0.0}));
  const auto adjusted = evaluate(source, 1, 1, warm);
  assert(adjusted.succeeded());
  assert(adjusted.image.bytes[0] > adjusted.image.bytes[1]);
  assert(adjusted.image.bytes[1] > adjusted.image.bytes[2]);
  assert(adjusted.image.bytes[3] == 73);

  RasterEditGraph magenta;
  magenta.nodes.push_back(node(30, RasterWhiteBalanceParams{0.0, 100.0}));
  const auto tinted = evaluate(source, 1, 1, std::move(magenta));
  assert(tinted.succeeded());
  assert(tinted.image.bytes[0] > tinted.image.bytes[1]);
  assert(tinted.image.bytes[2] > tinted.image.bytes[1]);
  assert(tinted.image.bytes[3] == 73);

  std::vector<uint8_t> source16;
  for (const auto value : {uint16_t{32768}, uint16_t{32768},
                           uint16_t{32768}, uint16_t{0x5a5a}})
    append_u16_le(source16, value);
  RasterEditGraph warm16;
  warm16.nodes.push_back(node(31, RasterWhiteBalanceParams{100.0, 0.0}));
  RasterMemoryBudget budget(1024);
  const RasterPixelView view{source16, 1, 1, 8,
      RasterPixelEncoding::SRgbRgba16LEStraightAlpha};
  const auto adjusted16 = RasterEditEvaluator::evaluate(view, warm16, budget);
  assert(adjusted16.succeeded());
  assert(read_u16_le(adjusted16.image.bytes, 0) >
         read_u16_le(adjusted16.image.bytes, 2));
  assert(read_u16_le(adjusted16.image.bytes, 2) >
         read_u16_le(adjusted16.image.bytes, 4));
  assert(read_u16_le(adjusted16.image.bytes, 6) == 0x5a5a);

  RasterEditGraph identity;
  identity.nodes.push_back(node(28, RasterWhiteBalanceParams{}));
  const auto unchanged = evaluate(source, 1, 1, std::move(identity));
  assert(unchanged.succeeded() && unchanged.image.bytes == source);

  RasterEditGraph invalid;
  invalid.nodes.push_back(node(29, RasterWhiteBalanceParams{0.0, 100.01}));
  const auto rejected = evaluate(source, 1, 1, std::move(invalid));
  assert(rejected.status == RasterEvaluationStatus::InvalidGraph);
}

void vibrance_boosts_muted_colors_more_than_vivid_colors_and_preserves_alpha() {
  using pikdraw::document::RasterVibranceParams;
  const std::vector<uint8_t> source{
      200, 150, 150, 77, 255, 0, 0, 88, 128, 128, 128, 91};
  RasterEditGraph graph;
  graph.nodes.push_back(node(32, RasterVibranceParams{100.0}));
  const auto boosted = evaluate(source, 3, 1, std::move(graph));
  assert(boosted.succeeded());
  assert(boosted.image.bytes[0] > source[0] &&
      boosted.image.bytes[1] < source[1] &&
      boosted.image.bytes[2] < source[2] && boosted.image.bytes[3] == 77);
  assert(std::equal(boosted.image.bytes.begin() + 4,
      boosted.image.bytes.begin() + 8, source.begin() + 4));
  assert(std::equal(boosted.image.bytes.begin() + 8,
      boosted.image.bytes.begin() + 12, source.begin() + 8));

  RasterEditGraph desaturate;
  desaturate.nodes.push_back(node(33, RasterVibranceParams{-100.0}));
  const auto muted = evaluate(source, 3, 1, std::move(desaturate));
  assert(muted.succeeded());
  assert(muted.image.bytes[0] == 175 && muted.image.bytes[1] == 175 &&
      muted.image.bytes[2] == 175 && muted.image.bytes[3] == 77);

  std::vector<uint8_t> source16;
  for (const auto value : {uint16_t{40000}, uint16_t{30000}, uint16_t{30000},
                           uint16_t{0x5a5a}})
    append_u16_le(source16, value);
  RasterEditGraph graph16;
  graph16.nodes.push_back(node(34, RasterVibranceParams{100.0}));
  RasterMemoryBudget budget(1024);
  const RasterPixelView view{source16, 1, 1, 8,
      RasterPixelEncoding::SRgbRgba16LEStraightAlpha};
  const auto boosted16 = RasterEditEvaluator::evaluate(view, graph16, budget);
  assert(boosted16.succeeded());
  assert(read_u16_le(boosted16.image.bytes, 0) > 40000 &&
      read_u16_le(boosted16.image.bytes, 2) < 30000 &&
      read_u16_le(boosted16.image.bytes, 6) == 0x5a5a);
}

void vibrance_identity_is_bit_exact_and_invalid_amount_is_rejected() {
  const std::vector<uint8_t> source{1, 2, 3, 4, 5, 6, 7, 8};
  RasterEditGraph identity;
  identity.nodes.push_back(node(35, pikdraw::document::RasterVibranceParams{}));
  const auto unchanged = evaluate(source, 2, 1, std::move(identity));
  assert(unchanged.succeeded() && unchanged.image.bytes == source);

  RasterEditGraph invalid;
  invalid.nodes.push_back(node(36,
      pikdraw::document::RasterVibranceParams{100.01}));
  const auto rejected = evaluate(source, 2, 1, std::move(invalid));
  assert(rejected.status == RasterEvaluationStatus::InvalidGraph);
}

void selective_hsl_targets_feathered_hue_bands_and_preserves_neutrals_alpha() {
  using pikdraw::document::RasterHueRange;
  using pikdraw::document::RasterSelectiveHslParams;
  const std::vector<uint8_t> source{
      255, 0, 0, 71,       // center of the red range
      0, 0, 255, 82,        // blue range must be untouched
      128, 128, 128, 93,    // neutral gray has no hue
      255, 64, 0, 104};      // near the red/yellow edge
  RasterEditGraph graph;
  graph.nodes.push_back(node(37, RasterSelectiveHslParams{
      RasterHueRange::Reds, 30.0, 0.0, 0.0, 100.0}));
  const auto feathered = evaluate(source, 4, 1, std::move(graph));
  assert(feathered.succeeded());
  assert(feathered.image.bytes[0] == 255 &&
      feathered.image.bytes[1] > 0 && feathered.image.bytes[2] == 0 &&
      feathered.image.bytes[3] == 71);
  assert(std::equal(feathered.image.bytes.begin() + 4,
      feathered.image.bytes.begin() + 8, source.begin() + 4));
  assert(std::equal(feathered.image.bytes.begin() + 8,
      feathered.image.bytes.begin() + 12, source.begin() + 8));
  assert(feathered.image.bytes[13] > source[13] &&
      feathered.image.bytes[13] < 192 && feathered.image.bytes[15] == 104);

  RasterEditGraph hard_edge;
  hard_edge.nodes.push_back(node(38, RasterSelectiveHslParams{
      RasterHueRange::Reds, 30.0, 0.0, 0.0, 0.0}));
  const auto full_weight = evaluate(
      std::vector<uint8_t>{255, 64, 0, 104}, 1, 1, std::move(hard_edge));
  assert(full_weight.succeeded());
  assert(full_weight.image.bytes[1] > feathered.image.bytes[13]);
}

void selective_hsl_supports_u16_identity_and_rejects_invalid_ranges() {
  using pikdraw::document::RasterHueRange;
  using pikdraw::document::RasterSelectiveHslParams;
  std::vector<uint8_t> source16;
  for (const auto value : {uint16_t{60000}, uint16_t{20000}, uint16_t{10000},
                           uint16_t{0x5a5a}})
    append_u16_le(source16, value);
  RasterEditGraph graph16;
  graph16.nodes.push_back(node(39, RasterSelectiveHslParams{
      RasterHueRange::Reds, -20.0, 15.0, 10.0, 40.0}));
  RasterMemoryBudget budget(1024);
  const RasterPixelView view{source16, 1, 1, 8,
      RasterPixelEncoding::SRgbRgba16LEStraightAlpha};
  const auto changed = RasterEditEvaluator::evaluate(view, graph16, budget);
  assert(changed.succeeded());
  assert(read_u16_le(changed.image.bytes, 0) != 60000 ||
      read_u16_le(changed.image.bytes, 2) != 20000 ||
      read_u16_le(changed.image.bytes, 4) != 10000);
  assert(read_u16_le(changed.image.bytes, 6) == 0x5a5a);

  const std::vector<uint8_t> pixel{12, 34, 56, 78};
  RasterEditGraph identity;
  identity.nodes.push_back(node(40, RasterSelectiveHslParams{}));
  const auto unchanged = evaluate(pixel, 1, 1, std::move(identity));
  assert(unchanged.succeeded() && unchanged.image.bytes == pixel);

  RasterEditGraph invalid_range;
  invalid_range.nodes.push_back(node(41, RasterSelectiveHslParams{
      static_cast<RasterHueRange>(6), 0.0, 0.0, 0.0, 50.0}));
  assert(evaluate(pixel, 1, 1, std::move(invalid_range)).status ==
      RasterEvaluationStatus::InvalidGraph);
  RasterEditGraph invalid_feather;
  invalid_feather.nodes.push_back(node(42, RasterSelectiveHslParams{
      RasterHueRange::Reds, 0.0, 0.0, 0.0, 100.1}));
  assert(evaluate(pixel, 1, 1, std::move(invalid_feather)).status ==
      RasterEvaluationStatus::InvalidGraph);
}

void u16_exposure_flip_rotate_preserve_precision_and_alpha() {
  std::vector<uint8_t> source;
  for (const auto value : {0x1234u, 0x2345u, 0x3456u, 0x4567u,
                           0x1235u, 0x2346u, 0x3457u, 0x5678u})
    append_u16_le(source, static_cast<uint16_t>(value));

  const RasterPixelView view{source, 2, 1, 16,
      RasterPixelEncoding::SRgbRgba16LEStraightAlpha};
  RasterMemoryBudget identity_budget(1024);
  const auto identity = RasterEditEvaluator::evaluate(
      view, RasterEditGraph{}, identity_budget);
  assert(identity.succeeded());
  assert(identity.image.encoding == RasterPixelEncoding::SRgbRgba16LEStraightAlpha);
  assert(identity.image.bytes == source);

  RasterEditGraph graph;
  graph.nodes.push_back(node(20,
      pikdraw::document::RasterCropParams{0.0, 0.0, 1.0, 1.0}));
  graph.nodes.push_back(node(21,
      pikdraw::document::RasterFlipParams{true, false}));
  graph.nodes.push_back(node(22,
      pikdraw::document::RasterExposureParams{1.0}));
  graph.nodes.push_back(node(23,
      pikdraw::document::RasterRotateParams{90.0}));
  RasterMemoryBudget budget(4096);
  const auto result = RasterEditEvaluator::evaluate(view, graph, budget);
  assert(result.succeeded());
  assert(result.image.encoding == RasterPixelEncoding::SRgbRgba16LEStraightAlpha);
  assert(result.image.width == 1 && result.image.height == 2);
  assert(result.image.row_stride == 8 && result.image.bytes.size() == 16);
  assert(read_u16_le(result.image.bytes, 0) == expected_u16_exposure(0x1235, 1.0));
  assert(read_u16_le(result.image.bytes, 2) == expected_u16_exposure(0x2346, 1.0));
  assert(read_u16_le(result.image.bytes, 4) == expected_u16_exposure(0x3457, 1.0));
  assert(read_u16_le(result.image.bytes, 6) == 0x5678);
  assert(read_u16_le(result.image.bytes, 8) == expected_u16_exposure(0x1234, 1.0));
  assert(read_u16_le(result.image.bytes, 14) == 0x4567);
  const auto first_red = read_u16_le(result.image.bytes, 0);
  const auto second_red = read_u16_le(result.image.bytes, 8);
  assert(first_red != second_red);
  assert(first_red - second_red < 16);  // Sub-8-bit steps survive the pipeline.
}

void right_angle_rotation_is_exact_and_keeps_pixel_values() {
  const std::vector<uint8_t> source{
      255, 0, 0, 255, 0, 255, 0, 127};
  RasterEditGraph graph;
  graph.nodes.push_back(node(7, pikdraw::document::RasterRotateParams{90.0}));
  auto result = evaluate(source, 2, 1, std::move(graph));
  assert(result.succeeded());
  assert(result.image.width == 1 && result.image.height == 2);
  assert(result.image.bytes[0] == 255 && result.image.bytes[1] == 0);
  assert(result.image.bytes[4] == 0 && result.image.bytes[5] == 255);
  assert(result.image.bytes[7] == 127);
}

void arbitrary_rotation_expands_canvas_and_uses_transparent_edges() {
  const std::vector<uint8_t> source{
      255, 0, 0, 255, 0, 0, 255, 255};
  RasterEditGraph graph;
  graph.nodes.push_back(node(9, pikdraw::document::RasterRotateParams{45.0}));
  auto result = evaluate(source, 2, 1, std::move(graph));
  assert(result.succeeded());
  assert(result.image.width == 3 && result.image.height == 3);
  assert(result.image.bytes[11] == 0);  // top-right falls outside the rotated source
  assert(result.image.bytes[27] == 0);  // bottom-left falls outside the rotated source
}

void malformed_graphs_and_unsupported_encodings_are_rejected() {
  const std::vector<uint8_t> source{0, 0, 0, 255};
  RasterEditGraph duplicate_ids;
  duplicate_ids.nodes.push_back(node(1, pikdraw::document::RasterFlipParams{true, false}));
  duplicate_ids.nodes.push_back(node(1, pikdraw::document::RasterFlipParams{false, true}));
  auto malformed = evaluate(source, 1, 1, std::move(duplicate_ids));
  assert(malformed.status == RasterEvaluationStatus::InvalidGraph);
  assert(malformed.image.bytes.empty());

  RasterMemoryBudget budget(1024);
  const RasterPixelView unsupported{source, 1, 1, 4,
      static_cast<RasterPixelEncoding>(255)};
  auto result = RasterEditEvaluator::evaluate(unsupported, {}, budget);
  assert(result.status == RasterEvaluationStatus::UnsupportedEncoding);
  assert(result.image.bytes.empty());
}

void cancellation_and_budget_failure_release_partial_output() {
  const std::vector<uint8_t> source(4 * 4 * 4, 0x7f);
  RasterMemoryBudget budget(1024);
  RasterEvaluationOptions cancelled;
  cancelled.cancelled = [] { return true; };
  const RasterPixelView view{source, 4, 4, 16,
      RasterPixelEncoding::SRgbRgba8StraightAlpha};
  auto stopped = RasterEditEvaluator::evaluate(view, {}, budget, cancelled);
  assert(stopped.status == RasterEvaluationStatus::Cancelled);
  assert(stopped.image.bytes.empty());
  assert(budget.reserved_bytes() == 0);

  RasterMemoryBudget too_small(16);
  auto rejected = RasterEditEvaluator::evaluate(view, {}, too_small);
  assert(rejected.status == RasterEvaluationStatus::InsufficientMemory);
  assert(rejected.image.bytes.empty());
  assert(too_small.reserved_bytes() == 0);
}

void cancellation_interrupts_bulk_copy_chunks_and_releases_memory() {
  constexpr uint32_t width = 131072;
  const std::vector<uint8_t> source(static_cast<size_t>(width) * 4, 64);
  RasterMemoryBudget budget(2 * 1024 * 1024);
  RasterEvaluationOptions options;
  size_t cancellation_checks = 0;
  options.cancelled = [&] { return ++cancellation_checks >= 3; };
  const RasterPixelView view{source, width, 1,
      static_cast<uint64_t>(width) * 4,
      RasterPixelEncoding::SRgbRgba8StraightAlpha};

  const auto result = RasterEditEvaluator::evaluate(
      view, RasterEditGraph{}, budget, options);
  assert(result.status == RasterEvaluationStatus::Cancelled);
  assert(result.image.bytes.empty());
  assert(cancellation_checks == 3);
  assert(budget.reserved_bytes() == 0);
}

void cancellation_interrupts_wide_pixel_rows_and_releases_memory() {
  constexpr uint32_t width = 4096;
  const std::vector<uint8_t> source(static_cast<size_t>(width) * 4, 128);
  RasterMemoryBudget budget(256 * 1024);
  RasterEvaluationOptions options;
  size_t cancellation_checks = 0;
  options.cancelled = [&] { return ++cancellation_checks >= 3; };
  RasterEditGraph graph;
  graph.nodes.push_back(node(31,
      pikdraw::document::RasterExposureParams{0.5}));
  const RasterPixelView view{source, width, 1,
      static_cast<uint64_t>(width) * 4,
      RasterPixelEncoding::SRgbRgba8StraightAlpha};

  const auto result = RasterEditEvaluator::evaluate(view, graph, budget, options);
  assert(result.status == RasterEvaluationStatus::Cancelled);
  assert(result.image.bytes.empty());
  assert(cancellation_checks == 3);
  assert(budget.reserved_bytes() == 0);
}

void malformed_views_and_overflow_are_rejected_without_allocation() {
  RasterMemoryBudget budget(std::numeric_limits<uint64_t>::max());
  const std::array<uint8_t, 4> pixel{0, 0, 0, 255};
  const RasterPixelView short_stride{pixel, 1, 1, 3,
      RasterPixelEncoding::SRgbRgba8StraightAlpha};
  auto short_result = RasterEditEvaluator::evaluate(short_stride, {}, budget);
  assert(short_result.status == RasterEvaluationStatus::InvalidSource);
  assert(budget.reserved_bytes() == 0);

  const RasterPixelView missing_bytes{pixel, 1, 2, 4,
      RasterPixelEncoding::SRgbRgba8StraightAlpha};
  auto range_result = RasterEditEvaluator::evaluate(missing_bytes, {}, budget);
  assert(range_result.status == RasterEvaluationStatus::InvalidSource);
  assert(budget.reserved_bytes() == 0);

  const RasterPixelView overflowing{pixel, 0xffffffffu, 0xffffffffu,
      0xffffffffffffffffull,
      RasterPixelEncoding::SRgbRgba8StraightAlpha};
  auto overflow_result = RasterEditEvaluator::evaluate(overflowing, {}, budget);
  assert(overflow_result.status == RasterEvaluationStatus::InvalidSource);
  assert(budget.reserved_bytes() == 0);
}

}  // namespace

int main() {
  crop_then_flip_uses_pixel_edges_and_graph_order();
  crop_then_point_edits_fit_crop_only_memory_budget();
  exposure_is_linear_light_and_preserves_alpha();
  exposure_mask_limits_adjustment_and_rejects_wrong_graph_dimensions();
  packed_rgba8_evaluation_preserves_channel_order_and_alpha();
  levels_respects_range_channel_alpha_and_u16_precision();
  levels_identity_is_bit_exact_and_invalid_parameters_are_rejected();
  light_adjustment_targets_tones_preserves_alpha_and_validates_u16();
  channel_mixer_uses_linear_rgb_preserves_alpha_and_validates_matrix();
  color_balance_targets_tonal_range_preserves_alpha_and_mask();
  clone_stamp_freezes_source_and_blends_u16_alpha();
  raster_eraser_changes_alpha_non_destructively_with_u8_and_u16_coverage();
  hsl_rotates_hue_preserves_alpha_and_respects_u16_and_gray();
  hsl_identity_is_bit_exact_and_out_of_range_values_are_rejected();
  curves_adjust_selected_channel_preserve_alpha_and_keep_u16_precision();
  curves_identity_is_bit_exact_and_invalid_points_are_rejected();
  white_balance_uses_linear_rgb_preserves_alpha_and_validates_ranges();
  vibrance_boosts_muted_colors_more_than_vivid_colors_and_preserves_alpha();
  vibrance_identity_is_bit_exact_and_invalid_amount_is_rejected();
  selective_hsl_targets_feathered_hue_bands_and_preserves_neutrals_alpha();
  selective_hsl_supports_u16_identity_and_rejects_invalid_ranges();
  u16_exposure_flip_rotate_preserve_precision_and_alpha();
  right_angle_rotation_is_exact_and_keeps_pixel_values();
  arbitrary_rotation_expands_canvas_and_uses_transparent_edges();
  malformed_graphs_and_unsupported_encodings_are_rejected();
  cancellation_and_budget_failure_release_partial_output();
  cancellation_interrupts_bulk_copy_chunks_and_releases_memory();
  cancellation_interrupts_wide_pixel_rows_and_releases_memory();
  malformed_views_and_overflow_are_rejected_without_allocation();
  return 0;
}
