#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/color/preview/profile_color_transform.hpp"
#include "pikdraw/color/profiles/profile_resource.hpp"
#include "pikdraw/raster/profile_conversion.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <vector>

namespace {

uint8_t to_u8(double value) {
  return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
}

std::shared_ptr<const pikdraw::color::ProfileResource> find_alternate_rgb(
    const std::shared_ptr<const pikdraw::color::ProfileResource>& srgb) {
  using namespace pikdraw::color;
  const auto colors = std::filesystem::path(IccPreview::system_srgb_profile()).parent_path();
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(colors, error)) {
    if (error || !entry.is_regular_file()) continue;
    const auto extension = entry.path().extension().wstring();
    if (extension != L".icc" && extension != L".ICC" &&
        extension != L".icm" && extension != L".ICM") continue;
    const auto candidate = ProfileResource::load_file(entry.path(), ColorSpace::Rgb);
    if (!candidate || candidate.resource->identity() == srgb->identity()) continue;
    const auto transform = ProfileColorTransform::create(
        candidate.resource, srgb, {IccRenderingIntent::RelativeColorimetric, false});
    if (!transform) continue;
    for (const auto& input : {ColorValue::make_rgb(0.96, 0.08, 0.04),
                             ColorValue::make_rgb(0.03, 0.92, 0.17),
                             ColorValue::make_rgb(0.04, 0.15, 0.98)}) {
      ColorValue output;
      if (!transform.transform->convert(input, output)) continue;
      const auto* rgb = std::get_if<RGBColor>(&output.payload());
      const auto* source = std::get_if<RGBColor>(&input.payload());
      if (rgb && source &&
          (std::abs(rgb->r - source->r) + std::abs(rgb->g - source->g) +
           std::abs(rgb->b - source->b)) > 0.02)
        return candidate.resource;
    }
  }
  return {};
}

void test_embedded_rgb_conversion_matches_icc_oracle_and_preserves_alpha() {
#ifdef _WIN32
  using namespace pikdraw;
  using namespace pikdraw::color;
  using namespace pikdraw::raster;
  const auto srgb_path = IccPreview::system_srgb_profile();
  const auto srgb = ProfileResource::load_file(srgb_path, ColorSpace::Rgb);
  assert(srgb && !srgb_path.empty());
  const auto source = find_alternate_rgb(srgb.resource);
  if (!source) {
    std::cout << "[NOT_MEASURED] No installed non-sRGB RGB profile produced a distinct CMM transform\n";
    return;
  }
  const auto oracle = ProfileColorTransform::create(
      source, srgb.resource, {IccRenderingIntent::RelativeColorimetric, false});
  assert(oracle);

  document::ImageObject image;
  image.width_px = 3;
  image.height_px = 1;
  image.shared_pixels_rgba = std::make_shared<const std::vector<uint32_t>>(
      std::vector<uint32_t>{0xf52030ffu, 0x23d06080u, 0x1840f0aau});
  image.source.preview_pixel_color_basis =
      document::RasterPixelColorBasis::EmbeddedProfileUnapplied;
  image.source.icc_profile = source->bytes();
  image.source.original_bytes = {0x52, 0x47, 0x42};

  uint64_t reported = 0;
  RasterProfileConversionOptions options;
  options.progress = [&](uint64_t completed, uint64_t total) {
    assert(completed <= total);
    reported = completed;
  };
  const auto converted = convert_image_profile_to_rgb(image, srgb.resource, options);
  assert(converted.succeeded() && !converted.rgba16le_master);
  const auto source_pixels = image.rgba_pixels();
  for (size_t i = 0; i < source_pixels.size(); ++i) {
    const auto packed = source_pixels[i];
    const auto input = ColorValue::make_rgb(
        static_cast<double>((packed >> 24) & 0xffu) / 255.0,
        static_cast<double>((packed >> 16) & 0xffu) / 255.0,
        static_cast<double>((packed >> 8) & 0xffu) / 255.0,
        static_cast<double>(packed & 0xffu) / 255.0);
    ColorValue output;
    assert(oracle.transform->convert(input, output));
    const auto* rgb = std::get_if<RGBColor>(&output.payload());
    assert(rgb);
    const auto result_pixel = (*converted.rgba8_preview)[i];
    assert(std::abs(static_cast<int>((result_pixel >> 24) & 0xffu) -
                    static_cast<int>(to_u8(rgb->r))) <= 1);
    assert(std::abs(static_cast<int>((result_pixel >> 16) & 0xffu) -
                    static_cast<int>(to_u8(rgb->g))) <= 1);
    assert(std::abs(static_cast<int>((result_pixel >> 8) & 0xffu) -
                    static_cast<int>(to_u8(rgb->b))) <= 1);
    assert((result_pixel & 0xffu) == (packed & 0xffu));
  }
  assert(reported == source_pixels.size());

  document::ImageObject high_precision = image;
  high_precision.shared_pixels_rgba16le =
      std::make_shared<const std::vector<uint8_t>>(std::vector<uint8_t>{
          0x33, 0x11, 0x77, 0x22, 0xbb, 0x33, 0x5d, 0x44,
          0x29, 0x10, 0x81, 0x32, 0xe7, 0x54, 0xa6, 0x76,
          0x11, 0x20, 0x65, 0x40, 0xc3, 0x60, 0xef, 0x80});
  const auto converted16 = convert_image_profile_to_rgb(high_precision, srgb.resource);
  assert(converted16.succeeded() && converted16.rgba16le_master &&
         converted16.rgba16le_master->size() == 24);
  std::vector<uint8_t> oracle16(converted16.rgba16le_master->size());
  assert(oracle.transform->convert_process16le(
      high_precision.rgba16le_pixels(), oracle16, true, true));
  assert(*converted16.rgba16le_master == oracle16);
  for (size_t pixel = 0; pixel < 3; ++pixel) {
    const auto read = [&](size_t channel) {
      const size_t offset = pixel * 8 + channel * 2;
      return static_cast<uint16_t>(oracle16[offset] |
          (static_cast<uint16_t>(oracle16[offset + 1]) << 8));
    };
    const uint32_t expected = (static_cast<uint32_t>(to_u8(read(0) / 65535.0)) << 24) |
        (static_cast<uint32_t>(to_u8(read(1) / 65535.0)) << 16) |
        (static_cast<uint32_t>(to_u8(read(2) / 65535.0)) << 8) | to_u8(read(3) / 65535.0);
    assert((*converted16.rgba8_preview)[pixel] == expected);
    const auto original_alpha = static_cast<uint16_t>(
        high_precision.rgba16le_pixels()[pixel * 8 + 6] |
        (static_cast<uint16_t>(high_precision.rgba16le_pixels()[pixel * 8 + 7]) << 8));
    assert(read(3) == original_alpha);
  }

  RasterProfileConversionOptions cancelled;
  cancelled.cancelled = [] { return true; };
  const auto cancelled_result = convert_image_profile_to_rgb(
      image, srgb.resource, cancelled);
  assert(cancelled_result.status == RasterProfileConversionStatus::Cancelled);

  std::cout << "[PASS] ICC raster conversion matches scalar and 16-bit profile oracles\n";
#else
  std::cout << "[NOT_MEASURED] Windows ICC transform execution is unavailable\n";
#endif
}

}  // namespace

int main() {
  test_embedded_rgb_conversion_matches_icc_oracle_and_preserves_alpha();
  return 0;
}
