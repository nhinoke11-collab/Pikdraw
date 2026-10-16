#include "pikdraw/rendering/renderer.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

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
  Fixture group;
  GroupObject g;
  g.id = 1;
  group.objects = group.objects.set(1, g);
  group.page.layers[0].object_ids = {1};
  RenderMetric group_metric;
  require(!group.render(&group_metric), "unsupported group falsely succeeded");
  require(group_metric.diagnostics.size() == 1 && group_metric.diagnostics[0].error &&
              group_metric.diagnostics[0].object_id == 1,
          "missing per-object failure diagnostic");
  for (ObjectId id = 2; id <= 70; ++id) {
    g.id = id;
    group.objects = group.objects.set(id, g);
    group.page.layers[0].object_ids.push_back(id);
  }
  require(!group.render(&metric) && metric.diagnostics.size() == 65,
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
int main() {
  try {
    test_fill_and_holes();
    test_layers_and_alpha();
    test_stroke_and_culling();
    test_clipping_and_failure();
    test_target_and_camera();
    test_backend_exceptions();
    test_text_backend();
    test_curve_projection_and_source_preservation();
    std::cout << "Pixel semantics, winding, layers, alpha, stroke, clipping "
                 "and validation passed.\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
