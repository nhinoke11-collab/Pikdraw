#pragma once

#include "pikdraw/color/preview/profile_color_transform.hpp"
#include "pikdraw/color/profiles/profile_resource.hpp"
#include "pikdraw/document/model/object.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pikdraw::raster {

enum class RasterProfileConversionStatus : uint8_t {
  Success,
  InvalidSource,
  InvalidProfile,
  TransformUnavailable,
  Cancelled,
  AllocationFailure
};

struct RasterProfileConversionOptions {
  color::IccTransformOptions transform;
  std::function<bool()> cancelled;
  std::function<void(uint64_t completed_pixels, uint64_t total_pixels)> progress;
};

struct RasterProfileConversionResult {
  RasterProfileConversionStatus status{RasterProfileConversionStatus::InvalidSource};
  std::shared_ptr<const std::vector<uint32_t>> rgba8_preview;
  std::shared_ptr<const std::vector<uint8_t>> rgba16le_master;
  std::string diagnostic;

  [[nodiscard]] bool succeeded() const noexcept {
    return status == RasterProfileConversionStatus::Success && rgba8_preview;
  }
};

/// Converts an image whose decoded samples still use its embedded RGB ICC
/// profile into a caller-supplied RGB profile. Source bytes/profile and the
/// edit graph are not modified. The caller accounts for output memory and
/// publishes the result through a document command only after success.
[[nodiscard]] RasterProfileConversionResult convert_image_profile_to_rgb(
    const document::ImageObject& image,
    std::shared_ptr<const color::ProfileResource> destination,
    const RasterProfileConversionOptions& options = {});

}  // namespace pikdraw::raster
