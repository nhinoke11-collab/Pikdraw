#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/commands/raster_profile_commands.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/document/storage/native_format.hpp"
#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/color/profiles/profile_resource.hpp"

#include <cassert>
#include <memory>
#include <vector>

using namespace pikdraw::document;

int main() {
  Document doc("raster-profile-command-test");
  ImageObject image;
  image.id = generate_object_id();
  image.name = "ICC converted image";
  image.width_px = 2;
  image.height_px = 1;
  const std::vector<uint32_t> source_pixels{0x20406080u, 0xa0b0c0ffu};
  image.shared_pixels_rgba =
      std::make_shared<const std::vector<uint32_t>>(source_pixels);
  const std::vector<uint8_t> source_rgba16le{
      0x31, 0x12, 0x72, 0x23, 0xb3, 0x34, 0x5a, 0x45,
      0x29, 0x10, 0x81, 0x32, 0xe7, 0x54, 0xa6, 0x76};
  image.shared_pixels_rgba16le =
      std::make_shared<const std::vector<uint8_t>>(source_rgba16le);
  image.source.original_bytes = {0x89, 0x50, 0x4e, 0x47};
  image.source.media_type = "image/png";
  image.source.icc_profile.resize(128);
  image.source.icc_profile[16] = 'R';
  image.source.icc_profile[17] = 'G';
  image.source.icc_profile[18] = 'B';
  image.source.icc_profile[19] = ' ';
  image.source.preview_pixel_color_basis =
      RasterPixelColorBasis::EmbeddedProfileUnapplied;
  const auto source_profile = image.source.icc_profile;
  image.edit_graph.nodes.push_back({RasterEditNodeId{71}, true,
      RasterExposureParams{0.5}, 1});
  const auto image_id = image.id;
  assert(doc.execute_command(
      std::make_unique<AddObjectCommand>(ObjectVariant{image})).success);

  const auto srgb = pikdraw::color::ProfileResource::load_file(
      pikdraw::color::IccPreview::system_srgb_profile(),
      pikdraw::color::ColorSpace::Rgb);
  assert(srgb);
  assert(doc.execute_command(std::make_unique<AssignRasterImageProfileCommand>(
      image_id, srgb.resource)).success);
  auto assigned_object = doc.current_snapshot()->get_object(image_id);
  assert(assigned_object);
  const auto& assigned = std::get<ImageObject>(*assigned_object);
  assert(assigned.source.preview_pixel_color_basis ==
         RasterPixelColorBasis::EmbeddedProfileUnapplied);
  assert(assigned.source.icc_profile == srgb.resource->bytes());
  assert(assigned.rgba_pixels().data() == image.rgba_pixels().data());
  assert(assigned.rgba16le_pixels().data() == image.rgba16le_pixels().data());
  assert(assigned.source.original_data() == image.source.original_data());

  assert(doc.undo());
  auto unassigned_object = doc.current_snapshot()->get_object(image_id);
  assert(unassigned_object);
  const auto& unassigned = std::get<ImageObject>(*unassigned_object);
  assert(unassigned.source.icc_profile == source_profile);
  assert(unassigned.rgba_pixels().data() == image.rgba_pixels().data());
  assert(doc.redo());

  const auto converted_pixels = std::make_shared<const std::vector<uint32_t>>(
      std::vector<uint32_t>{0x30507080u, 0xb0c0d0ffu});
  const auto converted_rgba16le = std::make_shared<const std::vector<uint8_t>>(
      std::vector<uint8_t>{
          0x51, 0x12, 0x92, 0x23, 0xd3, 0x34, 0x5a, 0x45,
          0x49, 0x10, 0xa1, 0x32, 0xc7, 0x54, 0xa6, 0x76});
  assert(doc.execute_command(std::make_unique<ConvertRasterImageToSrgbCommand>(
      image_id, converted_pixels, converted_rgba16le)).success);
  auto converted_object = doc.current_snapshot()->get_object(image_id);
  assert(converted_object);
  const auto& converted = std::get<ImageObject>(*converted_object);
  assert(converted.source.preview_pixel_color_basis ==
         RasterPixelColorBasis::ConvertedToSRgb);
  assert(converted.rgba_pixels().data() == converted_pixels->data());
  assert(converted.rgba16le_pixels().data() == converted_rgba16le->data());
  assert(converted.source.original_data() == image.source.original_data());
  assert(converted.source.icc_profile == srgb.resource->bytes());
  assert(converted.edit_graph.nodes.size() == 1 &&
         converted.edit_graph.nodes.front().id == RasterEditNodeId{71});

  assert(doc.undo());
  auto undone_object = doc.current_snapshot()->get_object(image_id);
  assert(undone_object);
  const auto& undone = std::get<ImageObject>(*undone_object);
  assert(undone.source.preview_pixel_color_basis ==
         RasterPixelColorBasis::EmbeddedProfileUnapplied);
  assert(undone.source.icc_profile == srgb.resource->bytes());
  assert(std::vector<uint32_t>(undone.rgba_pixels().begin(),
                               undone.rgba_pixels().end()) == source_pixels);
  assert(std::vector<uint8_t>(undone.rgba16le_pixels().begin(),
                              undone.rgba16le_pixels().end()) == source_rgba16le);
  assert(doc.redo());

  assert(doc.undo());
  assert(doc.undo());
  auto original_profile_object = doc.current_snapshot()->get_object(image_id);
  assert(original_profile_object);
  const auto& original_profile_image = std::get<ImageObject>(*original_profile_object);
  assert(original_profile_image.source.icc_profile == source_profile);
  assert(original_profile_image.source.preview_pixel_color_basis ==
         RasterPixelColorBasis::EmbeddedProfileUnapplied);
  assert(doc.redo());
  assert(doc.redo());

  const auto serialized = NativeFormat::serialize(*doc.current_snapshot());
  std::string error;
  const auto reopened = NativeFormat::deserialize(
      serialized.data(), serialized.size(), &error);
  assert(reopened && error.empty());
  const auto restored_object = reopened->get_object(image_id);
  assert(restored_object);
  const auto& restored = std::get<ImageObject>(*restored_object);
  assert(restored.source.preview_pixel_color_basis ==
         RasterPixelColorBasis::ConvertedToSRgb);
  assert(std::vector<uint32_t>(restored.rgba_pixels().begin(),
                               restored.rgba_pixels().end()) == *converted_pixels);
  assert(std::vector<uint8_t>(restored.rgba16le_pixels().begin(),
                              restored.rgba16le_pixels().end()) == *converted_rgba16le);
  assert(restored.source.original_data() == image.source.original_data());
  assert(restored.source.icc_profile == srgb.resource->bytes());
  assert(restored.edit_graph.nodes.size() == 1 &&
         restored.edit_graph.nodes.front().id == RasterEditNodeId{71});
}
