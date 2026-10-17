#include "../../src/raster/path_raster.hpp"
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace pikdraw;
using namespace pikdraw::document;
using namespace pikdraw::rendering;

namespace {
constexpr int32_t kWidth = 8;
constexpr int32_t kHeight = 8;
constexpr int32_t kSampleX = 3;
constexpr int32_t kSampleY = 3;
constexpr color::RGBColor kSource{0.317f, 0.541f, 0.789f, 0.437f};

void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

PathObject rectangle() {
  PathObject object;
  object.path = geometry::Path2D::make_rect(-2.0, -2.0, 4.0, 4.0);
  object.fill = kSource;
  object.stroke = kSource;
  object.stroke_width_mm = 2.0;
  return object;
}

size_t pixel_offset(int32_t x, int32_t y, int32_t channels) {
  return (static_cast<size_t>(y) * static_cast<size_t>(kWidth) +
          static_cast<size_t>(x)) * static_cast<size_t>(channels);
}

void preview_float_fill_and_stroke_retain_source_precision() {
  Camera2D camera{{0.0, 0.0}, 1.0, kWidth, kHeight};
  auto object = rectangle();
  object.has_stroke = false;

  std::vector<float> fill_pixels(
      static_cast<size_t>(kWidth) * kHeight * 4, 0.0f);
  PixelBufferTarget float_fill{nullptr, kWidth, kHeight,
                               kWidth * 4 * static_cast<int32_t>(sizeof(float)),
                               fill_pixels.data()};
  check(raster::paint(object, camera, float_fill), "float fill raster failed");

  const auto preview = color::to_preview_rgba(object.fill);
  const auto center = pixel_offset(kSampleX, kSampleY, 4);
  constexpr float epsilon = 1.0e-6f;
  check(std::abs(fill_pixels[center] - preview.r * preview.a) < epsilon &&
            std::abs(fill_pixels[center + 1] - preview.g * preview.a) < epsilon &&
            std::abs(fill_pixels[center + 2] - preview.b * preview.a) < epsilon &&
            std::abs(fill_pixels[center + 3] - preview.a) < epsilon,
        "float fill was quantized before source-over");

  object.has_fill = false;
  object.has_stroke = true;
  std::vector<float> stroke_pixels(
      static_cast<size_t>(kWidth) * kHeight * 4, 0.0f);
  PixelBufferTarget float_stroke{
      nullptr, kWidth, kHeight,
      kWidth * 4 * static_cast<int32_t>(sizeof(float)), stroke_pixels.data()};
  check(raster::paint(object, camera, float_stroke),
        "float stroke raster failed");
  const auto edge = pixel_offset(kSampleX, 2, 4);
  check(std::abs(stroke_pixels[edge] - preview.r * preview.a) < epsilon &&
            std::abs(stroke_pixels[edge + 1] - preview.g * preview.a) < epsilon &&
            std::abs(stroke_pixels[edge + 2] - preview.b * preview.a) < epsilon &&
            std::abs(stroke_pixels[edge + 3] - preview.a) < epsilon,
        "float stroke was quantized before source-over");
}

void rgba8_fill_keeps_existing_device_conversion() {
  Camera2D camera{{0.0, 0.0}, 1.0, kWidth, kHeight};
  auto object = rectangle();
  object.has_stroke = false;
  std::vector<uint8_t> pixels(static_cast<size_t>(kWidth) * kHeight * 4, 0);
  PixelBufferTarget target{pixels.data(), kWidth, kHeight, kWidth * 4};
  check(raster::paint(object, camera, target), "byte fill raster failed");

  const auto expected = color::to_preview_rgba(object.fill).to_device();
  const auto center = pixel_offset(kSampleX, kSampleY, 4);
  check(pixels[center] == expected.r && pixels[center + 1] == expected.g &&
            pixels[center + 2] == expected.b && pixels[center + 3] == expected.a,
        "RGBA8 fill changed its established preview conversion");
}
} // namespace

int main() {
  preview_float_fill_and_stroke_retain_source_precision();
  rgba8_fill_keeps_existing_device_conversion();
  return 0;
}
