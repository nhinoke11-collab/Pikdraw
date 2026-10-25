#include "pikdraw/rendering/renderer.hpp"
#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/color/profiles/profile_resource.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace pikdraw;
using namespace rendering;
using namespace document;
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
struct Fixture {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(64 * 64 * 4);
  PixelBufferTarget target{bytes.data(), 64, 64, 64 * 4};
  Camera2D camera{{32, 32}, 1, 64, 64};
  SoftwareRasterizer renderer;
  Page page{64, 64, {Layer{}}};
  PersistentObjectMap objects;
  void add(PathObject object) {
    objects = objects.set(object.id, object);
    page.layers[0].object_ids.push_back(object.id);
  }
  bool render(RenderMetric *metric = nullptr) {
    return renderer.render(DocumentSnapshot{1, 7, page, objects}, camera,
                           target, metric);
  }
  int channel(int x, int y, int component) const {
    return bytes[static_cast<size_t>((y * 64 + x) * 4 + component)];
  }
};
PathObject rectangle(ObjectId id, color::RGBColor color, double x = 8,
                     double y = 8, double width = 40, double height = 40) {
  PathObject object;
  object.id = id;
  object.path = geometry::Path2D::make_rect(x, y, width, height);
  object.fill = color;
  object.has_stroke = false;
  return object;
}
void add_inner(geometry::Path2D &path, bool reverse) {
  path.move_to(20, 20);
  if (reverse) {
    path.line_to(20, 36);
    path.line_to(36, 36);
    path.line_to(36, 20);
  } else {
    path.line_to(36, 20);
    path.line_to(36, 36);
    path.line_to(20, 36);
  }
  path.close();
}
void test_fill_and_holes() {
  for (int mode = 0; mode < 3; ++mode) {
    Fixture f;
    auto object = rectangle(1, {1, 0, 0, 1});
    add_inner(object.path, mode == 2);
    object.path.set_winding_rule(mode == 0 ? geometry::WindingRule::EvenOdd
                                           : geometry::WindingRule::NonZero);
    f.add(object);
    require(f.render(), "fill frame failed");
    require(f.channel(12, 32, 1) == 0, "filled interior is missing");
    require(f.channel(28, 36, 1) == (mode == 1 ? 0 : 255),
            "hole winding semantics incorrect");
  }
  Fixture open;
  auto triangle = rectangle(1, {1, 0, 0, 1});
  triangle.path.clear();
  triangle.path.move_to(8, 8);
  triangle.path.line_to(48, 8);
  triangle.path.line_to(8, 48);
  open.add(triangle);
  require(open.render() && open.channel(12, 40, 1) == 0,
          "open fill must implicitly close");
}
void test_transparent_selection_render_can_omit_page_paper() {
  Fixture f;
  f.renderer.set_canvas_background({0, 0, 0, 0});
  f.renderer.set_render_page_background(false);
  f.add(rectangle(1, {1, 0, 0, 1}, 20, 20, 12, 12));
  require(f.render(), "transparent selection render failed");
  require(f.channel(2, 2, 3) == 0,
          "selection render must leave pixels outside selected artwork transparent");
  require(f.channel(25, 39, 0) == 255 && f.channel(25, 39, 1) == 0 &&
              f.channel(25, 39, 3) == 255,
          "selection render must retain opaque artwork pixels");
}
void test_thin_clipped_fill_preserves_pixel_centers() {
  // AC-R1 — Given a small path clipped by the viewport top/right edges, when
  // the software renderer fills it, then only the intersecting pixel centers
  // change and the clipped remainder stays white page background.
  Fixture f;
  f.add(rectangle(1, {1, 0, 0, 1}, 62, 63, 10, 4));
  RenderMetric metric;
  require(f.render(&metric), "thin clipped fill failed");
  require(metric.rendered_objects == 1 && metric.diagnostics.empty(),
          "clipped fill did not complete cleanly");
  for (int x = 62; x < 64; ++x) {
    require(f.channel(x, 0, 0) == 255 && f.channel(x, 0, 1) == 0 &&
                f.channel(x, 0, 2) == 0,
            "visible pixel center of clipped fill has the wrong color");
  }
  require(f.channel(61, 0, 0) == 255 && f.channel(61, 0, 1) == 255 &&
              f.channel(62, 1, 0) == 255 && f.channel(62, 1, 1) == 255,
          "fill changed pixels outside its clipped pixel-center coverage");
}
void test_layers_and_alpha() {
  Fixture f;
  f.add(rectangle(100, {1, 0, 0, 1}));
  f.add(rectangle(1, {0, 0, 1, 0.5}));
  require(f.render(), "ordered frame failed");
  require(std::abs(f.channel(24, 32, 0) - 128) <= 1 &&
              std::abs(f.channel(24, 32, 2) - 127) <= 1,
          "layer painter order or source-over alpha incorrect");
  f.page.layers[0].visible = false;
  require(f.render() && f.channel(24, 32, 0) == 255 &&
              f.channel(24, 32, 2) == 255,
          "hidden layer rendered");
  f.page.layers[0].visible = true;
  f.page.layers.push_back(Layer{"top", false, false, {1}});
  f.page.layers[0].object_ids = {100};
  require(f.render() && f.channel(24, 32, 2) == 0,
          "hidden upper layer rendered");
}
void test_stroke_and_culling() {
  Fixture f;
  PathObject line;
  line.id = 1;
  line.has_fill = false;
  line.stroke = color::RGBColor{1, 0, 0, 0.5};
  line.stroke_width_mm = 8;
  line.path.move_to(-2, 8);
  line.path.line_to(-2, 56);
  f.add(line);
  RenderMetric metric;
  require(f.render(&metric), "stroke frame failed");
  require(metric.rendered_objects == 1 && f.channel(0, 32, 1) < 200,
          "stroke intersecting viewport was culled by centerline bounds");
  require(f.channel(4, 32, 1) == 255, "stroke width too wide");
  Fixture overlap;
  line.path.clear();
  line.path.move_to(8, 32);
  line.path.line_to(48, 32);
  line.path.line_to(8, 32);
  overlap.add(line);
  require(overlap.render() && overlap.channel(24, 32, 1) >= 127,
          "stroke self-overlap alpha was applied multiple times");
  Fixture scaled;
  line.stroke = color::RGBColor{1, 0, 0, 1};
  line.transform = geometry::Transform2D::scale(1, 2);
  line.path.clear();
  line.path.move_to(8, 16);
  line.path.line_to(48, 16);
  scaled.add(line);
  require(scaled.render() && scaled.channel(24, 25, 1) == 0 &&
              scaled.channel(24, 22, 1) == 255,
          "nonuniform transform did not transform stroke width");

  // Rotated stroke anti-aliasing verification
  Fixture rotated;
  PathObject rot_line;
  rot_line.id = 2;
  rot_line.has_fill = false;
  rot_line.stroke = color::RGBColor{0, 0, 0, 1}; // black stroke on white background
  rot_line.stroke_width_mm = 4;
  rot_line.transform = geometry::Transform2D::translation(32, 32) *
                       geometry::Transform2D::rotation_degrees(45.0) *
                       geometry::Transform2D::translation(-32, -32);
  rot_line.path.move_to(8, 32);
  rot_line.path.line_to(56, 32);
  rotated.add(rot_line);
  require(rotated.render(), "rotated stroke render failed");
  // Center of diagonal line must be solid black (0)
  require(rotated.channel(32, 32, 0) == 0, "rotated stroke center missing");
  // Edge pixels must have smooth anti-aliasing (fractional values between 0 and 255)
  bool found_fractional = false;
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      const int c = rotated.channel(x, y, 0);
      if (c > 10 && c < 245) {
        found_fractional = true;
        break;
      }
    }
    if (found_fractional)
      break;
  }
  require(found_fractional, "rotated stroke missing anti-aliasing edge gradient");

  // Polyline stroke with shallow angle and sharp join anti-aliasing verification
  Fixture poly;
  PathObject poly_line;
  poly_line.id = 3;
  poly_line.has_fill = false;
  poly_line.stroke = color::RGBColor{0, 0, 0, 1};
  poly_line.stroke_width_mm = 3;
  // Shallow angle line meeting a steep descent with a sharp apex at (40, 20)
  poly_line.path.move_to(8, 28);
  poly_line.path.line_to(40, 20);
  poly_line.path.line_to(56, 52);
  poly.add(poly_line);
  require(poly.render(), "polyline stroke render failed");
  // Subpixel 4x MSAA ensures rich anti-aliasing gradations across shallow-angle segments
  std::set<int> distinct_alphas;
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      const int c = poly.channel(x, y, 0);
      if (c > 5 && c < 250) {
        distinct_alphas.insert(c);
      }
    }
  }
  require(distinct_alphas.size() >= 8,
          "polyline stroke missing rich subpixel anti-aliasing gradient");
}
void test_clipping_and_failure() {
  Fixture f;
  PathObject line;
  line.id = 1;
  line.has_fill = false;
  line.stroke_width_mm = 2;
  line.path.move_to(-1e10, 32);
  line.path.line_to(1e10, 32);
  bool rejected_coordinate = false;
  try {
    f.add(line);
  } catch (const std::invalid_argument &) {
    rejected_coordinate = true;
  }
  require(rejected_coordinate && f.objects.empty(),
          "out-of-range source coordinate entered an immutable snapshot");
  line.path = {};
  line.path.move_to(geometry::limits::kCanvasMinMm, 32);
  line.path.line_to(geometry::limits::kCanvasMaxMm, 32);
  f.add(line);
  const auto start = std::chrono::steady_clock::now();
  require(f.render(), "huge clipped line failed");
  require(std::chrono::steady_clock::now() - start < std::chrono::seconds(2),
          "offscreen clipping performed unbounded raster work");
  require(f.channel(32, 32, 0) == 0, "huge clipped line missing");
  Fixture bad;
  auto arc = rectangle(1, {1, 0, 0, 1});
  arc.path.arc_to(5, 5, 0, false, false, 20, 20);
  bad.add(arc);
  RenderMetric metric;
  require(!bad.render(&metric) && metric.rendered_objects == 0,
          "unsupported arc falsely succeeded");
  Fixture invalid;
  auto bad_color =
      rectangle(1, {std::numeric_limits<double>::quiet_NaN(), 0, 0, 1});
  bool rejected_color = false;
  try {
    invalid.add(bad_color);
  } catch (const std::invalid_argument &) {
    rejected_color = true;
  }
  require(rejected_color && invalid.objects.empty(),
          "invalid source color entered an immutable snapshot");
  Fixture unsupported_image;
  ImageObject bad_clip_image;
  bad_clip_image.id = 1;
  bad_clip_image.width_px = 1;
  bad_clip_image.height_px = 1;
  bad_clip_image.pixels_rgba = {0xff0000ff};
  bad_clip_image.bounds_rect = {8, 8, 40, 40};
  geometry::Path2D unsupported_clip;
  unsupported_clip.move_to(8, 8);
  unsupported_clip.arc_to(4, 4, 0, false, true, 48, 48);
  bad_clip_image.clip_path = unsupported_clip;
  unsupported_image.objects = unsupported_image.objects.set(1, bad_clip_image);
  unsupported_image.page.layers[0].object_ids = {1};
  RenderMetric image_metric;
  require(!unsupported_image.render(&image_metric),
          "unsupported image clip falsely succeeded");
  require(image_metric.diagnostics.size() == 1 &&
              image_metric.diagnostics[0].error &&
              image_metric.diagnostics[0].object_id == 1,
          "missing per-object image failure diagnostic");
  for (ObjectId id = 2; id <= 70; ++id) {
    bad_clip_image.id = id;
    unsupported_image.objects =
        unsupported_image.objects.set(id, bad_clip_image);
    unsupported_image.page.layers[0].object_ids.push_back(id);
  }
  require(!unsupported_image.render(&metric) && metric.diagnostics.size() == 65,
          "diagnostics are unbounded or missing overflow summary");
}
void test_curve_projection_and_source_preservation() {
  Fixture f;
  auto ellipse = rectangle(1, {0, 0, 0, 1});
  ellipse.path = geometry::Path2D::make_ellipse(32, 32, 16, 16);
  const auto source_size = ellipse.path.size();
  f.add(ellipse);
  require(f.render(), "adaptive cubic ellipse failed");
  require(f.channel(32, 32, 0) == 0 && f.channel(17, 17, 0) == 255,
          "curve fill is a bounding rectangle or is missing");
  require(std::get<PathObject>(*f.objects.get(1)).path.size() == source_size,
          "render approximation modified source curves");
  Fixture translated;
  ellipse.transform = geometry::Transform2D::translation(20000, 0);
  translated.camera.set_center({20032, 32});
  translated.page.width_mm = 20100;
  translated.add(ellipse);
  require(translated.render() && translated.channel(32, 32, 0) == 0,
          "camera-relative projection loses a 20m object");
  Fixture invalid;
  ellipse.transform = geometry::Transform2D::identity();
  ellipse.path.clear();
  ellipse.path.line_to(32, 32);
  invalid.add(ellipse);
  require(!invalid.render(), "path without MoveTo falsely succeeded");
}

void test_nested_groups_apply_parent_transforms() {
  Fixture f;
  auto child = rectangle(2, {1, 0, 0, 1}, 8, 8, 8, 8);
  GroupObject inner;
  inner.id = 3;
  inner.children_ids = {child.id};
  inner.transform = geometry::Transform2D::translation(5, 0);
  GroupObject outer;
  outer.id = 1;
  outer.children_ids = {inner.id};
  outer.transform = geometry::Transform2D::translation(15, 0);
  f.objects = f.objects.set(child.id, child);
  f.objects = f.objects.set(inner.id, inner);
  f.objects = f.objects.set(outer.id, outer);
  f.page.layers[0].object_ids = {outer.id};

  RenderMetric metric;
  require(f.render(&metric), "nested group frame failed");
  require(f.channel(32, 52, 0) == 255 && f.channel(32, 52, 1) == 0 &&
              f.channel(32, 52, 2) == 0,
          "parent group transforms were not composed onto child geometry");
  require(f.channel(12, 52, 0) == 255 && f.channel(12, 52, 1) == 255,
          "group child rendered at its untransformed location");
  require(metric.total_objects == 3 && metric.rendered_objects == 3 &&
              metric.culled_objects == 0,
          "group rendering metrics omitted hierarchy objects");
}

void test_image_clip_masks_transformed_pixels() {
  Fixture f;
  ImageObject image;
  image.id = 10;
  image.width_px = 2;
  image.height_px = 1;
  image.shared_pixels_rgba = std::make_shared<const std::vector<uint32_t>>(
      std::vector<uint32_t>{0xff0000ff, 0x0000ffff});
  image.bounds_rect = {8, 8, 40, 40};
  image.transform = geometry::Transform2D::translation(10, 0);
  image.clip_path = geometry::Path2D::make_rect(8, 8, 20, 40);
  f.objects = f.objects.set(image.id, image);
  f.page.layers[0].object_ids = {image.id};

  require(f.render(), "transformed clipped image frame failed");
  require(f.channel(25, 32, 0) == 255 && f.channel(25, 32, 1) == 0 &&
              f.channel(25, 32, 2) == 0,
          "image transform or clip path removed an included source pixel");
  require(f.channel(45, 32, 0) == 255 && f.channel(45, 32, 1) == 255 &&
              f.channel(45, 32, 2) == 255,
          "image clip path did not mask pixels outside its path");
}

void test_image_scaling_uses_linear_sampling() {
  Fixture f;
  ImageObject image;
  image.id = 451;
  image.width_px = 2;
  image.height_px = 2;
  image.pixels_rgba = {0xff0000ff, 0x00ff00ff,
                       0x0000ffff, 0xffffffff};
  image.bounds_rect = {10, 10, 4, 4};
  f.objects = f.objects.set(image.id, image);
  f.page.layers[0].object_ids = {image.id};

  require(f.render(), "scaled image frame failed");
  require(std::abs(f.channel(11, 51, 0) - 159) <= 2 &&
              std::abs(f.channel(11, 51, 1) - 64) <= 2 &&
              std::abs(f.channel(11, 51, 2) - 64) <= 2,
          "scaled image used nearest-neighbor sampling or changed source orientation");
}

void test_locked_pdf_preview_renders_without_document_objects() {
  Fixture f;
  auto asset = std::make_shared<PdfSourceAsset>();
  asset->sha256_hex = std::string(64, 'a');
  asset->page_count = 1;
  asset->bytes = std::make_shared<const std::vector<uint8_t>>(
      std::vector<uint8_t>{'%', 'P', 'D', 'F'});
  auto preview = std::make_shared<const std::vector<uint32_t>>(
      std::vector<uint32_t>(4, 0xff0000ffu));
  auto page = make_document_page(
      "PDF page", f.page, {}, PdfSourcePageReference{asset, 0, 0, 2, 2, preview});
  const PageId active_page_id = page.id;
  const DocumentSnapshot snapshot{
      1, 7, std::vector<DocumentPage>{std::move(page)},
      active_page_id, {}, DocumentContentMode::PdfPrintLocked};
  require(f.renderer.render(snapshot, f.camera, f.target),
          "locked PDF preview cache did not render");
  require(snapshot.object_count() == 0 && f.channel(32, 32, 0) == 255 &&
              f.channel(32, 32, 1) == 0 && f.channel(32, 32, 2) == 0,
          "PDF screen preview was not drawn independently of editable objects");
}

void test_bound_custom_cmyk_profile_renders_through_icc_preview() {
  using namespace pikdraw::color;
  const auto srgb_path = IccPreview::system_srgb_profile();
  if (srgb_path.empty()) {
    std::cout << "[NOT_MEASURED] System sRGB ICC profile is unavailable for bound-paint render\n";
    return;
  }
  const auto cmyk_path = srgb_path.parent_path() / L"RSWOP.icm";
  if (!std::filesystem::is_regular_file(cmyk_path)) {
    std::cout << "[NOT_MEASURED] Installed SWOP ICC profile is unavailable for bound-paint render\n";
    return;
  }
  const auto srgb = ProfileResource::load_file(srgb_path, ColorSpace::Rgb);
  const auto swop = ProfileResource::load_file(cmyk_path, ColorSpace::Cmyk);
  if (!srgb || !swop) {
    std::cout << "[NOT_MEASURED] Windows CMM rejected the installed ICC render fixtures\n";
    return;
  }
  std::string diagnostic;
  const auto render_context = IccPreview::create_from_resources(srgb.resource, {}, &diagnostic);
  const auto reference_context = IccPreview::create_from_resources(
      srgb.resource, swop.resource, &diagnostic);
  if (!render_context || !reference_context) {
    std::cout << "[NOT_MEASURED] Windows CMM could not create ICC render transforms\n";
    return;
  }

  auto paint = ColorValue::make_cmyk(0.0, 1.0, 1.0, 0.0, 1.0);
  require(paint.set_profile_resource(swop.resource), "could not bind SWOP ICC to the paint");
  const auto expected = reference_context->convert(paint);
  Fixture fixture;
  require(fixture.renderer.set_color_preview(render_context), "CPU preview binding failed");
  auto object = rectangle(701, {0.0, 0.0, 0.0, 1.0});
  object.fill = paint;
  fixture.add(std::move(object));
  require(fixture.render(), "bound custom-profile render failed");
  const auto byte = [](float channel) {
    return static_cast<int>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255.0f));
  };
  require(std::abs(fixture.channel(20, 20, 0) - byte(expected.r)) <= 1 &&
              std::abs(fixture.channel(20, 20, 1) - byte(expected.g)) <= 1 &&
              std::abs(fixture.channel(20, 20, 2) - byte(expected.b)) <= 1 &&
              fixture.channel(20, 20, 3) == byte(expected.a),
          "SoftwareRasterizer did not use the bound paint ICC profile");
  require(render_context->conversion_failure_count() == 0,
          "bound paint ICC preview recorded a conversion failure");
  require(paint.profile_resource() == swop.resource &&
              std::get<CMYKColor>(paint.payload()).a == 1.0,
          "rendering mutated bound paint source values");
  std::cout << "[PASS] bound CMYK paint uses its ICC profile in SoftwareRasterizer\n";
}

void test_text_backend() {
  Fixture f;
  TextObject text;
  text.id = 1;
  text.text = "O";
  text.font.family_name = "Arial";
  text.font.size_pt = 80;
  text.position = {12, 16};
  text.fill = color::RGBColor{0, 0, 0, 1};
  f.objects = f.objects.set(1, text);
  f.page.layers[0].object_ids = {1};
#ifdef PIKDRAW_TYPOGRAPHY_REAL_OUTLINES
  RenderMetric metric;
  require(f.render(&metric), "genuine text backend failed");
  size_t black = 0;
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x)
      if (f.channel(x, y, 0) == 0)
        ++black;
  }
  require(black > 20 && black < 400,
          "glyph coverage looks empty or like a bounding rectangle");
#else
  require(!f.render(), "text succeeded without a genuine outline backend");
#endif
}

struct InkBounds {
  bool any{};
  int min_x{64};
  int min_y{64};
  int max_x{-1};
  int max_y{-1};
  size_t count{};
};

InkBounds dark_pixel_bounds(const std::vector<uint8_t> &pixels,
                            uint8_t threshold) {
  InkBounds bounds;
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      const auto offset = static_cast<size_t>((y * 64 + x) * 4);
      if (pixels[offset] >= threshold || pixels[offset + 1] >= threshold ||
          pixels[offset + 2] >= threshold)
        continue;
      bounds.any = true;
      bounds.min_x = std::min(bounds.min_x, x);
      bounds.min_y = std::min(bounds.min_y, y);
      bounds.max_x = std::max(bounds.max_x, x);
      bounds.max_y = std::max(bounds.max_y, y);
      ++bounds.count;
    }
  }
  return bounds;
}

std::vector<uint8_t> render_text_appearance(Appearance appearance) {
  Fixture f;
  TextObject text;
  text.id = 1;
  text.text = "O";
  text.font.family_name = "Arial";
  text.font.size_pt = 80;
  text.position = {12, 16};
  text.fill = color::RGBColor{0, 0, 0, 1};
  text.appearance = std::move(appearance);
  f.objects = f.objects.set(text.id, text);
  f.page.layers[0].object_ids = {text.id};
  RenderMetric metric;
  require(f.render(&metric), "Text appearance failed with real glyph outlines");
  require(metric.rendered_objects == 1,
          "Text appearance did not render one complete object");
  return f.bytes;
}

void test_text_appearance_effects_use_glyph_alpha() {
#ifdef PIKDRAW_TYPOGRAPHY_REAL_OUTLINES
  // Given real outlined Text glyphs, when Appearance opacity and raster
  // effects are applied, then they change glyph coverage rather than a text
  // layout rectangle, while the native renderer still reports a complete frame.
  const auto baseline = render_text_appearance(Appearance{});
  const auto baseline_bounds = dark_pixel_bounds(baseline, 250);
  require(baseline_bounds.any && baseline_bounds.count > 20,
          "real Text glyph baseline is empty");

  const auto darkest = std::find_if(
      baseline.begin(), baseline.end(), [](uint8_t value) { return value == 0; });
  require(darkest != baseline.end(), "Text glyph has no opaque source pixel");
  const auto anchor = static_cast<size_t>(darkest - baseline.begin()) / 4;
  const auto require_half_opacity = [anchor](
      const std::vector<uint8_t> &pixels, const char *message) {
    const int value = pixels[anchor * 4];
    require(value >= 110 && value <= 145, message);
  };

  Appearance fill_half;
  fill_half.fill_opacity = 0.5;
  require_half_opacity(render_text_appearance(fill_half),
                       "Text Fill Opacity did not scale glyph alpha once");
  Appearance object_half;
  object_half.object_opacity = 0.5;
  require_half_opacity(render_text_appearance(object_half),
                       "Text Object Opacity did not scale glyph alpha once");

  Appearance blurred;
  EffectNode blur;
  blur.id = 9201;
  blur.parameters = GaussianBlurEffect{2.0, 2.0};
  blurred.effects.push_back(blur);
  const auto blur_bounds =
      dark_pixel_bounds(render_text_appearance(blurred), 250);
  require(blur_bounds.any && blur_bounds.count > baseline_bounds.count &&
              (blur_bounds.min_x < baseline_bounds.min_x ||
               blur_bounds.min_y < baseline_bounds.min_y ||
               blur_bounds.max_x > baseline_bounds.max_x ||
               blur_bounds.max_y > baseline_bounds.max_y),
          "Gaussian Blur did not expand the real Text glyph alpha");

  Appearance shadowed;
  EffectNode shadow;
  shadow.id = 9202;
  shadow.parameters = DropShadowEffect{
      12.0, 8.0, 0.0, 0.0, color::RGBColor{0, 0, 0, 1}, 0.75};
  shadowed.effects.push_back(shadow);
  const auto shadow_bounds =
      dark_pixel_bounds(render_text_appearance(shadowed), 250);
  require(shadow_bounds.any && shadow_bounds.count > baseline_bounds.count &&
              (shadow_bounds.min_x < baseline_bounds.min_x ||
               shadow_bounds.min_y < baseline_bounds.min_y ||
               shadow_bounds.max_x > baseline_bounds.max_x ||
               shadow_bounds.max_y > baseline_bounds.max_y),
          "Drop Shadow did not follow and extend the real Text glyph alpha");
#else
  // Given no real outline backend, when a Text appearance is rendered, then
  // the renderer rejects it instead of substituting a rectangle or claiming
  // that an effect was applied to actual glyphs.
  Fixture f;
  TextObject text;
  text.id = 1;
  text.text = "O";
  text.font.family_name = "Arial";
  text.font.size_pt = 80;
  text.position = {12, 16};
  EffectNode blur;
  blur.id = 9203;
  blur.parameters = GaussianBlurEffect{2.0, 2.0};
  text.appearance.effects.push_back(blur);
  f.objects = f.objects.set(text.id, text);
  f.page.layers[0].object_ids = {text.id};
  RenderMetric metric;
  require(!f.render(&metric),
          "Text appearance succeeded without a real outline backend");
  require(!metric.diagnostics.empty(),
          "unsupported Text appearance has no render diagnostic");
#endif
}

struct ThrowingBackend : IRenderBackend {
  DeviceLossStatus device_status() const noexcept override {
    return DeviceLossStatus::Healthy;
  }
  bool recover_device() override {
    throw std::runtime_error("recover failure");
  }
  bool render(const DocumentSnapshot &, const Camera2D &, PixelBufferTarget &,
              RenderMetric *) override {
    throw std::runtime_error("render failure");
  }
};
void test_backend_exceptions() {
  Fixture f;
  ViewportRenderer renderer{std::make_shared<ThrowingBackend>()};
  RenderMetric metric;
  metric.rendered_objects = 42;
  const DocumentSnapshot snapshot{1, 7, f.page, f.objects};
  require(!renderer.render_frame(snapshot, f.target, &metric) &&
              metric.rendered_objects == 0,
          "failed backend propagated exception or stale metric");
  require(!renderer.handle_device_loss_recovery(),
          "recover exception propagated");
}
void test_target_and_camera() {
  Camera2D camera{{0, 0}, std::numeric_limits<double>::quiet_NaN(), 64, 64};
  camera.set_zoom(std::numeric_limits<double>::infinity());
  camera.set_center({std::numeric_limits<double>::quiet_NaN(), 0});
  camera.zoom_at(-1, 0, 0);
  camera.pan_pixels(std::numeric_limits<double>::infinity(), 0);
  require(std::isfinite(camera.zoom()) && camera.center().is_finite(),
          "nonfinite camera accepted");
  camera.set_zoom(1e-9);
  require(camera.zoom() == 0.01, "C3 minimum zoom is not enforced");
  camera.set_zoom(1e9);
  require(camera.zoom() == 1000.0, "C3 maximum zoom is not enforced");
  uint8_t byte = 0;
  PixelBufferTarget overflow{&byte, std::numeric_limits<int32_t>::max(), 1, 4};
  require(!overflow.is_valid(), "target multiplication overflow accepted");
  std::vector<uint8_t> padding(40, 77);
  PixelBufferTarget unaligned{padding.data() + 1, 2, 2, 12};
  unaligned.clear(1, 2, 3, 4);
  require(padding[0] == 77 && padding[9] == 77 && padding[13] == 1,
          "unaligned/stride writes corrupt padding");
  uint8_t alpha[4]{0, 0, 0, 0};
  PixelBufferTarget translucent{alpha, 1, 1, 4};
  translucent.blend_pixel(0, 0, 200, 100, 50, 128);
  require(alpha[0] == 200 && alpha[3] == 128,
          "straight alpha target compositing incorrect");
}
} // namespace
void test_powerclip_reveal_view_is_transient() {
    Fixture f;
    auto content=rectangle(1,{1,0,0,1},4,4,52,52);
    auto frame=rectangle(2,{0,0,0,1},20,20,12,12);
    GroupObject group;group.id=3;group.children_ids={2,1};group.clip=ClipProperties{};
    group.clip->mask_object_id=2;group.clip->preserve_frame_appearance=false;
    f.objects=f.objects.set(1,content).set(2,frame).set(3,group);f.page.layers[0].object_ids={3};
    require(f.render(),"normal clip render failed");
    require(f.channel(10,54,1)>200,"normal view revealed hidden content");
    require(f.channel(25,39,0)>240&&f.channel(25,39,1)<10,"clip content missing");
    f.renderer.set_view_context({3,1,true,false});
    require(f.render(),"reveal view failed");
    require(f.channel(10,54,0)>240&&f.channel(10,54,1)<10,"edit view did not reveal content");
    f.renderer.set_view_context({});
    require(f.render(),"normal view after finish failed");
    require(f.channel(10,54,1)>200,"edit view leaked into normal output");
}
int main() {
  try {
    test_powerclip_reveal_view_is_transient();
    test_transparent_selection_render_can_omit_page_paper();
    test_fill_and_holes();
    test_thin_clipped_fill_preserves_pixel_centers();
    test_layers_and_alpha();
    test_stroke_and_culling();
    test_clipping_and_failure();
    test_target_and_camera();
    test_backend_exceptions();
    test_text_backend();
    test_text_appearance_effects_use_glyph_alpha();
    test_curve_projection_and_source_preservation();
    test_nested_groups_apply_parent_transforms();
    test_image_clip_masks_transformed_pixels();
    test_image_scaling_uses_linear_sampling();
    test_locked_pdf_preview_renders_without_document_objects();
    test_bound_custom_cmyk_profile_renders_through_icc_preview();
    std::cout << "Pixel semantics, winding, layers, alpha, stroke, clipping "
                 "and validation passed.\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
