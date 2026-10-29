#include "pikdraw/document/commands/clip_commands.hpp"
#include "pikdraw/document/commands/annotation_commands.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/color/profiles/profile_resource.hpp"
#include "pikdraw/document/commands/contour_commands.hpp"
#include "pikdraw/rendering/renderer.hpp"
#include "pikdraw/rendering/d2d_backend.hpp"
#include "../../src/gpu/d2d/d2d_internal.hpp"
#include <cassert>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
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
  po.stroke_width_mm = 1.0;
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

void test_detail_view_renders_clipped_source_content() {
  Document doc("detail-view-source-render");
  PathObject source;
  source.id = generate_object_id();
  source.name = "Detail source";
  source.path = geometry::Path2D::make_rect(0, 0, 20, 20);
  source.fill = color::RGBColor{0, 1, 0, 1};
  source.has_fill = true;
  source.has_stroke = false;
  assert(doc.execute_command(std::make_unique<AddObjectCommand>(source)).success);

  DetailViewObject detail;
  detail.source_page_id = doc.active_page_id();
  detail.source_region = {0, 0, 20, 20};
  detail.frame_rect = {60, 0, 40, 40};
  detail.scale_ratio = 2.0;
  detail.shape = DetailViewShape::Rectangle;
  assert(doc.execute_command(
      std::make_unique<AddDetailViewCommand>(detail)).success);

  ViewportRenderer renderer(std::make_shared<SoftwareRasterizer>());
  renderer.camera().set_center({50.0, 20.0});
  renderer.camera().set_zoom(2.0);
  constexpr int32_t width = 200;
  constexpr int32_t height = 120;
  std::vector<uint8_t> pixels(width * height * 4, 0);
  PixelBufferTarget target{pixels.data(), width, height, width * 4};
  RenderMetric metric;
  assert(renderer.render_frame(*doc.current_snapshot(), target, &metric));

  // The destination frame center maps to the center of the green source path.
  const auto index = static_cast<size_t>(60 * width + 160) * 4;
  assert(pixels[index] < 8);
  assert(pixels[index + 1] > 245);
  assert(pixels[index + 2] < 8);
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
  GpuSurfaceTarget invalid_dpi{GpuTargetType::GpuTexture, buffer.data(), w, h,
                               std::numeric_limits<float>::quiet_NaN()};
  assert(!backend.render_gpu(*doc.current_snapshot(), camera, invalid_dpi, &metric));
}

void test_d2d_swap_chain_target() {
  constexpr int32_t width = 64;
  constexpr int32_t height = 64;
  HWND hwnd = CreateWindowExW(0, L"STATIC", L"PikDraw swap-chain test",
                             WS_POPUP, 0, 0, width, height, nullptr, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
  assert(hwnd != nullptr);
  struct WindowCleanup {
    HWND hwnd;
    ~WindowCleanup() { if (hwnd) DestroyWindow(hwnd); }
  } cleanup{hwnd};

  Document doc("d2d-swap-chain-target");
  PathObject path;
  path.id = generate_object_id();
  path.path = geometry::Path2D::make_rect(16, 16, 32, 32);
  path.fill = color::RGBColor{1, 0, 0, 1};
  path.has_stroke = false;
  assert(doc.execute_command(std::make_unique<AddObjectCommand>(path)).success);

  D2DRenderBackend backend;
  assert(backend.create_swap_chain_target(hwnd, width, height));
  Camera2D camera({32, 32}, 1, width, height);
  RenderMetric metric;
  auto target = backend.swap_chain_target();
  assert(target.is_valid() && target.type == GpuTargetType::DxgiSwapChain);
  assert(backend.render_gpu(*doc.current_snapshot(), camera, target, &metric));
  assert(metric.used_gpu && metric.rendered_objects == 1);

  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
  PixelBufferTarget capture{pixels.data(), width, height, width * 4};
  assert(backend.capture_swap_chain_target(capture));
  const size_t center = (32 * width + 32) * 4;
  assert(pixels[center] > 245 && pixels[center + 1] < 8 && pixels[center + 2] < 8);

  auto *gdi_dc = static_cast<HDC>(backend.acquire_swap_chain_target_dc());
  assert(gdi_dc != nullptr);
  assert(SetPixel(gdi_dc, 2, 2, RGB(0, 255, 0)) != CLR_INVALID);
  assert(!backend.present_swap_chain_target());
  assert(!backend.render_gpu(*doc.current_snapshot(), camera, target, &metric));
  assert(backend.release_swap_chain_target_dc());
  assert(backend.capture_swap_chain_target(capture));
  const size_t gdi_pixel = (2 * width + 2) * 4;
  assert(pixels[gdi_pixel] < 8 && pixels[gdi_pixel + 1] > 245 &&
         pixels[gdi_pixel + 2] < 8);
  assert(backend.present_swap_chain_target());

  gdi_dc = static_cast<HDC>(backend.acquire_swap_chain_target_dc());
  assert(gdi_dc != nullptr);
  HBRUSH blue = CreateSolidBrush(RGB(0, 0, 255));
  RECT full_surface{0, 0, width, height};
  assert(FillRect(gdi_dc, &full_surface, blue) != 0);
  DeleteObject(blue);
  assert(backend.release_swap_chain_target_dc());
  target.viewport_x = 8;
  target.viewport_y = 8;
  target.viewport_width = 32;
  target.viewport_height = 32;
  Camera2D viewport_camera({32, 32}, 1, 32, 32);
  assert(backend.render_gpu(*doc.current_snapshot(), viewport_camera,
                            target, &metric));
  assert(backend.capture_swap_chain_target(capture));
  const size_t partial_center = (24 * width + 24) * 4;
  const size_t outside_viewport = (4 * width + 4) * 4;
  assert(pixels[partial_center] > 245 && pixels[partial_center + 1] < 8 &&
         pixels[partial_center + 2] < 8);
  assert(pixels[outside_viewport] < 8 && pixels[outside_viewport + 1] < 8 &&
         pixels[outside_viewport + 2] > 245);
  assert(backend.present_swap_chain_target());
  target.viewport_x = 0;
  target.viewport_y = 0;
  target.viewport_width = 0;
  target.viewport_height = 0;

  constexpr int32_t resized_width = 80;
  constexpr int32_t resized_height = 48;
  assert(backend.resize_swap_chain_target(resized_width, resized_height));
  assert(!backend.render_gpu(*doc.current_snapshot(), camera, target, &metric));
  target = backend.swap_chain_target();
  assert(target.width == resized_width && target.height == resized_height);
  camera = Camera2D({40, 24}, 1, resized_width, resized_height);
  assert(backend.render_gpu(*doc.current_snapshot(), camera, target, &metric));
  std::vector<uint8_t> resized_pixels(
      static_cast<size_t>(resized_width) * resized_height * 4);
  PixelBufferTarget resized_capture{resized_pixels.data(), resized_width,
                                    resized_height, resized_width * 4};
  assert(backend.capture_swap_chain_target(resized_capture));
  const size_t resized_center = (24 * resized_width + 40) * 4;
  assert(resized_pixels[resized_center] > 245 &&
         resized_pixels[resized_center + 1] < 8 &&
         resized_pixels[resized_center + 2] < 8);
  assert(backend.present_swap_chain_target());
}

void test_d2d_swap_chain_10k_timing() {
  constexpr int32_t width = 800;
  constexpr int32_t height = 600;
  PersistentObjectMap objects;
  std::vector<ObjectId> ids;
  ids.reserve(10000);
  for (int i = 0; i < 10000; ++i) {
    PathObject object;
    object.id = generate_object_id();
    object.path = geometry::Path2D::make_rect((i % 100) * 20.0,
                                               (i / 100) * 20.0, 10, 10);
    object.fill = color::RGBColor{0.2, 0.4, 0.8, 1.0};
    object.has_stroke = false;
    objects = objects.set(object.id, object);
    ids.push_back(object.id);
  }
  Page page = Page::make_default();
  page.layers.front().object_ids = std::move(ids);
  const DocumentSnapshot snapshot(1, 1, page, objects);

  HWND hwnd = CreateWindowExW(0, L"STATIC", L"PikDraw swap-chain benchmark",
                             WS_POPUP, 0, 0, width, height, nullptr, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
  if (!hwnd)
    throw std::runtime_error("Could not create the hidden benchmark HWND");
  struct WindowCleanup {
    HWND hwnd;
    ~WindowCleanup() { if (hwnd) DestroyWindow(hwnd); }
  } cleanup{hwnd};

  D2DRenderBackend backend;
  if (!backend.create_swap_chain_target(hwnd, width, height))
    throw std::runtime_error("Could not create the benchmark DXGI swap chain");
  const auto target = backend.swap_chain_target();
  const Camera2D camera({500, 500}, 1, width, height);
  std::vector<uint8_t> pixel_buffer(static_cast<size_t>(width) * height * 4);
  PixelBufferTarget cpu_readback{pixel_buffer.data(), width, height, width * 4};
  RenderMetric metric;
  auto render_pixel_buffer_and_check = [&] {
    if (!backend.render(snapshot, camera, cpu_readback, &metric) || !metric.used_gpu)
      throw std::runtime_error("PixelBuffer comparison workload render failed");
  };
  auto draw_and_check = [&] {
    if (!backend.render_gpu(snapshot, camera, target, &metric) || !metric.used_gpu)
      throw std::runtime_error("DXGI swap-chain workload render failed");
  };
  auto present_and_check = [&] {
    if (!backend.present_swap_chain_target(0))
      throw std::runtime_error("DXGI swap-chain benchmark present failed");
  };
  for (int warmup = 0; warmup < 3; ++warmup) {
    render_pixel_buffer_and_check();
    draw_and_check();
    present_and_check();
  }

  std::vector<double> readback_ms;
  std::vector<double> draw_ms;
  std::vector<double> present_ms;
  readback_ms.reserve(21);
  draw_ms.reserve(21);
  present_ms.reserve(21);
  for (int sample = 0; sample < 21; ++sample) {
    auto sample_readback = [&] {
      const auto start = std::chrono::steady_clock::now();
      render_pixel_buffer_and_check();
      readback_ms.push_back(std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - start).count());
    };
    auto sample_swap_chain = [&] {
      auto start = std::chrono::steady_clock::now();
      draw_and_check();
      draw_ms.push_back(std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - start).count());
      start = std::chrono::steady_clock::now();
      present_and_check();
      present_ms.push_back(std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - start).count());
    };
    if (sample % 2 == 0) {
      sample_readback();
      sample_swap_chain();
    } else {
      sample_swap_chain();
      sample_readback();
    }
  }
  if (metric.total_objects != 10000 || metric.culled_objects == 0)
    throw std::runtime_error("DXGI benchmark did not exercise the 10k-object culler");

  auto summarize = [](std::vector<double>& values) {
    std::sort(values.begin(), values.end());
    const size_t p95 = static_cast<size_t>(std::ceil(values.size() * 0.95)) - 1;
    return std::array<double, 3>{values[values.size() / 2], values[p95],
                                 values.back()};
  };
  const auto readback = summarize(readback_ms);
  const auto draw = summarize(draw_ms);
  const auto present = summarize(present_ms);
  std::cout << "[SWAPCHAIN-BENCHMARK] paired 10k objects, cached viewport, hidden HWND, "
            << (backend.is_hardware_accelerated() ? "hardware" : "WARP")
            << "; PixelBuffer+readback p50/p95/max=" << readback[0] << "/"
            << readback[1] << "/" << readback[2]
            << " ms; swap-chain render_gpu p50/p95/max=" << draw[0] << "/" << draw[1]
            << "/" << draw[2] << " ms; Present(0) p50/p95/max="
            << present[0] << "/" << present[1] << "/" << present[2]
            << " ms; no visible FPS measured.\n" << std::flush;
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

void test_d2d_radial_geometry_and_midpoint_match_sampler() {
  Document doc("d2d-radial-gradient-attributes");
  PathObject path;
  path.id = generate_object_id();
  path.path = geometry::Path2D::make_rect(20, 20, 60, 60);
  path.transform = geometry::Transform2D::translation(3, -2);
  path.has_stroke = false;

  auto gradient = GradientPaint::make_default_radial(
      allocate_gradient_paint_id(), {20, 20, 60, 60},
      color::RGBColor{0.0, 0.0, 1.0, 1.0},
      color::RGBColor{1.0, 0.0, 0.0, 1.0});
  gradient.stops = {
      GradientStop{allocate_gradient_stop_id(), 0.0,
                   color::RGBColor{0.0, 0.0, 1.0, 1.0}, 1.0, 0.2},
      GradientStop{allocate_gradient_stop_id(), 0.4,
                   color::RGBColor{1.0, 1.0, 0.0, 1.0}, 1.0, 0.8},
      GradientStop{allocate_gradient_stop_id(), 1.0,
                   color::RGBColor{1.0, 0.0, 0.0, 1.0}, 1.0, 0.5}};
  const double angle = 20.0 * 3.14159265358979323846 / 180.0;
  gradient.gradient_to_object = geometry::Transform2D{
      30.0 * std::cos(angle), 30.0 * std::sin(angle),
      -20.0 * std::sin(angle), 20.0 * std::cos(angle), 50.0, 50.0};
  path.fill_gradient = std::make_shared<const GradientPaint>(std::move(gradient));
  assert(doc.execute_command(std::make_unique<AddObjectCommand>(path)).success);

  constexpr int32_t width = 100;
  constexpr int32_t height = 100;
  std::vector<uint8_t> direct2d(width * height * 4, 0);
  std::vector<uint8_t> software(width * height * 4, 0);
  PixelBufferTarget d2d_target{direct2d.data(), width, height, width * 4};
  PixelBufferTarget software_target{software.data(), width, height, width * 4};
  const Camera2D camera({50, 50}, 1.0, width, height);
  D2DRenderBackend backend;
  SoftwareRasterizer oracle;
  RenderMetric metric;
  assert(backend.render(*doc.current_snapshot(), camera, d2d_target, &metric));
  assert(metric.used_gpu);
  assert(oracle.render(*doc.current_snapshot(), camera, software_target));

  // The translated Path's radial center must remain the blue center stop.
  const size_t center = (52 * width + 53) * 4;
  assert(direct2d[center + 2] > 200);
  assert(direct2d[center] < 60);

  // Interior pixels must agree with the canonical sampler, including both
  // non-default midpoint intervals and the Path transform.
  const auto inverse_object = path.transform.inverted();
  const auto inverse_gradient = path.fill_gradient->gradient_to_object.inverted();
  assert(inverse_object && inverse_gradient);
  for (int y = 27; y < 75; y += 3) {
    for (int x = 29; x < 82; x += 3) {
      const size_t pixel = (static_cast<size_t>(y) * width + x) * 4;
      if (software[pixel + 3] < 250 || direct2d[pixel + 3] < 250)
        continue;
      const geometry::Point2D world{static_cast<double>(x) + 0.5,
                                    99.5 - static_cast<double>(y)};
      const auto local = inverse_object->map_point(world);
      const auto q = inverse_gradient->map_point(local);
      if (std::hypot(q.x, q.y) > 0.94)
        continue;
      for (size_t channel = 0; channel < 3; ++channel) {
        assert(std::abs(static_cast<int>(direct2d[pixel + channel]) -
                        static_cast<int>(software[pixel + channel])) <= 6);
      }
    }
  }
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
  const size_t top_pixel = (35 * width + 72) * 4;
  const size_t bottom_pixel = (60 * width + 72) * 4;
  assert(pixels[top_pixel] == 255 && pixels[top_pixel + 1] == 0 &&
         pixels[top_pixel + 2] == 0 && pixels[top_pixel + 3] == 255);
  assert(pixels[bottom_pixel] == 0 && pixels[bottom_pixel + 1] == 0 &&
         pixels[bottom_pixel + 2] == 255 && pixels[bottom_pixel + 3] == 255);
  const size_t old_position_pixel = (40 * width + 32) * 4;
  assert(pixels[old_position_pixel] == 255 &&
         pixels[old_position_pixel + 1] == 255 &&
         pixels[old_position_pixel + 2] == 255);

  // Independent RGBA chart covers channel order, row orientation, transparent
  // hidden RGB and low alpha. sRGB pixels are not transformed a second time.
  Document chart("raster-alpha-chart");
  image.id = generate_object_id();
  image.width_px = 4; image.height_px = 2;
  image.bounds_rect = {0, 0, 4, 2};
  image.transform = geometry::Transform2D::identity();
  image.shared_pixels_rgba = std::make_shared<const std::vector<uint32_t>>(
      std::vector<uint32_t>{0xdca22680, 0x01020301, 0xabcdef00, 0xff0000ff,
                             0x00ff00ff, 0x0000ffff, 0x7654327f, 0x010203fe});
  image.source.preview_pixel_color_basis = RasterPixelColorBasis::ConvertedToSRgb;
  assert(chart.execute_command(std::make_unique<AddObjectCommand>(image)).success);
  std::vector<uint8_t> chart_pixels(4 * 2 * 4);
  PixelBufferTarget chart_target{chart_pixels.data(), 4, 2, 16};
  SoftwareRasterizer cpu;
  for (IRenderBackend* renderer : {static_cast<IRenderBackend*>(&backend),
                                    static_cast<IRenderBackend*>(&cpu)}) {
    assert(renderer->render(*chart.current_snapshot(), Camera2D({2, 1}, 1, 4, 2), chart_target));
    for (size_t index = 0; index < 8; ++index) {
      const auto rgba = image.rgba_pixels()[index];
      const auto alpha = rgba & 255;
      for (unsigned channel = 0; channel < 3; ++channel) {
        const auto value = (rgba >> (24 - channel * 8)) & 255;
        const int expected = (value * alpha + 127) / 255 + 255 - alpha;
        assert(std::abs(int(chart_pixels[index * 4 + channel]) - expected) <= 1);
      }
      assert(chart_pixels[index * 4 + 3] == 255);
    }
  }
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
  // Keep this as an offscreen 60 Hz regression ceiling. Hardware acceleration
  // does not imply a 120 Hz display; visible-present acceptance is measured separately.
  constexpr double frame_budget_ms = 16.7;
  std::cout << "[BENCHMARK] 10,000-object offscreen render+readback median (9 samples): "
            << frame_time_ms << " ms; equivalent rate "
            << (1000.0 / std::max(frame_time_ms, 0.001))
            << " frames/s (not display FPS), culled: "
            << metric.culled_objects << "/" << metric.total_objects
            << ", budget: " << frame_budget_ms << " ms\n" << std::flush;
#ifdef PIKDRAW_RELEASE_BENCHMARK
  if (frame_time_ms > frame_budget_ms)
    throw std::runtime_error("Release 10k-object frame exceeded the measured backend budget");
#else
  std::cout << "[NOT_MEASURED] Debug timing is diagnostic only; Release enforces the frame budget.\n";
#endif
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
  assert(revised && revised.Get() == even_odd.Get());
  cache.evict_stale(3);
  assert(cache.get_or_create_path(device.factory(), id, 3, path).Get() == revised.Get());
  auto changed_path = geometry::Path2D::make_rect(0, 0, 30, 20);
  changed_path.set_winding_rule(geometry::WindingRule::EvenOdd);
  const auto changed = cache.get_or_create_path(device.factory(), id, 3, changed_path);
  assert(changed && changed.Get() != revised.Get());

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

  // A paint may carry a print profile different from the page preview profile.
  // The production renderers must honor that binding instead of using the
  // approximate channel fallback when the page has no configured CMYK profile.
  const auto swop_resource = color::ProfileResource::load_file(swop, color::ColorSpace::Cmyk);
  assert(swop_resource);
  auto bound_source = color::ColorValue::make_cmyk(0, 1, 1, 0, 1);
  assert(bound_source.set_profile_resource(swop_resource.resource));
  const auto page_preview = color::IccPreview::create({}, {}, &error);
  assert(page_preview);
  const auto expected = preview->convert(bound_source);
  const auto approximate_bound = color::to_preview_rgba(bound_source);
  assert(std::abs(expected.r - approximate_bound.r) +
         std::abs(expected.g - approximate_bound.g) +
         std::abs(expected.b - approximate_bound.b) > .02);
  backend.set_color_preview(page_preview);
  oracle.set_color_preview(page_preview);
  PathObject bound_path;
  bound_path.id = generate_object_id();
  bound_path.path = geometry::Path2D::make_rect(20, 20, 60, 60);
  bound_path.fill = bound_source;
  bound_path.has_stroke = false;
  Document bound_doc("d2d-bound-icc-paint");
  assert(bound_doc.execute_command(std::make_unique<AddObjectCommand>(bound_path)).success);
  RenderMetric bound_metric;
  assert(backend.render(*bound_doc.current_snapshot(), camera, target, &bound_metric));
  assert(bound_metric.used_gpu);
  assert(oracle.render(*bound_doc.current_snapshot(), camera, reference));
  const size_t bound_sample = (50 * 100 + 50) * 4;
  for (size_t channel = 0; channel < 4; ++channel)
    assert(std::abs(int(gpu[bound_sample + channel]) - int(cpu[bound_sample + channel])) <= 1);
  assert(std::abs(int(gpu[bound_sample]) - int(std::lround(expected.r * 255))) <= 2);
  assert(std::abs(int(gpu[bound_sample + 1]) - int(std::lround(expected.g * 255))) <= 2);
  assert(std::abs(int(gpu[bound_sample + 2]) - int(std::lround(expected.b * 255))) <= 2);
  assert(page_preview->conversion_failure_count() == 0);
}

void test_d2d_contour_uses_typed_source_paint() {
  Document doc("d2d-contour-source-paint");
  PathObject source;
  source.id = generate_object_id();
  source.path = geometry::Path2D::make_rect(30, 30, 40, 40);
  source.has_fill = false;
  source.has_stroke = false;
  assert(doc.execute_command(std::make_unique<AddObjectCommand>(source)).success);

  document::ContourFeature feature;
  feature.spacing_mm = 5;
  feature.has_outline = false;
  feature.outline_color = color::SpotColor{
      "UnusedContourOutline", 1.0, {1.0, 0.0, 1.0, 1.0}, false};
  feature.has_fill = true;
  feature.fill_color = color::SpotColor{
      "ContourGreen", 1.0, {0.0, 1.0, 0.0, 1.0}, false};
  assert(doc.execute_command(
      std::make_unique<document::ApplyContourCommand>(source.id, feature)).success);

  D2DRenderBackend backend;
  Camera2D camera({50, 50}, 1, 100, 100);
  std::vector<uint8_t> pixels(100 * 100 * 4);
  PixelBufferTarget target{pixels.data(), 100, 100, 400};
  RenderMetric metric;
  assert(backend.render(*doc.current_snapshot(), camera, target, &metric));
  assert(metric.used_gpu);
  const size_t sample = (50 * 100 + 25) * 4;
  assert(pixels[sample] < 10 && pixels[sample + 1] > 240 && pixels[sample + 2] < 10);
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
  {
    using namespace pikdraw::document::resources;
    constexpr uint64_t GiB = 1024ull * 1024 * 1024;
    SystemMemoryStatus status{8 * GiB, 6 * GiB, 8 * GiB, 8 * GiB};
    auto policy = std::make_shared<MemoryBudgetPolicy>([&] { return status; });
    d2d::D2DImageCache adaptive(0, 1024, policy);
    ImageObject sample; sample.width_px = sample.height_px = 1;
    sample.pixels_rgba = {0x102030ff};
    assert(adaptive.get(device.context(), sample, 1));
    assert(adaptive.capacity_bytes() == GiB / 2 && adaptive.cached_image_count() == 1);
    assert(policy->reserved_bytes() == 8); // bitmap + legacy pixel copy
    status.available_physical_bytes = GiB / 8;
    adaptive.begin_frame();
    assert(adaptive.capacity_bytes() == 0 && adaptive.cached_image_count() == 0);
    assert(policy->reserved_bytes() == 0);
    assert(!adaptive.get(device.context(), sample, 2));
    assert(adaptive.memory_pressure_encountered());
    status = {32 * GiB, 24 * GiB, 32 * GiB, 32 * GiB};
    adaptive.begin_frame();
    assert(!adaptive.memory_pressure_encountered());
    assert(adaptive.get(device.context(), sample, 3));
    assert(adaptive.capacity_bytes() == 2 * GiB && adaptive.cached_image_count() == 1);
    adaptive.begin_frame(device.d3d_device()); // live hardware/WARP adapter budget
    assert(adaptive.capacity_bytes() <= 2 * GiB);
  }
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
  const auto uploads = cache.upload_count();
  for (uint64_t revision = 4; revision < 40; ++revision) {
    shared_b.transform = geometry::Transform2D::translation(revision, 0);
    assert(cache.get(device.context(), shared_b, revision).Get() == shared_bitmap.Get());
  }
  assert(cache.upload_count() == uploads);
  std::weak_ptr<const std::vector<uint32_t>> lifetime = shared_pixels;
  shared_a.shared_pixels_rgba.reset();
  shared_b.shared_pixels_rgba.reset();
  shared_pixels.reset();
  assert(!lifetime.expired());
  cache.clear();
  assert(lifetime.expired());

  // Byte and entry limits evict the least recently used upload without
  // rejecting placements or changing their authoritative pixels.
  d2d::D2DImageCache bounded(16, 2);
  ImageObject lru_a = image, lru_b = image, lru_c = image;
  lru_b.pixels_rgba = {0x00ff00ff};
  lru_c.pixels_rgba = {0xff0000ff};
  auto lru_first = bounded.get(device.context(), lru_a, 1); assert(lru_first);
  auto lru_second = bounded.get(device.context(), lru_b, 1); assert(lru_second);
  assert(bounded.get(device.context(), lru_a, 2).Get() == lru_first.Get());
  assert(bounded.get(device.context(), lru_c, 3));
  assert(bounded.cached_image_count() == 2 && bounded.cached_bytes() <= 16);
  assert(bounded.get(device.context(), lru_a, 4).Get() == lru_first.Get());
  assert(bounded.get(device.context(), lru_b, 4).Get() != lru_second.Get());
  lru_a.pixels_rgba[0] = 0x00112280;
  assert(bounded.get(device.context(), lru_a, 4).Get() != lru_first.Get());
  d2d::D2DImageCache transient(1, 1);
  assert(transient.get(device.context(), lru_a, 1));
  assert(transient.cached_image_count() == 0 && transient.cached_bytes() == 0);

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
void test_powerclip_edit_pixels_on_native_backend() {
    Document doc("clip-native-pixels");
    PathObject frame;frame.id=generate_object_id();frame.path=geometry::Path2D::make_rect(20,20,12,12);frame.has_stroke=false;
    PathObject content;content.id=generate_object_id();content.path=geometry::Path2D::make_rect(4,4,52,52);content.fill=color::RGBColor{1,0,0,1};content.has_stroke=false;
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(frame)).success);
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(content)).success);
    auto command=std::make_unique<ClipCommand>(ClipOperation::Place,frame.id,std::vector<ObjectId>{content.id});
    const auto clip=command->result_id();assert(doc.execute_command(std::move(command)).success);
    D2DRenderBackend backend;SoftwareRasterizer cpu;
    Camera2D camera{{32,32},1,64,64};
    std::vector<uint8_t> bytes(64*64*4),oracle(bytes.size());
    PixelBufferTarget target{bytes.data(),64,64,256},expected{oracle.data(),64,64,256};
    RenderMetric metric;
    for(bool edit:{false,true,false}) {
        RenderViewContext view{edit?clip:0,1,true,false};
        backend.set_view_context(view);cpu.set_view_context(view);
        assert(backend.render(*doc.current_snapshot(),camera,target,&metric));assert(metric.used_gpu);
        assert(cpu.render(*doc.current_snapshot(),camera,expected));
        const auto outside=(54*64+10)*4;
        assert(edit?bytes[outside]>240&&bytes[outside+1]<10:bytes[outside+1]>240);
        for(auto channel:{outside,outside+1,outside+2})assert(std::abs(int(bytes[channel])-int(oracle[channel]))<3);
    }
    std::cout << "PowerClip native backend: " << (backend.is_hardware_accelerated()?"hardware":"WARP") << "\n";
}

int main(int argc,char** argv) {
  if(argc>1&&std::string_view(argv[1])=="--powerclip-only") {test_powerclip_edit_pixels_on_native_backend();return 0;}
  if(argc>1&&std::string_view(argv[1])=="--swapchain-only") {test_d2d_swap_chain_target();return 0;}
  if(argc>1&&std::string_view(argv[1])=="--swapchain-benchmark-only") {test_d2d_swap_chain_10k_timing();return 0;}
  test_powerclip_edit_pixels_on_native_backend();
  test_document_raster_effects_dpi();
  std::cout << "[test_rasterizer] Running software rasterizer tests...\n";
  test_bgra_targets_and_image_upload_cache();
  test_software_rasterizer_frame();
  test_detail_view_renders_clipped_source_content();
  test_analytic_ellipse_renders_from_authoring_source();
  test_backend_status_state_machine();
  test_d2d_render_backend();
  test_d2d_swap_chain_target();
  test_d2d_renders_transform_gradient_and_effects();
  test_d2d_radial_geometry_and_midpoint_match_sampler();
  test_d2d_renders_images();
  test_d2d_page_and_local_geometry_cache();
  test_d2d_icc_preview_matches_software();
  test_d2d_contour_uses_typed_source_paint();
  test_d2d_zero_fallback_complex_scene_proof();
  test_d2d_10k_objects_benchmark();
  std::cout << "[test_rasterizer] All tests passed!\n";
  return 0;
}
