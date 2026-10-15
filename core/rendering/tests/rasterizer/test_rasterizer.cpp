#include "pikdraw/document/document.hpp"
#include "pikdraw/rendering/renderer.hpp"
#include <cassert>
#include <iostream>
#include <vector>

using namespace pikdraw;
using namespace pikdraw::rendering;
using namespace pikdraw::document;

void test_software_rasterizer_frame() {
  Document doc("doc-raster-test");

  PathObject po;
  po.id = generate_object_id();
  po.path.move_to({100, 100});
  po.path.line_to({300, 100});
  po.path.line_to({300, 300});
  po.path.close();
  doc.execute_command(std::make_unique<AddObjectCommand>(po));

  ViewportRenderer renderer;
  renderer.camera().set_center({200.0, 200.0});
  renderer.camera().set_zoom(1.0);

  const int32_t w = 400;
  const int32_t h = 300;
  std::vector<uint8_t> buffer(w * h * 4, 0);
  PixelBufferTarget target{buffer.data(), w, h, w * 4};

  RenderMetric metric;
  bool ok = renderer.render_frame(*doc.current_snapshot(), target, &metric);

  assert(ok);
  assert(metric.total_objects == 1);
  assert(metric.rendered_objects == 1);
  assert(metric.culled_objects == 0);

  // Pixel-center (200.5,149.5) lies on the diagonal x+y=350.
  size_t stroke_idx = (149 * w + 200) * 4;
  assert(buffer[stroke_idx + 0] == 0);
  assert(buffer[stroke_idx + 1] == 0);
  assert(buffer[stroke_idx + 2] == 0);

  // Pixel (250,200) is inside the filled triangle, not bare paper.
  size_t page_idx = (200 * w + 250) * 4;
  assert(buffer[page_idx + 0] == 51);
  assert(buffer[page_idx + 1] == 102);
  assert(buffer[page_idx + 2] == 204);

  // Now test off-page canvas background
  renderer.camera().set_center({-5000.0, -5000.0}); // Far outside page
  renderer.render_frame(*doc.current_snapshot(), target, &metric);
  assert(buffer[0] == 18);
  assert(buffer[1] == 20);
  assert(buffer[2] == 26);
}

void test_backend_status_state_machine() {
  auto rasterizer = std::make_shared<SoftwareRasterizer>();
  ViewportRenderer renderer(rasterizer);

  Document doc("doc-loss-test");
  const int32_t w = 200;
  const int32_t h = 200;
  std::vector<uint8_t> buffer(w * h * 4, 0);
  PixelBufferTarget target{buffer.data(), w, h, w * 4};

  // 1. Healthy render
  assert(renderer.render_frame(*doc.current_snapshot(), target));

  // 2. Simulate Recoverable Loss
  rasterizer->simulate_device_loss(DeviceLossStatus::RecoverableLoss);
  // Exercise retry orchestration on an injected software status only.
  bool recovered = renderer.render_frame(*doc.current_snapshot(), target);
  assert(recovered);
  assert(rasterizer->device_status() == DeviceLossStatus::Healthy);

  // 3. Inject status only; this is not a native driver crash test.
  rasterizer->simulate_device_loss(DeviceLossStatus::FatalDriverCrash);
  bool fatal_ok = renderer.render_frame(*doc.current_snapshot(), target);
  assert(!fatal_ok);
  assert(rasterizer->device_status() == DeviceLossStatus::FatalDriverCrash);
}

int main() {
  std::cout << "[test_rasterizer] Running software rasterizer tests...\n";
  test_software_rasterizer_frame();
  test_backend_status_state_machine();
  std::cout << "[test_rasterizer] All tests passed!\n";
  return 0;
}
