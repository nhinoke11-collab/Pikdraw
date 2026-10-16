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

PIKDRAW_TEST(boolean_rectangle_union_and_back_minus) {
    Rect2D r1{0.0, 0.0, 100.0, 100.0};
    Rect2D r2{50.0, 0.0, 100.0, 100.0};

    auto res_u = boolean_rectangles(r1, r2, BooleanOp::Union);
    PIKDRAW_ASSERT(res_u.success);
    PIKDRAW_ASSERT(!res_u.path.empty());

    auto res_bm = boolean_rectangles(r1, r2, BooleanOp::BackMinusFront);
    PIKDRAW_ASSERT(res_bm.success);
    PIKDRAW_ASSERT(!res_bm.rectangles.empty());
    double bm_area = 0.0;
    for (const auto& r : res_bm.rectangles) bm_area += r.width * r.height;
    PIKDRAW_ASSERT_NEAR(bm_area, 5000.0, 1e-6);
}

PIKDRAW_TEST(split_rectangle_by_vertical_line) {
    Rect2D r{0.0, 0.0, 100.0, 100.0};
    Point2D p1{40.0, -10.0};
    Point2D p2{40.0, 110.0};

    auto [ok, pieces] = split_rect_by_line(r, p1, p2);
    PIKDRAW_ASSERT(ok);
    PIKDRAW_ASSERT(pieces.size() == 2);
    PIKDRAW_ASSERT(!pieces[0].empty());
    PIKDRAW_ASSERT(!pieces[1].empty());

    auto b1 = pieces[0].bounds();
    auto b2 = pieces[1].bounds();
    // One piece has width 60, one has width 40
    double w1 = std::min(b1.width, b2.width);
    double w2 = std::max(b1.width, b2.width);
    PIKDRAW_ASSERT_NEAR(w1, 40.0, 1e-6);
    PIKDRAW_ASSERT_NEAR(w2, 60.0, 1e-6);
}

