#include "pikdraw/document/commands/raster_edit_commands.hpp"

#include "pikdraw/document/document.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace pikdraw::document {
namespace {

bool valid_node(const RasterEditNode& node) noexcept {
  if (!node.id.valid() || node.algorithm_version != 1) return false;
  if (node.mask) {
    const auto& mask = *node.mask;
    const uint64_t mask_pixels = static_cast<uint64_t>(mask.width_px) * mask.height_px;
    const bool pointwise =
        std::holds_alternative<RasterExposureParams>(node.parameters) ||
        std::holds_alternative<RasterLevelsParams>(node.parameters) ||
        std::holds_alternative<RasterHslParams>(node.parameters) ||
        std::holds_alternative<RasterCurvesParams>(node.parameters) ||
        std::holds_alternative<RasterWhiteBalanceParams>(node.parameters) ||
        std::holds_alternative<RasterVibranceParams>(node.parameters) ||
        std::holds_alternative<RasterSelectiveHslParams>(node.parameters) ||
        std::holds_alternative<RasterLightParams>(node.parameters) ||
        std::holds_alternative<RasterColorBalanceParams>(node.parameters) ||
        std::holds_alternative<RasterChannelMixerParams>(node.parameters);
    const bool valid_payload = mask.kind == RasterAdjustmentMaskKind::Bitmap
        ? mask_pixels == mask.coverage.size()
        : (mask.kind == RasterAdjustmentMaskKind::Rectangle ||
           mask.kind == RasterAdjustmentMaskKind::LinearGradient ||
           mask.kind == RasterAdjustmentMaskKind::RadialGradient) && mask.coverage.empty() &&
          std::isfinite(mask.left) && std::isfinite(mask.top) &&
          std::isfinite(mask.width) && std::isfinite(mask.height) &&
          std::isfinite(mask.feather_px) && mask.left >= 0.0 && mask.top >= 0.0 &&
          mask.width > 0.0 && mask.height > 0.0 &&
          mask.left + mask.width <= 1.0 && mask.top + mask.height <= 1.0 &&
          mask.feather_px >= 0.0 &&
          mask.feather_px <= (std::max)(mask.width_px, mask.height_px) &&
          (mask.kind == RasterAdjustmentMaskKind::LinearGradient
              ? std::isfinite(mask.gradient_angle_degrees) &&
                    std::abs(mask.gradient_angle_degrees) <= 36000.0
              : mask.gradient_angle_degrees == 0.0);
    if (!pointwise || mask.id == 0 || mask.width_px == 0 || mask.height_px == 0 ||
        mask.density_percent > 100 ||
        (mask.kind != RasterAdjustmentMaskKind::LinearGradient &&
         mask.gradient_angle_degrees != 0.0) || !valid_payload) return false;
  }
  return std::visit([](const auto& params) noexcept {
    using T = std::decay_t<decltype(params)>;
    if constexpr (std::is_same_v<T, RasterCropParams>) {
      return std::isfinite(params.x) && std::isfinite(params.y) &&
          std::isfinite(params.width) && std::isfinite(params.height) &&
          params.x >= 0.0 && params.y >= 0.0 && params.width > 0.0 &&
          params.height > 0.0 && params.x + params.width <= 1.0 &&
          params.y + params.height <= 1.0;
    } else if constexpr (std::is_same_v<T, RasterRotateParams>) {
      return std::isfinite(params.degrees) &&
          std::abs(params.degrees) <= 360000.0;
    } else if constexpr (std::is_same_v<T, RasterExposureParams>) {
      return std::isfinite(params.exposure_ev) &&
          std::abs(params.exposure_ev) <= 10.0;
    } else if constexpr (std::is_same_v<T, RasterLevelsParams>) {
      const auto channel = static_cast<uint8_t>(params.channel);
      return channel <= static_cast<uint8_t>(RasterToneChannel::Blue) &&
          std::isfinite(params.black_in) && std::isfinite(params.white_in) &&
          std::isfinite(params.gamma) && std::isfinite(params.black_out) &&
          std::isfinite(params.white_out) && params.black_in >= 0.0 &&
          params.white_in <= 1.0 && params.black_in < params.white_in &&
          params.gamma >= 0.1 && params.gamma <= 10.0 &&
          params.black_out >= 0.0 && params.white_out <= 1.0 &&
          params.black_out <= params.white_out;
    } else if constexpr (std::is_same_v<T, RasterHslParams>) {
      return std::isfinite(params.hue_degrees) &&
          std::abs(params.hue_degrees) <= 180.0 &&
          std::isfinite(params.saturation_percent) &&
          std::abs(params.saturation_percent) <= 100.0 &&
          std::isfinite(params.lightness_percent) &&
          std::abs(params.lightness_percent) <= 100.0;
    } else if constexpr (std::is_same_v<T, RasterCurvesParams>) {
      return static_cast<uint8_t>(params.channel) <=
          static_cast<uint8_t>(RasterToneChannel::Blue) &&
          std::all_of(params.output_points.begin(), params.output_points.end(),
              [](double value) {
                return std::isfinite(value) && value >= 0.0 && value <= 1.0;
              });
    } else if constexpr (std::is_same_v<T, RasterWhiteBalanceParams>) {
      return std::isfinite(params.temperature_shift_percent) &&
          std::abs(params.temperature_shift_percent) <= 100.0 &&
          std::isfinite(params.tint_percent) &&
          std::abs(params.tint_percent) <= 100.0;
    } else if constexpr (std::is_same_v<T, RasterVibranceParams>) {
      return std::isfinite(params.amount_percent) &&
          std::abs(params.amount_percent) <= 100.0;
    } else if constexpr (std::is_same_v<T, RasterSelectiveHslParams>) {
      return static_cast<uint8_t>(params.range) <=
              static_cast<uint8_t>(RasterHueRange::Magentas) &&
          std::isfinite(params.hue_degrees) &&
          std::abs(params.hue_degrees) <= 30.0 &&
          std::isfinite(params.saturation_percent) &&
          std::abs(params.saturation_percent) <= 100.0 &&
          std::isfinite(params.lightness_percent) &&
          std::abs(params.lightness_percent) <= 100.0 &&
          std::isfinite(params.feather_percent) &&
          params.feather_percent >= 0.0 && params.feather_percent <= 100.0;
    } else if constexpr (std::is_same_v<T, RasterLightParams>) {
      return std::isfinite(params.exposure_ev) &&
          std::abs(params.exposure_ev) <= 5.0 &&
          std::isfinite(params.contrast_percent) &&
          std::abs(params.contrast_percent) <= 100.0 &&
          std::isfinite(params.highlights_percent) &&
          std::abs(params.highlights_percent) <= 100.0 &&
          std::isfinite(params.shadows_percent) &&
          std::abs(params.shadows_percent) <= 100.0 &&
          std::isfinite(params.whites_percent) &&
          std::abs(params.whites_percent) <= 100.0 &&
          std::isfinite(params.blacks_percent) &&
          std::abs(params.blacks_percent) <= 100.0;
    } else if constexpr (std::is_same_v<T, RasterColorBalanceParams>) {
      return static_cast<uint8_t>(params.range) <=
              static_cast<uint8_t>(RasterTonalRange::Highlights) &&
          std::isfinite(params.red_cyan_percent) &&
          std::abs(params.red_cyan_percent) <= 100.0 &&
          std::isfinite(params.green_magenta_percent) &&
          std::abs(params.green_magenta_percent) <= 100.0 &&
          std::isfinite(params.blue_yellow_percent) &&
          std::abs(params.blue_yellow_percent) <= 100.0;
    } else if constexpr (std::is_same_v<T, RasterChannelMixerParams>) {
      return std::all_of(params.coefficients.begin(), params.coefficients.end(),
          [](double value) { return std::isfinite(value) && value >= -2.0 && value <= 2.0; });
    } else if constexpr (std::is_same_v<T, RasterCloneStampParams>) {
      return std::isfinite(params.source_x) && std::isfinite(params.source_y) &&
          std::isfinite(params.target_x) && std::isfinite(params.target_y) &&
          params.source_x >= 0.0 && params.source_x <= 1.0 &&
          params.source_y >= 0.0 && params.source_y <= 1.0 &&
          params.target_x >= 0.0 && params.target_x <= 1.0 &&
          params.target_y >= 0.0 && params.target_y <= 1.0 &&
          std::isfinite(params.radius_percent) && params.radius_percent > 0.0 &&
          params.radius_percent <= 25.0 &&
          std::isfinite(params.hardness_percent) &&
          params.hardness_percent >= 0.0 && params.hardness_percent <= 100.0 &&
          std::isfinite(params.opacity_percent) &&
          params.opacity_percent >= 0.0 && params.opacity_percent <= 100.0;
    } else if constexpr (std::is_same_v<T, RasterEraserParams>) {
      return !params.points.empty() && params.points.size() <= RasterEraserParams::kMaxPoints &&
          std::isfinite(params.diameter_px) && params.diameter_px > 0.0 &&
          params.diameter_px <= 4096.0 &&
          std::all_of(params.points.begin(), params.points.end(), [](const auto& point) {
            return std::isfinite(point.x) && std::isfinite(point.y) &&
                point.x >= 0.0 && point.x <= 1.0 &&
                point.y >= 0.0 && point.y <= 1.0;
          });
    } else {
      return true;
    }
  }, node.parameters);
}

bool same_node(const RasterEditNode& left,
               const RasterEditNode& right) noexcept {
  if (left.id != right.id || left.enabled != right.enabled ||
      left.algorithm_version != right.algorithm_version ||
      left.parameters.index() != right.parameters.index() || left.mask != right.mask)
    return false;
  return std::visit([&](const auto& params) noexcept {
    using T = std::decay_t<decltype(params)>;
    const auto* other = std::get_if<T>(&right.parameters);
    if (!other) return false;
    if constexpr (std::is_same_v<T, RasterCropParams>)
      return params.x == other->x && params.y == other->y &&
          params.width == other->width && params.height == other->height;
    else if constexpr (std::is_same_v<T, RasterFlipParams>)
      return params.horizontal == other->horizontal &&
          params.vertical == other->vertical;
    else if constexpr (std::is_same_v<T, RasterRotateParams>)
      return params.degrees == other->degrees;
    else if constexpr (std::is_same_v<T, RasterExposureParams>)
      return params.exposure_ev == other->exposure_ev;
    else if constexpr (std::is_same_v<T, RasterLevelsParams>)
      return params.channel == other->channel &&
          params.black_in == other->black_in &&
          params.white_in == other->white_in &&
          params.gamma == other->gamma &&
          params.black_out == other->black_out &&
          params.white_out == other->white_out;
    else if constexpr (std::is_same_v<T, RasterHslParams>)
      return params.hue_degrees == other->hue_degrees &&
          params.saturation_percent == other->saturation_percent &&
          params.lightness_percent == other->lightness_percent;
    else if constexpr (std::is_same_v<T, RasterCurvesParams>)
      return params.channel == other->channel &&
          params.output_points == other->output_points;
    else if constexpr (std::is_same_v<T, RasterWhiteBalanceParams>)
      return params.temperature_shift_percent == other->temperature_shift_percent &&
          params.tint_percent == other->tint_percent;
    else if constexpr (std::is_same_v<T, RasterVibranceParams>)
      return params.amount_percent == other->amount_percent;
    else if constexpr (std::is_same_v<T, RasterSelectiveHslParams>)
      return params.range == other->range &&
          params.hue_degrees == other->hue_degrees &&
          params.saturation_percent == other->saturation_percent &&
          params.lightness_percent == other->lightness_percent &&
          params.feather_percent == other->feather_percent;
    else if constexpr (std::is_same_v<T, RasterLightParams>)
      return params.exposure_ev == other->exposure_ev &&
          params.contrast_percent == other->contrast_percent &&
          params.highlights_percent == other->highlights_percent &&
          params.shadows_percent == other->shadows_percent &&
          params.whites_percent == other->whites_percent &&
          params.blacks_percent == other->blacks_percent;
    else if constexpr (std::is_same_v<T, RasterColorBalanceParams>)
      return params.range == other->range &&
          params.red_cyan_percent == other->red_cyan_percent &&
          params.green_magenta_percent == other->green_magenta_percent &&
          params.blue_yellow_percent == other->blue_yellow_percent &&
          params.preserve_luminosity == other->preserve_luminosity;
    else if constexpr (std::is_same_v<T, RasterEraserParams>)
      return params.diameter_px == other->diameter_px &&
          params.points == other->points;
    else if constexpr (std::is_same_v<T, RasterChannelMixerParams>)
      return params.coefficients == other->coefficients;
    else
      return params.source_x == other->source_x &&
          params.source_y == other->source_y &&
          params.target_x == other->target_x &&
          params.target_y == other->target_y &&
          params.radius_percent == other->radius_percent &&
          params.hardness_percent == other->hardness_percent &&
          params.opacity_percent == other->opacity_percent;
  }, left.parameters);
}

}  // namespace

SetRasterEditNodeCommand::SetRasterEditNodeCommand(ObjectId image_id,
                                                   RasterEditNode node)
    : image_id_(image_id), node_(std::move(node)) {}

CommandResult SetRasterEditNodeCommand::execute(Document& doc) {
  if (!valid_node(node_))
    return CommandResult::fail("Raster edit parameters or node ID are invalid");
  const auto* value = doc.objects().lookup_ref(image_id_);
  if (!value || !std::holds_alternative<ImageObject>(*value))
    return CommandResult::fail("Raster image object was not found");
  const auto& current = std::get<ImageObject>(*value);
  if (current.locked)
    return CommandResult::fail("Raster image is locked");
  if (!current.asset_source && current.source.original_data().empty() &&
      current.rgba16le_pixels().empty())
    return CommandResult::fail(
        "Non-destructive edits require a preserved encoded or high-precision raster source");

  auto updated = current;
  auto found = std::find_if(updated.edit_graph.nodes.begin(),
      updated.edit_graph.nodes.end(), [&](const RasterEditNode& existing) {
        return existing.id == node_.id;
      });
  if (found != updated.edit_graph.nodes.end() && same_node(*found, node_))
    return CommandResult::no_change();
  if (found == updated.edit_graph.nodes.end()) {
    if (updated.edit_graph.nodes.size() >= RasterEditGraph::kMaxNodes)
      return CommandResult::fail("Raster edit graph is full");
    updated.edit_graph.nodes.push_back(node_);
  } else {
    *found = node_;
  }
  updated.edit_graph.schema_version = RasterEditGraph::kSchemaVersion;

  previous_graph_ = current.edit_graph;
  auto objects = doc.objects().set(image_id_, ObjectVariant{std::move(updated)});
  doc.apply_mutation(doc.page(), std::move(objects));
  executed_ = true;
  return CommandResult::ok();
}

CommandResult SetRasterEditNodeCommand::undo(Document& doc) {
  if (!executed_ || !previous_graph_)
    return CommandResult::fail("Raster edit command has no state to restore");
  const auto* value = doc.objects().lookup_ref(image_id_);
  if (!value || !std::holds_alternative<ImageObject>(*value))
    return CommandResult::fail("Raster image object was not found for Undo");
  auto updated = std::get<ImageObject>(*value);
  updated.edit_graph = *previous_graph_;
  auto objects = doc.objects().set(image_id_, ObjectVariant{std::move(updated)});
  doc.apply_mutation(doc.page(), std::move(objects));
  executed_ = false;
  return CommandResult::ok();
}

SetRasterEditGraphCommand::SetRasterEditGraphCommand(
    ObjectId image_id, RasterEditGraph graph,
    std::optional<uint64_t> expected_epoch,
    std::optional<uint64_t> expected_revision)
    : image_id_(image_id), graph_(std::move(graph)),
      expected_epoch_(expected_epoch), expected_revision_(expected_revision) {}

CommandResult SetRasterEditGraphCommand::execute(Document& doc) {
  if ((expected_epoch_ && doc.epoch() != *expected_epoch_) ||
      (expected_revision_ && doc.revision() != *expected_revision_))
    return CommandResult::fail("Document changed before the raster graph update");
  if (graph_.nodes.size() > RasterEditGraph::kMaxNodes)
    return CommandResult::fail("Raster edit graph is too large");
  std::unordered_set<uint64_t> ids;
  for (const auto& node : graph_.nodes) {
    if (!valid_node(node) || !ids.insert(node.id.value).second)
      return CommandResult::fail("Raster edit graph contains an invalid or duplicate node");
  }
  const auto* value = doc.objects().lookup_ref(image_id_);
  if (!value || !std::holds_alternative<ImageObject>(*value))
    return CommandResult::fail("Raster image object was not found");
  const auto& current = std::get<ImageObject>(*value);
  if (current.locked)
    return CommandResult::fail("Raster image is locked");
  if (!current.asset_source && current.source.original_data().empty() &&
      current.rgba16le_pixels().empty())
    return CommandResult::fail(
        "Non-destructive edits require a preserved encoded or high-precision raster source");
  bool identical = graph_.nodes.size() == current.edit_graph.nodes.size();
  if (identical) {
    for (size_t index = 0; index < graph_.nodes.size(); ++index) {
      if (!same_node(graph_.nodes[index], current.edit_graph.nodes[index])) {
        identical = false;
        break;
      }
    }
  }
  if (identical) return CommandResult::no_change();

  graph_.schema_version = RasterEditGraph::kSchemaVersion;
  auto updated = current;
  previous_graph_ = current.edit_graph;
  updated.edit_graph = graph_;
  auto objects = doc.objects().set(image_id_, ObjectVariant{std::move(updated)});
  doc.apply_mutation(doc.page(), std::move(objects));
  executed_ = true;
  return CommandResult::ok();
}

CommandResult SetRasterEditGraphCommand::undo(Document& doc) {
  if (!executed_ || !previous_graph_)
    return CommandResult::fail("Raster graph command has no state to restore");
  const auto* value = doc.objects().lookup_ref(image_id_);
  if (!value || !std::holds_alternative<ImageObject>(*value))
    return CommandResult::fail("Raster image object was not found for Undo");
  auto updated = std::get<ImageObject>(*value);
  updated.edit_graph = *previous_graph_;
  auto objects = doc.objects().set(image_id_, ObjectVariant{std::move(updated)});
  doc.apply_mutation(doc.page(), std::move(objects));
  executed_ = false;
  return CommandResult::ok();
}

}  // namespace pikdraw::document
