#include "test_harness.hpp"
#include "pikdraw/geometry/operations/align.hpp"
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

PIKDRAW_TEST(distribution_orders_items_by_the_distributed_coordinate) {
    // Unequal sizes make left/top order differ from center/right/bottom order.
    const std::vector<ObjectAlignmentItem> items{
        {1, {0.0, 0.0, 100.0, 100.0}},
        {2, {20.0, 20.0, 10.0, 120.0}},
        {3, {80.0, 80.0, 20.0, 10.0}}
    };
    const auto delta = [](const std::vector<AlignmentDelta>& deltas, uint64_t id) {
        for (const auto& value : deltas)
            if (value.id == id) return value;
        return AlignmentDelta{};
    };

    // Leading edges remain ordered by their own coordinates.
    const auto horizontal_left = compute_distribution(items, DistributeMode::LeftEdges);
    PIKDRAW_ASSERT_NEAR(delta(horizontal_left, 1).dx_mm, 0.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(delta(horizontal_left, 2).dx_mm, 20.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(delta(horizontal_left, 3).dx_mm, 0.0, 1e-9);

    // AC-DIST-01 — centers are ordered by center, even when left-edge order differs.
    const auto horizontal_centers = compute_distribution(items, DistributeMode::CentersHorizontal);
    PIKDRAW_ASSERT(horizontal_centers.size() == 3);
    PIKDRAW_ASSERT_NEAR(delta(horizontal_centers, 1).dx_mm, 7.5, 1e-9);
    PIKDRAW_ASSERT_NEAR(delta(horizontal_centers, 2).dx_mm, 0.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(delta(horizontal_centers, 3).dx_mm, 0.0, 1e-9);

    // Right edges are sorted by their right edge, not by each object's left edge.
    const auto horizontal_right = compute_distribution(items, DistributeMode::RightEdges);
    const double right_a = items[0].bounds.right() + delta(horizontal_right, 1).dx_mm;
    const double right_b = items[1].bounds.right() + delta(horizontal_right, 2).dx_mm;
    const double right_c = items[2].bounds.right() + delta(horizontal_right, 3).dx_mm;
    PIKDRAW_ASSERT_NEAR(right_a - right_b, right_c - right_a, 1e-9);

    // Vertical center ordering follows center Y; bottom-edge ordering follows bottom Y.
    const std::vector<ObjectAlignmentItem> vertical_items{
        {1, {0.0, 0.0, 10.0, 100.0}},
        {2, {20.0, 20.0, 120.0, 10.0}},
        {3, {80.0, 80.0, 20.0, 120.0}}
    };
    const auto vertical_top = compute_distribution(vertical_items, DistributeMode::TopEdges);
    PIKDRAW_ASSERT_NEAR(delta(vertical_top, 1).dy_mm, 0.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(delta(vertical_top, 2).dy_mm, 20.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(delta(vertical_top, 3).dy_mm, 0.0, 1e-9);

    const auto vertical_centers = compute_distribution(vertical_items, DistributeMode::CentersVertical);
    PIKDRAW_ASSERT_NEAR(delta(vertical_centers, 1).dy_mm, 32.5, 1e-9);
    PIKDRAW_ASSERT_NEAR(delta(vertical_centers, 2).dy_mm, 0.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(delta(vertical_centers, 3).dy_mm, 0.0, 1e-9);

    const auto vertical_bottom = compute_distribution(vertical_items, DistributeMode::BottomEdges);
    const double bottom_a = vertical_items[0].bounds.bottom() + delta(vertical_bottom, 1).dy_mm;
    const double bottom_b = vertical_items[1].bounds.bottom() + delta(vertical_bottom, 2).dy_mm;
    const double bottom_c = vertical_items[2].bounds.bottom() + delta(vertical_bottom, 3).dy_mm;
    PIKDRAW_ASSERT_NEAR(bottom_a - bottom_c, bottom_b - bottom_a, 1e-9);

    const std::vector<ObjectAlignmentItem> gap_items{
        {1, {0.0, 0.0, 10.0, 10.0}},
        {2, {25.0, 25.0, 10.0, 10.0}},
        {3, {70.0, 70.0, 10.0, 10.0}}
    };
    const auto horizontal_gap = compute_distribution(gap_items, DistributeMode::SpacingHorizontal);
    const auto vertical_gap = compute_distribution(gap_items, DistributeMode::SpacingVertical);
    PIKDRAW_ASSERT_NEAR(delta(horizontal_gap, 2).dx_mm, 10.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(delta(vertical_gap, 2).dy_mm, 10.0, 1e-9);
}

PIKDRAW_TEST(boolean_rectangle_union_and_back_minus) {
    Rect2D r1{0.0, 0.0, 100.0, 100.0};
    Rect2D r2{50.0, 0.0, 100.0, 100.0};

    auto res_u = boolean_rectangles(r1, r2, BooleanOp::Union);
    PIKDRAW_ASSERT(res_u.success);
    PIKDRAW_ASSERT(!res_u.path.empty());
    const auto union_bounds = res_u.path.bounds();
    PIKDRAW_ASSERT_NEAR(union_bounds.x, 0.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(union_bounds.width, 150.0, 1e-9);
    size_t union_contours = 0;
    for (const auto& element : res_u.path.elements())
        if (element.verb == PathVerb::MoveTo) ++union_contours;
    PIKDRAW_ASSERT(union_contours == 1);

    auto res_bm = boolean_rectangles(r1, r2, BooleanOp::BackMinusFront);
    PIKDRAW_ASSERT(res_bm.success);
    PIKDRAW_ASSERT(!res_bm.rectangles.empty());
    PIKDRAW_ASSERT_NEAR(res_bm.path.bounds().x, 100.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(res_bm.path.bounds().width, 50.0, 1e-9);
    double bm_area = 0.0;
    for (const auto& r : res_bm.rectangles) bm_area += r.width * r.height;
    PIKDRAW_ASSERT_NEAR(bm_area, 5000.0, 1e-6);
}

PIKDRAW_TEST(boolean_rectangle_union_traces_touching_and_disjoint_regions) {
    const auto touching = boolean_rectangles(
        {0.0, 0.0, 10.0, 10.0}, {10.0, 0.0, 10.0, 10.0}, BooleanOp::Union);
    PIKDRAW_ASSERT(touching.success);
    size_t touching_contours = 0;
    for (const auto& element : touching.path.elements())
        if (element.verb == PathVerb::MoveTo) ++touching_contours;
    PIKDRAW_ASSERT(touching_contours == 1);
    PIKDRAW_ASSERT_NEAR(touching.path.bounds().width, 20.0, 1e-9);

    const auto disjoint = boolean_rectangles(
        {0.0, 0.0, 10.0, 10.0}, {20.0, 0.0, 10.0, 10.0}, BooleanOp::Union);
    PIKDRAW_ASSERT(disjoint.success);
    size_t disjoint_contours = 0;
    for (const auto& element : disjoint.path.elements())
        if (element.verb == PathVerb::MoveTo) ++disjoint_contours;
    PIKDRAW_ASSERT(disjoint_contours == 2);
    PIKDRAW_ASSERT_NEAR(disjoint.path.bounds().width, 30.0, 1e-9);
}

PIKDRAW_TEST(boolean_rectangle_difference_traces_outer_and_hole_contours) {
    const auto result = boolean_rectangles(
        {0.0, 0.0, 100.0, 100.0}, {25.0, 25.0, 50.0, 50.0}, BooleanOp::Difference);
    PIKDRAW_ASSERT(result.success);
    size_t contours = 0;
    for (const auto& element : result.path.elements())
        if (element.verb == PathVerb::MoveTo) ++contours;
    PIKDRAW_ASSERT(contours == 2);
    double area = 0.0;
    for (const auto& rectangle : result.rectangles) area += rectangle.width * rectangle.height;
    PIKDRAW_ASSERT_NEAR(area, 7500.0, 1e-6);
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

