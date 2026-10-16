#include "test_harness.hpp"
#include "pikdraw/geometry/curves/curve.hpp"
#include "pikdraw/geometry/curves/path.hpp"
#include "pikdraw/geometry/flattening/flattening.hpp"

#include <cmath>
#include <limits>
#include <variant>

using namespace pikdraw::geometry;

PIKDRAW_TEST(endpoint_arcs_resolve_and_flatten_analytically) {
    const auto resolved = resolve_endpoint_arc({{100.0, 0.0}, 100.0, 50.0, 0.0, false, true, {0.0, 50.0}});
    PIKDRAW_ASSERT(resolved.kind == ArcResolutionKind::Arc);
    PIKDRAW_ASSERT_NEAR(resolved.arc.point_at(0.0).x, 100.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(resolved.arc.point_at(0.0).y, 0.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(resolved.arc.point_at(1.0).x, 0.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(resolved.arc.point_at(1.0).y, 50.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(resolved.arc.point_at(0.5).x, 100.0 / std::sqrt(2.0), 1e-9);
    PIKDRAW_ASSERT_NEAR(resolved.arc.point_at(0.5).y, 50.0 / std::sqrt(2.0), 1e-9);

    Path2D path;
    path.move_to(100.0, 0.0);
    path.arc_to(100.0, 50.0, 0.0, false, true, 0.0, 50.0);
    const auto flattened = flatten_path_checked(path, {0.01, 16});
    PIKDRAW_ASSERT(flattened.status == FlatteningStatus::Success);
    PIKDRAW_ASSERT(flattened.subpaths.size() == 1);
    PIKDRAW_ASSERT(flattened.subpaths[0].points.size() > 2);
    PIKDRAW_ASSERT_NEAR(flattened.subpaths[0].points.back().x, 0.0, 1e-12);
    PIKDRAW_ASSERT_NEAR(flattened.subpaths[0].points.back().y, 50.0, 1e-12);
}

PIKDRAW_TEST(analytic_arc_bounds_and_ellipse_factory_extents) {
    const auto resolved = resolve_endpoint_arc({{10.0, 0.0}, 10.0, 5.0, 0.0, false, true, {0.0, 5.0}});
    PIKDRAW_ASSERT(resolved.kind == ArcResolutionKind::Arc);
    const Rect2D bounds = resolved.arc.bounds();
    PIKDRAW_ASSERT(bounds.left() <= 0.0);
    PIKDRAW_ASSERT(bounds.right() >= 10.0);
    PIKDRAW_ASSERT(bounds.top() <= 0.0);
    PIKDRAW_ASSERT(bounds.bottom() >= 5.0);

    const Path2D ellipse = Path2D::make_ellipse(1200.0, -300.0, 40.0, 15.0);
    const Rect2D ellipse_bounds = ellipse.bounds();
    PIKDRAW_ASSERT_NEAR(ellipse_bounds.left(), 1160.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(ellipse_bounds.right(), 1240.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(ellipse_bounds.top(), -315.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(ellipse_bounds.bottom(), -285.0, 1e-9);
}

PIKDRAW_TEST(affine_arc_roundtrip_preserves_reflection_and_shear) {
    Path2D source;
    source.move_to(100.0, 0.0);
    source.arc_to(100.0, 50.0, 17.0, false, true, -15.0, 45.0);
    const Transform2D transform{1.7, 0.35, 0.6, 0.9, -250.0, 75.0};
    const Path2D mapped = source.transformed(transform);
    const auto& element = mapped.elements()[1];
    const auto mapped_arc = resolve_endpoint_arc({
        transform.map_point(source.elements()[0].pt), element.rx, element.ry,
        element.rotation_deg, element.large_arc, element.sweep,
        transform.map_point(source.elements()[1].pt)
    });
    const auto source_arc = resolve_endpoint_arc({
        source.elements()[0].pt, source.elements()[1].rx, source.elements()[1].ry,
        source.elements()[1].rotation_deg, source.elements()[1].large_arc,
        source.elements()[1].sweep, source.elements()[1].pt
    });
    PIKDRAW_ASSERT(mapped_arc.kind == ArcResolutionKind::Arc);
    PIKDRAW_ASSERT(source_arc.kind == ArcResolutionKind::Arc);
    for (int i = 0; i <= 20; ++i) {
        const double t = static_cast<double>(i) / 20.0;
        const Point2D expected = transform.map_point(source_arc.arc.point_at(t));
        const Point2D actual = mapped_arc.arc.point_at(t);
        PIKDRAW_ASSERT_NEAR(actual.x, expected.x, 2e-8);
        PIKDRAW_ASSERT_NEAR(actual.y, expected.y, 2e-8);
    }

    const Transform2D reflection{-1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    const auto reflected = source.transformed(reflection);
    const auto& reflected_element = reflected.elements()[1];
    const auto reflected_arc = resolve_endpoint_arc({
        reflection.map_point(source.elements()[0].pt), reflected_element.rx, reflected_element.ry,
        reflected_element.rotation_deg, reflected_element.large_arc, reflected_element.sweep,
        reflection.map_point(source.elements()[1].pt)
    });
    PIKDRAW_ASSERT(reflected_arc.kind == ArcResolutionKind::Arc);
    for (int i = 0; i <= 20; ++i) {
        const double t = static_cast<double>(i) / 20.0;
        const Point2D expected = reflection.map_point(source_arc.arc.point_at(t));
        const Point2D actual = reflected_arc.arc.point_at(t);
        PIKDRAW_ASSERT_NEAR(actual.x, expected.x, 2e-8);
        PIKDRAW_ASSERT_NEAR(actual.y, expected.y, 2e-8);
    }
    const auto singular = source.transformed_checked(Transform2D::scale(1.0, 0.0));
    PIKDRAW_ASSERT(singular.status == GeometryOperationStatus::InvalidParameter);
}

PIKDRAW_TEST(bezier_split_and_length_preserve_source_curve) {
    const CubicBezier cubic{{20000.0, 0.0}, {20010.0, 18.0}, {20020.0, -18.0}, {20030.0, 0.0}};
    const auto split = split_curve(CurveSegment{cubic}, 0.37);
    PIKDRAW_ASSERT(split.status == GeometryOperationStatus::Success);
    PIKDRAW_ASSERT(split.first.has_value() && split.second.has_value());
    const auto& left = std::get<CubicBezier>(*split.first);
    const auto& right = std::get<CubicBezier>(*split.second);
    PIKDRAW_ASSERT_NEAR(left.p3.x, right.p0.x, 1e-10);
    PIKDRAW_ASSERT_NEAR(left.p3.y, right.p0.y, 1e-10);
    for (int i = 0; i <= 20; ++i) {
        const double t = static_cast<double>(i) / 20.0;
        const Point2D expected = cubic.point_at(0.37 * t);
        const Point2D actual = left.point_at(t);
        PIKDRAW_ASSERT_NEAR(actual.x, expected.x, 2e-9);
        PIKDRAW_ASSERT_NEAR(actual.y, expected.y, 2e-9);
    }
    const auto length = measure_curve_length(CurveSegment{cubic}, 1e-7, 20);
    PIKDRAW_ASSERT(length.status == GeometryOperationStatus::Success);
    PIKDRAW_ASSERT(length.length > 30.0);
    PIKDRAW_ASSERT(length.estimated_error <= 1e-7);
    const auto invalid_split = split_curve(CurveSegment{cubic}, 1.5);
    PIKDRAW_ASSERT(invalid_split.status == GeometryOperationStatus::InvalidParameter);
}

PIKDRAW_TEST(degenerate_and_large_coordinate_curves_remain_finite) {
    const CubicBezier cusp{{20000000.0, 1000.0}, {20000000.01, 1000.0},
                           {20000000.0, 1000.0}, {20000000.0, 1000.00001}};
    const Rect2D bounds = cusp.bounds();
    PIKDRAW_ASSERT(bounds.is_finite());
    const auto split = split_curve(CurveSegment{cusp}, 0.5);
    PIKDRAW_ASSERT(split.status == GeometryOperationStatus::Success);
    PIKDRAW_ASSERT(curve_point_at(CurveSegment{cusp}, 0.5).is_finite());

    PIKDRAW_ASSERT(resolve_endpoint_arc({{0.0, 0.0}, 0.0, 10.0, 0.0, false, true, {20.0, 0.0}}).kind == ArcResolutionKind::Line);
    PIKDRAW_ASSERT(resolve_endpoint_arc({{2.0, 3.0}, 10.0, 5.0, 0.0, false, true, {2.0, 3.0}}).kind == ArcResolutionKind::Empty);
    const auto invalid_arc = resolve_endpoint_arc({
        {0.0, 0.0}, std::numeric_limits<double>::quiet_NaN(), 5.0, 0.0, false, true, {1.0, 1.0}
    });
    PIKDRAW_ASSERT(invalid_arc.kind == ArcResolutionKind::Invalid);
}

PIKDRAW_TEST(flattening_reports_tolerance_and_work_budgets) {
    Path2D path;
    path.move_to(0.0, 0.0);
    path.cubic_to(0.0, 100.0, 100.0, -100.0, 100.0, 0.0);
    const auto limited = flatten_path_checked(path, {1e-12, 0});
    PIKDRAW_ASSERT(limited.status == FlatteningStatus::ToleranceNotMet);
    PIKDRAW_ASSERT(limited.element_index == 1);
    PIKDRAW_ASSERT(limited.subpaths.empty());
    PIKDRAW_ASSERT(limited.unmet_error_bound_mm > 1e-12);
    PIKDRAW_ASSERT(flatten_path_checked(path, {0.0, 8}).status == FlatteningStatus::InvalidOptions);

    Path2D over_budget;
    over_budget.move_to(0.0, 0.0);
    over_budget.line_to(1.0, 0.0);
    const auto limited_output = flatten_path_checked(over_budget, {0.01, 8, 1});
    PIKDRAW_ASSERT(limited_output.status == FlatteningStatus::WorkBudgetExceeded);
    PIKDRAW_ASSERT(limited_output.subpaths.empty());
}

PIKDRAW_TEST(close_returns_to_subpath_start_and_keeps_later_segment_separate) {
    Path2D path;
    path.move_to(0.0, 0.0);
    path.line_to(10.0, 0.0);
    path.line_to(10.0, 10.0);
    path.close();
    path.line_to(-5.0, 0.0);
    const auto flattened = flatten_path_checked(path);
    PIKDRAW_ASSERT(flattened.status == FlatteningStatus::Success);
    PIKDRAW_ASSERT(flattened.subpaths.size() == 2);
    PIKDRAW_ASSERT(flattened.subpaths[0].is_closed);
    PIKDRAW_ASSERT(!flattened.subpaths[1].is_closed);
    PIKDRAW_ASSERT(flattened.subpaths[1].points.size() == 2);
    PIKDRAW_ASSERT(flattened.subpaths[1].points[0].approx_equal({0.0, 0.0}));
    PIKDRAW_ASSERT(flattened.subpaths[1].points[1].approx_equal({-5.0, 0.0}));
}

PIKDRAW_TEST(path_checked_bounds_rejects_missing_move_without_breaking_legacy_hull) {
    Path2D invalid;
    invalid.line_to(32.0, 32.0);
    const auto checked = invalid.bounds_checked();
    PIKDRAW_ASSERT(checked.status == GeometryOperationStatus::InvalidInput);
    PIKDRAW_ASSERT(checked.element_index == 0);
    PIKDRAW_ASSERT(invalid.bounds().contains({32.0, 32.0}));
}

PIKDRAW_TEST(curve_queries_return_bounded_nearest_and_line_intersections) {
    const QuadraticBezier curve{{0.0, 0.0}, {1.0, 2.0}, {2.0, 0.0}};
    const auto nearest = nearest_point_on_curve(CurveSegment{curve}, {1.0, 1.0}, 1e-5);
    PIKDRAW_ASSERT(nearest.status == GeometryOperationStatus::Success);
    PIKDRAW_ASSERT_NEAR(nearest.parameter, 0.5, 1e-9);
    PIKDRAW_ASSERT_NEAR(nearest.distance, 0.0, 1e-9);
    PIKDRAW_ASSERT(nearest.absolute_error_bound <= 1e-5);

    const auto crossing = intersect_line_segments({{0.0, 0.0}, {10.0, 10.0}}, {{0.0, 10.0}, {10.0, 0.0}});
    PIKDRAW_ASSERT(crossing.kind == IntersectionKind::Point);
    PIKDRAW_ASSERT_NEAR(crossing.first.x, 5.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(crossing.first.y, 5.0, 1e-9);
    const auto overlap = intersect_line_segments({{0.0, 0.0}, {10.0, 0.0}}, {{5.0, 0.0}, {15.0, 0.0}});
    PIKDRAW_ASSERT(overlap.kind == IntersectionKind::Overlap);
    PIKDRAW_ASSERT_NEAR(overlap.first.x, 5.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(overlap.second.x, 10.0, 1e-9);
    const auto invalid = intersect_line_segments({{0.0, 0.0}, {1.0, 0.0}}, {{0.0, 1.0}, {1.0, 1.0}}, 0.0);
    PIKDRAW_ASSERT(invalid.kind == IntersectionKind::InvalidInput);
}
