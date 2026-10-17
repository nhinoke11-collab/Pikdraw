#include "pikdraw/rendering/culling.hpp"
#include "pikdraw/rendering/software_rasterizer.hpp"
#include "pikdraw/document/document.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

using namespace pikdraw;
using namespace pikdraw::rendering;
using namespace pikdraw::document;

namespace {
void expect(bool condition, std::string_view message) {
    if (condition)
        return;
    std::cerr << "FAIL: " << message << '\n';
    std::abort();
}

void render_fractional_edge_halo(double source_x, double source_y,
                                 double zoom, double sigma_x,
                                 double sigma_y, int32_t sample_y) {
    Document doc("doc-cull-filter-pixel-boundary-test");
    PathObject blurred;
    blurred.id = generate_object_id();
    blurred.path = geometry::Path2D::make_rect(source_x, source_y, 4.0, 4.0);
    blurred.fill = color::RGBColor{1.0, 0.0, 0.0, 1.0};
    blurred.has_stroke = false;
    EffectNode blur;
    blur.id = generate_effect_instance_id();
    blur.parameters = GaussianBlurEffect{sigma_x, sigma_y};
    blurred.appearance.effects.push_back(blur);
    const auto add_result =
        doc.execute_command(std::make_unique<AddObjectCommand>(blurred));
    expect(add_result.success, "could not add edge-halo test path");

    constexpr int32_t width = 64;
    constexpr int32_t height = 64;
    std::vector<float> pixels(static_cast<size_t>(width) * height * 4, 0.0f);
    PixelBufferTarget target;
    target.width = width;
    target.height = height;
    target.stride_bytes = width * 4 * static_cast<int32_t>(sizeof(float));
    target.premultiplied_rgba = pixels.data();
    const Camera2D camera({0, 0}, zoom, width, height);
    SoftwareRasterizer renderer;
    const bool background_set =
        renderer.set_canvas_background({0, 0, 0, 0});
    expect(background_set, "could not set transparent background");
    RenderMetric metric;
    const bool rendered =
        renderer.render(*doc.current_snapshot(), camera, target, &metric);
    expect(rendered, "software rasterizer rejected edge-halo scene");

    const size_t pixel = (static_cast<size_t>(sample_y) * width + 63) * 4;
    expect(pixels[pixel + 3] > 0.0f,
           "rounded filter support did not reach the last viewport pixel");
    expect(metric.rendered_objects == 1,
           "culler removed the edge-halo object");
}
} // namespace

void test_spatial_culling() {
    Document doc("doc-cull-test");

    // Object 1: inside visible bounds [0, 0] to [100, 100]
    PathObject p1;
    p1.id = generate_object_id();
    p1.name = "Inside";
    p1.path.move_to({10, 10});
    p1.path.line_to({50, 50});
    doc.execute_command(std::make_unique<AddObjectCommand>(p1));

    // Given an object without appearance effects whose source bounds are far
    // outside the viewport, when culling runs, then it remains culled.
    PathObject p2;
    p2.id = generate_object_id();
    p2.name = "Outside";
    p2.path.move_to({5000, 5000});
    p2.path.line_to({5100, 5100});
    doc.execute_command(std::make_unique<AddObjectCommand>(p2));

    // Object 3: inside bounds but invisible
    PathObject p3;
    p3.id = generate_object_id();
    p3.name = "Hidden";
    p3.visible = false;
    p3.path.move_to({20, 20});
    p3.path.line_to({40, 40});
    doc.execute_command(std::make_unique<AddObjectCommand>(p3));

    auto snap = doc.current_snapshot();
    assert(snap->object_count() == 3);

    geometry::Rect2D view_bounds{0, 0, 200, 200};
    auto culled = SpatialCuller::query_visible(*snap, view_bounds);

    assert(culled.size() == 1);
    assert(culled[0].id == p1.id);
}

void test_gaussian_blur_bounds_keep_offscreen_source_visible() {
    // Given a path wholly left of the viewport with a blur halo reaching it,
    // when culling runs, then the effect output bounds keep the path visible.
    Document doc("doc-cull-blur-test");
    PathObject blurred;
    blurred.id = generate_object_id();
    blurred.name = "Blur reaches viewport";
    blurred.path.move_to({-90, 20});
    blurred.path.line_to({-70, 40});
    blurred.has_stroke = false;
    EffectNode blur;
    blur.id = generate_effect_instance_id();
    blur.parameters = GaussianBlurEffect{20.0, 0.0};
    blurred.appearance.effects.push_back(blur);
    doc.execute_command(std::make_unique<AddObjectCommand>(blurred));

    const geometry::Rect2D viewport{0, 0, 100, 100};
    const auto snapshot = doc.current_snapshot();
    const auto visible =
        SpatialCuller::query_visible_refs(*snapshot, viewport, 1.0);

    expect(visible.size() == 1, "blur object was not retained by culling");
    expect(visible.front().id == blurred.id, "wrong blur object was retained");
    expect(visible.front().bounds.approx_equal({-171, 19, 182, 22}),
           "blur output bounds did not match rounded kernel support");
}

void test_translated_shadow_bounds_keep_offscreen_source_visible() {
    // Given a path wholly left of the viewport whose translated, blurred
    // shadow reaches it, when culling runs, then the union bounds keep it.
    Document doc("doc-cull-shadow-test");
    PathObject shadowed;
    shadowed.id = generate_object_id();
    shadowed.name = "Shadow reaches viewport";
    shadowed.path.move_to({-30, 30});
    shadowed.path.line_to({-20, 50});
    shadowed.has_stroke = false;
    EffectNode shadow;
    shadow.id = generate_effect_instance_id();
    shadow.schema_minor = kDropShadowBlendSchemaMinor;
    shadow.parameters = DropShadowEffect{20.0, 0.0, 2.0, 0.0,
                                         color::RGBColor{0, 0, 0, 1}, 0.5};
    std::get<DropShadowEffect>(shadow.parameters).blend_mode = BlendMode::Multiply;
    shadowed.appearance.effects.push_back(shadow);
    doc.execute_command(std::make_unique<AddObjectCommand>(shadowed));

    const geometry::Rect2D viewport{0, 0, 100, 100};
    const auto snapshot = doc.current_snapshot();
    const auto visible =
        SpatialCuller::query_visible_refs(*snapshot, viewport, 1.0);

    expect(visible.size() == 1, "shadow object was not retained by culling");
    expect(visible.front().id == shadowed.id,
           "wrong shadow object was retained");
    expect(visible.front().bounds.approx_equal({-31, 29, 40, 22}),
           "shadow output bounds did not include its translated support");
}

void test_fractional_filter_support_near_viewport_edge_is_not_culled() {
    // Given a blur whose rounded kernel reaches the last pixel center while
    // its continuous 4-sigma bound misses the viewport, when SoftwareRasterizer
    // culls and renders, then the fractional halo still reaches that pixel.
    // At zoom 1, the source begins at 33.4mm. Its continuous 4-sigma bound
    // begins at 32.2mm and misses the viewport's x=32 edge; the rounded 2px
    // kernel still contributes at the last pixel center (31.5mm).
    render_fractional_edge_halo(33.4, -20.0, 1.0, 0.3, 0.3, 50);
}

void test_non_unit_zoom_rounding_keeps_edge_halo_visible() {
    // At zoom 2, sigma=.3mm becomes .6px and rounds to 3px support. The
    // continuous support begins at 16.05mm, beyond the viewport edge at 16mm,
    // but the rounded kernel reaches the final pixel center at 15.75mm.
    render_fractional_edge_halo(17.25, -10.75, 2.0, 0.3, 0.0, 51);
}

void test_legacy_culler_keeps_effectful_objects_without_camera_zoom() {
    // Given an effectful path and a legacy visibility query without camera
    // density, when culling runs, then the path is retained conservatively.
    Document doc("doc-cull-legacy-query-test");
    PathObject blurred;
    blurred.id = generate_object_id();
    blurred.path = geometry::Path2D::make_rect(1000.0, 1000.0, 2.0, 2.0);
    blurred.has_stroke = false;
    EffectNode blur;
    blur.id = generate_effect_instance_id();
    blur.parameters = GaussianBlurEffect{0.2, 0.2};
    blurred.appearance.effects.push_back(blur);
    const auto add_result =
        doc.execute_command(std::make_unique<AddObjectCommand>(blurred));
    expect(add_result.success, "could not add legacy-query test path");

    const auto visible = SpatialCuller::query_visible_refs(
        *doc.current_snapshot(), geometry::Rect2D{0.0, 0.0, 1.0, 1.0});
    expect(visible.size() == 1,
           "legacy culler dropped effectful path without zoom");
    expect(visible.front().id == blurred.id,
           "legacy culler retained the wrong effectful path");
}

int main() {
    std::cout << "[test_culling] Running spatial culling tests...\n";
    test_spatial_culling();
    test_gaussian_blur_bounds_keep_offscreen_source_visible();
    test_translated_shadow_bounds_keep_offscreen_source_visible();
    test_fractional_filter_support_near_viewport_edge_is_not_culled();
    test_non_unit_zoom_rounding_keeps_edge_halo_visible();
    test_legacy_culler_keeps_effectful_objects_without_camera_zoom();
    std::cout << "[test_culling] All tests passed!\n";
    return 0;
}
