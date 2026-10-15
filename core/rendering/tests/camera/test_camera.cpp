#include "pikdraw/rendering/camera.hpp"
#include <cassert>
#include <iostream>
#include <cmath>

using namespace pikdraw;
using namespace pikdraw::rendering;

void test_camera_world_to_screen_roundtrip() {
    Camera2D cam(geometry::Point2D{1000.0, 500.0}, 0.1, 800, 600);

    geometry::Point2D pt{1250.0, 600.0};
    auto screen = cam.world_to_screen(pt);
    auto back = cam.screen_to_world(screen);

    assert(std::abs(back.x - pt.x) < 1e-4);
    assert(std::abs(back.y - pt.y) < 1e-4);
}

void test_camera_20m_billboard_precision() {
    // 20-meter billboard at x = 20,000 mm, y = 1,000 mm
    Camera2D cam(geometry::Point2D{19990.0, 990.0}, 10.0, 1920, 1080);

    geometry::Point2D billboard_pt{20000.0, 1000.0};
    auto screen = cam.world_to_screen(billboard_pt);

    // Delta is 10 mm. At zoom 10.0 px/mm, delta px should be 100 px.
    // Origin subtraction in double prevents precision cancellation!
    float center_screen_x = 1920.0f * 0.5f;
    float expected_screen_x = center_screen_x + 100.0f;
    assert(std::abs(screen.x - expected_screen_x) < 0.05f);

    auto back = cam.screen_to_world(screen);
    assert(std::abs(back.x - billboard_pt.x) < 1e-3);
    assert(std::abs(back.y - billboard_pt.y) < 1e-3);
}

void test_camera_visible_bounds() {
    Camera2D cam(geometry::Point2D{0.0, 0.0}, 1.0, 800, 600);
    auto bounds = cam.visible_world_bounds();

    assert(bounds.x < 0.0);
    assert(bounds.right() > 0.0);
    assert(std::abs(bounds.width - 800.0) < 1e-3);
    assert(std::abs(bounds.height - 600.0) < 1e-3);
}

int main() {
    std::cout << "[test_camera] Running camera tests...\n";
    test_camera_world_to_screen_roundtrip();
    test_camera_20m_billboard_precision();
    test_camera_visible_bounds();
    std::cout << "[test_camera] All tests passed!\n";
    return 0;
}
