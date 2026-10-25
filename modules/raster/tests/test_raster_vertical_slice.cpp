#include "pikdraw/document/commands/raster_edit_commands.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/document/storage/native_format.hpp"
#include "pikdraw/raster/evaluator.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

using namespace pikdraw;
using namespace pikdraw::document;

void hsl_command_roundtrip_and_undo() {
  Document document("raster-hsl-vertical-slice");
  ImageObject image;
  image.id = generate_object_id();
  image.name = "HSL graph round-trip";
  image.width_px = 1;
  image.height_px = 1;
  image.bounds_rect = {0.0, 0.0, 1.0, 1.0};
  image.pixels_rgba = {0x0000ff49u};
  image.source.media_type = "application/x-pikdraw-rgba8";
  image.source.original_bytes = {0, 0, 255, 73};
  image.source.preview_pixel_color_basis = RasterPixelColorBasis::AssumedSRgb;
  assert(document.execute_command(
      std::make_unique<AddObjectCommand>(image)).success);

  const RasterEditNode hsl_node{
      RasterEditNodeId{501}, true, RasterHslParams{120.0, 0.0, 0.0}};
  assert(document.execute_command(std::make_unique<SetRasterEditNodeCommand>(
      image.id, hsl_node)).success);
  const auto bytes = NativeFormat::serialize(*document.current_snapshot());
  std::string error;
  const auto reopened = NativeFormat::deserialize(
      bytes.data(), bytes.size(), &error);
  assert(reopened && error.empty());
  const auto* value = reopened->objects().lookup_ref(image.id);
  assert(value && std::holds_alternative<ImageObject>(*value));
  const auto& restored = std::get<ImageObject>(*value);
  assert(restored.edit_graph.nodes.size() == 1);
  assert(restored.edit_graph.nodes.front().id == hsl_node.id);

  const uint32_t packed = restored.rgba_pixels().front();
  const std::vector<uint8_t> rgba{
      static_cast<uint8_t>(packed >> 24),
      static_cast<uint8_t>(packed >> 16),
      static_cast<uint8_t>(packed >> 8),
      static_cast<uint8_t>(packed)};
  raster::RasterMemoryBudget budget(1024);
  const auto evaluated = raster::RasterEditEvaluator::evaluate(
      {rgba, 1, 1, 4, raster::RasterPixelEncoding::SRgbRgba8StraightAlpha},
      restored.edit_graph, budget);
  assert(evaluated.succeeded());
  assert(evaluated.image.bytes == std::vector<uint8_t>({255, 0, 0, 73}));

  assert(document.undo());
  const auto* undone = document.objects().lookup_ref(image.id);
  assert(undone && std::get<ImageObject>(*undone).edit_graph.nodes.empty());
  assert(document.redo());
  const auto* redone = document.objects().lookup_ref(image.id);
  assert(redone && std::get<ImageObject>(*redone).edit_graph.nodes.size() == 1);
}

void vibrance_command_roundtrip_pixels_and_undo() {
  Document document("raster-vibrance-vertical-slice");
  ImageObject image;
  image.id = generate_object_id();
  image.name = "Vibrance graph round-trip";
  image.width_px = 1;
  image.height_px = 1;
  image.bounds_rect = {0.0, 0.0, 1.0, 1.0};
  image.pixels_rgba = {0xc896964du};
  image.source.media_type = "application/x-pikdraw-rgba8";
  image.source.original_bytes = {200, 150, 150, 77};
  image.source.preview_pixel_color_basis = RasterPixelColorBasis::AssumedSRgb;
  assert(document.execute_command(
      std::make_unique<AddObjectCommand>(image)).success);

  const RasterEditNode vibrance_node{
      RasterEditNodeId{701}, true, RasterVibranceParams{100.0}};
  assert(document.execute_command(std::make_unique<SetRasterEditNodeCommand>(
      image.id, vibrance_node)).success);
  const auto bytes = NativeFormat::serialize(*document.current_snapshot());
  std::string error;
  const auto reopened = NativeFormat::deserialize(
      bytes.data(), bytes.size(), &error);
  assert(reopened && error.empty());
  const auto* value = reopened->objects().lookup_ref(image.id);
  assert(value && std::holds_alternative<ImageObject>(*value));
  const auto& restored = std::get<ImageObject>(*value);
  assert(restored.edit_graph.schema_version == RasterEditGraph::kSchemaVersion);
  assert(restored.edit_graph.nodes.size() == 1);
  assert(restored.edit_graph.nodes.front().id == vibrance_node.id);
  const auto* parameters = std::get_if<RasterVibranceParams>(
      &restored.edit_graph.nodes.front().parameters);
  assert(parameters && parameters->amount_percent == 100.0);
  raster::RasterMemoryBudget budget(1024);
  const auto evaluated = raster::RasterEditEvaluator::evaluate(
      {restored.source.original_data(), 1, 1, 4,
       raster::RasterPixelEncoding::SRgbRgba8StraightAlpha},
      restored.edit_graph, budget);
  assert(evaluated.succeeded());
  assert(evaluated.image.bytes[0] > 200 && evaluated.image.bytes[1] < 150 &&
      evaluated.image.bytes[2] < 150 && evaluated.image.bytes[3] == 77);

  assert(document.undo());
  const auto* undone = document.objects().lookup_ref(image.id);
  assert(undone && std::get<ImageObject>(*undone).edit_graph.nodes.empty());
  assert(document.redo());
  const auto* redone = document.objects().lookup_ref(image.id);
  assert(redone && std::get<ImageObject>(*redone).edit_graph.nodes.size() == 1);
}

void selective_hsl_command_roundtrip_pixels_and_undo() {
  Document document("raster-selective-hsl-vertical-slice");
  ImageObject image;
  image.id = generate_object_id();
  image.name = "Selective HSL graph round-trip";
  image.width_px = 1;
  image.height_px = 1;
  image.bounds_rect = {0.0, 0.0, 1.0, 1.0};
  image.pixels_rgba = {0xff000049u};
  image.source.media_type = "application/x-pikdraw-rgba8";
  image.source.original_bytes = {255, 0, 0, 73};
  image.source.preview_pixel_color_basis = RasterPixelColorBasis::AssumedSRgb;
  assert(document.execute_command(
      std::make_unique<AddObjectCommand>(image)).success);

  const RasterSelectiveHslParams parameters{
      RasterHueRange::Reds, 30.0, 0.0, 0.0, 0.0};
  const RasterEditNode selective_node{
      RasterEditNodeId{702}, true, parameters};
  assert(document.execute_command(std::make_unique<SetRasterEditNodeCommand>(
      image.id, selective_node)).success);
  const auto bytes = NativeFormat::serialize(*document.current_snapshot());
  std::string error;
  const auto reopened = NativeFormat::deserialize(
      bytes.data(), bytes.size(), &error);
  assert(reopened && error.empty());
  const auto* value = reopened->objects().lookup_ref(image.id);
  assert(value && std::holds_alternative<ImageObject>(*value));
  const auto& restored = std::get<ImageObject>(*value);
  assert(restored.edit_graph.schema_version == RasterEditGraph::kSchemaVersion);
  assert(restored.edit_graph.nodes.size() == 1);
  assert(restored.edit_graph.nodes.front().id == selective_node.id);
  const auto* restored_parameters = std::get_if<RasterSelectiveHslParams>(
      &restored.edit_graph.nodes.front().parameters);
  assert(restored_parameters && restored_parameters->range == parameters.range &&
      restored_parameters->hue_degrees == parameters.hue_degrees &&
      restored_parameters->saturation_percent == parameters.saturation_percent &&
      restored_parameters->lightness_percent == parameters.lightness_percent &&
      restored_parameters->feather_percent == parameters.feather_percent);
  raster::RasterMemoryBudget budget(1024);
  const auto evaluated = raster::RasterEditEvaluator::evaluate(
      {restored.source.original_data(), 1, 1, 4,
       raster::RasterPixelEncoding::SRgbRgba8StraightAlpha},
      restored.edit_graph, budget);
  assert(evaluated.succeeded());
  assert(evaluated.image.bytes[0] == 255 && evaluated.image.bytes[1] > 0 &&
      evaluated.image.bytes[2] == 0 && evaluated.image.bytes[3] == 73);

  assert(document.undo());
  const auto* undone = document.objects().lookup_ref(image.id);
  assert(undone && std::get<ImageObject>(*undone).edit_graph.nodes.empty());
  assert(document.redo());
  const auto* redone = document.objects().lookup_ref(image.id);
  assert(redone && std::get<ImageObject>(*redone).edit_graph.nodes.size() == 1);
}

void curves_command_roundtrip_and_undo() {
  Document document("raster-curves-vertical-slice");
  ImageObject image;
  image.id = generate_object_id();
  image.name = "Curves graph round-trip";
  image.width_px = 1;
  image.height_px = 1;
  image.bounds_rect = {0.0, 0.0, 1.0, 1.0};
  image.pixels_rgba = {0x4080c049u};
  image.source.media_type = "application/x-pikdraw-rgba8";
  image.source.original_bytes = {64, 128, 192, 73};
  image.source.preview_pixel_color_basis = RasterPixelColorBasis::AssumedSRgb;
  assert(document.execute_command(
      std::make_unique<AddObjectCommand>(image)).success);

  RasterCurvesParams parameters;
  parameters.channel = RasterToneChannel::Blue;
  parameters.output_points = {0.0, 0.0, 0.25, 0.5, 1.0};
  const RasterEditNode curves_node{
      RasterEditNodeId{601}, true, parameters};
  assert(document.execute_command(std::make_unique<SetRasterEditNodeCommand>(
      image.id, curves_node)).success);
  RasterCurvesParams red_parameters;
  red_parameters.channel = RasterToneChannel::Red;
  red_parameters.output_points = {0.0, 0.2, 0.4, 0.7, 1.0};
  const RasterEditNode red_curves_node{
      RasterEditNodeId{602}, true, red_parameters};
  assert(document.execute_command(std::make_unique<SetRasterEditNodeCommand>(
      image.id, red_curves_node)).success);
  const auto bytes = NativeFormat::serialize(*document.current_snapshot());
  std::string error;
  const auto reopened = NativeFormat::deserialize(
      bytes.data(), bytes.size(), &error);
  assert(reopened && error.empty());
  const auto* value = reopened->objects().lookup_ref(image.id);
  assert(value && std::holds_alternative<ImageObject>(*value));
  const auto& restored = std::get<ImageObject>(*value);
  assert(restored.edit_graph.schema_version == RasterEditGraph::kSchemaVersion);
  assert(restored.edit_graph.nodes.size() == 2);
  const auto* restored_curves = std::get_if<RasterCurvesParams>(
      &restored.edit_graph.nodes[0].parameters);
  const auto* restored_red_curves = std::get_if<RasterCurvesParams>(
      &restored.edit_graph.nodes[1].parameters);
  assert(restored.edit_graph.nodes[0].id == curves_node.id &&
         restored_curves && restored_curves->channel == RasterToneChannel::Blue &&
         restored_curves->output_points == parameters.output_points);
  assert(restored.edit_graph.nodes[1].id == red_curves_node.id &&
         restored_red_curves &&
         restored_red_curves->channel == RasterToneChannel::Red &&
         restored_red_curves->output_points == red_parameters.output_points);

  const uint32_t packed = restored.rgba_pixels().front();
  const std::vector<uint8_t> rgba{
      static_cast<uint8_t>(packed >> 24),
      static_cast<uint8_t>(packed >> 16),
      static_cast<uint8_t>(packed >> 8),
      static_cast<uint8_t>(packed)};
  raster::RasterMemoryBudget budget(1024);
  const auto evaluated = raster::RasterEditEvaluator::evaluate(
      {rgba, 1, 1, 4, raster::RasterPixelEncoding::SRgbRgba8StraightAlpha},
      restored.edit_graph, budget);
  assert(evaluated.succeeded());
  assert(evaluated.image.bytes[0] != 64 && evaluated.image.bytes[1] == 128 &&
         evaluated.image.bytes[2] != 192 && evaluated.image.bytes[3] == 73);

  assert(document.undo());
  const auto* undone = document.objects().lookup_ref(image.id);
  assert(undone && std::get<ImageObject>(*undone).edit_graph.nodes.size() == 1);
  assert(document.undo());
  undone = document.objects().lookup_ref(image.id);
  assert(undone && std::get<ImageObject>(*undone).edit_graph.nodes.empty());
  assert(document.redo());
  assert(document.redo());
  const auto* redone = document.objects().lookup_ref(image.id);
  assert(redone && std::get<ImageObject>(*redone).edit_graph.nodes.size() == 2);
}

void white_balance_command_roundtrip_evaluation_and_undo() {
  Document document("raster-white-balance-vertical-slice");
  ImageObject image;
  image.id = generate_object_id();
  image.name = "White Balance graph round-trip";
  image.width_px = 1;
  image.height_px = 1;
  image.bounds_rect = {0.0, 0.0, 1.0, 1.0};
  image.pixels_rgba = {0x80808049u};
  image.source.media_type = "application/x-pikdraw-rgba8";
  image.source.original_bytes = {128, 128, 128, 73};
  image.source.preview_pixel_color_basis = RasterPixelColorBasis::AssumedSRgb;
  assert(document.execute_command(
      std::make_unique<AddObjectCommand>(image)).success);

  const RasterWhiteBalanceParams parameters{75.0, -20.0};
  const RasterEditNode white_balance_node{
      RasterEditNodeId{701}, true, parameters};
  assert(document.execute_command(std::make_unique<SetRasterEditNodeCommand>(
      image.id, white_balance_node)).success);
  const auto bytes = NativeFormat::serialize(*document.current_snapshot());
  std::string error;
  const auto reopened = NativeFormat::deserialize(
      bytes.data(), bytes.size(), &error);
  assert(reopened && error.empty());
  const auto* value = reopened->objects().lookup_ref(image.id);
  assert(value && std::holds_alternative<ImageObject>(*value));
  const auto& restored = std::get<ImageObject>(*value);
  assert(restored.edit_graph.schema_version == RasterEditGraph::kSchemaVersion);
  assert(restored.edit_graph.nodes.size() == 1);
  assert(restored.edit_graph.nodes.front().id == white_balance_node.id);
  const auto* restored_parameters = std::get_if<RasterWhiteBalanceParams>(
      &restored.edit_graph.nodes.front().parameters);
  assert(restored_parameters &&
      restored_parameters->temperature_shift_percent ==
          parameters.temperature_shift_percent &&
      restored_parameters->tint_percent == parameters.tint_percent);

  const std::vector<uint8_t> source{128, 128, 128, 73};
  raster::RasterMemoryBudget budget(1024);
  const auto evaluated = raster::RasterEditEvaluator::evaluate(
      {source, 1, 1, 4, raster::RasterPixelEncoding::SRgbRgba8StraightAlpha},
      restored.edit_graph, budget);
  assert(evaluated.succeeded());
  assert(evaluated.image.bytes[0] != 128 || evaluated.image.bytes[1] != 128 ||
         evaluated.image.bytes[2] != 128);
  assert(evaluated.image.bytes[3] == 73);

  assert(document.undo());
  const auto* undone = document.objects().lookup_ref(image.id);
  assert(undone && std::get<ImageObject>(*undone).edit_graph.nodes.empty());
  assert(document.redo());
}

void clone_stamp_ui_command_roundtrip_pixels_and_undo() {
  Document document("raster-clone-stamp-vertical-slice");
  ImageObject image;
  image.id = generate_object_id();
  image.name = "Clone Stamp graph round-trip";
  image.width_px = 8;
  image.height_px = 8;
  image.bounds_rect = {0.0, 0.0, 8.0, 8.0};
  image.pixels_rgba.reserve(64);
  image.source.original_bytes.reserve(8u * 8u * 4u);
  image.source.media_type = "image/rgba8";
  image.source.preview_pixel_color_basis = RasterPixelColorBasis::AssumedSRgb;
  for (uint32_t y = 0; y < 8; ++y) {
    for (uint32_t x = 0; x < 8; ++x) {
      const uint8_t red = static_cast<uint8_t>(x * 20);
      const uint8_t green = static_cast<uint8_t>(y * 20);
      const uint8_t blue = 15;
      constexpr uint8_t alpha = 200;
      image.pixels_rgba.push_back((static_cast<uint32_t>(red) << 24) |
          (static_cast<uint32_t>(green) << 16) |
          (static_cast<uint32_t>(blue) << 8) | alpha);
      image.source.original_bytes.insert(image.source.original_bytes.end(),
          {red, green, blue, alpha});
    }
  }
  assert(document.execute_command(
      std::make_unique<AddObjectCommand>(image)).success);

  const RasterEditNode dab{RasterEditNodeId{512}, true,
      RasterCloneStampParams{2.0 / 7.0, 4.0 / 7.0, 3.0 / 7.0, 4.0 / 7.0,
                             25.0, 100.0, 100.0}};
  assert(document.execute_command(std::make_unique<SetRasterEditNodeCommand>(
      image.id, dab)).success);
  const auto bytes = NativeFormat::serialize(*document.current_snapshot());
  std::string error;
  const auto reopened = NativeFormat::deserialize(bytes.data(), bytes.size(), &error);
  assert(reopened && error.empty());
  const auto* restored_value = reopened->objects().lookup_ref(image.id);
  assert(restored_value && std::holds_alternative<ImageObject>(*restored_value));
  const auto& restored = std::get<ImageObject>(*restored_value);
  assert(restored.edit_graph.nodes.size() == 1);
  assert(std::get<RasterCloneStampParams>(restored.edit_graph.nodes.front().parameters)
      .source_x == 2.0 / 7.0);

  std::vector<uint8_t> source_rgba;
  source_rgba.reserve(restored.rgba_pixels().size() * 4);
  for (const uint32_t packed : restored.rgba_pixels()) {
    source_rgba.push_back(static_cast<uint8_t>(packed >> 24));
    source_rgba.push_back(static_cast<uint8_t>(packed >> 16));
    source_rgba.push_back(static_cast<uint8_t>(packed >> 8));
    source_rgba.push_back(static_cast<uint8_t>(packed));
  }
  raster::RasterMemoryBudget budget(4096);
  const auto evaluated = raster::RasterEditEvaluator::evaluate(
      {source_rgba, 8, 8, 32,
       raster::RasterPixelEncoding::SRgbRgba8StraightAlpha},
      restored.edit_graph, budget);
  assert(evaluated.succeeded());
  const size_t x3 = (4u * 8u + 3u) * 4u;
  const size_t x4 = (4u * 8u + 4u) * 4u;
  assert(evaluated.image.bytes[x3] == 40 && evaluated.image.bytes[x4] == 60);
  assert(evaluated.image.bytes[x4 + 3] == 200);
  assert(document.undo());
  assert(std::get<ImageObject>(*document.objects().lookup_ref(image.id))
      .edit_graph.nodes.empty());
  assert(document.redo());
  assert(std::get<ImageObject>(*document.objects().lookup_ref(image.id))
      .edit_graph.nodes.size() == 1);
}

int main() {
  hsl_command_roundtrip_and_undo();
  vibrance_command_roundtrip_pixels_and_undo();
  selective_hsl_command_roundtrip_pixels_and_undo();
  curves_command_roundtrip_and_undo();
  white_balance_command_roundtrip_evaluation_and_undo();
  clone_stamp_ui_command_roundtrip_pixels_and_undo();
  Document document("raster-edit-vertical-slice");
  ImageObject image;
  image.id = generate_object_id();
  image.name = "Raster edit test";
  image.width_px = 2;
  image.height_px = 2;
  image.bounds_rect = {10.0, 20.0, 40.0, 40.0};
  image.pixels_rgba = {
      0x404040ffu, 0xffffffffu,
      0x202020ffu, 0xff0000ffu};
  image.source.media_type = "application/x-pikdraw-rgba8";
  image.source.original_bytes = {0x52, 0x47, 0x42, 0x41};
  assert(document.execute_command(
      std::make_unique<AddObjectCommand>(image)).success);

  constexpr RasterEditNodeId crop_id{101};
  constexpr RasterEditNodeId exposure_id{102};
  constexpr RasterEditNodeId flip_id{103};
  const RasterEditNode initial_crop{
      crop_id, true, RasterCropParams{0.0, 0.0, 0.75, 1.0}};
  const RasterEditNode final_crop{
      crop_id, true, RasterCropParams{0.0, 0.0, 0.5, 1.0}};
  assert(document.execute_command(std::make_unique<SetRasterEditNodeCommand>(
      image.id, initial_crop)).success);
  assert(document.execute_command(std::make_unique<SetRasterEditNodeCommand>(
      image.id, final_crop)).success);
  assert(document.execute_command(std::make_unique<SetRasterEditNodeCommand>(
      image.id, RasterEditNode{exposure_id, true,
                               RasterExposureParams{1.0}})).success);
  assert(document.execute_command(std::make_unique<SetRasterEditNodeCommand>(
      image.id, RasterEditNode{flip_id, true,
                               RasterFlipParams{false, true}})).success);

  const auto* edited_value = document.objects().lookup_ref(image.id);
  assert(edited_value && std::holds_alternative<ImageObject>(*edited_value));
  const auto& edited = std::get<ImageObject>(*edited_value);
  assert(edited.edit_graph.nodes.size() == 3);
  assert(edited.edit_graph.nodes[0].id == crop_id);
  assert(std::get<RasterCropParams>(edited.edit_graph.nodes[0].parameters).width ==
         0.5);
  assert(edited.edit_graph.nodes[1].id == exposure_id);
  assert(edited.edit_graph.nodes[2].id == flip_id);

  const auto before_invalid = document.current_snapshot();
  const auto invalid = document.execute_command(
      std::make_unique<SetRasterEditNodeCommand>(
          image.id, RasterEditNode{crop_id, true,
                                   RasterCropParams{0.8, 0.0, 0.5, 1.0}}));
  assert(!invalid.success);
  assert(document.current_snapshot() == before_invalid);

  assert(document.undo());
  const auto* undone_value = document.objects().lookup_ref(image.id);
  assert(undone_value &&
         std::get<ImageObject>(*undone_value).edit_graph.nodes.size() == 2);
  assert(document.redo());

  const auto snapshot = document.current_snapshot();
  const auto bytes = NativeFormat::serialize(*snapshot);
  std::string error;
  const auto reopened = NativeFormat::deserialize(
      bytes.data(), bytes.size(), &error);
  assert(reopened && error.empty());
  const auto* restored_value = reopened->objects().lookup_ref(image.id);
  assert(restored_value && std::holds_alternative<ImageObject>(*restored_value));
  const auto& restored = std::get<ImageObject>(*restored_value);
  assert(restored.source.original_data() == image.source.original_bytes);
  assert(restored.edit_graph.schema_version == RasterEditGraph::kSchemaVersion);
  assert(restored.edit_graph.nodes.size() == 3);
  assert(restored.edit_graph.nodes[0].id == crop_id);
  assert(restored.edit_graph.nodes[1].id == exposure_id);
  assert(restored.edit_graph.nodes[2].id == flip_id);

  std::vector<uint8_t> source_rgba(restored.rgba_pixels().size() * 4);
  for (size_t index = 0; index < restored.rgba_pixels().size(); ++index) {
    const uint32_t packed = restored.rgba_pixels()[index];
    source_rgba[index * 4] = static_cast<uint8_t>(packed >> 24);
    source_rgba[index * 4 + 1] = static_cast<uint8_t>(packed >> 16);
    source_rgba[index * 4 + 2] = static_cast<uint8_t>(packed >> 8);
    source_rgba[index * 4 + 3] = static_cast<uint8_t>(packed);
  }
  raster::RasterMemoryBudget budget(4096);
  const auto evaluated = raster::RasterEditEvaluator::evaluate(
      {source_rgba, 2, 2, 8,
       raster::RasterPixelEncoding::SRgbRgba8StraightAlpha},
      restored.edit_graph, budget);
  assert(evaluated.succeeded());
  assert(evaluated.image.width == 1 && evaluated.image.height == 2);
  assert(evaluated.image.bytes.size() == 8);
  assert(evaluated.image.bytes[0] == 0x2f);
  assert(evaluated.image.bytes[1] == 0x2f);
  assert(evaluated.image.bytes[2] == 0x2f);
  assert(evaluated.image.bytes[3] == 0xff);
  assert(evaluated.image.bytes[4] == 0x5a);
  assert(evaluated.image.bytes[5] == 0x5a);
  assert(evaluated.image.bytes[6] == 0x5a);
  assert(evaluated.image.bytes[7] == 0xff);

  std::cout << "[PASS] raster edit command -> .pr reopen -> pixel evaluator\n";
  return 0;
}
