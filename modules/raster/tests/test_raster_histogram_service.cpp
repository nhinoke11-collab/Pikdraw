#include "pikdraw/document/commands/raster_edit_commands.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/raster/analysis/histogram_service.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace pikdraw;
using namespace pikdraw::document;

namespace {

void check(bool condition, const char* expression) {
  if (condition) return;
  std::cerr << "[FAIL] " << expression << '\n';
  std::exit(1);
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression)

}  // namespace

int main() {
  Document document("raster-histogram-service-test");
  ImageObject image;
  image.id = generate_object_id();
  image.name = "Histogram source";
  image.width_px = 2;
  image.height_px = 1;
  image.bounds_rect = {0.0, 0.0, 2.0, 1.0};
  image.pixels_rgba = {0x8040207fu, 0x000000ffu};
  image.source.media_type = "application/x-pikdraw-rgba8";
  image.source.original_bytes = {0x52, 0x47, 0x42, 0x41};
  image.source.preview_pixel_color_basis = RasterPixelColorBasis::AssumedSRgb;
  image.edit_graph.nodes.push_back({RasterEditNodeId{1}, true,
      RasterExposureParams{1.0}});
  const auto add_image = document.execute_command(
      std::make_unique<AddObjectCommand>(image));
  CHECK(add_image.success);

  raster::RasterHistogramService service;
  std::mutex mutex;
  std::condition_variable condition;
  bool completed = false;
  const auto snapshot = document.current_snapshot();
  service.request(snapshot, image.id, [&] {
    {
      std::lock_guard lock(mutex);
      completed = true;
    }
    condition.notify_one();
  });
  {
    std::unique_lock lock(mutex);
    CHECK(condition.wait_for(lock, std::chrono::seconds(3), [&] {
      return completed;
    }));
  }
  const auto histogram = service.find(*snapshot, image.id);
  CHECK(histogram && histogram->succeeded());
  CHECK(histogram->channels[0].bins[0x80] == 255 &&
         histogram->channels[1].bins[0] == 255);
  CHECK(histogram->channels[1].bins[0x40] == 255);

  completed = false;
  service.request(snapshot, image.id, [&] {
    {
      std::lock_guard lock(mutex);
      completed = true;
    }
    condition.notify_one();
  }, raster::RasterHistogramMode::Edited);
  {
    std::unique_lock lock(mutex);
    CHECK(condition.wait_for(lock, std::chrono::seconds(3), [&] {
      return completed;
    }));
  }
  const auto edited_histogram = service.find(
      *snapshot, image.id, raster::RasterHistogramMode::Edited);
  CHECK(edited_histogram && edited_histogram->succeeded());
  CHECK(edited_histogram->channels[0].bins[176] == 255);
  CHECK(edited_histogram->channels[0].bins[0] == 255);
  CHECK(edited_histogram->channels[1].bins[90] == 255);
  CHECK(edited_histogram->channels[2].bins[47] == 255);
  CHECK(edited_histogram->channels[3].bins[127] == 255);
  CHECK(edited_histogram->channels[3].display_bins[127] == 255);

  ImageObject high_precision;
  high_precision.id = generate_object_id();
  high_precision.name = "16-bit histogram source";
  high_precision.width_px = 1;
  high_precision.height_px = 1;
  high_precision.bounds_rect = {3.0, 0.0, 1.0, 1.0};
  high_precision.pixels_rgba = {0x000000ffu};
  high_precision.shared_pixels_rgba16le =
      std::make_shared<const std::vector<uint8_t>>(
          std::vector<uint8_t>{0x34, 0x12, 0x78, 0x56,
                              0xbc, 0x9a, 0xff, 0xff});
  high_precision.source.media_type = "application/x-pikdraw-rgba8";
  high_precision.source.original_bytes = {0x52, 0x47, 0x42, 0x41};
  const auto add_high_precision = document.execute_command(
      std::make_unique<AddObjectCommand>(high_precision));
  CHECK(add_high_precision.success);
  const auto high_snapshot = document.current_snapshot();
  completed = false;
  service.request(high_snapshot, high_precision.id, [&] {
    {
      std::lock_guard lock(mutex);
      completed = true;
    }
    condition.notify_one();
  });
  {
    std::unique_lock lock(mutex);
    CHECK(condition.wait_for(lock, std::chrono::seconds(3), [&] {
      return completed;
    }));
  }
  const auto high_histogram = service.find(*high_snapshot, high_precision.id);
  CHECK(high_histogram && high_histogram->succeeded());
  CHECK(high_histogram->bin_count == 65536);
  CHECK(high_histogram->channels[0].bins[0x1234] == 255);
  CHECK(high_histogram->channels[1].bins[0x5678] == 255);
  CHECK(high_histogram->channels[2].bins[0x9abc] == 255);
  CHECK(high_histogram->channels[0].display_bins[0x12] == 255);

  ImageObject large_image;
  large_image.id = generate_object_id();
  large_image.name = "Superseded histogram source";
  large_image.width_px = 2000;
  large_image.height_px = 2000;
  large_image.bounds_rect = {5.0, 0.0, 20.0, 20.0};
  large_image.shared_pixels_rgba =
      std::make_shared<const std::vector<uint32_t>>(
          4'000'000, 0x234567ffu);
  large_image.source.media_type = "application/x-pikdraw-rgba8";
  large_image.source.original_bytes = {0x52, 0x47, 0x42, 0x41};
  const auto add_large = document.execute_command(
      std::make_unique<AddObjectCommand>(large_image));
  CHECK(add_large.success);
  const auto latest_snapshot = document.current_snapshot();
  bool stale_completed = false;
  bool replacement_completed = false;
  service.request(latest_snapshot, large_image.id, [&] {
    {
      std::lock_guard lock(mutex);
      stale_completed = true;
    }
    condition.notify_one();
  });
  const auto start_deadline = std::chrono::steady_clock::now() +
      std::chrono::seconds(1);
  while (!service.is_processing(*latest_snapshot, large_image.id) &&
         std::chrono::steady_clock::now() < start_deadline)
    std::this_thread::yield();
  CHECK(service.is_processing(*latest_snapshot, large_image.id));
  service.request(latest_snapshot, image.id, [&] {
    {
      std::lock_guard lock(mutex);
      replacement_completed = true;
    }
    condition.notify_one();
  });
  {
    std::unique_lock lock(mutex);
    CHECK(condition.wait_for(lock, std::chrono::seconds(3), [&] {
      return replacement_completed;
    }));
    CHECK(!stale_completed);
  }
  CHECK(!service.find(*latest_snapshot, large_image.id));
  const auto replacement = service.find(*latest_snapshot, image.id);
  CHECK(replacement && replacement->succeeded());

  service.cancel();
  CHECK(!service.find(*latest_snapshot, image.id));
  return 0;
}

#undef CHECK
