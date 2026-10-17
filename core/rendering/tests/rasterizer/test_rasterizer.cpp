#include "pikdraw/document/document.hpp"
#include "pikdraw/rendering/renderer.hpp"
#include "pikdraw/rendering/d2d_backend.hpp"
#include "pikdraw/rendering/skia_backend.hpp"
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

void test_analytic_ellipse_renders_from_authoring_source() {
  Document doc("analytic-ellipse-render");
  PathObject object;
  object.id = generate_object_id();
  object.fill = color::RGBColor{1, 0, 0, 1};
  object.has_stroke = false;
  EditablePath source;
  EditableSubpath subpath;
  subpath.id = generate_path_element_id<SubpathId>();
  subpath.closed = true;
  constexpr double pi = 3.14159265358979323846;
  const geometry::Point2D center{100, 100};
  const geometry::Point2D axis_u{30, 0};
  const geometry::Point2D axis_v{0, 20};
  for (size_t i = 0; i < 4; ++i) {
    const double angle = static_cast<double>(i) * pi * 0.5;
    subpath.nodes.push_back(PathNode{
        generate_path_element_id<NodeId>(),
        center + axis_u * std::cos(angle) + axis_v * std::sin(angle)});
    EditableSegment segment;
    segment.id = generate_path_element_id<SegmentId>();
    segment.kind = PathSegmentKind::EllipticArc;
    segment.arc = {center, axis_u, axis_v, angle, pi * 0.5};
    subpath.segments.push_back(segment);
  }
  source.subpaths.push_back(std::move(subpath));
  object.set_authoring_source(std::move(source));
  assert(doc.execute_command(std::make_unique<AddObjectCommand>(object)).success);

  ViewportRenderer renderer;
  renderer.camera().set_center(center);
  renderer.camera().set_zoom(1.0);
  constexpr int32_t width = 200;
  constexpr int32_t height = 200;
  std::vector<uint8_t> pixels(width * height * 4, 0);
  PixelBufferTarget target{pixels.data(), width, height, width * 4};
  assert(renderer.render_frame(*doc.current_snapshot(), target));
  const size_t center_pixel = (100 * width + 100) * 4;
  assert(pixels[center_pixel] == 255 && pixels[center_pixel + 1] == 0 &&
         pixels[center_pixel + 2] == 0);
}

void test_d2d_render_backend() {
  Document doc("doc-d2d-test");
  PathObject po;
  po.id = generate_object_id();
  po.path.move_to({50, 50});
  po.path.line_to({150, 50});
  po.path.line_to({150, 150});
  po.path.close();
  po.fill = color::RGBColor{0.2, 0.4, 0.8, 1.0};
  po.has_fill = true;
  doc.execute_command(std::make_unique<AddObjectCommand>(po));

  D2DRenderBackend backend;
  assert(backend.device_status() == DeviceLossStatus::Healthy);

  const int32_t w = 200;
  const int32_t h = 200;
  std::vector<uint8_t> buffer(w * h * 4, 0);
  PixelBufferTarget target{buffer.data(), w, h, w * 4};

  Camera2D camera({100.0, 100.0}, 1.0, w, h);
  RenderMetric metric;
  bool ok = backend.render(*doc.current_snapshot(), camera, target, &metric);
  assert(ok);
  assert(metric.total_objects == 1);
  assert(metric.rendered_objects == 1);

  // Verify real pixel output from D2D
  size_t bg_idx = (10 * w + 10) * 4;
  assert(buffer[bg_idx + 0] == 255);
  assert(buffer[bg_idx + 1] == 255);
  assert(buffer[bg_idx + 2] == 255);

  size_t inside_idx = (120 * w + 120) * 4;
  assert(buffer[inside_idx + 0] == 51);  // R: 0.2 * 255
  assert(buffer[inside_idx + 1] == 102); // G: 0.4 * 255
  assert(buffer[inside_idx + 2] == 204); // B: 0.8 * 255
  assert(buffer[inside_idx + 3] == 255); // A: 1.0 * 255

  // Test GPU interface handles invalid targets gracefully
  GpuSurfaceTarget invalid_target{};
  bool gpu_ok = backend.render_gpu(*doc.current_snapshot(), camera, invalid_target, &metric);
  assert(!gpu_ok);
}

void test_d2d_renders_transform_gradient_and_effects() {
  PathObject path;
  path.id = generate_object_id();
  path.path = geometry::Path2D::make_rect(35, 40, 35, 30);
  path.has_stroke = false;
  path.transform = geometry::Transform2D::translation(12, -4);
  path.fill_gradient = std::make_shared<const GradientPaint>(
      GradientPaint::make_default_linear(
          allocate_gradient_paint_id(), {35, 40, 35, 30},
          color::RGBColor{1, 0, 0, 1}, color::RGBColor{0, 0, 1, 1}));
  EffectNode blur;
  blur.id = generate_effect_instance_id();
  blur.parameters = GaussianBlurEffect{2.0, 1.0};
  path.appearance.effects.push_back(blur);
  GroupObject group;
  group.id = generate_object_id();
  group.children_ids = {path.id};
  PersistentObjectMap objects;
  objects = objects.set(path.id, path);
  objects = objects.set(group.id, group);
  Page page = Page::make_default();
  page.layers.front().object_ids = {group.id};
  const DocumentSnapshot snapshot(1, 1, page, objects);

  constexpr int32_t width = 160;
  constexpr int32_t height = 160;
  std::vector<uint8_t> actual(width * height * 4, 0);
  PixelBufferTarget actual_target{actual.data(), width, height, width * 4};
  const Camera2D camera({80, 80}, 1.0, width, height);
  D2DRenderBackend backend;
  RenderMetric actual_metric;
  assert(backend.render(snapshot, camera, actual_target, &actual_metric));
  assert(actual_metric.total_objects == 2);
  assert(actual_metric.rendered_objects == 2);

  // Background pixel at top-left should be white (255, 255, 255, 255)
  assert(actual[0] == 255 && actual[1] == 255 && actual[2] == 255 && actual[3] == 255);

  // Center of transformed rect is in camera view; verify gradient rendered with non-zero color
  bool has_red = false;
  bool has_blue = false;
  for (size_t i = 0; i < actual.size(); i += 4) {
    if (actual[i] < 250 && actual[i + 2] > 50) has_blue = true;
    if (actual[i] > 100 && actual[i + 2] < 200) has_red = true;
  }
  assert(has_red);
  assert(has_blue);

  // Verify GPU staging readback to premultiplied float target
  std::vector<float> float_buffer(width * height * 4, 0.0f);
  PixelBufferTarget float_target{nullptr, width, height,
                                 static_cast<int32_t>(width * 4 * sizeof(float)),
                                 float_buffer.data()};
  RenderMetric float_metric;
  assert(backend.render(snapshot, camera, float_target, &float_metric));
  assert(float_metric.rendered_objects == 2);
  assert(float_buffer[0] == 1.0f && float_buffer[1] == 1.0f && float_buffer[2] == 1.0f && float_buffer[3] == 1.0f);
}

void test_d2d_renders_images() {
  Document doc("doc-d2d-image-test");
  ImageObject image;
  image.id = generate_object_id();
  image.width_px = 1;
  image.height_px = 2;
  image.pixels_rgba = {0xff0000ff, 0x0000ffff};
  image.bounds_rect = {30, 30, 40, 40};
  image.transform = geometry::Transform2D::translation(10, 0);
  assert(doc.execute_command(std::make_unique<AddObjectCommand>(image)).success);

  constexpr int32_t width = 100;
  constexpr int32_t height = 100;
  std::vector<uint8_t> pixels(width * height * 4, 0);
  PixelBufferTarget target{pixels.data(), width, height, width * 4};
  const Camera2D camera({50, 50}, 1.0, width, height);
  D2DRenderBackend backend;
  RenderMetric metric;
  assert(backend.render(*doc.current_snapshot(), camera, target, &metric));
  assert(metric.total_objects == 1);
  assert(metric.rendered_objects == 1);
  const size_t top_pixel = (40 * width + 72) * 4;
  const size_t bottom_pixel = (60 * width + 72) * 4;
  assert(pixels[top_pixel] == 255 && pixels[top_pixel + 1] == 0 &&
         pixels[top_pixel + 2] == 0 && pixels[top_pixel + 3] == 255);
  assert(pixels[bottom_pixel] == 0 && pixels[bottom_pixel + 1] == 0 &&
         pixels[bottom_pixel + 2] == 255 && pixels[bottom_pixel + 3] == 255);
  const size_t old_position_pixel = (40 * width + 32) * 4;
  assert(pixels[old_position_pixel] == 255 &&
         pixels[old_position_pixel + 1] == 255 &&
         pixels[old_position_pixel + 2] == 255);
}

void test_skia_render_backend() {
  Document doc("doc-skia-test");
  PathObject po;
  po.id = generate_object_id();
  po.path.move_to({20, 20});
  po.path.line_to({120, 20});
  po.path.line_to({120, 120});
  po.path.close();
  po.fill = color::RGBColor{0.9, 0.1, 0.1, 1.0};
  po.has_fill = true;
  doc.execute_command(std::make_unique<AddObjectCommand>(po));

  SkiaRenderBackend backend;
  assert(!backend.is_skia_available());
  assert(backend.device_status() == DeviceLossStatus::Healthy);

  const int32_t w = 200;
  const int32_t h = 200;
  std::vector<uint8_t> buffer(w * h * 4, 0);
  PixelBufferTarget target{buffer.data(), w, h, w * 4};

  Camera2D camera({70.0, 70.0}, 1.0, w, h);
  RenderMetric metric;
  bool ok = backend.render(*doc.current_snapshot(), camera, target, &metric);
  assert(ok);
  assert(metric.total_objects == 1);
  assert(metric.rendered_objects == 1);

  // Test GPU interface handles invalid targets gracefully
  GpuSurfaceTarget invalid_target{};
  bool gpu_ok = backend.render_gpu(*doc.current_snapshot(), camera, invalid_target, &metric);
  assert(!gpu_ok);
}

int main() {
  std::cout << "[test_rasterizer] Running software rasterizer tests...\n";
  test_software_rasterizer_frame();
  test_analytic_ellipse_renders_from_authoring_source();
  test_backend_status_state_machine();
  test_d2d_render_backend();
  test_d2d_renders_transform_gradient_and_effects();
  test_d2d_renders_images();
  test_skia_render_backend();
  std::cout << "[test_rasterizer] All tests passed!\n";
  return 0;
}
