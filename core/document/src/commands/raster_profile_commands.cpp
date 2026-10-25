#include "pikdraw/document/commands/raster_profile_commands.hpp"

#include "pikdraw/document/document.hpp"

#include <cstdint>
#include <limits>
#include <utility>

namespace pikdraw::document {
namespace {

ImageObject copy_raster_metadata(const ImageObject& image) {
  ImageObject copy;
  copy.id = image.id;
  copy.name = image.name;
  copy.bounds_rect = image.bounds_rect;
  copy.transform = image.transform;
  copy.width_px = image.width_px;
  copy.height_px = image.height_px;
  copy.source = image.source;
  copy.asset_source = image.asset_source;
  copy.edit_graph = image.edit_graph;
  copy.clip_path = image.clip_path;
  copy.visible = image.visible;
  copy.locked = image.locked;
  return copy;
}

bool has_valid_raster_samples(const ImageObject& image) noexcept {
  if (image.width_px <= 0 || image.height_px <= 0) return false;
  const uint64_t pixels = static_cast<uint64_t>(image.width_px) *
      static_cast<uint64_t>(image.height_px);
  if (pixels > std::numeric_limits<size_t>::max() / sizeof(uint32_t) ||
      image.rgba_pixels().size() != static_cast<size_t>(pixels))
    return false;
  const auto rgba16 = image.rgba16le_pixels();
  return rgba16.empty() ||
      (pixels <= std::numeric_limits<size_t>::max() / 8u &&
       rgba16.size() == static_cast<size_t>(pixels) * 8u);
}

}  // namespace

AssignRasterImageProfileCommand::AssignRasterImageProfileCommand(
    ObjectId image_id,
    std::shared_ptr<const color::ProfileResource> profile)
    : image_id_(image_id), profile_(std::move(profile)) {}

CommandResult AssignRasterImageProfileCommand::execute(Document& doc) {
  if (executed_)
    return CommandResult::fail("Raster profile assignment is already applied");
  if (!profile_ || profile_->identity().color_space != color::ColorSpace::Rgb)
    return CommandResult::fail("Raster profile assignment requires a valid RGB ICC resource");
  const auto* value = doc.objects().lookup_ref(image_id_);
  const auto* current = value ? std::get_if<ImageObject>(value) : nullptr;
  if (!current) return CommandResult::fail("Raster image object was not found");
  if (current->locked) return CommandResult::fail("Raster image is locked");
  if (!has_valid_raster_samples(*current))
    return CommandResult::fail("Raster image samples do not match its dimensions");
  if (current->source.preview_pixel_color_basis == RasterPixelColorBasis::LegacyUnknown)
    return CommandResult::fail("Raster image channel interpretation is unknown");
  if (current->source.original_data().empty() && !current->asset_source)
    return CommandResult::fail("Raster profile assignment requires a preserved source asset");
  if (profile_->bytes().empty() ||
      profile_->bytes().size() > RasterSourceMetadata::kMaxIccProfileBytes)
    return CommandResult::fail("Assigned raster ICC profile exceeds the document limit");
  if (current->source.preview_pixel_color_basis ==
          RasterPixelColorBasis::EmbeddedProfileUnapplied &&
      current->source.icc_profile == profile_->bytes())
    return CommandResult::fail("Raster image already uses this ICC interpretation");

  if (!initialized_) {
    previous_profile_ = current->source.icc_profile;
    previous_basis_ = current->source.preview_pixel_color_basis;
    initialized_ = true;
  }

  auto updated = copy_raster_metadata(*current);
  updated.pixels_rgba = current->pixels_rgba;
  updated.shared_pixels_rgba = current->shared_pixels_rgba;
  updated.shared_pixels_rgba16le = current->shared_pixels_rgba16le;
  updated.source.icc_profile = profile_->bytes();
  updated.source.preview_pixel_color_basis =
      RasterPixelColorBasis::EmbeddedProfileUnapplied;
  auto objects = doc.objects().set(image_id_, ObjectVariant{std::move(updated)});
  doc.apply_mutation(doc.page(), std::move(objects));
  executed_ = true;
  return CommandResult::ok();
}

CommandResult AssignRasterImageProfileCommand::undo(Document& doc) {
  if (!executed_ || !initialized_)
    return CommandResult::fail("Raster profile assignment has no state to restore");
  const auto* value = doc.objects().lookup_ref(image_id_);
  const auto* current = value ? std::get_if<ImageObject>(value) : nullptr;
  if (!current) return CommandResult::fail("Raster image object was not found for Undo");
  auto updated = copy_raster_metadata(*current);
  updated.pixels_rgba = current->pixels_rgba;
  updated.shared_pixels_rgba = current->shared_pixels_rgba;
  updated.shared_pixels_rgba16le = current->shared_pixels_rgba16le;
  updated.source.icc_profile = previous_profile_;
  updated.source.preview_pixel_color_basis = previous_basis_;
  auto objects = doc.objects().set(image_id_, ObjectVariant{std::move(updated)});
  doc.apply_mutation(doc.page(), std::move(objects));
  executed_ = false;
  return CommandResult::ok();
}

ConvertRasterImageToSrgbCommand::ConvertRasterImageToSrgbCommand(
    ObjectId image_id,
    std::shared_ptr<const std::vector<uint32_t>> rgba8_preview,
    std::shared_ptr<const std::vector<uint8_t>> rgba16le_master)
    : image_id_(image_id), next_rgba8_(std::move(rgba8_preview)),
      next_rgba16le_(std::move(rgba16le_master)) {}

ImageObject ConvertRasterImageToSrgbCommand::copy_metadata(
    const ImageObject& image) {
  return copy_raster_metadata(image);
}

CommandResult ConvertRasterImageToSrgbCommand::execute(Document& doc) {
  if (executed_)
    return CommandResult::fail("Raster profile conversion is already applied");
  const auto* value = doc.objects().lookup_ref(image_id_);
  const auto* current = value ? std::get_if<ImageObject>(value) : nullptr;
  if (!current) return CommandResult::fail("Raster image object was not found");
  if (current->locked) return CommandResult::fail("Raster image is locked");
  if (current->source.preview_pixel_color_basis !=
      RasterPixelColorBasis::EmbeddedProfileUnapplied)
    return CommandResult::fail("Raster image no longer has an unresolved RGB profile");
  if (current->source.icc_profile.size() < 128 ||
      current->source.icc_profile.size() > RasterSourceMetadata::kMaxIccProfileBytes ||
      current->source.icc_profile[16] != 'R' ||
      current->source.icc_profile[17] != 'G' ||
      current->source.icc_profile[18] != 'B' ||
      current->source.icc_profile[19] != ' ')
    return CommandResult::fail("Raster conversion requires a preserved RGB ICC profile");
  if (current->source.original_data().empty() && !current->asset_source)
    return CommandResult::fail("Raster conversion requires a preserved source asset");
  if (current->width_px <= 0 || current->height_px <= 0 || !next_rgba8_)
    return CommandResult::fail("Converted raster buffers are missing or invalid");

  const uint64_t pixels = static_cast<uint64_t>(current->width_px) *
      static_cast<uint64_t>(current->height_px);
  if (pixels > std::numeric_limits<size_t>::max() / sizeof(uint32_t) ||
      next_rgba8_->size() != static_cast<size_t>(pixels) ||
      current->rgba_pixels().size() != static_cast<size_t>(pixels))
    return CommandResult::fail("Converted RGBA8 preview dimensions do not match the image");
  const auto source16 = current->rgba16le_pixels();
  if (!source16.empty()) {
    if (pixels > std::numeric_limits<size_t>::max() / 8u || !next_rgba16le_ ||
        next_rgba16le_->size() != static_cast<size_t>(pixels) * 8u)
      return CommandResult::fail("Converted RGBA16 master dimensions do not match the image");
  } else if (next_rgba16le_ && !next_rgba16le_->empty()) {
    return CommandResult::fail("An 8-bit image cannot receive an RGBA16 master");
  }

  if (!initialized_) {
    previous_shared_rgba8_ = current->shared_pixels_rgba;
    if (!current->shared_pixels_rgba)
      previous_owned_rgba8_ = current->pixels_rgba;
    previous_rgba16le_ = current->shared_pixels_rgba16le;
    previous_basis_ = current->source.preview_pixel_color_basis;
    initialized_ = true;
  }

  auto updated = copy_metadata(*current);
  updated.shared_pixels_rgba = next_rgba8_;
  updated.shared_pixels_rgba16le = next_rgba16le_;
  updated.source.preview_pixel_color_basis = RasterPixelColorBasis::ConvertedToSRgb;
  auto objects = doc.objects().set(image_id_, ObjectVariant{std::move(updated)});
  doc.apply_mutation(doc.page(), std::move(objects));
  executed_ = true;
  return CommandResult::ok();
}

CommandResult ConvertRasterImageToSrgbCommand::undo(Document& doc) {
  if (!executed_ || !initialized_)
    return CommandResult::fail("Raster profile conversion has no state to restore");
  const auto* value = doc.objects().lookup_ref(image_id_);
  const auto* current = value ? std::get_if<ImageObject>(value) : nullptr;
  if (!current) return CommandResult::fail("Raster image object was not found for Undo");
  auto updated = copy_metadata(*current);
  updated.shared_pixels_rgba = previous_shared_rgba8_;
  updated.pixels_rgba = previous_owned_rgba8_;
  updated.shared_pixels_rgba16le = previous_rgba16le_;
  updated.source.preview_pixel_color_basis = previous_basis_;
  auto objects = doc.objects().set(image_id_, ObjectVariant{std::move(updated)});
  doc.apply_mutation(doc.page(), std::move(objects));
  executed_ = false;
  return CommandResult::ok();
}

}  // namespace pikdraw::document
