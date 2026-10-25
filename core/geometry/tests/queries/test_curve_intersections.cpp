#include "test_harness.hpp"
#include "pikdraw/geometry/queries/curve_intersections.hpp"
#include "pikdraw/geometry/queries/aabb_index.hpp"
#include "pikdraw/geometry/curves/curve.hpp"

#include <cmath>
#include <limits>
#include <utility>
#include <vector>
#include <iostream>

using namespace pikdraw::geometry;

constexpr double kPi = 3.141592653589793238462643383279502884;

PIKDRAW_TEST(analytic_center_and_quadrant_points) {
    // Circle at (100, 200) with radius 50
    EllipticArc full_circle{
        Point2D{100.0, 200.0},
        Point2D{50.0, 0.0},
        Point2D{0.0, 50.0},
        0.0,
        2.0 * kPi
    };

    Point2D center = find_analytic_center(full_circle);
    PIKDRAW_ASSERT_NEAR(center.x, 100.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(center.y, 200.0, 1e-9);

    auto q_res = find_quadrant_points(full_circle);
    PIKDRAW_ASSERT(q_res.status == CurveQueryStatus::ExactForPrimitive);
    PIKDRAW_ASSERT(q_res.points.size() == 4);

    // Angles: 0 -> (150, 200), pi/2 -> (100, 250), pi -> (50, 200), 3pi/2 -> (100, 150)
    PIKDRAW_ASSERT_NEAR(q_res.points[0].point.x, 150.0, 1e-6);
    PIKDRAW_ASSERT_NEAR(q_res.points[0].point.y, 200.0, 1e-6);
    PIKDRAW_ASSERT_NEAR(q_res.points[1].point.x, 100.0, 1e-6);
    PIKDRAW_ASSERT_NEAR(q_res.points[1].point.y, 250.0, 1e-6);
    PIKDRAW_ASSERT_NEAR(q_res.points[2].point.x, 50.0, 1e-6);
    PIKDRAW_ASSERT_NEAR(q_res.points[2].point.y, 200.0, 1e-6);
    PIKDRAW_ASSERT_NEAR(q_res.points[3].point.x, 100.0, 1e-6);
    PIKDRAW_ASSERT_NEAR(q_res.points[3].point.y, 150.0, 1e-6);

    // Partial arc: 0 to 3pi/4 (only 0 and pi/2 should be present)
    EllipticArc partial_arc{
        Point2D{100.0, 200.0},
        Point2D{50.0, 0.0},
        Point2D{0.0, 50.0},
        0.0,
        3.0 * kPi / 4.0
    };
    auto partial_q = find_quadrant_points(partial_arc);
    PIKDRAW_ASSERT(partial_q.status == CurveQueryStatus::ExactForPrimitive);
    PIKDRAW_ASSERT(partial_q.points.size() == 2);
    PIKDRAW_ASSERT_NEAR(partial_q.points[0].point.x, 150.0, 1e-6);
    PIKDRAW_ASSERT_NEAR(partial_q.points[1].point.x, 100.0, 1e-6);
}

PIKDRAW_TEST(ac_s02_t_mid_vs_half_curve_length) {
    // Asymmetric cubic bezier with non-uniform velocity
    CubicBezier asymmetric_cubic{
        Point2D{0.0, 0.0},
        Point2D{5.0, 0.0},
        Point2D{10.0, 0.0},
        Point2D{100.0, 60.0}
    };

    Point2D t_mid_point = asymmetric_cubic.point_at(0.5);

    auto half_len = find_half_curve_length(CurveSegment{asymmetric_cubic});
    PIKDRAW_ASSERT(half_len.status == CurveQueryStatus::BoundedApproximation);
    PIKDRAW_ASSERT(half_len.residual_mm <= 1e-4);
    // Because velocity is much higher in the second half, t_half should be significantly > 0.5
    PIKDRAW_ASSERT(half_len.parameter > 0.55);
    PIKDRAW_ASSERT(std::abs(half_len.point.x - t_mid_point.x) > 1.0);
}

PIKDRAW_TEST(ac_s04_curve_intersections) {
    // 1. Line-Line intersection
    LineSegment l1{Point2D{0.0, 0.0}, Point2D{100.0, 100.0}};
    LineSegment l2{Point2D{0.0, 100.0}, Point2D{100.0, 0.0}};
    auto res_ll = find_curve_intersections(CurveSegment{l1}, CurveSegment{l2});
    PIKDRAW_ASSERT(res_ll.status == CurveQueryStatus::ExactForPrimitive);
    PIKDRAW_ASSERT(res_ll.points.size() == 1);
    PIKDRAW_ASSERT_NEAR(res_ll.points[0].point.x, 50.0, 1e-6);
    PIKDRAW_ASSERT_NEAR(res_ll.points[0].point.y, 50.0, 1e-6);

    // 2. Line-Line overlap
    LineSegment l3{Point2D{0.0, 0.0}, Point2D{100.0, 0.0}};
    LineSegment l4{Point2D{50.0, 0.0}, Point2D{150.0, 0.0}};
    auto res_overlap = find_curve_intersections(CurveSegment{l3}, CurveSegment{l4});
    PIKDRAW_ASSERT(res_overlap.status == CurveQueryStatus::Overlap);

    // 3. Line-Arc intersection
    EllipticArc circle{Point2D{0.0, 0.0}, Point2D{50.0, 0.0}, Point2D{0.0, 50.0}, 0.0, 2.0 * kPi};
    LineSegment hline{Point2D{-100.0, 30.0}, Point2D{100.0, 30.0}};
    // Circle x^2 + y^2 = 50^2 = 2500. y = 30 -> x^2 = 2500 - 900 = 1600 -> x = +/- 40
    auto res_la = find_curve_intersections(CurveSegment{hline}, CurveSegment{circle});
    PIKDRAW_ASSERT(res_la.status == CurveQueryStatus::BoundedApproximation);
    PIKDRAW_ASSERT(res_la.points.size() == 2);
    PIKDRAW_ASSERT_NEAR(res_la.points[0].point.y, 30.0, 1e-4);
    PIKDRAW_ASSERT_NEAR(res_la.points[1].point.y, 30.0, 1e-4);
    double x1 = res_la.points[0].point.x;
    double x2 = res_la.points[1].point.x;
    PIKDRAW_ASSERT_NEAR(std::min(x1, x2), -40.0, 1e-3);
    PIKDRAW_ASSERT_NEAR(std::max(x1, x2), 40.0, 1e-3);

    // 4. Arc-Arc intersection
    EllipticArc c1{Point2D{0.0, 0.0}, Point2D{50.0, 0.0}, Point2D{0.0, 50.0}, 0.0, 2.0 * kPi};
    EllipticArc c2{Point2D{60.0, 0.0}, Point2D{50.0, 0.0}, Point2D{0.0, 50.0}, 0.0, 2.0 * kPi};
    // x^2 + y^2 = 2500 and (x-60)^2 + y^2 = 2500 -> -120x + 3600 = 0 -> x = 30, y = +/- 40
    auto res_aa = find_curve_intersections(CurveSegment{c1}, CurveSegment{c2});
    PIKDRAW_ASSERT(res_aa.status == CurveQueryStatus::BoundedApproximation);
    PIKDRAW_ASSERT(res_aa.points.size() == 2);
    for (const auto& pt : res_aa.points) {
        PIKDRAW_ASSERT_NEAR(pt.point.x, 30.0, 1e-3);
        PIKDRAW_ASSERT_NEAR(std::abs(pt.point.y), 40.0, 1e-3);
        PIKDRAW_ASSERT(pt.residual_mm <= 1e-4);
    }
}

PIKDRAW_TEST(ac_s05_perpendicular_and_tangent) {
    // Circle at (0,0) radius 10
    EllipticArc circle{Point2D{0.0, 0.0}, Point2D{10.0, 0.0}, Point2D{0.0, 10.0}, 0.0, 2.0 * kPi};
    Point2D anchor{0.0, 20.0};

    // Perpendicular from (0, 20) to circle: nearest is (0, 10), farthest is (0, -10)
    auto perp_res = find_perpendicular_points(CurveSegment{circle}, anchor);
    PIKDRAW_ASSERT(perp_res.status == CurveQueryStatus::BoundedApproximation);
    PIKDRAW_ASSERT(perp_res.points.size() >= 2);
    bool found_top = false;
    for (const auto& p : perp_res.points) {
        if (std::abs(p.point.x) < 1e-3 && std::abs(p.point.y - 10.0) < 1e-3) {
            found_top = true;
            PIKDRAW_ASSERT(p.residual_mm <= 1e-4);
        }
    }
    PIKDRAW_ASSERT(found_top);

    // Tangent from (0, 20) to circle radius 10:
    // Distance from center is 20, radius 10.
    // Tangent length is sqrt(20^2 - 10^2) = sqrt(300) = 10*sqrt(3) ~= 17.3205.
    // Contact points are (+/- 5*sqrt(3), 5) = (+/- 8.66025, 5).
    auto tan_res = find_tangent_points(CurveSegment{circle}, anchor);
    PIKDRAW_ASSERT(tan_res.status == CurveQueryStatus::BoundedApproximation);
    PIKDRAW_ASSERT(tan_res.points.size() == 2);
    for (const auto& p : tan_res.points) {
        PIKDRAW_ASSERT_NEAR(p.point.y, 5.0, 1e-2);
        PIKDRAW_ASSERT_NEAR(std::abs(p.point.x), 5.0 * std::sqrt(3.0), 1e-2);
        PIKDRAW_ASSERT(p.residual_mm <= 1e-4);
    }
}

PIKDRAW_TEST(aabb_index_is_conservative_and_returns_painter_order) {
    std::vector<AabbIndexEntry> source;
    source.reserve(1003);
    for (uint32_t i = 0; i < 1000; ++i) {
        const double x = static_cast<double>(i % 100) * 20.0;
        const double y = static_cast<double>(i / 100) * 20.0;
        source.push_back({static_cast<uint64_t>(i + 1), {x, y, 5.0, 5.0}, i});
    }
    source.push_back({2001, {20.0, 0.0, 5.0, 5.0}, 2001});
    source.push_back({2002, {22.0, 4.0, 0.0, 5.0}, 2002});
    source.push_back({2003, {std::numeric_limits<double>::quiet_NaN(), 0.0, 5.0, 5.0}, 2003});

    AabbIndex index;
    PIKDRAW_ASSERT(index.rebuild(std::move(source)));
    PIKDRAW_ASSERT(index.size() == 1003);
    const auto matches = index.query({19.0, 0.0, 7.0, 6.0});
    PIKDRAW_ASSERT(matches.size() == 4);
    PIKDRAW_ASSERT(matches[0].id == 2);
    PIKDRAW_ASSERT(matches[1].id == 2001);
    PIKDRAW_ASSERT(matches[2].id == 2002);
    PIKDRAW_ASSERT(matches[3].id == 2003);

    const auto remote = index.query({9000.0, 9000.0, 10.0, 10.0});
    PIKDRAW_ASSERT(remote.size() == 1);
    PIKDRAW_ASSERT(remote.front().id == 2003);
}
