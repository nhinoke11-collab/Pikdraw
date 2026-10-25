#pragma once

#include "pikdraw/color/profiles/profile_resource.hpp"
#include "pikdraw/document/commands/command.hpp"

#include <memory>
#include <vector>

namespace pikdraw::document {

/// Changes the ICC interpretation of a raster image while preserving its
/// current encoded channel values and source bytes.
class AssignRasterImageProfileCommand final : public DocumentCommand {
 public:
  AssignRasterImageProfileCommand(
      ObjectId image_id,
      std::shared_ptr<const color::ProfileResource> profile);

  [[nodiscard]] std::string_view name() const noexcept override {
    return "Assign Raster Image Profile";
  }
  CommandResult execute(Document& doc) override;
  CommandResult undo(Document& doc) override;

 private:
  ObjectId image_id_{kInvalidObjectId};
  std::shared_ptr<const color::ProfileResource> profile_;
  std::vector<uint8_t> previous_profile_;
  RasterPixelColorBasis previous_basis_{RasterPixelColorBasis::AssumedSRgb};
  bool initialized_{false};
  bool executed_{false};
};

/// Publishes an ICC-converted raster buffer as sRGB while retaining source
/// bytes, source ICC metadata, and the current edit graph for Undo/provenance.
class ConvertRasterImageToSrgbCommand final : public DocumentCommand {
 public:
  ConvertRasterImageToSrgbCommand(
      ObjectId image_id,
      std::shared_ptr<const std::vector<uint32_t>> rgba8_preview,
      std::shared_ptr<const std::vector<uint8_t>> rgba16le_master = {});

  [[nodiscard]] std::string_view name() const noexcept override {
    return "Convert Raster Image to sRGB";
  }
  CommandResult execute(Document& doc) override;
  CommandResult undo(Document& doc) override;

 private:
  [[nodiscard]] static ImageObject copy_metadata(const ImageObject& image);

  ObjectId image_id_{kInvalidObjectId};
  std::shared_ptr<const std::vector<uint32_t>> next_rgba8_;
  std::shared_ptr<const std::vector<uint8_t>> next_rgba16le_;
  std::shared_ptr<const std::vector<uint32_t>> previous_shared_rgba8_;
  std::vector<uint32_t> previous_owned_rgba8_;
  std::shared_ptr<const std::vector<uint8_t>> previous_rgba16le_;
  RasterPixelColorBasis previous_basis_{RasterPixelColorBasis::EmbeddedProfileUnapplied};
  bool initialized_{false};
  bool executed_{false};
};

}  // namespace pikdraw::document
