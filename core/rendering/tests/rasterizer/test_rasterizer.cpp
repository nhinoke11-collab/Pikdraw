#include "pikdraw/document/document.hpp"
#include "pikdraw/rendering/renderer.hpp"
#include "pikdraw/rendering/d2d_backend.hpp"
#include "../../src/gpu/d2d/d2d_internal.hpp"
#include <cassert>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
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

  ViewportRenderer renderer(std::make_shared<SoftwareRasterizer>());
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
  assert(metric.used_gpu);
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

  SoftwareRasterizer oracle;
  std::vector<uint8_t> oracle_pixels(buffer.size(), 0);
  PixelBufferTarget oracle_target{oracle_pixels.data(), w, h, w * 4};
  assert(oracle.render(*doc.current_snapshot(), camera, oracle_target));
  uint64_t absolute_error_sum = 0;
  for (size_t index = 0; index < buffer.size(); ++index) {
    absolute_error_sum += static_cast<uint64_t>(std::abs(
        static_cast<int>(buffer[index]) - static_cast<int>(oracle_pixels[index])));
  }
  const double mean_absolute_error =
      static_cast<double>(absolute_error_sum) / static_cast<double>(buffer.size());
  assert(mean_absolute_error <= 5.0);

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
  image.shared_pixels_rgba = std::make_shared<const std::vector<uint32_t>>(
      std::vector<uint32_t>{0xff0000ff, 0x0000ffff});
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

void test_d2d_zero_fallback_complex_scene_proof() {
  // 1. PathObject with Rotation & Scale Transform
  PathObject transformed_path;
  transformed_path.id = generate_object_id();
  transformed_path.path = geometry::Path2D::make_rect(20, 20, 30, 30);
  transformed_path.fill = color::RGBColor{0.1, 0.8, 0.2, 1.0};
  transformed_path.stroke = color::RGBColor{0.0, 0.0, 0.0, 1.0};
  transformed_path.stroke_width_mm = 2.0;
  transformed_path.has_stroke = true;
  transformed_path.transform = geometry::Transform2D::rotation_degrees(45.0) *
                               geometry::Transform2D::scale(1.2, 1.2);

  // 2. PathObject with Linear Gradient Fill
  PathObject linear_grad_path;
  linear_grad_path.id = generate_object_id();
  linear_grad_path.path = geometry::Path2D::make_rect(60, 20, 40, 30);
  linear_grad_path.has_stroke = false;
  linear_grad_path.fill_gradient = std::make_shared<const GradientPaint>(
      GradientPaint::make_default_linear(
          allocate_gradient_paint_id(), {60, 20, 40, 30},
          color::RGBColor{1, 0, 0, 1}, color::RGBColor{0, 0, 1, 1}));

  // 3. PathObject with Radial Gradient Fill
  PathObject radial_grad_path;
  radial_grad_path.id = generate_object_id();
  radial_grad_path.path = geometry::Path2D::make_rect(110, 20, 40, 30);
  radial_grad_path.has_stroke = false;
  radial_grad_path.fill_gradient = std::make_shared<const GradientPaint>(
      GradientPaint::make_default_radial(
          allocate_gradient_paint_id(), {110, 20, 40, 30},
          color::RGBColor{1, 1, 0, 1}, color::RGBColor{0, 1, 0, 1}));

  // 4. ImageObject
  ImageObject image;
  image.id = generate_object_id();
  image.width_px = 2;
  image.height_px = 2;
  image.pixels_rgba = {0xff0000ff, 0x00ff00ff, 0x0000ffff, 0xffff00ff};
  image.bounds_rect = {20, 70, 30, 30};

  // 5. GroupObject containing child Path
  PathObject child1;
  child1.id = generate_object_id();
  child1.path = geometry::Path2D::make_rect(60, 70, 20, 20);
  child1.fill = color::RGBColor{0.9, 0.4, 0.1, 1.0};
  child1.has_stroke = false;

  GroupObject group;
  group.id = generate_object_id();
  group.children_ids = {child1.id};

  // 6. PathObject with DropShadowEffect
  PathObject shadow_path;
  shadow_path.id = generate_object_id();
  shadow_path.path = geometry::Path2D::make_rect(100, 70, 30, 30);
  shadow_path.fill = color::RGBColor{0.5, 0.2, 0.8, 1.0};
  shadow_path.has_stroke = false;
  EffectNode shadow_effect;
  shadow_effect.id = generate_effect_instance_id();
  shadow_effect.parameters = DropShadowEffect{3.0, 3.0, 2.0, 2.0, color::RGBColor{0, 0, 0, 0.6}, 0.6};
  shadow_path.appearance.effects.push_back(shadow_effect);

  // 7. PathObject with GaussianBlurEffect
  PathObject blur_path;
  blur_path.id = generate_object_id();
  blur_path.path = geometry::Path2D::make_rect(140, 70, 30, 30);
  blur_path.fill = color::RGBColor{0.2, 0.7, 0.9, 1.0};
  blur_path.has_stroke = false;
  EffectNode blur_effect;
  blur_effect.id = generate_effect_instance_id();
  blur_effect.parameters = GaussianBlurEffect{2.5, 1.0};
  blur_path.appearance.effects.push_back(blur_effect);

  // 8. Real Unicode TextObject: proves DirectWrite glyph outlines on the GPU.
  TextObject unicode_text;
  unicode_text.id = generate_object_id();
  unicode_text.text = "Tiếng Việt GPU";
  unicode_text.position = {25, 145};
  unicode_text.fill = color::RGBColor{0.1, 0.1, 0.1, 1.0};

  PersistentObjectMap objects;
  objects = objects.set(transformed_path.id, transformed_path);
  objects = objects.set(linear_grad_path.id, linear_grad_path);
  objects = objects.set(radial_grad_path.id, radial_grad_path);
  objects = objects.set(image.id, image);
  objects = objects.set(child1.id, child1);
  objects = objects.set(group.id, group);
  objects = objects.set(shadow_path.id, shadow_path);
  objects = objects.set(blur_path.id, blur_path);
  objects = objects.set(unicode_text.id, unicode_text);

  Page page = Page::make_default();
  page.layers.front().object_ids = {
      transformed_path.id,
      linear_grad_path.id,
      radial_grad_path.id,
      image.id,
      group.id,
      shadow_path.id,
      blur_path.id,
      unicode_text.id
  };
  const DocumentSnapshot snapshot(1, 1, page, objects);
  constexpr int32_t width = 200;
  constexpr int32_t height = 160;

  // Render via Direct2D 1.1 GPU backend
  D2DRenderBackend d2d_backend;
  assert(d2d_backend.device_status() == DeviceLossStatus::Healthy);
  Camera2D camera({100, 80}, 1.0, width, height);
  std::vector<uint8_t> d2d_pixels(width * height * 4, 0);
  PixelBufferTarget d2d_target{d2d_pixels.data(), width, height, width * 4};
  RenderMetric d2d_metric;
  bool d2d_ok = d2d_backend.render(snapshot, camera, d2d_target, &d2d_metric);
  assert(d2d_ok);
  assert(d2d_metric.used_gpu);
  assert(d2d_metric.total_objects == snapshot.object_count());
  assert(d2d_metric.rendered_objects == snapshot.object_count());
  assert(std::none_of(d2d_metric.diagnostics.begin(), d2d_metric.diagnostics.end(),
                      [](const RenderDiagnostic& diagnostic) { return diagnostic.error; }));

  // Verify non-zero GPU rendering output
  size_t non_white_count = 0;
  for (size_t i = 0; i < d2d_pixels.size(); i += 4) {
    if (d2d_pixels[i] < 250 || d2d_pixels[i + 1] < 250 || d2d_pixels[i + 2] < 250) {
      ++non_white_count;
    }
  }
  assert(non_white_count > 100);

  // Verify float readback premultiplied_rgba
  std::vector<float> d2d_floats(width * height * 4, 0.0f);
  PixelBufferTarget float_target{nullptr, width, height,
                                 static_cast<int32_t>(width * 4 * sizeof(float)),
                                 d2d_floats.data()};
  RenderMetric float_metric;
  bool float_ok = d2d_backend.render(snapshot, camera, float_target, &float_metric);
  assert(float_ok);
  assert(float_metric.used_gpu);
  assert(float_metric.rendered_objects == snapshot.object_count());

  // Render via SoftwareRasterizer Golden Reference Oracle
  SoftwareRasterizer sw_rasterizer;
  std::vector<uint8_t> sw_pixels(width * height * 4, 0);
  PixelBufferTarget sw_target{sw_pixels.data(), width, height, width * 4};
  RenderMetric sw_metric;
  bool sw_ok = sw_rasterizer.render(snapshot, camera, sw_target, &sw_metric);
  assert(sw_ok);
  assert(sw_metric.rendered_objects == snapshot.object_count());

  // Both should have matching white paper background at top-left corner
  assert(d2d_pixels[0] == 255 && d2d_pixels[1] == 255 && d2d_pixels[2] == 255 && d2d_pixels[3] == 255);
  assert(sw_pixels[0] == 255 && sw_pixels[1] == 255 && sw_pixels[2] == 255 && sw_pixels[3] == 255);
  uint64_t complex_error_sum = 0;
  size_t pixels_in_union = 0;
  size_t pixels_in_intersection = 0;
  for (size_t offset = 0; offset < d2d_pixels.size(); offset += 4) {
    bool d2d_foreground = false;
    bool software_foreground = false;
    for (size_t channel = 0; channel < 3; ++channel) {
      d2d_foreground |= d2d_pixels[offset + channel] < 250;
      software_foreground |= sw_pixels[offset + channel] < 250;
      complex_error_sum += static_cast<uint64_t>(std::abs(
          static_cast<int>(d2d_pixels[offset + channel]) -
          static_cast<int>(sw_pixels[offset + channel])));
    }
    if (d2d_foreground || software_foreground)
      ++pixels_in_union;
    if (d2d_foreground && software_foreground)
      ++pixels_in_intersection;
  }
  const double complex_mean_error = static_cast<double>(complex_error_sum) /
      static_cast<double>(width * height * 3);
  const double foreground_overlap = pixels_in_union == 0 ? 1.0 :
      static_cast<double>(pixels_in_intersection) / static_cast<double>(pixels_in_union);
  assert(complex_mean_error <= 12.0);
  assert(foreground_overlap >= 0.70);
  std::cout << "[PIXEL-PARITY] complex mean RGB error: " << complex_mean_error
            << ", foreground overlap: " << foreground_overlap << "\n";
  std::cout << "[PASS] test_d2d_zero_fallback_complex_scene_proof\n";
}

void test_d2d_10k_objects_benchmark() {
  constexpr int total_count = 10000;
  PersistentObjectMap objects;
  std::vector<ObjectId> ids;
  ids.reserve(total_count);
  for (int i = 0; i < total_count; ++i) {
    PathObject obj;
    obj.id = generate_object_id();
    double x = (i % 100) * 20.0;
    double y = (i / 100) * 20.0;
    obj.path = geometry::Path2D::make_rect(x, y, 10, 10);
    obj.fill = color::RGBColor{0.2, 0.4, 0.8, 1.0};
    obj.has_stroke = false;
    objects = objects.set(obj.id, obj);
    ids.push_back(obj.id);
  }

  Page page = Page::make_default();
  page.layers.front().object_ids = std::move(ids);
  const DocumentSnapshot snapshot(1, 1, page, objects);

  ViewportRenderer renderer(std::make_shared<D2DRenderBackend>());
  renderer.camera().set_center({500.0, 500.0});
  renderer.camera().set_zoom(1.0);

  constexpr int32_t width = 800;
  constexpr int32_t height = 600;
  std::vector<uint8_t> buffer(width * height * 4, 0);
  PixelBufferTarget target{buffer.data(), width, height, width * 4};

  RenderMetric metric;
  for (int warmup = 0; warmup < 3; ++warmup)
    assert(renderer.render_frame(snapshot, target, &metric));
  std::vector<double> samples_ms;
  samples_ms.reserve(9);
  for (int sample = 0; sample < 9; ++sample) {
    const auto start = std::chrono::high_resolution_clock::now();
    const bool ok = renderer.render_frame(snapshot, target, &metric);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    assert(ok);
    samples_ms.push_back(elapsed.count() / 1000.0);
  }

  std::sort(samples_ms.begin(), samples_ms.end());
  const double frame_time_ms = samples_ms[samples_ms.size() / 2];
  assert(metric.total_objects == total_count);
  assert(metric.used_gpu);
  assert(metric.culled_objects > 0);
  const double frame_budget_ms = renderer.backend().is_hardware_accelerated() ? 8.3 : 16.6;
  std::cout << "[BENCHMARK] 10,000 objects warmed median (9 samples): " << frame_time_ms << " ms ("
            << (1000.0 / std::max(frame_time_ms, 0.001)) << " FPS), culled: "
            << metric.culled_objects << "/" << metric.total_objects
            << ", budget: " << frame_budget_ms << " ms\n" << std::flush;
  assert(frame_time_ms <= frame_budget_ms);
  std::cout << "[PASS] test_d2d_10k_objects_benchmark\n";
}

void test_d2d_page_and_local_geometry_cache() {
  // Given a dark canvas around a smaller white page, both renderers expose paper.
  Page page = Page::make_default();
  page.width_mm = 30;
  page.height_mm = 40;
  const DocumentSnapshot empty(1, 1, page, PersistentObjectMap{});
  Camera2D camera({0, 0}, 1, 100, 100);
  D2DRenderBackend backend;
  SoftwareRasterizer oracle;
  backend.set_canvas_background({18, 20, 26, 255});
  oracle.set_canvas_background({18, 20, 26, 255});
  std::vector<uint8_t> gpu(100 * 100 * 4), cpu(gpu.size());
  PixelBufferTarget target{gpu.data(), 100, 100, 400};
  PixelBufferTarget reference{cpu.data(), 100, 100, 400};
  RenderMetric metric;
  assert(backend.render(empty, camera, target, &metric) && metric.used_gpu);
  assert(oracle.render(empty, camera, reference));
  assert(gpu == cpu); // Includes off-page background and projected page edges.
  assert(gpu[(25 * 100 + 65) * 4] == 255);
  assert(gpu[(75 * 100 + 25) * 4] == 18);

  // Given object-local geometry, camera motion cannot change the cache key.
  d2d::D2DDeviceManager device;
  assert(device.initialize());
  d2d::D2DGeometryCache cache;
  const ObjectId id = generate_object_id();
  auto path = geometry::Path2D::make_rect(0, 0, 20, 20);
  const auto first = cache.get_or_create_path(device.factory(), id, 1, path);
  assert(first);
  camera.set_center({1000000000.25, 1000000000.5});
  camera.set_zoom(2);
  const auto panned = cache.get_or_create_path(device.factory(), id, 1, path);
  assert(first.Get() == panned.Get());
  const auto clip = cache.get_or_create_path(device.factory(), id, 1, path, 1);
  assert(clip && clip.Get() != first.Get());
  path.set_winding_rule(geometry::WindingRule::EvenOdd);
  const auto even_odd = cache.get_or_create_path(device.factory(), id, 1, path);
  assert(even_odd && even_odd.Get() != first.Get());
  const auto revised = cache.get_or_create_path(device.factory(), id, 2, path);
  assert(revised && revised.Get() != even_odd.Get());

  // When panning/zooming a transformed path at a large world origin, it stays
  // in the same object-local geometry and projects like a fresh GPU render.
  PathObject object;
  object.id = id;
  object.path = path;
  object.transform = geometry::Transform2D::translation(1000000000, 1000000000);
  object.has_stroke = false;
  object.fill = color::RGBColor{1, 0, 0, 1};
  PersistentObjectMap objects;
  objects = objects.set(id, object);
  page.layers.front().object_ids = {id};
  const DocumentSnapshot snapshot(2, 2, page, objects);
  assert(backend.render(snapshot, camera, target, &metric) && metric.used_gpu);
  const size_t inside = (30 * 100 + 70) * 4;
  assert(gpu[inside] == 255 && gpu[inside + 1] == 0 && gpu[inside + 2] == 0);
  camera.set_center({1000000005.25, 1000000004.5});
  camera.set_zoom(1.5);
  assert(backend.render(snapshot, camera, target, &metric) && metric.used_gpu);
  D2DRenderBackend fresh_backend;
  fresh_backend.set_canvas_background({18, 20, 26, 255});
  assert(fresh_backend.render(snapshot, camera, reference));
  assert(gpu == cpu);
}

void test_d2d_icc_preview_matches_software() {
  // Given installed print ICC, exercise a real CMYK transform, not its fallback.
  const auto swop = color::IccPreview::system_srgb_profile().parent_path() / "RSWOP.icm";
  if (!std::filesystem::is_regular_file(swop)) {
    std::cout << "[NOT_MEASURED] No installed SWOP profile for D2D ICC parity\n";
    return;
  }
  std::string error;
  const auto preview = color::IccPreview::create({}, swop, &error);
  assert(preview && preview->has_cmyk_transform());
  const auto source = color::ColorValue::make_cmyk(0, 1, 1, 0, 1);
  const auto converted = preview->convert(source);
  const auto approximate = color::to_preview_rgba(source);
  assert(std::abs(converted.r - approximate.r) + std::abs(converted.g - approximate.g) +
         std::abs(converted.b - approximate.b) > .02);
  D2DRenderBackend backend;
  SoftwareRasterizer oracle;
  backend.set_color_preview(preview);
  oracle.set_color_preview(preview);
  Camera2D camera({50, 50}, 1, 100, 100);
  std::vector<uint8_t> gpu(100 * 100 * 4), cpu(gpu.size());
  PixelBufferTarget target{gpu.data(), 100, 100, 400};
  PixelBufferTarget reference{cpu.data(), 100, 100, 400};
  for (int mode = 0; mode < 3; ++mode) {
    PathObject path;
    path.id = generate_object_id();
    path.path = geometry::Path2D::make_rect(20, 20, 60, 60);
    path.fill = source;
    path.stroke = source;
    path.has_stroke = mode == 2;
    path.has_fill = mode != 2;
    path.stroke_width_mm = 10;
    if (mode == 1) {
      path.fill_gradient = std::make_shared<const GradientPaint>(
          GradientPaint::make_default_linear(allocate_gradient_paint_id(),
                                             {20, 20, 60, 60}, source, source));
    }
    Document doc("d2d-icc-parity");
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(path)).success);
    RenderMetric metric;
    assert(backend.render(*doc.current_snapshot(), camera, target, &metric));
    assert(metric.used_gpu);
    assert(oracle.render(*doc.current_snapshot(), camera, reference));
    const size_t sample = ((mode == 2 ? 20 : 50) * 100 + 50) * 4;
    for (size_t channel = 0; channel < 4; ++channel)
      assert(std::abs(int(gpu[sample + channel]) - int(cpu[sample + channel])) <= 1);
    assert(gpu[sample + 1] > 5 || gpu[sample + 2] > 5); // ICC differs from naive red.
  }
  assert(preview->conversion_failure_count() == 0);
}

// Given byte targets, when CPU/GPU render alpha and effects, BGRA preserves RGBA semantics.
void test_bgra_targets_and_image_upload_cache() {
  Document doc("bgra-frame");
  PathObject path;
  path.id = generate_object_id();
  path.path.move_to({5, 5}); path.path.line_to({35, 5});
  path.path.line_to({35, 35}); path.path.line_to({5, 35}); path.path.close();
  path.fill = color::RGBColor{0.7, 0.2, 0.4, 1};
  path.has_fill = true; path.has_stroke = false;
  path.appearance.object_opacity = 0.5;
  EffectNode effect;
  effect.id = generate_effect_instance_id();
  effect.parameters = GaussianBlurEffect{1.0, 1.0};
  path.appearance.effects.push_back(effect);
  assert(doc.execute_command(std::make_unique<AddObjectCommand>(path)).success);
  Camera2D camera;
  camera.set_viewport_size(48, 48); camera.set_center({24, 24}); camera.set_zoom(1);
  D2DRenderBackend gpu;
  SoftwareRasterizer cpu;
  for (IRenderBackend* backend : std::vector<IRenderBackend*>{&gpu, &cpu}) {
    backend->set_canvas_background({10, 20, 30, 0});
    std::vector<uint8_t> rgba(48 * 48 * 4);
    std::vector<uint32_t> bgra(48 * 48);
    PixelBufferTarget reference{rgba.data(), 48, 48, 48 * 4};
    PixelBufferTarget target{nullptr, 48, 48, 48 * 4}; target.bgra_pixels = bgra.data();
    assert(target.is_valid());
    assert(backend->render(*doc.current_snapshot(), camera, reference));
    assert(backend->render(*doc.current_snapshot(), camera, target));
    for (size_t pixel = 0; pixel < bgra.size(); ++pixel) {
      const auto packed = bgra[pixel];
      assert((packed & 255) == rgba[pixel * 4 + 2]);
      assert(((packed >> 8) & 255) == rgba[pixel * 4 + 1]);
      assert(((packed >> 16) & 255) == rgba[pixel * 4]);
      assert((packed >> 24) == rgba[pixel * 4 + 3]);
    }
    target.pixels = rgba.data(); assert(!target.is_valid());
  }
  d2d::D2DDeviceManager device; assert(device.initialize());
  d2d::D2DImageCache cache;
  ImageObject image; image.id = generate_object_id();
  image.width_px = image.height_px = 1; image.pixels_rgba = {0xFF0000FF};
  auto first = cache.get(device.context(), image, 1); assert(first);
  assert(cache.get(device.context(), image, 1).Get() == first.Get());
  image.pixels_rgba[0] = 0x0000FFFF;
  auto changed = cache.get(device.context(), image, 2); assert(changed);
  assert(changed.Get() != first.Get());
  cache.clear();
  assert(cache.get(device.context(), image, 2).Get() != changed.Get());

  auto shared_pixels = std::make_shared<const std::vector<uint32_t>>(
      std::vector<uint32_t>{0x102030ff});
  ImageObject shared_a; shared_a.id = generate_object_id();
  shared_a.width_px = shared_a.height_px = 1;
  shared_a.shared_pixels_rgba = shared_pixels;
  ImageObject shared_b = shared_a; shared_b.id = generate_object_id();
  cache.clear();
  auto shared_bitmap = cache.get(device.context(), shared_a, 3); assert(shared_bitmap);
  assert(cache.get(device.context(), shared_b, 3).Get() == shared_bitmap.Get());
  assert(cache.cached_image_count() == 1);

  std::vector<ImageObject> many_images;
  many_images.reserve(68);
  for (uint32_t index = 0; index < 68; ++index) {
    ImageObject many;
    many.id = generate_object_id();
    many.width_px = many.height_px = 1;
    many.pixels_rgba = {0x000000ffu | (index << 24)};
    many_images.push_back(std::move(many));
  }
  cache.clear();
  auto first_many_bitmap = cache.get(device.context(), many_images.front(), 4);
  assert(first_many_bitmap);
  for (size_t index = 1; index < many_images.size(); ++index)
    assert(cache.get(device.context(), many_images[index], 4));
  assert(cache.cached_image_count() == many_images.size());
  assert(cache.get(device.context(), many_images.front(), 4).Get() ==
         first_many_bitmap.Get());

  // The CPU viewport guard must allow a 4K/16:10 frame above the former 8 MP cap.
  std::vector<uint32_t> large(4096 * 2160);
  PixelBufferTarget target{nullptr, 4096, 2160, 4096 * 4}; target.bgra_pixels = large.data();
  camera.set_viewport_size(4096, 2160);
  assert(cpu.render(*doc.current_snapshot(), camera, target));
}

void test_document_raster_effects_dpi();
int main() {
  test_document_raster_effects_dpi();
  std::cout << "[test_rasterizer] Running software rasterizer tests...\n";
  test_bgra_targets_and_image_upload_cache();
  test_software_rasterizer_frame();
  test_analytic_ellipse_renders_from_authoring_source();
  test_backend_status_state_machine();
  test_d2d_render_backend();
  test_d2d_renders_transform_gradient_and_effects();
  test_d2d_renders_images();
  test_d2d_page_and_local_geometry_cache();
  test_d2d_icc_preview_matches_software();
  test_d2d_zero_fallback_complex_scene_proof();
  test_d2d_10k_objects_benchmark();
  std::cout << "[test_rasterizer] All tests passed!\n";
  return 0;
}
