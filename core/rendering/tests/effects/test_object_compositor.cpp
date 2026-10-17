#include "../../src/effects/object_compositor.hpp"
#include "../../src/effects/blend_operator.hpp"
#include "../../src/raster/path_raster.hpp"
#include "pikdraw/document/commands/path_commands.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/rendering/software_rasterizer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace pikdraw;
using namespace pikdraw::document;
using namespace pikdraw::rendering;

namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

uint8_t alpha_at(const std::vector<uint8_t> &pixels, int32_t width, int32_t x,
                 int32_t y) {
  return pixels[(static_cast<size_t>(y) * static_cast<size_t>(width) +
                 static_cast<size_t>(x)) * 4 + 3];
}

PathObject overlap_path() {
  PathObject path;
  path.id = 1001;
  path.path = geometry::Path2D::make_rect(-10.0, -10.0, 20.0, 20.0);
  path.fill = color::RGBColor{1.0, 0.0, 0.0, 1.0};
  path.stroke = color::RGBColor{0.0, 0.0, 1.0, 1.0};
  path.stroke_width_mm = 4.0;
  path.has_fill = true;
  path.has_stroke = true;
  return path;
}

void object_and_component_opacity_composite_once() {
  constexpr int32_t width = 64;
  constexpr int32_t height = 64;
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, 0);
  PixelBufferTarget target{pixels.data(), width, height, width * 4};
  Camera2D camera({0, 0}, 1.0, width, height);
  const auto path = overlap_path();
  Appearance appearance;
  appearance.fill_opacity = 0.5;
  appearance.stroke_opacity = 0.5;
  appearance.object_opacity = 0.5;

  check(effects::render_path_with_appearance(path, appearance, camera, target),
        "valid opacity appearance did not render");
  check(alpha_at(pixels, width, 32, 32) == 64,
        "fill opacity and object opacity did not multiply once");
  check(alpha_at(pixels, width, 22, 32) == 96,
        "fill/stroke overlap did not composite before object opacity");
  check(std::get<color::RGBColor>(path.fill.payload()).a == 1.0 &&
            std::get<color::RGBColor>(path.stroke.payload()).a == 1.0,
        "render changed the source color alpha");

  std::fill(pixels.begin(), pixels.end(), 0);
  appearance.object_opacity = 0.0;
  check(effects::render_path_with_appearance(path, appearance, camera, target),
        "zero object opacity should remain a valid render");
  check(std::all_of(pixels.begin(), pixels.end(),
                    [](uint8_t value) { return value == 0; }),
        "zero object opacity changed the transparent backdrop");
}

void component_opacity_keeps_degenerate_stroked_paths_visible() {
  // Given a horizontal centerline with a visible stroke, when component
  // opacity isolates it for compositing, then the stroke bounds still include
  // the line and the rendered alpha is reduced exactly once.
  constexpr int32_t width = 64;
  constexpr int32_t height = 64;
  std::vector<uint8_t> baseline(static_cast<size_t>(width) * height * 4, 0);
  std::vector<uint8_t> actual(baseline.size(), 0);
  PixelBufferTarget baseline_target{baseline.data(), width, height, width * 4};
  PixelBufferTarget actual_target{actual.data(), width, height, width * 4};
  Camera2D camera({0, 0}, 1.0, width, height);
  PathObject path;
  path.id = 1006;
  path.path.move_to(-10.0, 0.0);
  path.path.line_to(10.0, 0.0);
  path.has_fill = false;
  path.has_stroke = true;
  path.stroke_width_mm = 4.0;
  path.stroke = color::RGBColor{1.0, 0.0, 0.0, 1.0};
  path.appearance.stroke_opacity = 0.5;

  check(raster::paint(path, camera, baseline_target),
        "direct horizontal stroke baseline failed");
  check(effects::render_path_with_appearance(path, path.appearance, camera,
                                             actual_target),
        "component opacity rejected a horizontal stroked path");
  const size_t pixel = (static_cast<size_t>(32) * width + 32) * 4;
  const int baseline_alpha = baseline[pixel + 3];
  const int actual_alpha = actual[pixel + 3];
  check(baseline_alpha > 0 &&
            std::abs(actual_alpha - static_cast<int>(std::lround(
                                        baseline_alpha * 0.5))) <= 1,
        "component opacity culled the centerline or changed stroke alpha more than once");
}

void multiply_blend_mode_composites_against_existing_backdrop() {
  constexpr int32_t width = 64;
  constexpr int32_t height = 64;
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
  for (size_t offset = 0; offset < pixels.size(); offset += 4) {
    pixels[offset] = 51;
    pixels[offset + 1] = 128;
    pixels[offset + 2] = 179;
    pixels[offset + 3] = 255;
  }
  PixelBufferTarget target{pixels.data(), width, height, width * 4};
  Camera2D camera({0, 0}, 1.0, width, height);
  PathObject path;
  path.id = 1002;
  path.path = geometry::Path2D::make_rect(-10.0, -10.0, 20.0, 20.0);
  path.fill = color::RGBColor{0.8, 0.4, 0.2, 1.0};
  path.has_stroke = false;
  Appearance appearance;
  appearance.blend_mode = BlendMode::Multiply;

  check(effects::render_path_with_appearance(path, appearance, camera, target),
        "supported Multiply appearance did not render");
  const auto index = (static_cast<size_t>(32) * width + 32) * 4;
  check(pixels[index] == 41 && pixels[index + 1] == 51 &&
            pixels[index + 2] == 36 && pixels[index + 3] == 255,
        "Multiply did not use encoded-sRGB source/backdrop components");
  check(pixels[0] == 51 && pixels[1] == 128 && pixels[2] == 179,
        "Multiply changed a pixel outside the isolated source bounds");
}

void blend_modes_keep_source_over_alpha_rounding_identical() {
  constexpr int32_t width = 64;
  constexpr int32_t height = 64;
  PathObject path;
  path.id = 1003;
  path.path = geometry::Path2D::make_rect(-10.0, -10.0, 20.0, 20.0);
  path.fill = color::RGBColor{0.8, 0.4, 0.2, 0.018};
  path.has_stroke = false;
  Camera2D camera({0, 0}, 1.0, width, height);
  const auto render_alpha = [&](BlendMode mode) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    for (size_t offset = 0; offset < pixels.size(); offset += 4) {
      pixels[offset] = 17;
      pixels[offset + 1] = 33;
      pixels[offset + 2] = 49;
      pixels[offset + 3] = 5;
    }
    PixelBufferTarget target{pixels.data(), width, height, width * 4};
    Appearance appearance;
    appearance.fill_opacity = 0.999;
    appearance.blend_mode = mode;
    check(effects::render_path_with_appearance(path, appearance, camera,
                                               target),
          "low-alpha source did not render for alpha parity");
    return pixels[(static_cast<size_t>(32) * width + 32) * 4 + 3];
  };

  const auto normal_alpha = render_alpha(BlendMode::Normal);
  const auto multiply_alpha = render_alpha(BlendMode::Multiply);
  check(normal_alpha == 9 && multiply_alpha == normal_alpha,
        "source-over alpha rounding depended on the color blend mode");
}

void invalid_late_float_backdrop_is_rejected_before_any_blend_write() {
  constexpr int32_t width = 64;
  constexpr int32_t height = 64;
  std::vector<float> scratch(static_cast<size_t>(width) * height * 4, 0.0f);
  const size_t invalid_pixel = (static_cast<size_t>(41) * width + 41) * 4;
  scratch[invalid_pixel + 3] = 1.25f;
  const auto before = scratch;
  PixelBufferTarget target;
  target.width = width;
  target.height = height;
  target.stride_bytes = width * 4 * static_cast<int32_t>(sizeof(float));
  target.premultiplied_rgba = scratch.data();
  Camera2D camera({0, 0}, 1.0, width, height);
  PathObject path;
  path.id = 1004;
  path.path = geometry::Path2D::make_rect(-10.0, -10.0, 20.0, 20.0);
  path.fill = color::RGBColor{1.0, 0.0, 0.0, 1.0};
  path.has_stroke = false;
  Appearance appearance;
  appearance.blend_mode = BlendMode::Multiply;

  check(!effects::render_path_with_appearance(path, appearance, camera, target),
        "invalid late float backdrop sample was accepted");
  check(scratch == before,
        "invalid float backdrop partially modified an earlier pixel");
}

void normal_opacity_rejects_invalid_late_float_backdrop_atomically() {
  constexpr int32_t width = 64;
  constexpr int32_t height = 64;
  std::vector<float> scratch(static_cast<size_t>(width) * height * 4, 0.0f);
  const size_t invalid_pixel = (static_cast<size_t>(41) * width + 41) * 4;
  scratch[invalid_pixel + 3] = 1.25f;
  const auto before = scratch;
  PixelBufferTarget target;
  target.width = width;
  target.height = height;
  target.stride_bytes = width * 4 * static_cast<int32_t>(sizeof(float));
  target.premultiplied_rgba = scratch.data();
  Camera2D camera({0, 0}, 1.0, width, height);
  PathObject path;
  path.id = 1005;
  path.path = geometry::Path2D::make_rect(-10.0, -10.0, 20.0, 20.0);
  path.fill = color::RGBColor{1.0, 0.0, 0.0, 1.0};
  path.has_stroke = false;
  path.appearance.object_opacity = 0.5;

  check(!effects::render_path_with_appearance(path, path.appearance, camera,
                                               target),
        "Normal opacity accepted an invalid float backdrop sample");
  check(scratch == before,
        "Normal opacity partially wrote before validating the full backdrop");
}

void float_surface_preserves_premultiplied_intermediates() {
  std::vector<float> scratch(4, 0.0f);
  PixelBufferTarget target;
  target.width = 1;
  target.height = 1;
  target.stride_bytes = 4 * static_cast<int32_t>(sizeof(float));
  target.premultiplied_rgba = scratch.data();
  check(target.is_valid(), "float scratch target rejected a valid row stride");
  target.blend_pixel(0, 0, 255, 0, 0, 128);
  target.blend_pixel(0, 0, 0, 0, 255, 128);
  const float alpha = 128.0f / 255.0f;
  check(std::abs(scratch[0] - alpha * (1.0f - alpha)) < 1e-6f &&
            std::abs(scratch[2] - alpha) < 1e-6f &&
            std::abs(scratch[3] - (alpha + alpha * (1.0f - alpha))) < 1e-6f,
        "float surface quantized or miscomputed source-over intermediates");
}

void invalid_graph_and_failed_source_are_atomic() {
  constexpr int32_t width = 64;
  constexpr int32_t height = 64;
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, 0);
  PixelBufferTarget target{pixels.data(), width, height, width * 4};
  Camera2D camera({0, 0}, 1.0, width, height);
  const auto path = overlap_path();
  Appearance appearance;
  appearance.object_opacity = 0.5;
  appearance.blend_mode = static_cast<BlendMode>(255);
  check(!effects::render_path_with_appearance(path, appearance, camera, target),
        "unknown blend mode was silently accepted");
  check(std::all_of(pixels.begin(), pixels.end(),
                    [](uint8_t value) { return value == 0; }),
        "unsupported blend mode partially changed the backdrop");

  appearance.blend_mode = BlendMode::Normal;
  EffectNode blur;
  blur.id = 9001;
  blur.parameters = GaussianBlurEffect{1.0, 1.0};
  blur.schema_version = document::kBasicEffectNodeSchemaVersion + 1;
  appearance.effects.push_back(blur);
  check(!effects::render_path_with_appearance(path, appearance, camera, target),
        "unknown effect schema was silently accepted");
  check(std::all_of(pixels.begin(), pixels.end(),
                    [](uint8_t value) { return value == 0; }),
        "unknown effect schema partially changed the backdrop");

  appearance.effects.clear();
  auto unsupported_path = path;
  unsupported_path.path = {};
  unsupported_path.path.move_to({-8, -8});
  unsupported_path.path.arc_to(5, 5, 0, false, true, 8, 8);
  check(!effects::render_path_with_appearance(unsupported_path, appearance,
                                               camera, target),
        "raster failure did not reject the isolated source");
  check(std::all_of(pixels.begin(), pixels.end(),
                    [](uint8_t value) { return value == 0; }),
        "failed source raster partially changed the backdrop");
}

void gaussian_blur_renders_a_finite_halo_outside_source_geometry() {
  // Given an opaque square and a Gaussian Blur node, when composited, then
  // the source remains visible and the finite halo reaches outside its path.
  constexpr int32_t width = 64;
  constexpr int32_t height = 64;
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, 0);
  PixelBufferTarget target{pixels.data(), width, height, width * 4};
  Camera2D camera({0, 0}, 1.0, width, height);
  PathObject path;
  path.id = 1010;
  path.path = geometry::Path2D::make_rect(-4.0, -4.0, 8.0, 8.0);
  path.fill = color::RGBColor{0.2, 0.5, 0.9, 1.0};
  path.has_stroke = false;
  EffectNode blur;
  blur.id = 9100;
  blur.parameters = GaussianBlurEffect{2.0, 2.0};
  path.appearance.effects.push_back(blur);

  size_t peak_gaussian_scratch_capacity_bytes = 0;
  check(effects::render_path_with_appearance(path, path.appearance, camera,
                                             target, nullptr,
                                             &peak_gaussian_scratch_capacity_bytes),
        "Gaussian effect graph did not render");
  check(peak_gaussian_scratch_capacity_bytes >
            static_cast<size_t>(width) * height * sizeof(float) &&
            peak_gaussian_scratch_capacity_bytes < 1024u * 1024u,
        "Gaussian compositor did not report bounded live buffer capacity");
  const auto center_alpha = alpha_at(pixels, width, 32, 32);
  const auto halo_alpha = alpha_at(pixels, width, 39, 32);
  check(center_alpha > halo_alpha && halo_alpha > 0,
        "Gaussian output did not retain source and emit an outside halo");
}

void software_rasterizer_reports_gaussian_scratch_capacity() {
  Document document("gaussian-scratch-metric-acceptance");
  PathObject path;
  path.id = generate_object_id();
  path.path = geometry::Path2D::make_rect(20.0, 20.0, 18.0, 12.0);
  path.fill = color::RGBColor{0.2, 0.5, 0.9, 1.0};
  path.has_stroke = false;
  EffectNode blur;
  blur.id = generate_effect_instance_id();
  blur.parameters = GaussianBlurEffect{1.5, 1.5};
  path.appearance.effects.push_back(blur);
  check(document.execute_command(std::make_unique<AddObjectCommand>(path)).success,
        "add Gaussian scratch metric path");

  constexpr int32_t width = 96;
  constexpr int32_t height = 80;
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, 0);
  PixelBufferTarget target{pixels.data(), width, height, width * 4};
  Camera2D camera({48.0, 40.0}, 1.5, width, height);
  SoftwareRasterizer renderer;
  RenderMetric metric;
  check(renderer.render(*document.current_snapshot(), camera, target, &metric),
        "software renderer rejected Gaussian scratch metric fixture");
  check(metric.rendered_objects == 1 &&
            metric.peak_gaussian_blur_scratch_capacity_bytes > 0,
        "software RenderMetric lost Gaussian scratch telemetry");
}

void software_rasterizer_reports_drop_shadow_scratch_capacity_separately() {
  Document document("drop-shadow-scratch-metric-acceptance");
  PathObject path;
  path.id = generate_object_id();
  path.path = geometry::Path2D::make_rect(20.0, 20.0, 18.0, 12.0);
  path.fill = color::RGBColor{0.2, 0.5, 0.9, 1.0};
  path.has_stroke = false;
  EffectNode shadow;
  shadow.id = generate_effect_instance_id();
  shadow.parameters = DropShadowEffect{
      2.0, 1.0, 1.5, 1.0, color::RGBColor{0.0, 0.0, 0.0, 1.0}, 0.75};
  path.appearance.effects.push_back(shadow);
  check(document.execute_command(std::make_unique<AddObjectCommand>(path)).success,
        "add Drop Shadow scratch metric path");

  constexpr int32_t width = 96;
  constexpr int32_t height = 80;
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, 0);
  PixelBufferTarget target{pixels.data(), width, height, width * 4};
  Camera2D camera({48.0, 40.0}, 1.5, width, height);
  SoftwareRasterizer renderer;
  RenderMetric metric;
  check(renderer.render(*document.current_snapshot(), camera, target, &metric),
        "software renderer rejected Drop Shadow scratch metric fixture");
  check(metric.rendered_objects == 1 &&
            metric.peak_drop_shadow_scratch_capacity_bytes > 0 &&
            metric.peak_gaussian_blur_scratch_capacity_bytes == 0,
        "SoftwareRasterizer did not keep Drop Shadow telemetry separate");
}

void drop_shadow_uses_local_axes_and_object_opacity_once() {
  // Given a half-alpha source transformed by 90 degrees and a local +X shadow,
  // when object opacity is applied, then the shadow moves along world +Y and
  // has alpha source(0.5)*shadow(0.5)*object(0.5)=0.125 outside the source.
  constexpr int32_t width = 96;
  constexpr int32_t height = 96;
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, 0);
  PixelBufferTarget target{pixels.data(), width, height, width * 4};
  Camera2D camera({0, 0}, 1.0, width, height);
  PathObject path;
  path.id = 1011;
  path.path = geometry::Path2D::make_rect(-4.0, -4.0, 8.0, 8.0);
  path.transform = geometry::Transform2D::translation(12.0, 0.0) *
                   geometry::Transform2D::rotation_degrees(90.0);
  path.fill = color::RGBColor{0.8, 0.3, 0.1, 0.5};
  path.has_stroke = false;
  path.appearance.object_opacity = 0.5;
  EffectNode shadow;
  shadow.id = 9101;
  shadow.parameters = DropShadowEffect{10.0, 0.0, 0.0, 0.0,
                                       color::RGBColor{0.0, 0.0, 0.0, 1.0},
                                       0.5};
  path.appearance.effects.push_back(shadow);

  check(effects::render_path_with_appearance(path, path.appearance, camera,
                                             target),
        "Drop Shadow effect graph did not render");
  check(alpha_at(pixels, width, 60, 48) == 64,
        "object opacity was not applied once to the source");
  const auto shadow_alpha = alpha_at(pixels, width, 60, 37);
  check(shadow_alpha >= 31 && shadow_alpha <= 33,
        "Drop Shadow alpha or local +Y-up transformed offset is incorrect");
  check(alpha_at(pixels, width, 70, 48) == 0,
        "local +X shadow was incorrectly mapped to world +X under rotation");
}

void ordered_stack_changes_output_and_disabled_nodes_are_identity() {
  // Given blur and shadow nodes, when their order changes, then the resulting
  // pixels differ. A disabled node remains an identity in the same graph.
  constexpr int32_t width = 64;
  constexpr int32_t height = 64;
  Camera2D camera({0, 0}, 1.0, width, height);
  const auto render = [&](std::vector<EffectNode> nodes) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, 0);
    PixelBufferTarget target{pixels.data(), width, height, width * 4};
    PathObject path;
    path.id = 1012;
    path.path = geometry::Path2D::make_rect(-4.0, -4.0, 8.0, 8.0);
    path.fill = color::RGBColor{0.9, 0.2, 0.1, 1.0};
    path.has_stroke = false;
    path.appearance.effects = std::move(nodes);
    check(effects::render_path_with_appearance(path, path.appearance, camera,
                                               target),
          "ordered effect graph did not render");
    return pixels;
  };

  EffectNode blur;
  blur.id = 9102;
  blur.parameters = GaussianBlurEffect{2.0, 2.0};
  EffectNode shadow;
  shadow.id = 9103;
  shadow.parameters = DropShadowEffect{10.0, -3.0, 1.0, 1.0,
                                       color::RGBColor{0.0, 0.0, 0.0, 1.0},
                                       0.7};
  const auto blur_then_shadow = render({blur, shadow});
  const auto shadow_then_blur = render({shadow, blur});
  check(blur_then_shadow != shadow_then_blur,
        "effect stack order did not affect the rendered output");

  auto disabled_blur = blur;
  disabled_blur.enabled = false;
  const auto identity = render({disabled_blur});
  std::vector<uint8_t> direct(static_cast<size_t>(width) * height * 4, 0);
  PixelBufferTarget direct_target{direct.data(), width, height, width * 4};
  PathObject path;
  path.id = 1013;
  path.path = geometry::Path2D::make_rect(-4.0, -4.0, 8.0, 8.0);
  path.fill = color::RGBColor{0.9, 0.2, 0.1, 1.0};
  path.has_stroke = false;
  check(raster::paint(path, camera, direct_target),
        "disabled effect direct baseline failed");
  check(identity == direct,
        "disabled effect node changed pixels or bypassed the default path");
}

void invalid_effect_backdrop_is_rejected_before_any_write() {
  // Given an invalid later float-backdrop sample under a blur, when effect
  // rendering preflights the final output, then the entire target stays intact.
  constexpr int32_t width = 64;
  constexpr int32_t height = 64;
  std::vector<float> pixels(static_cast<size_t>(width) * height * 4, 0.0f);
  const size_t invalid = (static_cast<size_t>(41) * width + 41) * 4;
  pixels[invalid + 3] = 1.25f;
  const auto before = pixels;
  PixelBufferTarget target;
  target.width = width;
  target.height = height;
  target.stride_bytes = width * 4 * static_cast<int32_t>(sizeof(float));
  target.premultiplied_rgba = pixels.data();
  Camera2D camera({0, 0}, 1.0, width, height);
  PathObject path;
  path.id = 1014;
  path.path = geometry::Path2D::make_rect(-4.0, -4.0, 8.0, 8.0);
  path.fill = color::RGBColor{1.0, 0.0, 0.0, 1.0};
  path.has_stroke = false;
  EffectNode blur;
  blur.id = 9104;
  blur.parameters = GaussianBlurEffect{2.0, 2.0};
  path.appearance.effects.push_back(blur);
  path.appearance.blend_mode = BlendMode::Multiply;

  check(!effects::render_path_with_appearance(path, path.appearance, camera,
                                              target),
        "invalid float backdrop was accepted by the effect compositor");
  check(pixels == before,
        "invalid float backdrop partially changed the target");
}

void cached_effect_output_matches_multiply_compositing() {
  // Given the same blurred source and premultiplied backdrop, when the effect
  // output is staged for Multiply, then every cached pixel matches the blend
  // operator result and transparent samples leave the backdrop unchanged.
  constexpr int32_t width = 64;
  constexpr int32_t height = 64;
  constexpr size_t pixel_count = static_cast<size_t>(width) * height;
  constexpr effects::PremultipliedSrgbRgba backdrop{0.2f, 0.3f, 0.4f,
                                                    0.75f};
  std::vector<float> source_pixels(pixel_count * 4, 0.0f);
  std::vector<float> actual_pixels(pixel_count * 4, 0.0f);
  for (size_t index = 0; index < pixel_count; ++index) {
    actual_pixels[index * 4] = backdrop.r;
    actual_pixels[index * 4 + 1] = backdrop.g;
    actual_pixels[index * 4 + 2] = backdrop.b;
    actual_pixels[index * 4 + 3] = backdrop.a;
  }

  PixelBufferTarget source_target;
  source_target.width = width;
  source_target.height = height;
  source_target.stride_bytes = width * 4 * static_cast<int32_t>(sizeof(float));
  source_target.premultiplied_rgba = source_pixels.data();
  PixelBufferTarget actual_target = source_target;
  actual_target.premultiplied_rgba = actual_pixels.data();
  Camera2D camera({0, 0}, 1.0, width, height);
  PathObject path;
  path.id = 1016;
  path.path = geometry::Path2D::make_rect(-8.0, -8.0, 16.0, 16.0);
  path.fill = color::RGBColor{0.85, 0.25, 0.1, 0.8};
  path.has_stroke = false;
  EffectNode blur;
  blur.id = 9107;
  blur.parameters = GaussianBlurEffect{2.0, 2.0};
  Appearance appearance;
  appearance.effects.push_back(blur);

  check(effects::render_path_with_appearance(path, appearance, camera,
                                             source_target),
        "blurred source reference did not render");
  appearance.blend_mode = BlendMode::Multiply;
  check(effects::render_path_with_appearance(path, appearance, camera,
                                             actual_target),
        "cached Multiply effect output did not render");

  for (size_t index = 0; index < pixel_count; ++index) {
    const effects::PremultipliedSrgbRgba source{
        source_pixels[index * 4], source_pixels[index * 4 + 1],
        source_pixels[index * 4 + 2], source_pixels[index * 4 + 3]};
    effects::PremultipliedSrgbRgba expected = backdrop;
    if (source.a > 0.0f) {
      const auto composed =
          effects::blend_source_over(BlendMode::Multiply, backdrop, source);
      check(composed.has_value(), "reference Multiply pixel was rejected");
      expected = *composed;
    }
    check(std::abs(actual_pixels[index * 4] - expected.r) < 1e-6f &&
              std::abs(actual_pixels[index * 4 + 1] - expected.g) < 1e-6f &&
              std::abs(actual_pixels[index * 4 + 2] - expected.b) < 1e-6f &&
              std::abs(actual_pixels[index * 4 + 3] - expected.a) < 1e-6f,
          "cached effect output diverged from Multiply compositing");
  }
}

void drop_shadow_blend_uses_the_painter_order_backdrop() {
  // Given a solid backdrop and a separated shadow, each supported shadow
  // mode blends with the page pixels under the shadow while the source stays
  // in front. Object opacity mixes the complete result with the original page.
  constexpr int32_t width = 64;
  constexpr int32_t height = 64;
  constexpr BlendMode modes[]{BlendMode::Normal, BlendMode::Multiply,
                              BlendMode::Screen, BlendMode::Overlay,
                              BlendMode::Darken, BlendMode::Lighten};
  constexpr int expected_shadow_rgb[][3] = {
      {128, 128, 128}, {26, 51, 102}, {153, 179, 230},
      {51, 102, 204}, {51, 102, 128}, {128, 128, 204}};
  Camera2D camera({0, 0}, 1.0, width, height);
  PathObject path;
  path.id = 1017;
  path.path = geometry::Path2D::make_rect(-4.0, -4.0, 8.0, 8.0);
  path.fill = color::RGBColor{1.0, 0.0, 0.0, 1.0};
  path.has_stroke = false;
  EffectNode node;
  node.id = 9108;
  node.schema_minor = kDropShadowBlendSchemaMinor;
  DropShadowEffect shadow{12.0, 0.0, 0.0, 0.0,
                          color::RGBColor{0.5, 0.5, 0.5, 1.0}, 1.0};
  path.appearance.effects.push_back(node);

  const size_t shadow_pixel = (static_cast<size_t>(32) * width + 44) * 4;
  const size_t source_pixel = (static_cast<size_t>(32) * width + 32) * 4;
  for (size_t mode_index = 0; mode_index < std::size(modes); ++mode_index) {
    const auto mode = modes[mode_index];
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    for (size_t offset = 0; offset < pixels.size(); offset += 4) {
      pixels[offset] = 51;
      pixels[offset + 1] = 102;
      pixels[offset + 2] = 204;
      pixels[offset + 3] = 255;
    }
    auto current = shadow;
    current.blend_mode = mode;
    path.appearance.effects.front().parameters = current;
    PixelBufferTarget target{pixels.data(), width, height, width * 4};
    check(effects::render_path_with_appearance(path, path.appearance, camera,
                                               target),
          "supported Drop Shadow blend mode did not render");
    check(std::abs(static_cast<int>(pixels[shadow_pixel]) - expected_shadow_rgb[mode_index][0]) <= 1 &&
              std::abs(static_cast<int>(pixels[shadow_pixel + 1]) - expected_shadow_rgb[mode_index][1]) <= 1 &&
              std::abs(static_cast<int>(pixels[shadow_pixel + 2]) - expected_shadow_rgb[mode_index][2]) <= 1 &&
              pixels[shadow_pixel + 3] == 255,
          "Drop Shadow mode did not blend with the artwork underneath it");
    check(pixels[source_pixel] == 255 && pixels[source_pixel + 1] == 0 &&
              pixels[source_pixel + 2] == 0 && pixels[source_pixel + 3] == 255,
          "shadow blend changed the source object painted above its shadow");
  }

  std::vector<uint8_t> half_opacity_pixels(
      static_cast<size_t>(width) * height * 4);
  for (size_t offset = 0; offset < half_opacity_pixels.size(); offset += 4) {
    half_opacity_pixels[offset] = 51;
    half_opacity_pixels[offset + 1] = 102;
    half_opacity_pixels[offset + 2] = 204;
    half_opacity_pixels[offset + 3] = 255;
  }
  auto multiply_shadow = shadow;
  multiply_shadow.blend_mode = BlendMode::Multiply;
  path.appearance.object_opacity = 0.5;
  path.appearance.effects.front().parameters = multiply_shadow;
  PixelBufferTarget half_target{half_opacity_pixels.data(), width, height,
                                width * 4};
  check(effects::render_path_with_appearance(path, path.appearance, camera,
                                             half_target),
        "shadow blend with object opacity did not render");
  check(std::abs(static_cast<int>(half_opacity_pixels[shadow_pixel]) - 38) <= 1 &&
            std::abs(static_cast<int>(half_opacity_pixels[shadow_pixel + 1]) - 77) <= 1 &&
            std::abs(static_cast<int>(half_opacity_pixels[shadow_pixel + 2]) - 153) <= 1,
        "object opacity was not applied to the complete shadow/source group");

  auto invalid_stack = path;
  invalid_stack.appearance.object_opacity = 1.0;
  EffectNode later_blur;
  later_blur.id = 9109;
  later_blur.parameters = GaussianBlurEffect{0.5, 0.5};
  invalid_stack.appearance.effects.push_back(later_blur);
  auto unchanged_pixels = half_opacity_pixels;
  PixelBufferTarget invalid_target{unchanged_pixels.data(), width, height,
                                   width * 4};
  check(!effects::render_path_with_appearance(
            invalid_stack, invalid_stack.appearance, camera, invalid_target) &&
            unchanged_pixels == half_opacity_pixels,
        "non-Normal Shadow accepted a later enabled node or partially wrote the target");
  invalid_stack.appearance.effects.back().enabled = false;
  check(effects::render_path_with_appearance(
            invalid_stack, invalid_stack.appearance, camera, invalid_target),
        "disabled nodes after non-Normal Shadow should not invalidate the stack");
}

void translucent_shadow_composites_in_linear_srgb() {
  // Given a half-alpha blue source and a half-opacity red shadow at zero
  // offset, when placed beneath the source in linear sRGB, then the mixed
  // unassociated output is linear (0.2, 0, 0.8) before encoding.
  constexpr int32_t width = 64;
  constexpr int32_t height = 64;
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, 0);
  PixelBufferTarget target{pixels.data(), width, height, width * 4};
  Camera2D camera({0, 0}, 1.0, width, height);
  PathObject path;
  path.id = 1015;
  path.path = geometry::Path2D::make_rect(-4.0, -4.0, 8.0, 8.0);
  path.fill = color::RGBColor{0.0, 0.0, 1.0, 0.5};
  path.has_stroke = false;
  EffectNode shadow;
  shadow.id = 9106;
  shadow.parameters = DropShadowEffect{0.0, 0.0, 0.0, 0.0,
                                       color::RGBColor{1.0, 0.0, 0.0, 1.0},
                                       0.5};
  path.appearance.effects.push_back(shadow);

  check(effects::render_path_with_appearance(path, path.appearance, camera,
                                             target),
        "translucent colored shadow did not render");
  const size_t pixel = (static_cast<size_t>(32) * width + 32) * 4;
  check(pixels[pixel] >= 123 && pixels[pixel] <= 125 &&
            pixels[pixel + 1] == 0 && pixels[pixel + 2] >= 230 &&
            pixels[pixel + 2] <= 232 && pixels[pixel + 3] == 159,
        "shadow RGB or alpha did not match the independent linear-sRGB oracle");
}

void software_rasterizer_keeps_offscreen_source_when_shadow_reaches_viewport() {
  // Given source geometry beyond the viewport with a shadow reaching inside,
  // when the software renderer culls and paints the document, then the shadow
  // remains visible and the object is reported as rendered.
  Document document("effects-culling-render-acceptance");
  PathObject path;
  path.id = generate_object_id();
  path.path = geometry::Path2D::make_rect(-47.0, -2.0, 5.0, 4.0);
  path.fill = color::RGBColor{0.8, 0.2, 0.1, 0.5};
  path.has_stroke = false;
  EffectNode shadow;
  shadow.id = 9105;
  shadow.parameters = DropShadowEffect{25.0, 0.0, 1.0, 1.0,
                                       color::RGBColor{0.0, 0.0, 0.0, 1.0},
                                       0.8};
  path.appearance.effects.push_back(shadow);
  check(document.execute_command(std::make_unique<AddObjectCommand>(path)).success,
        "add offscreen effect path");

  constexpr int32_t width = 64;
  constexpr int32_t height = 64;
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, 0);
  PixelBufferTarget target{pixels.data(), width, height, width * 4};
  Camera2D camera({0, 0}, 1.0, width, height);
  SoftwareRasterizer renderer;
  RenderMetric metric;
  check(renderer.render(*document.current_snapshot(), camera, target, &metric),
        "software renderer rejected an offscreen source with visible shadow");
  check(alpha_at(pixels, width, 12, 31) > 0 && metric.rendered_objects == 1,
        "culling removed the effect whose output reaches the viewport");
}

void default_appearance_keeps_the_existing_fast_path_pixels() {
  constexpr int32_t width = 64;
  constexpr int32_t height = 64;
  std::vector<uint8_t> expected(static_cast<size_t>(width) * height * 4, 0);
  std::vector<uint8_t> actual(expected.size(), 0);
  PixelBufferTarget expected_target{expected.data(), width, height, width * 4};
  PixelBufferTarget actual_target{actual.data(), width, height, width * 4};
  Camera2D camera({0, 0}, 1.0, width, height);
  const auto path = overlap_path();
  check(raster::paint(path, camera, expected_target),
        "baseline raster path failed");
  check(effects::render_path_with_appearance(path, path.appearance, camera,
                                             actual_target),
        "default appearance path failed");
  check(actual == expected,
        "default appearance changed the existing direct-render fast path");
}

void software_rasterizer_uses_path_appearance_over_page_backdrop() {
  Document document("opacity-renderer-acceptance");
  PathObject path;
  path.id = generate_object_id();
  path.path = geometry::Path2D::make_rect(10.0, 10.0, 10.0, 10.0);
  path.fill = color::RGBColor{1.0, 0.0, 0.0, 1.0};
  path.has_stroke = false;
  path.appearance.object_opacity = 0.5;
  check(document.execute_command(std::make_unique<AddObjectCommand>(path)).success,
        "add renderer opacity path");

  constexpr int32_t width = 64;
  constexpr int32_t height = 64;
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, 0);
  PixelBufferTarget target{pixels.data(), width, height, width * 4};
  Camera2D camera({32.0, 32.0}, 1.0, width, height);
  SoftwareRasterizer renderer;
  RenderMetric metric;
  check(renderer.render(*document.current_snapshot(), camera, target, &metric),
        "software renderer rejected a supported opacity path");
  const auto index = (static_cast<size_t>(49) * width + 15) * 4;
  check(pixels[index] == 255 && pixels[index + 1] >= 127 &&
            pixels[index + 1] <= 128 && pixels[index + 2] >= 127 &&
            pixels[index + 2] <= 128 && pixels[index + 3] == 255 &&
            metric.rendered_objects == 1,
        "software renderer did not apply object opacity over page pixels");
}
} // namespace

int main() {
  try {
    object_and_component_opacity_composite_once();
    component_opacity_keeps_degenerate_stroked_paths_visible();
    multiply_blend_mode_composites_against_existing_backdrop();
    blend_modes_keep_source_over_alpha_rounding_identical();
    invalid_late_float_backdrop_is_rejected_before_any_blend_write();
    normal_opacity_rejects_invalid_late_float_backdrop_atomically();
    float_surface_preserves_premultiplied_intermediates();
    invalid_graph_and_failed_source_are_atomic();
    gaussian_blur_renders_a_finite_halo_outside_source_geometry();
    software_rasterizer_reports_gaussian_scratch_capacity();
    drop_shadow_uses_local_axes_and_object_opacity_once();
    ordered_stack_changes_output_and_disabled_nodes_are_identity();
    invalid_effect_backdrop_is_rejected_before_any_write();
    cached_effect_output_matches_multiply_compositing();
    drop_shadow_blend_uses_the_painter_order_backdrop();
    software_rasterizer_reports_drop_shadow_scratch_capacity_separately();
    translucent_shadow_composites_in_linear_srgb();
    software_rasterizer_keeps_offscreen_source_when_shadow_reaches_viewport();
    default_appearance_keeps_the_existing_fast_path_pixels();
    software_rasterizer_uses_path_appearance_over_page_backdrop();
    std::cout << "PASS opacity, Blend, Gaussian Blur, Drop Shadow, ordered stack, "
                 "atomic failure, culling and default fast path\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
