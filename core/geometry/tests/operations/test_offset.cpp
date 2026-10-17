#include "test_harness.hpp"
#include "pikdraw/geometry/operations/offset.hpp"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace pikdraw::geometry;

PIKDRAW_TEST(contour_offset_ac_c01_rectangle) {
    // Rectangle 100x60 mm
    Path2D rect = Path2D::make_rect(10.0, 20.0, 100.0, 60.0);

    // Outside 10 mm Miter
    ContourRequest req_out;
    req_out.path = rect;
    req_out.direction = ContourDirection::Outside;
    req_out.spacing_mm = 10.0;
    req_out.steps = 1;
    req_out.join_type = JoinType::Miter;

    const auto res_out = compute_contour(req_out);
    PIKDRAW_ASSERT(res_out.status == OffsetStatus::Success);
    PIKDRAW_ASSERT(res_out.steps.size() == 1);
    PIKDRAW_ASSERT(!res_out.steps[0].is_empty);

    const auto b_out = res_out.steps[0].combined_path.bounds();
    PIKDRAW_ASSERT_NEAR(b_out.x, 0.0, 1e-6);
    PIKDRAW_ASSERT_NEAR(b_out.y, 10.0, 1e-6);
    PIKDRAW_ASSERT_NEAR(b_out.width, 120.0, 1e-6);
    PIKDRAW_ASSERT_NEAR(b_out.height, 80.0, 1e-6);

    // Inside 30 mm (collapses 60 - 2*30 = 0) -> Empty
    ContourRequest req_in;
    req_in.path = rect;
    req_in.direction = ContourDirection::Inside;
    req_in.spacing_mm = 30.0;
    req_in.steps = 1;
    req_in.join_type = JoinType::Miter;

    const auto res_in = compute_contour(req_in);
    PIKDRAW_ASSERT(res_in.status == OffsetStatus::Empty);
    PIKDRAW_ASSERT(res_in.steps.size() == 1);
    PIKDRAW_ASSERT(res_in.steps[0].is_empty);
}

PIKDRAW_TEST(contour_offset_ac_c02_star_round) {
    // 5-point star (10 vertices), outer radius 100, inner radius 40
    Path2D star;
    const int num_points = 5;
    for (int i = 0; i < 2 * num_points; ++i) {
        const double angle = i * M_PI / num_points - M_PI / 2.0;
        const double r = (i % 2 == 0) ? 100.0 : 40.0;
        const double x = r * std::cos(angle);
        const double y = r * std::sin(angle);
        if (i == 0) star.move_to(x, y);
        else star.line_to(x, y);
    }
    star.close();

    ContourRequest req;
    req.path = star;
    req.direction = ContourDirection::Outside;
    req.spacing_mm = 20.0;
    req.steps = 1;
    req.join_type = JoinType::Round;
    req.arc_tolerance_mm = 0.005;

    const auto res = compute_contour(req);
    PIKDRAW_ASSERT(res.status == OffsetStatus::Success);
    PIKDRAW_ASSERT(res.steps.size() == 1);
    PIKDRAW_ASSERT(!res.steps[0].is_empty);

    const auto bounds = res.steps[0].combined_path.bounds();
    // Tip at (0, -100) expands outward by 20 to y = -120
    PIKDRAW_ASSERT_NEAR(bounds.top(), -120.0, 0.1);
    PIKDRAW_ASSERT(bounds.width > 200.0);
    PIKDRAW_ASSERT(bounds.height > 200.0);
}

PIKDRAW_TEST(contour_offset_ac_c03_donut) {
    // Donut: outer circle R=50, hole circle R=10
    Path2D donut;
    // Outer circle CCW (32 segments)
    const int segs = 32;
    for (int i = 0; i < segs; ++i) {
        const double angle = 2.0 * M_PI * i / segs;
        const double x = 50.0 * std::cos(angle);
        const double y = 50.0 * std::sin(angle);
        if (i == 0) donut.move_to(x, y);
        else donut.line_to(x, y);
    }
    donut.close();

    // Hole circle CW
    for (int i = 0; i < segs; ++i) {
        const double angle = -2.0 * M_PI * i / segs;
        const double x = 10.0 * std::cos(angle);
        const double y = 10.0 * std::sin(angle);
        if (i == 0) donut.move_to(x, y);
        else donut.line_to(x, y);
    }
    donut.close();

    // Outside 2 mm: outer R50 -> R52, hole R10 -> R8
    ContourRequest req_out;
    req_out.path = donut;
    req_out.direction = ContourDirection::Outside;
    req_out.spacing_mm = 2.0;
    req_out.steps = 1;
    req_out.join_type = JoinType::Miter;

    const auto res_out = compute_contour(req_out);
    PIKDRAW_ASSERT(res_out.status == OffsetStatus::Success);
    PIKDRAW_ASSERT(res_out.steps.size() == 1);
    PIKDRAW_ASSERT(res_out.steps[0].loops.size() == 2);

    const auto outer_bounds = res_out.steps[0].loops[0].bounds();
    const auto hole_bounds = res_out.steps[0].loops[1].bounds();
    PIKDRAW_ASSERT_NEAR(outer_bounds.width, 104.0, 0.5); // 2 * 52
    PIKDRAW_ASSERT_NEAR(hole_bounds.width, 16.0, 0.5);   // 2 * 8

    // Inside 2 mm: outer R50 -> R48, hole R10 -> R12
    ContourRequest req_in;
    req_in.path = donut;
    req_in.direction = ContourDirection::Inside;
    req_in.spacing_mm = 2.0;
    req_in.steps = 1;
    req_in.join_type = JoinType::Miter;

    const auto res_in = compute_contour(req_in);
    PIKDRAW_ASSERT(res_in.status == OffsetStatus::Success);
    PIKDRAW_ASSERT(res_in.steps.size() == 1);
    PIKDRAW_ASSERT(res_in.steps[0].loops.size() == 2);

    const auto in_outer_bounds = res_in.steps[0].loops[0].bounds();
    const auto in_hole_bounds = res_in.steps[0].loops[1].bounds();
    PIKDRAW_ASSERT_NEAR(in_outer_bounds.width, 96.0, 0.5); // 2 * 48
    PIKDRAW_ASSERT_NEAR(in_hole_bounds.width, 24.0, 0.5);  // 2 * 12
}

PIKDRAW_TEST(contour_offset_ac_c04_acute_bevel_and_bowtie_reject) {
    // 1. Acute corner with miter limit 2 -> fallback to Bevel
    Path2D triangle;
    triangle.move_to(0.0, 0.0);
    triangle.line_to(100.0, 10.0);
    triangle.line_to(100.0, -10.0);
    triangle.close();

    ContourRequest req_acute;
    req_acute.path = triangle;
    req_acute.direction = ContourDirection::Outside;
    req_acute.spacing_mm = 5.0;
    req_acute.steps = 1;
    req_acute.join_type = JoinType::Miter;
    req_acute.miter_limit = 2.0; // Very acute corner at (0,0) will exceed 2.0 and fallback to bevel

    const auto res_acute = compute_contour(req_acute);
    PIKDRAW_ASSERT(res_acute.status == OffsetStatus::Success);
    PIKDRAW_ASSERT(res_acute.steps.size() == 1);
    // Tip at (0,0) has beveled points instead of extending far to the left
    const auto bounds = res_acute.steps[0].combined_path.bounds();
    PIKDRAW_ASSERT(bounds.x > -20.0); // If miter was not limited, it would shoot out past -50

    // 2. Self-crossing bow-tie -> rejected
    Path2D bowtie;
    bowtie.move_to(0.0, 0.0);
    bowtie.line_to(50.0, 50.0);
    bowtie.line_to(0.0, 50.0);
    bowtie.line_to(50.0, 0.0);
    bowtie.close(); // Edges cross strictly in the middle

    ContourRequest req_bowtie;
    req_bowtie.path = bowtie;
    req_bowtie.direction = ContourDirection::Outside;
    req_bowtie.spacing_mm = 2.0;

    const auto res_bowtie = compute_contour(req_bowtie);
    PIKDRAW_ASSERT(res_bowtie.status == OffsetStatus::InvalidInput);
    PIKDRAW_ASSERT(res_bowtie.error_message != nullptr);
}

PIKDRAW_TEST(contour_offset_ac_c05_multi_step) {
    Path2D rect = Path2D::make_rect(0.0, 0.0, 100.0, 100.0);

    ContourRequest req;
    req.path = rect;
    req.direction = ContourDirection::Outside;
    req.spacing_mm = 2.0;
    req.steps = 3;
    req.join_type = JoinType::Miter;

    const auto res = compute_contour(req);
    PIKDRAW_ASSERT(res.status == OffsetStatus::Success);
    PIKDRAW_ASSERT(res.steps.size() == 3);

    // Step 1: d = 2mm -> 104 x 104
    PIKDRAW_ASSERT_NEAR(res.steps[0].distance_mm, 2.0, 1e-6);
    PIKDRAW_ASSERT_NEAR(res.steps[0].combined_path.bounds().width, 104.0, 1e-6);

    // Step 2: d = 4mm -> 108 x 108
    PIKDRAW_ASSERT_NEAR(res.steps[1].distance_mm, 4.0, 1e-6);
    PIKDRAW_ASSERT_NEAR(res.steps[1].combined_path.bounds().width, 108.0, 1e-6);

    // Step 3: d = 6mm -> 112 x 112
    PIKDRAW_ASSERT_NEAR(res.steps[2].distance_mm, 6.0, 1e-6);
    PIKDRAW_ASSERT_NEAR(res.steps[2].combined_path.bounds().width, 112.0, 1e-6);
}

PIKDRAW_TEST(contour_offset_ac_c06_to_center_bounded) {
    // Narrow rectangle 100 x 10 mm
    Path2D narrow = Path2D::make_rect(0.0, 0.0, 100.0, 10.0);

    ContourRequest req;
    req.path = narrow;
    req.direction = ContourDirection::ToCenter;
    req.spacing_mm = 1.0; // Collapses when 2*k >= 10, so k = 5 steps: steps 1, 2, 3, 4 valid
    req.join_type = JoinType::Miter;

    const auto res = compute_contour(req);
    PIKDRAW_ASSERT(res.status == OffsetStatus::Success);
    PIKDRAW_ASSERT(res.steps.size() == 4);
    PIKDRAW_ASSERT(!res.truncated);

    // Step 4 width = 100 - 8 = 92, height = 10 - 8 = 2
    PIKDRAW_ASSERT_NEAR(res.steps[3].combined_path.bounds().width, 92.0, 1e-6);
    PIKDRAW_ASSERT_NEAR(res.steps[3].combined_path.bounds().height, 2.0, 1e-6);
}
