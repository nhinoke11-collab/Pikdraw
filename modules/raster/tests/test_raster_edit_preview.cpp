#include "pikdraw/document/commands/raster_edit_commands.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/color/preview/profile_color_transform.hpp"
#include "pikdraw/color/profiles/profile_resource.hpp"
#include "pikdraw/raster/edit_preview_service.hpp"
#include "pikdraw/rendering/software_rasterizer.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <cmath>
#include <algorithm>
#include <vector>

using namespace pikdraw;
using namespace pikdraw::document;

namespace {

uint8_t channel_u8(double value) {
  return static_cast<uint8_t>(std::lround(
      std::clamp(value, 0.0, 1.0) * 255.0));
}

std::shared_ptr<const color::ProfileResource> find_distinct_rgb_profile(
    const std::shared_ptr<const color::ProfileResource>& srgb) {
#ifdef _WIN32
  const auto directory = color::IccPreview::system_srgb_profile().parent_path();
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
    if (error || !entry.is_regular_file()) continue;
    const auto extension = entry.path().extension().wstring();
    if (extension != L".icc" && extension != L".ICC" &&
        extension != L".icm" && extension != L".ICM") continue;
    const auto candidate = color::ProfileResource::load_file(
        entry.path(), color::ColorSpace::Rgb);
    if (!candidate || candidate.resource->identity() == srgb->identity()) continue;
    const auto transform = color::ProfileColorTransform::create(
        candidate.resource, srgb,
        {color::IccRenderingIntent::RelativeColorimetric, false});
    if (!transform) continue;
    color::ColorValue output;
    if (!transform.transform->convert(color::ColorValue::make_rgb(0.96, 0.08, 0.04), output))
      continue;
    const auto* converted = std::get_if<color::RGBColor>(&output.payload());
    if (converted && std::abs(converted->r - 0.96) +
        std::abs(converted->g - 0.08) + std::abs(converted->b - 0.04) > 0.02)
      return candidate.resource;
  }
#else
  (void)srgb;
#endif
  return {};
}

void verify_unresolved_profile_is_rendered_through_async_preview() {
#ifdef _WIN32
  const auto srgb_path = color::IccPreview::system_srgb_profile();
  const auto srgb = color::ProfileResource::load_file(
      srgb_path, color::ColorSpace::Rgb);
  assert(srgb && !srgb_path.empty());
  const auto source_profile = find_distinct_rgb_profile(srgb.resource);
  if (!source_profile) {
    std::cout << "[NOT_MEASURED] No distinct installed RGB ICC profile for raster preview oracle\n";
    return;
  }

  Document document("raster-icc-preview-test");
  ImageObject image;
  image.id = generate_object_id();
  image.name = "ICC preview target";
  image.width_px = 1;
  image.height_px = 1;
  image.bounds_rect = {0.0, 0.0, 1.0, 1.0};
  image.pixels_rgba = {0xf52030ffu};
  image.source.media_type = "application/x-pikdraw-rgba8";
  image.source.original_bytes = {0x52, 0x47, 0x42};
  image.source.icc_profile = source_profile->bytes();
  image.source.preview_pixel_color_basis =
      RasterPixelColorBasis::EmbeddedProfileUnapplied;
  assert(document.execute_command(std::make_unique<AddObjectCommand>(image)).success);
  const auto snapshot = document.current_snapshot();

  std::mutex mutex;
  std::condition_variable condition;
  size_t completions = 0;
  raster::RasterEditPreviewService previews(
      4 * 1024 * 1024, 2 * 1024 * 1024, srgb.resource);
  previews.request(snapshot, [&] {
    {
      std::lock_guard lock(mutex);
      ++completions;
    }
    condition.notify_one();
  });
  {
    std::unique_lock lock(mutex);
    assert(condition.wait_for(lock, std::chrono::seconds(5),
        [&] { return completions == 1; }));
  }
  const auto* value = snapshot->objects().lookup_ref(image.id);
  assert(value && std::holds_alternative<ImageObject>(*value));
  const auto& source = std::get<ImageObject>(*value);
  const auto preview = previews.find(*snapshot, source);
  assert(preview && preview->source.preview_pixel_color_basis ==
      RasterPixelColorBasis::ConvertedToSRgb);
  assert(source.rgba_pixels().front() == image.pixels_rgba.front());
  assert(source.source.preview_pixel_color_basis ==
      RasterPixelColorBasis::EmbeddedProfileUnapplied);

  const auto transform = color::ProfileColorTransform::create(
      source_profile, srgb.resource,
      {color::IccRenderingIntent::RelativeColorimetric, false});
  assert(transform);
  color::ColorValue expected;
  assert(transform.transform->convert(color::ColorValue::make_rgb(0xf5 / 255.0,
      0x20 / 255.0, 0x30 / 255.0), expected));
  const auto* rgb = std::get_if<color::RGBColor>(&expected.payload());
  assert(rgb);
  const auto actual = preview->rgba_pixels().front();
  assert(std::abs(static_cast<int>(actual >> 24) - channel_u8(rgb->r)) <= 1);
  assert(std::abs(static_cast<int>((actual >> 16) & 0xffu) - channel_u8(rgb->g)) <= 1);
  assert(std::abs(static_cast<int>((actual >> 8) & 0xffu) - channel_u8(rgb->b)) <= 1);
  assert(static_cast<uint8_t>(actual) == 0xff);
  std::cout << "[PASS] unresolved RGB ICC preview matches the scalar CMM oracle without changing source pixels\n";
#else
  std::cout << "[NOT_MEASURED] Windows ICC preview is unavailable on this platform\n";
#endif
}

}  // namespace

void test_adaptive_preview_retains_pixel_reservation_after_eviction() {
  using namespace pikdraw::document::resources;
  constexpr uint64_t GiB = 1024ull * 1024 * 1024;
  std::atomic<uint64_t> available{3 * GiB};
  auto policy = std::make_shared<MemoryBudgetPolicy>([&] {
    return SystemMemoryStatus{4 * GiB, available.load(), 4 * GiB, 4 * GiB};
  });
  Document doc("adaptive-preview");
  ImageObject source;
  source.id = generate_object_id();
  source.width_px = 2; source.height_px = 1;
  source.bounds_rect = {0, 0, 2, 1};
  source.pixels_rgba = {0x404040ff, 0x808080ff};
  source.source.preview_pixel_color_basis = RasterPixelColorBasis::AssumedSRgb;
  source.edit_graph.nodes.push_back({RasterEditNodeId{1}, true, RasterExposureParams{1}});
  assert(doc.execute_command(std::make_unique<AddObjectCommand>(source)).success);
  const auto snapshot = doc.current_snapshot();
  const auto& image = std::get<ImageObject>(*snapshot->objects().lookup_ref(source.id));
  std::mutex mutex;
  std::condition_variable condition;
  bool completed = false;
  const auto callback = [&] {
    { std::lock_guard lock(mutex); completed = true; }
    condition.notify_one();
  };
  std::shared_ptr<const ImageObject> pinned;
  {
    raster::RasterEditPreviewService previews(0, 0, {}, policy);
    previews.request(snapshot, callback);
    {
      std::unique_lock lock(mutex);
      assert(condition.wait_for(lock, std::chrono::seconds(5), [&] { return completed; }));
    }
    pinned = previews.find(*snapshot, image);
    assert(pinned && pinned->rgba_pixels()[0] == 0x5a5a5aff);
    available = GiB / 8;
    assert(!previews.find(*snapshot, image)); // pressure trims the cache
    assert(policy->reserved_bytes() >= 8); // external pixel owner is still charged
  }
  assert(policy->reserved_bytes() == 8 && pinned->rgba_pixels().size() == 2);
  pinned.reset();
  assert(policy->reserved_bytes() == 0);
  available = 3 * GiB;
  assert(policy->try_reserve(768ull * 1024 * 1024));
  std::cout << "[PASS] adaptive preview eviction preserves pinned pixels and their memory charge\n";
}

int main() {
  test_adaptive_preview_retains_pixel_reservation_after_eviction();
  Document document("raster-edit-preview-test");
  ImageObject image;
  image.id = generate_object_id();
  image.name = "Preview target";
  image.width_px = 2;
  image.height_px = 2;
  image.bounds_rect = {0.0, 0.0, 2.0, 2.0};
  image.pixels_rgba = {
      0x404040ffu, 0xffffffffu,
      0x202020ffu, 0xff0000ffu};
  image.shared_pixels_rgba = std::make_shared<const std::vector<uint32_t>>(
      image.pixels_rgba);
  image.pixels_rgba.clear();
  image.source.media_type = "application/x-pikdraw-rgba8";
  image.source.original_bytes = {0x52, 0x47, 0x42, 0x41};
  assert(document.execute_command(
      std::make_unique<AddObjectCommand>(image)).success);
  assert(document.execute_command(std::make_unique<SetRasterEditNodeCommand>(
      image.id, RasterEditNode{RasterEditNodeId{11}, true,
          RasterCropParams{0.0, 0.0, 0.5, 1.0}})).success);
  assert(document.execute_command(std::make_unique<SetRasterEditNodeCommand>(
      image.id, RasterEditNode{RasterEditNodeId{12}, true,
          RasterExposureParams{1.0}})).success);
  assert(document.execute_command(std::make_unique<SetRasterEditNodeCommand>(
      image.id, RasterEditNode{RasterEditNodeId{13}, true,
          RasterLevelsParams{}})).success);

  ImageObject high_precision_image;
  high_precision_image.id = generate_object_id();
  high_precision_image.name = "16-bit preview target";
  high_precision_image.width_px = 1;
  high_precision_image.height_px = 1;
  high_precision_image.bounds_rect = {3.0, 0.0, 1.0, 1.0};
  high_precision_image.pixels_rgba = {0x000000ffu};
  high_precision_image.shared_pixels_rgba16le =
      std::make_shared<const std::vector<uint8_t>>(
          std::vector<uint8_t>{
              0xe8, 0x03, 0x10, 0x27, 0x20, 0x4e, 0xff, 0xff});
  assert(document.execute_command(
      std::make_unique<AddObjectCommand>(high_precision_image)).success);
  assert(document.execute_command(std::make_unique<SetRasterEditNodeCommand>(
      high_precision_image.id, RasterEditNode{RasterEditNodeId{21}, true,
          RasterExposureParams{1.0}})).success);

  const auto snapshot = document.current_snapshot();
  std::mutex completion_mutex;
  std::condition_variable completion_condition;
  size_t completion_count = 0;
  raster::RasterEditPreviewService previews(16 * 1024 * 1024,
                                             8 * 1024 * 1024);
  previews.request(snapshot, [&] {
    {
      std::lock_guard lock(completion_mutex);
      ++completion_count;
    }
    completion_condition.notify_one();
  });
  {
    std::unique_lock lock(completion_mutex);
    assert(completion_condition.wait_for(lock, std::chrono::seconds(5),
        [&] { return completion_count == 2; }));
  }

  const auto* source_value = snapshot->objects().lookup_ref(image.id);
  assert(source_value && std::holds_alternative<ImageObject>(*source_value));
  const auto& source_image = std::get<ImageObject>(*source_value);
  const auto preview = previews.find(*snapshot, source_image);
  assert(preview);
  assert(preview->width_px == 1 && preview->height_px == 2);
  assert(preview->bounds_rect.x == 0.0 && preview->bounds_rect.width == 1.0);
  assert(preview->bounds_rect.height == 2.0);
  assert(preview->rgba_pixels()[0] == 0x5a5a5affu);
  assert(preview->rgba_pixels()[1] == 0x2f2f2fffU);

  assert(document.execute_command(std::make_unique<SetRasterEditNodeCommand>(
      image.id, RasterEditNode{RasterEditNodeId{12}, true,
          RasterExposureParams{2.0}})).success);
  const auto pending_snapshot = document.current_snapshot();
  const auto* pending_value = pending_snapshot->objects().lookup_ref(image.id);
  assert(pending_value && std::holds_alternative<ImageObject>(*pending_value));
  const auto& pending_image = std::get<ImageObject>(*pending_value);
  assert(!previews.find(*pending_snapshot, pending_image));
  const auto last_good = previews.find_last_good(*pending_snapshot, pending_image);
  assert(last_good && last_good->rgba_pixels()[0] == preview->rgba_pixels()[0]);
  assert(last_good->edit_graph.nodes.size() ==
      pending_image.edit_graph.nodes.size());
  assert(std::get<RasterExposureParams>(
      last_good->edit_graph.nodes[1].parameters).exposure_ev == 2.0);

  const auto* high_precision_value =
      snapshot->objects().lookup_ref(high_precision_image.id);
  assert(high_precision_value &&
         std::holds_alternative<ImageObject>(*high_precision_value));
  const auto& high_precision_source =
      std::get<ImageObject>(*high_precision_value);
  const auto high_precision_preview =
      previews.find(*snapshot, high_precision_source);
  assert(high_precision_preview);
  const auto high_precision_pixel = high_precision_preview->rgba_pixels().front();
  const auto red = static_cast<uint8_t>(high_precision_pixel >> 24);
  const auto green = static_cast<uint8_t>(high_precision_pixel >> 16);
  const auto blue = static_cast<uint8_t>(high_precision_pixel >> 8);
  assert(red > 0 && green > red && blue > green);
  assert(static_cast<uint8_t>(high_precision_pixel) == 0xff);

  rendering::SoftwareRasterizer renderer;
  renderer.set_raster_image_resolver(
      [&previews](const DocumentSnapshot& current,
                  const ImageObject& source) {
        return previews.find(current, source);
      });
  rendering::Camera2D camera({1.0, 1.0}, 1.0, 2, 2);
  std::array<uint32_t, 4> bgra{};
  rendering::PixelBufferTarget target{nullptr, 2, 2, 8};
  target.bgra_pixels = bgra.data();
  assert(renderer.render(*snapshot, camera, target));
  const auto* first_pixel = reinterpret_cast<const uint8_t*>(&bgra[0]);
  assert(first_pixel[0] == first_pixel[1] && first_pixel[1] == first_pixel[2]);
  assert(first_pixel[0] == 0x2f || first_pixel[0] == 0x5a);

  std::cout << "[PASS] asynchronous raster preview reaches the software renderer\n";
  verify_unresolved_profile_is_rendered_through_async_preview();
  return 0;
}
