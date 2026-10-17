#include "pikdraw/document/document.hpp"
#include "pikdraw/rendering/d2d_backend.hpp"
#include "pikdraw/rendering/skia_backend.hpp"
#include "pikdraw/rendering/software_rasterizer.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

bool require(bool condition, const char *message) {
  if (condition)
    return true;
  std::cerr << "FAIL: " << message << '\n';
  return false;
}

int run_independent_fallback_contract() {
  using namespace pikdraw;
  using namespace pikdraw::document;
  using namespace pikdraw::rendering;

  Document document("independent-skia-fallback-contract");
  PathObject rectangle;
  rectangle.id = generate_object_id();
  rectangle.path = geometry::Path2D::make_rect(20.0, 20.0, 40.0, 40.0);
  rectangle.fill = color::RGBColor{0.8, 0.1, 0.2, 1.0};
  rectangle.has_fill = true;

  const auto add_result = document.execute_command(
      std::make_unique<AddObjectCommand>(rectangle));
  if (!require(add_result.success, "could not create deterministic scene"))
    return 1;

  constexpr int32_t width = 96;
  constexpr int32_t height = 96;
  constexpr int32_t stride = width * 4;
  std::vector<uint8_t> oracle_pixels(width * height * 4, 0);
  std::vector<uint8_t> d2d_pixels(width * height * 4, 0);
  std::vector<uint8_t> candidate_pixels(width * height * 4, 0);
  PixelBufferTarget oracle_target{oracle_pixels.data(), width, height, stride};
  PixelBufferTarget d2d_target{d2d_pixels.data(), width, height, stride};
  PixelBufferTarget candidate_target{candidate_pixels.data(), width, height,
                                     stride};
  const Camera2D camera({48.0, 48.0}, 1.0, width, height);

  SoftwareRasterizer oracle;
  D2DRenderBackend d2d_fallback;
  SkiaRenderBackend candidate;
  if (!require(oracle.render(*document.current_snapshot(), camera,
                             oracle_target),
               "SoftwareRasterizer oracle render failed"))
    return 1;
  if (!require(d2d_fallback.render(*document.current_snapshot(), camera,
                                   d2d_target),
               "D2D fallback render failed"))
    return 1;
  if (!require(candidate.render(*document.current_snapshot(), camera,
                                candidate_target),
               "Skia backend fallback render failed"))
    return 1;

  const bool matches_software =
      std::equal(oracle_pixels.begin(), oracle_pixels.end(),
                 candidate_pixels.begin());
  const bool matches_d2d =
      std::equal(d2d_pixels.begin(), d2d_pixels.end(),
                 candidate_pixels.begin());
  if (!require(matches_software || matches_d2d,
               "fallback pixels match neither supported fallback renderer"))
    return 1;

  const size_t center_pixel = (40 * width + 40) * 4;
  if (!require(oracle_pixels[center_pixel] > oracle_pixels[center_pixel + 1] &&
                   oracle_pixels[center_pixel + 3] == 255,
               "oracle scene did not produce the expected opaque red fill"))
    return 1;

  if (!require(!candidate.is_skia_available(),
               "Skia reports available although this build has no Skia runtime"))
    return 1;

  std::cout << "PASS: independent Skia fallback contract\n";
  return 0;
}

} // namespace

int main() { return run_independent_fallback_contract(); }
