#include "pikdraw/rendering/culling.hpp"
#include "pikdraw/rendering/spatial_index.hpp"
#include "pikdraw/rendering/damage_tracker.hpp"
#include "pikdraw/rendering/backend.hpp"
#include "pikdraw/rendering/software_rasterizer.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/document/commands/appearance_commands.hpp"
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

void expect_replacement_damage(ObjectVariant before, ObjectVariant after,
                               std::string_view message) {
    const auto object_id = std::visit([](const auto &object) {
        return object.id;
    }, before);
    Page page = Page::make_default();
    page.layers.front().object_ids.push_back(object_id);
    PersistentObjectMap old_objects;
    PersistentObjectMap new_objects;
    old_objects = old_objects.set(object_id, std::move(before));
    new_objects = new_objects.set(object_id, std::move(after));
    const DocumentSnapshot old_snapshot(1, 1, page, old_objects);
    const DocumentSnapshot new_snapshot(1, 2, page, new_objects);

    geometry::Rect2D old_bounds;
    geometry::Rect2D new_bounds;
    expect(SpatialCuller::compute_object_bounds(
               *old_snapshot.objects().lookup_ref(object_id), old_bounds, 1.0),
           "could not calculate old replacement bounds");
    expect(SpatialCuller::compute_object_bounds(
               *new_snapshot.objects().lookup_ref(object_id), new_bounds, 1.0),
           "could not calculate new replacement bounds");

    DamageTracker tracker;
    tracker.update(old_snapshot, new_snapshot, 1.0);
    expect(tracker.has_damage(), message);
    expect(!tracker.is_full_damage(), "ordinary object edit caused full damage");
    const auto damage = tracker.damage_rect();
    const double left = std::min(old_bounds.x, new_bounds.x);
    const double top = std::min(old_bounds.y, new_bounds.y);
    const double right = std::max(old_bounds.right(), new_bounds.right());
    const double bottom = std::max(old_bounds.bottom(), new_bounds.bottom());
    expect(damage.x <= left && damage.y <= top &&
               damage.right() >= right && damage.bottom() >= bottom,
           "replacement damage did not enclose old and new bounds");
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

void test_spatial_index_bvh_large_dataset() {
    Document doc("doc-spatial-index-bvh-test");
    // Add 300 objects across a grid to exceed the 256 object threshold
    constexpr int kObjectCount = 300;
    for (int i = 0; i < kObjectCount; ++i) {
        PathObject p;
        p.id = generate_object_id();
        const double x = (i % 20) * 100.0;
        const double y = (i / 20) * 100.0;
        p.path = geometry::Path2D::make_rect(x, y, 50.0, 50.0);
        p.has_stroke = false;
        doc.execute_command(std::make_unique<AddObjectCommand>(p));
    }

    const auto snap = doc.current_snapshot();
    expect(snap->object_count() == kObjectCount, "300 objects not created");

    SpatialIndex index;
    // Visible viewport: [200, 200] to [500, 500]
    const geometry::Rect2D view_bounds{200.0, 200.0, 300.0, 300.0};
    const auto bvh_results = index.query_visible_refs(*snap, view_bounds, 1.0);
    const auto linear_results = SpatialCuller::query_visible_refs(*snap, view_bounds, 1.0);

    expect(!bvh_results.empty(), "BVH returned no visible objects");
    expect(bvh_results.size() == linear_results.size(),
           "BVH results count does not match linear reference");
    for (size_t i = 0; i < bvh_results.size(); ++i) {
        expect(bvh_results[i].id == linear_results[i].id,
               "BVH order does not match linear painter order");
    }

    Document other_doc("doc-spatial-index-same-revision-test");
    for (int i = 0; i < kObjectCount; ++i) {
        PathObject p;
        p.id = generate_object_id();
        const double x = 10000.0 + (i % 20) * 100.0;
        const double y = 10000.0 + (i / 20) * 100.0;
        p.path = geometry::Path2D::make_rect(x, y, 50.0, 50.0);
        p.has_stroke = false;
        other_doc.execute_command(std::make_unique<AddObjectCommand>(p));
    }
    const auto other_snapshot = other_doc.current_snapshot();
    expect(other_snapshot->revision() == snap->revision(),
           "same-revision cache fixture has different revisions");
    const auto other_results = index.query_visible_refs(*other_snapshot, view_bounds, 1.0);
    const auto other_linear = SpatialCuller::query_visible_refs(*other_snapshot, view_bounds, 1.0);
    expect(other_results.size() == other_linear.size(),
           "spatial cache was reused across document identities");
}

void test_damage_tracker_unions() {
    Document doc("doc-damage-test");
    PathObject p1;
    p1.id = generate_object_id();
    p1.path = geometry::Path2D::make_rect(10.0, 10.0, 50.0, 50.0);
    p1.has_stroke = false;
    doc.execute_command(std::make_unique<AddObjectCommand>(p1));

    const auto snap1 = doc.current_snapshot();
    DamageTracker tracker;
    expect(!tracker.has_damage(), "initial tracker has damage");

    // Add second object
    PathObject p2;
    p2.id = generate_object_id();
    p2.path = geometry::Path2D::make_rect(200.0, 200.0, 60.0, 40.0);
    p2.has_stroke = false;
    doc.execute_command(std::make_unique<AddObjectCommand>(p2));
    const auto snap2 = doc.current_snapshot();

    tracker.update(*snap1, *snap2, 1.0);
    expect(tracker.has_damage(), "tracker failed to detect added object");
    const auto dmg = tracker.damage_rect();
    expect(dmg.x <= 200.0 && dmg.right() >= 260.0, "damage does not enclose new object");

    // Reset damage and test modification
    tracker.clear_damage();
    expect(!tracker.has_damage(), "tracker not cleared");

    // Move p2
    doc.execute_command(std::make_unique<TransformObjectCommand>(
        p2.id, geometry::Transform2D::translation(100.0, 100.0)));
    const auto snap3 = doc.current_snapshot();
    tracker.update(*snap2, *snap3, 1.0);

    expect(tracker.has_damage(), "tracker missed object transform change");
    const auto moved_dmg = tracker.damage_rect();
    // Damage must union old bounds [200, 260] and new bounds [300, 360]
    expect(moved_dmg.x <= 200.0 && moved_dmg.right() >= 360.0,
           "damage did not union old and new bounds on move");

    // A geometry edit replaces the immutable map entry even when other fields
    // stay equal. Damage must include both source and destination bounds.
    PathObject geometry_before;
    geometry_before.id = generate_object_id();
    geometry_before.path = geometry::Path2D::make_rect(10, 10, 20, 20);
    geometry_before.has_stroke = false;
    PathObject geometry_after = geometry_before;
    geometry_after.path = geometry::Path2D::make_rect(90, 70, 30, 25);
    expect_replacement_damage(geometry_before, geometry_after,
                              "tracker missed path geometry replacement");

    // Same-length effect stacks can change their parameters and visual bounds.
    PathObject effect_before;
    effect_before.id = generate_object_id();
    effect_before.path = geometry::Path2D::make_rect(40, 40, 10, 10);
    effect_before.has_stroke = false;
    PathObject effect_after = effect_before;
    EffectNode blur;
    blur.id = generate_effect_instance_id();
    blur.parameters = GaussianBlurEffect{3.0, 2.0};
    effect_after.appearance.effects.push_back(blur);
    expect_replacement_damage(effect_before, effect_after,
                              "tracker missed appearance effect edit");

    TextObject text_before;
    text_before.id = generate_object_id();
    text_before.text = "old text";
    TextObject text_after = text_before;
    text_after.text = "changed text";
    expect_replacement_damage(text_before, text_after,
                              "tracker missed text content edit");

    ImageObject image_before;
    image_before.id = generate_object_id();
    image_before.width_px = 1;
    image_before.height_px = 1;
    image_before.pixels_rgba = {0x11223344};
    image_before.bounds_rect = {5, 6, 20, 30};
    ImageObject image_after = image_before;
    image_after.pixels_rgba.front() = 0x55667788;
    expect_replacement_damage(image_before, image_after,
                              "tracker missed image pixel replacement");

    GroupObject group_before;
    group_before.id = generate_object_id();
    group_before.name = "before";
    GroupObject group_after = group_before;
    group_after.transform = geometry::Transform2D::translation(15, 25);
    expect_replacement_damage(group_before, group_after,
                              "tracker missed group transform edit");

    // Deletion invalidation must work on the tracker's first update, before a
    // cached bounds entry exists.
    PathObject deleted;
    deleted.id = generate_object_id();
    deleted.path = geometry::Path2D::make_rect(300, 120, 45, 35);
    deleted.has_stroke = false;
    Page before_page = Page::make_default();
    before_page.layers.front().object_ids.push_back(deleted.id);
    PersistentObjectMap before_objects;
    before_objects = before_objects.set(deleted.id, deleted);
    Page after_page = Page::make_default();
    PersistentObjectMap after_objects = before_objects.remove(deleted.id);
    const DocumentSnapshot before_delete(1, 1, before_page, before_objects);
    const DocumentSnapshot after_delete(1, 2, after_page, after_objects);
    DamageTracker fresh_tracker;
    fresh_tracker.update(before_delete, after_delete, 1.0);
    expect(fresh_tracker.has_damage(),
           "fresh tracker missed deleted object's old bounds");
    expect(!fresh_tracker.is_full_damage(),
           "single object deletion unexpectedly invalidated full canvas");
    const auto deleted_damage = fresh_tracker.damage_rect();
    expect(deleted_damage.x <= 300.0 && deleted_damage.right() >= 345.0 &&
               deleted_damage.y <= 120.0 && deleted_damage.bottom() >= 155.0,
           "deletion damage did not include old object bounds");
}

void test_gpu_surface_target_interface() {
    SoftwareRasterizer rasterizer;
    GpuSurfaceTarget target;
    target.type = GpuTargetType::GpuTexture;
    int dummy = 42;
    target.native_resource = &dummy;
    target.width = 100;
    target.height = 100;
    expect(target.is_valid(), "GpuSurfaceTarget should be valid");

    Document doc("doc-gpu-interface-test");
    const Camera2D camera({0, 0}, 1.0, 100, 100);
    const bool rendered = rasterizer.render_gpu(*doc.current_snapshot(), camera, target);
    expect(!rendered, "SoftwareRasterizer did not return expected default for render_gpu");
}

int main() {
    std::cout << "[test_culling] Running spatial culling tests...\n";
    test_spatial_culling();
    test_gaussian_blur_bounds_keep_offscreen_source_visible();
    test_translated_shadow_bounds_keep_offscreen_source_visible();
    test_fractional_filter_support_near_viewport_edge_is_not_culled();
    test_non_unit_zoom_rounding_keeps_edge_halo_visible();
    test_legacy_culler_keeps_effectful_objects_without_camera_zoom();
    test_spatial_index_bvh_large_dataset();
    test_damage_tracker_unions();
    test_gpu_surface_target_interface();
    std::cout << "[test_culling] All tests passed!\n";
    return 0;
}
