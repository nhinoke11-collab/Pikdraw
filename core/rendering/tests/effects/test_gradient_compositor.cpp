#include "pikdraw/rendering/software_rasterizer.hpp"
#include "pikdraw/rendering/gradient_sampler.hpp"
#include "pikdraw/document/model/object.hpp"
#include "../../src/effects/object_compositor.hpp"
#include "../../src/raster/path_raster.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace pikdraw;
using namespace pikdraw::rendering;

namespace {

document::PathObject make_rect_gradient_path(double x, double y, double w, double h) {
  document::PathObject po;
  po.id = 1;
  po.path = geometry::Path2D::make_rect(x, y, w, h);
  po.has_fill = true;
  po.has_stroke = false;

  document::GradientPaint grad;
  grad.id = 501;
  grad.kind = document::GradientKind::Linear;
  grad.spread = document::GradientSpread::Pad;
  grad.gradient_to_object = geometry::Transform2D::scale(w, h);
  grad.stops = {
      document::GradientStop{1, 0.0, color::RGBColor{1.0, 0.0, 0.0, 1.0}, 1.0, 0.5},
      document::GradientStop{2, 1.0, color::RGBColor{0.0, 0.0, 1.0, 1.0}, 1.0, 0.5}
  };
  po.fill_gradient = std::make_shared<const document::GradientPaint>(std::move(grad));
  return po;
}

} // namespace

void test_direct_paint_gradient() {
  const int width = 100;
  const int height = 50;
  std::vector<uint8_t> buffer(static_cast<size_t>(width * height * 4), 0);
  PixelBufferTarget target;
  target.pixels = buffer.data();
  target.width = width;
  target.height = height;
  target.stride_bytes = width * 4;

  Camera2D camera({50.0, 25.0}, 1.0, width, height);
  auto path = make_rect_gradient_path(0, 0, 100, 50);

  bool ok = raster::paint(path, camera, target);
  assert(ok);

  // Left pixel (near x=5): should be Red (255, 0, 0, 255)
  const size_t left_idx = (25 * width + 5) * 4;
  assert(buffer[left_idx + 0] > 200); // R
  assert(buffer[left_idx + 2] < 50);  // B
  assert(buffer[left_idx + 3] == 255); // A

  // Right pixel (near x=95): should be Blue (0, 0, 255, 255)
  const size_t right_idx = (25 * width + 95) * 4;
  assert(buffer[right_idx + 0] < 50);  // R
  assert(buffer[right_idx + 2] > 200); // B
  assert(buffer[right_idx + 3] == 255); // A

  // Outside pixel: should be transparent (0, 0, 0, 0)
  std::fill(buffer.begin(), buffer.end(), 0);
  auto small_path = make_rect_gradient_path(20, 10, 60, 30);
  ok = raster::paint(small_path, camera, target);
  assert(ok);
  const size_t out_idx = (5 * width + 5) * 4;
  assert(buffer[out_idx + 3] == 0);

  std::cout << "[PASS] test_direct_paint_gradient\n";
}

void test_compositor_direct_parity() {
  const int width = 100;
  const int height = 50;
  std::vector<uint8_t> buf1(static_cast<size_t>(width * height * 4), 0);
  std::vector<uint8_t> buf2(static_cast<size_t>(width * height * 4), 0);

  PixelBufferTarget target1;
  target1.pixels = buf1.data();
  target1.width = width;
  target1.height = height;
  target1.stride_bytes = width * 4;

  PixelBufferTarget target2;
  target2.pixels = buf2.data();
  target2.width = width;
  target2.height = height;
  target2.stride_bytes = width * 4;

  Camera2D camera({50.0, 25.0}, 1.0, width, height);
  auto path = make_rect_gradient_path(10, 10, 80, 30);

  // 1. Direct paint
  bool ok1 = raster::paint(path, camera, target1);
  assert(ok1);

  // 2. Compositor with default appearance
  bool ok2 = effects::render_path_with_appearance(
      path, path.appearance, camera, target2, nullptr, nullptr, nullptr);
  assert(ok2);

  // Verify exact parity between direct paint and compositor default path
  assert(buf1 == buf2);

  std::cout << "[PASS] test_compositor_direct_parity\n";
}

void test_compositor_effects_with_gradient() {
  const int width = 100;
  const int height = 50;
  std::vector<uint8_t> buffer(static_cast<size_t>(width * height * 4), 0);
  PixelBufferTarget target;
  target.pixels = buffer.data();
  target.width = width;
  target.height = height;
  target.stride_bytes = width * 4;

  Camera2D camera({50.0, 25.0}, 1.0, width, height);
  auto path = make_rect_gradient_path(20, 15, 60, 20);

  // Add Gaussian Blur effect to appearance
  document::EffectNode blur_node;
  blur_node.id = 1;
  blur_node.enabled = true;
  document::GaussianBlurEffect blur;
  blur.sigma_x_mm = 2.0;
  blur.sigma_y_mm = 2.0;
  blur_node.parameters = blur;
  path.appearance.effects.push_back(blur_node);

  size_t peak_blur = 0;
  size_t peak_shadow = 0;
  bool ok = effects::render_path_with_appearance(
      path, path.appearance, camera, target, nullptr, &peak_blur, &peak_shadow);
  assert(ok);
  assert(peak_blur > 0);

  // Blurred gradient should have non-zero alpha inside
  const size_t center_idx = (25 * width + 50) * 4;
  assert(buffer[center_idx + 3] > 0);

  std::cout << "[PASS] test_compositor_effects_with_gradient\n";
}

void test_hole_winding_rule() {
  const int width = 100;
  const int height = 100;
  std::vector<uint8_t> buffer(static_cast<size_t>(width * height * 4), 0);
  PixelBufferTarget target;
  target.pixels = buffer.data();
  target.width = width;
  target.height = height;
  target.stride_bytes = width * 4;

  Camera2D camera({50.0, 50.0}, 1.0, width, height);

  // Outer rectangle: (10, 10) to (90, 90)
  // Inner hole: (30, 30) to (70, 70)
  geometry::Path2D donut_path;
  donut_path.set_winding_rule(geometry::WindingRule::EvenOdd);
  donut_path.move_to(10, 10);
  donut_path.line_to(90, 10);
  donut_path.line_to(90, 90);
  donut_path.line_to(10, 90);
  donut_path.close();

  donut_path.move_to(30, 30);
  donut_path.line_to(70, 30);
  donut_path.line_to(70, 70);
  donut_path.line_to(30, 70);
  donut_path.close();

  document::PathObject po;
  po.id = 2;
  po.path = donut_path;
  po.has_fill = true;
  po.has_stroke = false;

  document::GradientPaint grad;
  grad.id = 502;
  grad.kind = document::GradientKind::Linear;
  grad.gradient_to_object = geometry::Transform2D::scale(100, 100);
  grad.stops = {
      document::GradientStop{1, 0.0, color::RGBColor{1, 0, 0, 1}, 1.0, 0.5},
      document::GradientStop{2, 1.0, color::RGBColor{0, 1, 0, 1}, 1.0, 0.5}
  };
  po.fill_gradient = std::make_shared<const document::GradientPaint>(std::move(grad));

  bool ok = raster::paint(po, camera, target);
  assert(ok);

  // Outer region (20, 20): filled
  const size_t outer_idx = (20 * width + 20) * 4;
  assert(buffer[outer_idx + 3] == 255);

  // Hole center (50, 50): MUST be transparent (alpha == 0)
  const size_t hole_idx = (50 * width + 50) * 4;
  assert(buffer[hole_idx + 3] == 0);

  std::cout << "[PASS] test_hole_winding_rule\n";
}

int main() {
  try {
    test_direct_paint_gradient();
    test_compositor_direct_parity();
    test_compositor_effects_with_gradient();
    test_hole_winding_rule();
    std::cout << "All test_gradient_compositor checks PASSED.\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Exception in test_gradient_compositor: " << e.what() << "\n";
    return 1;
  }
}
