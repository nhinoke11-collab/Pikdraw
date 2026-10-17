#include "../src/tools/snap_status_controller.hpp"

#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::desktop;
using namespace pikdraw::vector::precision::snap;

static void test_dwell_acquisition() {
    SnapStatusController controller;
    assert(controller.state() == InferenceState::Idle);
    assert(controller.dwell_time_ms() == 300);

    const auto t0 = std::chrono::steady_clock::now();
    SnapCandidate cand;
    cand.key.kind = SnapOption::Endpoint;
    cand.key.source_object_id = 1;
    cand.key.source_node_or_segment_id = 1;
    cand.world_point = {100.0, 100.0};

    // t = 0 ms -> HoverCandidate
    auto res = controller.update(cand.world_point, cand, t0);
    assert(res.state == InferenceState::HoverCandidate);
    assert(controller.state() == InferenceState::HoverCandidate);
    assert(controller.acquired_points().empty());

    // t = 150 ms -> still HoverCandidate (< 300 ms)
    res = controller.update(cand.world_point, cand, t0 + std::chrono::milliseconds(150));
    assert(res.state == InferenceState::HoverCandidate);
    assert(controller.state() == InferenceState::HoverCandidate);
    assert(controller.acquired_points().empty());

    // t = 300 ms -> Acquired (>= 300 ms)
    res = controller.update(cand.world_point, cand, t0 + std::chrono::milliseconds(300));
    assert(res.state == InferenceState::Acquired);
    assert(controller.state() == InferenceState::Acquired);
    assert(controller.acquired_points().size() == 1);
    assert(controller.acquired_points()[0].key == cand.key);
    assert(controller.acquired_points()[0].world_point.x == 100.0);
    assert(controller.acquired_points()[0].world_point.y == 100.0);

    // t = 500 ms with same candidate -> stays Acquired, no duplicate entry in queue
    res = controller.update(cand.world_point, cand, t0 + std::chrono::milliseconds(500));
    assert(res.state == InferenceState::Acquired);
    assert(controller.acquired_points().size() == 1);
}

static void test_jitter_reset() {
    SnapStatusController controller;
    const auto t0 = std::chrono::steady_clock::now();

    SnapCandidate cand1;
    cand1.key.kind = SnapOption::Endpoint;
    cand1.key.source_object_id = 1;
    cand1.key.source_node_or_segment_id = 1;
    cand1.world_point = {100.0, 100.0};

    controller.update(cand1.world_point, cand1, t0);
    controller.update(cand1.world_point, cand1, t0 + std::chrono::milliseconds(200));

    // Pointer moves > 10 mm away to (150, 100) with a different candidate
    SnapCandidate cand2;
    cand2.key.kind = SnapOption::Endpoint;
    cand2.key.source_object_id = 2;
    cand2.key.source_node_or_segment_id = 1;
    cand2.world_point = {150.0, 100.0};

    auto res = controller.update(cand2.world_point, cand2, t0 + std::chrono::milliseconds(250));
    assert(res.state == InferenceState::HoverCandidate);
    assert(controller.acquired_points().empty());

    // At t0 + 400 ms (only 150 ms since cand2 arrived), should not be acquired yet
    res = controller.update(cand2.world_point, cand2, t0 + std::chrono::milliseconds(400));
    assert(res.state == InferenceState::HoverCandidate);
    assert(controller.acquired_points().empty());

    // At t0 + 560 ms (310 ms since cand2 arrived), cand2 should be acquired
    res = controller.update(cand2.world_point, cand2, t0 + std::chrono::milliseconds(560));
    assert(res.state == InferenceState::Acquired);
    assert(controller.acquired_points().size() == 1);
    assert(controller.acquired_points()[0].key == cand2.key);
}

static void test_fifo_queue() {
    SnapStatusController controller;
    assert(controller.max_acquired_points() == 4);

    const auto t0 = std::chrono::steady_clock::now();

    for (uint64_t i = 1; i <= 5; ++i) {
        SnapCandidate cand;
        cand.key.kind = SnapOption::Endpoint;
        cand.key.source_object_id = i;
        cand.key.source_node_or_segment_id = 1;
        cand.world_point = {static_cast<double>(i * 10), 0.0};

        const auto dwell_time = t0 + std::chrono::milliseconds(i * 1000);
        controller.update(cand.world_point, cand, dwell_time);
        controller.update(cand.world_point, cand, dwell_time + std::chrono::milliseconds(310));
    }

    // Capacity is 4, so point 1 should have been evicted; points 2..5 remain
    const auto& queue = controller.acquired_points();
    assert(queue.size() == 4);
    assert(queue[0].key.source_object_id == 2);
    assert(queue[1].key.source_object_id == 3);
    assert(queue[2].key.source_object_id == 4);
    assert(queue[3].key.source_object_id == 5);
}

static void test_tracking_rays() {
    SnapStatusController controller;
    controller.set_polar_step_degrees(45.0);
    const auto t0 = std::chrono::steady_clock::now();

    // Acquire anchor point at (0, 0)
    SnapCandidate origin_cand;
    origin_cand.key.kind = SnapOption::Endpoint;
    origin_cand.key.source_object_id = 1;
    origin_cand.key.source_node_or_segment_id = 1;
    origin_cand.world_point = {0.0, 0.0};

    controller.update(origin_cand.world_point, origin_cand, t0);
    controller.update(origin_cand.world_point, origin_cand, t0 + std::chrono::milliseconds(310));
    assert(controller.acquired_points().size() == 1);

    // Pointer moves along horizontal 0° ray: (50.0, 0.5)
    // Distance to ray is 0.5 mm (< 6 mm aperture), length along ray is 50.0 mm (>= 5 mm min length)
    auto res = controller.update({50.0, 0.5}, std::nullopt, t0 + std::chrono::milliseconds(400));
    assert(res.state == InferenceState::Tracking);
    assert(controller.state() == InferenceState::Tracking);
    assert(res.active_ray.has_value());
    assert(res.active_ray->angle_deg == 0.0);
    assert(res.inference_point.has_value());
    assert(std::abs(res.inference_point->x - 50.0) < 1e-6);
    assert(std::abs(res.inference_point->y - 0.0) < 1e-6);

    // Pointer moves along 45° ray: (20.0, 20.2)
    res = controller.update({20.0, 20.2}, std::nullopt, t0 + std::chrono::milliseconds(500));
    assert(res.state == InferenceState::Tracking);
    assert(res.active_ray.has_value());
    assert(std::abs(res.active_ray->angle_deg - 45.0) < 1e-6);

    // Test Ortho mode: 45° ray is not available in Ortho mode
    controller.set_direction_mode(DirectionMode::Ortho);
    res = controller.update({20.0, 20.2}, std::nullopt, t0 + std::chrono::milliseconds(600));
    assert(res.state == InferenceState::Idle);
    assert(!res.active_ray.has_value());

    // In Ortho mode, 90° vertical ray still tracks: (0.2, 30.0)
    res = controller.update({0.2, 30.0}, std::nullopt, t0 + std::chrono::milliseconds(700));
    assert(res.state == InferenceState::Tracking);
    assert(res.active_ray.has_value());
    assert(std::abs(res.active_ray->angle_deg - 90.0) < 1e-6);
    assert(res.inference_point.has_value());
    assert(std::abs(res.inference_point->x - 0.0) < 1e-6);
    assert(std::abs(res.inference_point->y - 30.0) < 1e-6);
}

static void test_ray_lock_and_escape() {
    SnapStatusController controller;
    const auto t0 = std::chrono::steady_clock::now();

    // Acquire anchor point at (0, 0)
    SnapCandidate origin_cand;
    origin_cand.key.kind = SnapOption::Endpoint;
    origin_cand.key.source_object_id = 1;
    origin_cand.key.source_node_or_segment_id = 1;
    origin_cand.world_point = {0.0, 0.0};

    controller.update(origin_cand.world_point, origin_cand, t0);
    controller.update(origin_cand.world_point, origin_cand, t0 + std::chrono::milliseconds(310));

    // Track on 0° ray
    controller.update({40.0, 0.2}, std::nullopt, t0 + std::chrono::milliseconds(400));
    assert(controller.state() == InferenceState::Tracking);

    // Lock the ray
    bool locked = controller.lock_current();
    assert(locked);
    assert(controller.is_locked());
    assert(controller.state() == InferenceState::Locked);

    // Pointer wanders off-axis to (60.0, 50.0); locked ray keeps project point on 0° ray
    auto res = controller.update({60.0, 50.0}, std::nullopt, t0 + std::chrono::milliseconds(500));
    assert(res.state == InferenceState::Locked);
    assert(res.inference_point.has_value());
    assert(std::abs(res.inference_point->x - 60.0) < 1e-6);
    assert(std::abs(res.inference_point->y - 0.0) < 1e-6);

    // First Esc: unlocks ray
    bool handled = controller.handle_escape();
    assert(handled);
    assert(!controller.is_locked());
    assert(controller.state() == InferenceState::Idle);
    assert(!controller.acquired_points().empty()); // Acquired point preserved

    // Second Esc: clears acquired tracking points
    handled = controller.handle_escape();
    assert(handled);
    assert(controller.acquired_points().empty());

    // Third Esc: nothing left to cancel
    handled = controller.handle_escape();
    assert(!handled);
}

int main() {
    std::cout << "=== Running PikDraw Snap Status Controller Tests ===" << std::endl;

    test_dwell_acquisition();
    std::cout << "[PASS] Dwell acquisition & timing transitions" << std::endl;

    test_jitter_reset();
    std::cout << "[PASS] Jitter resets dwell timer" << std::endl;

    test_fifo_queue();
    std::cout << "[PASS] FIFO queue capacity management" << std::endl;

    test_tracking_rays();
    std::cout << "[PASS] Dynamic ray inference & Ortho/Polar angle modes" << std::endl;

    test_ray_lock_and_escape();
    std::cout << "[PASS] Ray locking and multi-stage Escape handling" << std::endl;

    std::cout << "All Snap Status Controller Tests passed successfully!" << std::endl;
    return 0;
}
