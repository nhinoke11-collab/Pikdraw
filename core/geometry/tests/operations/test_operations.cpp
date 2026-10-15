#include "test_harness.hpp"
#include "pikdraw/geometry/operations/boolean.hpp"
#include "pikdraw/geometry/operations/offset.hpp"

using namespace pikdraw::geometry;

PIKDRAW_TEST(boolean_rectangle_intersection) {
    Rect2D r1{0.0, 0.0, 100.0, 100.0};
    Rect2D r2{50.0, 50.0, 100.0, 100.0};

    auto res = boolean_rectangles(r1, r2, BooleanOp::Intersection);
    PIKDRAW_ASSERT(res.success);
    PIKDRAW_ASSERT(res.rectangles.size() == 1);
    PIKDRAW_ASSERT_NEAR(res.rectangles[0].x, 50.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(res.rectangles[0].y, 50.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(res.rectangles[0].width, 50.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(res.rectangles[0].height, 50.0, 1e-9);
}

PIKDRAW_TEST(boolean_rectangle_difference) {
    Rect2D r1{0.0, 0.0, 100.0, 100.0};
    Rect2D r2{50.0, 0.0, 100.0, 100.0}; // Overlaps right half

    auto res = boolean_rectangles(r1, r2, BooleanOp::Difference);
    PIKDRAW_ASSERT(res.success);
    PIKDRAW_ASSERT(!res.rectangles.empty());

    // Total area of remaining difference should be 50 * 100 = 5000 mm^2
    double total_area = 0.0;
    for (const auto& r : res.rectangles) {
        total_area += r.width * r.height;
    }
    PIKDRAW_ASSERT_NEAR(total_area, 5000.0, 1e-6);
}

PIKDRAW_TEST(offset_rectangle_expansion) {
    Rect2D r{10.0, 20.0, 100.0, 50.0};
    Rect2D expanded = offset_rect(r, 5.0);

    PIKDRAW_ASSERT_NEAR(expanded.x, 5.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(expanded.y, 15.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(expanded.width, 110.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(expanded.height, 60.0, 1e-9);
}
