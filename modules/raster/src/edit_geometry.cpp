#include "pikdraw/raster/edit_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace pikdraw::raster {
namespace {

uint32_t crop_boundary(double normalized, uint32_t extent) noexcept {
  const auto rounded = std::llround(normalized * extent);
  return static_cast<uint32_t>(std::clamp<long long>(
      rounded, 0, static_cast<long long>(extent)));
}

bool valid_crop(const document::RasterCropParams& crop) noexcept {
  return std::isfinite(crop.x) && std::isfinite(crop.y) &&
      std::isfinite(crop.width) && std::isfinite(crop.height) &&
      crop.x >= 0.0 && crop.y >= 0.0 && crop.width > 0.0 &&
      crop.height > 0.0 && crop.x + crop.width <= 1.0 &&
      crop.y + crop.height <= 1.0;
}

}  // namespace

bool raster_edit_requires_srgb_processing(
    const document::RasterEditGraph& graph) noexcept {
  for (const auto& node : graph.nodes) {
    if (!node.enabled) continue;
    if (std::holds_alternative<document::RasterExposureParams>(node.parameters) ||
        std::holds_alternative<document::RasterLevelsParams>(node.parameters) ||
        std::holds_alternative<document::RasterHslParams>(node.parameters) ||
        std::holds_alternative<document::RasterCurvesParams>(node.parameters) ||
        std::holds_alternative<document::RasterWhiteBalanceParams>(node.parameters) ||
        std::holds_alternative<document::RasterVibranceParams>(node.parameters) ||
        std::holds_alternative<document::RasterSelectiveHslParams>(node.parameters) ||
        std::holds_alternative<document::RasterLightParams>(node.parameters) ||
        std::holds_alternative<document::RasterColorBalanceParams>(node.parameters) ||
        std::holds_alternative<document::RasterChannelMixerParams>(node.parameters))
      return true;
    if (const auto* rotate = std::get_if<document::RasterRotateParams>(
            &node.parameters)) {
      double normalized = std::fmod(rotate->degrees, 360.0);
      if (normalized < 0.0) normalized += 360.0;
      if (normalized != 0.0 && normalized != 90.0 &&
          normalized != 180.0 && normalized != 270.0)
        return true;
    }
  }
  return false;
}

geometry::Rect2D raster_edited_bounds(const document::ImageObject& image,
                                     uint32_t output_width,
                                     uint32_t output_height) noexcept {
  auto bounds = image.bounds_rect;
  uint32_t width = image.width_px > 0
      ? static_cast<uint32_t>(image.width_px) : 0;
  uint32_t height = image.height_px > 0
      ? static_cast<uint32_t>(image.height_px) : 0;
  if (width == 0 || height == 0) return bounds;

  for (const auto& node : image.edit_graph.nodes) {
    if (!node.enabled) continue;
    if (const auto* crop = std::get_if<document::RasterCropParams>(
            &node.parameters)) {
      if (!valid_crop(*crop)) return bounds;
      const auto left = crop_boundary(crop->x, width);
      const auto top = crop_boundary(crop->y, height);
      auto right = crop_boundary(crop->x + crop->width, width);
      auto bottom = crop_boundary(crop->y + crop->height, height);
      if (right <= left) right = (std::min)(width, left + 1);
      if (bottom <= top) bottom = (std::min)(height, top + 1);
      const auto cropped_width = right - left;
      const auto cropped_height = bottom - top;
      bounds.x += bounds.width * static_cast<double>(left) / width;
      bounds.y += bounds.height * static_cast<double>(top) / height;
      bounds.width *= static_cast<double>(cropped_width) / width;
      bounds.height *= static_cast<double>(cropped_height) / height;
      width = cropped_width;
      height = cropped_height;
    } else if (const auto* rotate = std::get_if<document::RasterRotateParams>(
                   &node.parameters)) {
      double normalized = std::fmod(rotate->degrees, 360.0);
      if (normalized < 0.0) normalized += 360.0;
      if (normalized == 0.0) continue;
      const double center_x = bounds.x + bounds.width / 2.0;
      const double center_y = bounds.y + bounds.height / 2.0;
      uint32_t rotated_width = width;
      uint32_t rotated_height = height;
      if (normalized == 90.0 || normalized == 270.0) {
        rotated_width = height;
        rotated_height = width;
      } else if (normalized != 180.0) {
        const double radians = normalized * (std::acos(-1.0) / 180.0);
        const double rotated_width_f = std::ceil(
            std::abs(width * std::cos(radians)) +
            std::abs(height * std::sin(radians)));
        const double rotated_height_f = std::ceil(
            std::abs(width * std::sin(radians)) +
            std::abs(height * std::cos(radians)));
        if (!std::isfinite(rotated_width_f) ||
            !std::isfinite(rotated_height_f) || rotated_width_f < 1.0 ||
            rotated_height_f < 1.0 ||
            rotated_width_f > std::numeric_limits<uint32_t>::max() ||
            rotated_height_f > std::numeric_limits<uint32_t>::max())
          continue;
        rotated_width = static_cast<uint32_t>(rotated_width_f);
        rotated_height = static_cast<uint32_t>(rotated_height_f);
      }
      bounds.width *= static_cast<double>(rotated_width) / width;
      bounds.height *= static_cast<double>(rotated_height) / height;
      bounds.x = center_x - bounds.width / 2.0;
      bounds.y = center_y - bounds.height / 2.0;
      width = rotated_width;
      height = rotated_height;
    }
  }

  if (output_width != width)
    bounds.width *= static_cast<double>(output_width) / width;
  if (output_height != height)
    bounds.height *= static_cast<double>(output_height) / height;
  return bounds;
}

std::pair<uint32_t, uint32_t> raster_edited_pixel_dimensions(
    const document::ImageObject& image) noexcept {
  uint32_t width = image.width_px > 0 ? static_cast<uint32_t>(image.width_px) : 0;
  uint32_t height = image.height_px > 0 ? static_cast<uint32_t>(image.height_px) : 0;
  if (width == 0 || height == 0) return {0, 0};
  for (const auto& node : image.edit_graph.nodes) {
    if (!node.enabled) continue;
    if (const auto* crop = std::get_if<document::RasterCropParams>(&node.parameters)) {
      if (!valid_crop(*crop)) return {0, 0};
      const auto left = crop_boundary(crop->x, width);
      const auto top = crop_boundary(crop->y, height);
      auto right = crop_boundary(crop->x + crop->width, width);
      auto bottom = crop_boundary(crop->y + crop->height, height);
      if (right <= left) right = (std::min)(width, left + 1);
      if (bottom <= top) bottom = (std::min)(height, top + 1);
      width = right - left;
      height = bottom - top;
    } else if (const auto* rotate = std::get_if<document::RasterRotateParams>(
                   &node.parameters)) {
      double normalized = std::fmod(rotate->degrees, 360.0);
      if (!std::isfinite(normalized)) return {0, 0};
      if (normalized < 0.0) normalized += 360.0;
      if (normalized == 90.0 || normalized == 270.0) std::swap(width, height);
      else if (normalized != 0.0 && normalized != 180.0) {
        const double radians = normalized * (std::acos(-1.0) / 180.0);
        const double output_width = std::ceil(
            std::abs(width * std::cos(radians)) + std::abs(height * std::sin(radians)));
        const double output_height = std::ceil(
            std::abs(width * std::sin(radians)) + std::abs(height * std::cos(radians)));
        if (!std::isfinite(output_width) || !std::isfinite(output_height) ||
            output_width < 1.0 || output_height < 1.0 ||
            output_width > std::numeric_limits<uint32_t>::max() ||
            output_height > std::numeric_limits<uint32_t>::max()) return {0, 0};
        width = static_cast<uint32_t>(output_width);
        height = static_cast<uint32_t>(output_height);
      }
    }
  }
  return {width, height};
}

}  // namespace pikdraw::raster
