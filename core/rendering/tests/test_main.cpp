#include "pikdraw/rendering/types.hpp"
#include "pikdraw/rendering/camera.hpp"
#include "pikdraw/rendering/fixtures.hpp"
#include "pikdraw/rendering/culling.hpp"
#include "pikdraw/rendering/trace.hpp"
#include "pikdraw/rendering/backend.hpp"
#include <iostream>
#include <cmath>
#include <sstream>

using namespace pikdraw::rendering;

static int g_failed_checks = 0;

#define TEST_CHECK(cond, desc) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << (desc) << " at line " << __LINE__ << "\n"; \
            g_failed_checks++; \
        } \
    } while(0)

void test_contract_vector_large_sign_relative() {
    const Point2D pt{ 20000.005, 500.0 };
    const Point2D origin{ 20000.0, 0.0 };
    const double dx = pt.x - origin.x;
    const double dy = pt.y - origin.y;
    TEST_CHECK(std::abs(dx - 0.005) < 1e-9, "contract vector large-sign-relative x tolerance 1e-9");
    TEST_CHECK(std::abs(dy - 500.0) < 1e-9, "contract vector large-sign-relative y tolerance 1e-9");
}

void test_contract_vector_reflection() {
    const Point2D pt{ 2.0, 3.0 };
    const Transform2D mat{ -1.0, 0.0, 0.0, 1.0, 10.0, 0.0 };
    const Point2D result = mat.apply(pt);
    TEST_CHECK(std::abs(result.x - 8.0) < 1e-9, "contract vector reflection x must be 8.0");
    TEST_CHECK(std::abs(result.y - 3.0) < 1e-9, "contract vector reflection y must be 3.0");
}

void test_contract_vector_nonuniform_scale_translate() {
    const Point2D pt{ 2.0, 3.0 };
    const Transform2D mat{ 2.0, 0.0, 0.0, 0.5, 10.0, -4.0 };
    const Point2D result = mat.apply(pt);
    TEST_CHECK(std::abs(result.x - 14.0) < 1e-9, "contract vector nonuniform scale translate x");
    TEST_CHECK(std::abs(result.y - (-2.5)) < 1e-9, "contract vector nonuniform scale translate y");
}

void test_transform_composition_order() {
    const Transform2D parent = Transform2D::translation(10.0, 5.0);
    const Transform2D local = Transform2D::scale(2.0, 3.0);
    const Transform2D composed = Transform2D::compose(parent, local);
    const Point2D pt{ 1.0, 1.0 };
    const Point2D result = composed.apply(pt);
    TEST_CHECK(std::abs(result.x - 12.0) < 1e-9, "compose applies local then parent x");
    TEST_CHECK(std::abs(result.y - 8.0) < 1e-9, "compose applies local then parent y");
}

void test_transform_inversion_and_singularity() {
    const Transform2D singular{ 1.0, 2.0, 2.0, 4.0, 0.0, 0.0 };
    Transform2D inv;
    TEST_CHECK(!singular.invert(inv), "singular matrix inversion rejected");

    const Transform2D regular{ 2.0, 0.0, 0.0, 4.0, 10.0, 20.0 };
    TEST_CHECK(regular.invert(inv), "invertible matrix succeeds");
    const Point2D original{ 5.0, 7.0 };
    const Point2D roundtrip = inv.apply(regular.apply(original));
    TEST_CHECK(std::abs(roundtrip.x - original.x) < 1e-9, "roundtrip precision x");
    TEST_CHECK(std::abs(roundtrip.y - original.y) < 1e-9, "roundtrip precision y");
}

void test_camera_origin_subtraction_vs_float_precision() {
    Camera2D camera(Point2D{ 20000.0, 0.0 }, 10.0, 1000, 1000);
    const Point2D pt{ 20000.005, 500.0 };
    Point2F rel;
    TEST_CHECK(camera.world_to_local_relative(pt, rel), "world_to_local_relative succeeds");
    TEST_CHECK(std::abs(static_cast<double>(rel.x) - 0.005) <= 1e-6, "local float relative within float epsilon");

    Point2F screen_pt;
    TEST_CHECK(camera.world_to_screen(pt, screen_pt), "world_to_screen succeeds");
    Point2D restored;
    TEST_CHECK(camera.screen_to_world(screen_pt, restored), "screen_to_world succeeds");
    TEST_CHECK(std::abs(restored.x - pt.x) <= 1e-3, "screen pick quantization bounded by pixel pitch");
}

void test_camera_nan_inf_protection() {
    const double nan_val = std::numeric_limits<double>::quiet_NaN();
    const double inf_val = std::numeric_limits<double>::infinity();

    Camera2D camera(Point2D{ nan_val, 0.0 }, nan_val, -100, 0);
    TEST_CHECK(camera.center().is_finite(), "camera center initialized with NaN falls back to finite");
    TEST_CHECK(camera.zoom() >= Camera2D::MinZoom, "camera zoom initialized with NaN falls back to safe zoom");
    TEST_CHECK(camera.viewport_width() > 0 && camera.viewport_height() > 0, "viewport dims fall back to positive");

    TEST_CHECK(!camera.set_center(Point2D{ inf_val, 0.0 }), "set_center rejects Inf");
    TEST_CHECK(!camera.set_zoom(0.0), "set_zoom rejects 0.0");
    TEST_CHECK(!camera.set_zoom(nan_val), "set_zoom rejects NaN");
    TEST_CHECK(!camera.set_viewport_size(0, 100), "set_viewport_size rejects 0 width");
}

void test_fixtures_100k_nodes() {
    const auto fixture_small = FixtureGenerator::generate_many_small_paths(25000);
    TEST_CHECK(fixture_small.nodes.size() >= 25000, "many small paths node count");
    TEST_CHECK(fixture_small.total_points_count() >= 100000, "many small paths total vertices >= 100k");

    const auto fixture_large = FixtureGenerator::generate_few_large_paths(5, 20500);
    TEST_CHECK(fixture_large.nodes.size() >= 5, "few large paths node count");
    TEST_CHECK(fixture_large.total_points_count() >= 100000, "few large paths total vertices >= 100k");
}

void test_spatial_culling_with_stroke_halo() {
    const auto fixture = FixtureGenerator::generate_many_small_paths(25000);
    Camera2D camera(Point2D{ 10.0, 10.0 }, 50.0, 800, 600);
    const auto result = SpatialCuller::cull(fixture, camera);
    TEST_CHECK(result.total_nodes >= 25000, "culling processed all nodes");
    TEST_CHECK(result.culled_nodes > 0, "off-screen nodes culled");
    TEST_CHECK(result.visible_node_indices.size() < result.total_nodes, "visible nodes subset of total");
}

void test_stale_job_rejection() {
    ExperimentalSceneSnapshot scene;
    scene.epoch = 2;
    scene.revision = 15;
    TEST_CHECK(scene.is_stale(1, 15), "epoch mismatch rejected");
    TEST_CHECK(scene.is_stale(2, 10), "revision mismatch rejected");
    TEST_CHECK(!scene.is_stale(2, 15), "matching epoch and revision accepted");
}

void test_trace_bounded_capacity() {
    ViewportTracer tracer;
    for (uint64_t i = 1; i <= ViewportTracer::MaxRecordedFrames + 50; ++i) {
        FrameMetric m;
        m.frame_index = i;
        tracer.record_frame(m);
    }
    TEST_CHECK(tracer.frames().size() == ViewportTracer::MaxRecordedFrames, "tracer respects capacity limit");
}

void test_pixel_buffer_target_bounds_validation() {
    PixelBufferTarget t1{ nullptr, 1000, 100, 100, 400 };
    TEST_CHECK(!t1.is_valid(), "null buffer rejected");
    uint8_t dummy[100];
    PixelBufferTarget t2{ dummy, sizeof(dummy), 100, 100, 400 };
    TEST_CHECK(!t2.is_valid(), "capacity smaller than height*stride rejected");
    PixelBufferTarget t3{ dummy, sizeof(dummy), 0, 10, 0 };
    TEST_CHECK(!t3.is_valid(), "zero dimensions rejected");
}

int main() {
    {
        uint8_t byte{};
        PixelBufferTarget huge{&byte, 1, INT32_MAX, INT32_MAX, 4};
        TEST_CHECK(!huge.is_valid(), "signed overflow dimensions rejected");
        Camera2D camera;
        Point2F output{7, 9};
        TEST_CHECK(!camera.world_to_screen({1e308, 0}, output), "finite overflow rejected");
        TEST_CHECK(output.x == 7 && output.y == 9, "failure preserves output");
        TEST_CHECK(!camera.world_to_local_relative({1e308, 0}, output), "local overflow rejected");
        Transform2D bad; bad.tx = std::numeric_limits<double>::infinity();
        Transform2D inverse;
        TEST_CHECK(!bad.invert(inverse), "nonfinite inverse rejected");
    }
    std::cout << "=== Running PikDraw Core Rendering Tests ===\n";
    test_contract_vector_large_sign_relative();
    test_contract_vector_reflection();
    test_contract_vector_nonuniform_scale_translate();
    test_transform_composition_order();
    test_transform_inversion_and_singularity();
    test_camera_origin_subtraction_vs_float_precision();
    test_camera_nan_inf_protection();
    test_fixtures_100k_nodes();
    test_spatial_culling_with_stroke_halo();
    test_stale_job_rejection();
    test_trace_bounded_capacity();
    test_pixel_buffer_target_bounds_validation();

    if (g_failed_checks == 0) {
        std::cout << "All tests passed successfully.\n";
        return 0;
    }
    std::cerr << g_failed_checks << " test check(s) failed.\n";
    return 1;
}
