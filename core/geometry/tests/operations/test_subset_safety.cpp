#include "test_harness.hpp"
#include "pikdraw/geometry/operations/boolean.hpp"
#include "pikdraw/geometry/operations/offset.hpp"
#include <cmath>
#include <limits>
#include <vector>

using namespace pikdraw::geometry;

namespace {
double signed_area(const Path2D& path) {
    double total = 0.0;
    Point2D first{};
    Point2D previous{};
    bool active = false;
    for (const auto& element : path.elements()) {
        if (element.verb == PathVerb::MoveTo) {
            first = previous = element.pt;
            active = true;
        } else if (element.verb == PathVerb::LineTo && active) {
            total += previous.x * element.pt.y - element.pt.x * previous.y;
            previous = element.pt;
        } else if (element.verb == PathVerb::Close && active) {
            total += previous.x * first.y - first.x * previous.y;
            active = false;
        }
    }
    return total * 0.5;
}
}

PIKDRAW_TEST(general_path_boolean_uses_bounded_polygon_backend) {
    Path2D triangle;
    triangle.move_to(0, 0);
    triangle.line_to(10, 0);
    triangle.line_to(0, 10);
    triangle.close();
    const auto rectangle = Path2D::make_rect(0, 0, 10, 10);
    const auto ellipse = Path2D::make_ellipse(5, 5, 5, 5);
    const auto triangle_result = boolean_paths(triangle, rectangle, BooleanOp::Intersection);
    PIKDRAW_ASSERT(triangle_result.success && triangle_result.status == BooleanStatus::Success);
    PIKDRAW_ASSERT_NEAR(std::abs(signed_area(triangle_result.path)), 50.0, 1e-5);
    PIKDRAW_ASSERT(triangle_result.approximation_error_bound_mm > 0.01);

    const auto ellipse_result = boolean_paths(ellipse, rectangle, BooleanOp::Intersection);
    PIKDRAW_ASSERT(ellipse_result.success && ellipse_result.status == BooleanStatus::Success);
    PIKDRAW_ASSERT(!ellipse_result.path.empty());
    PIKDRAW_ASSERT(ellipse_result.approximation_error_bound_mm < 0.011);

    OffsetOptions options;
    options.distance_mm = 1;
    const auto offset = offset_path(ellipse, options);
    PIKDRAW_ASSERT(!offset.success && offset.path.empty() && offset.error_message);

    const auto exact = boolean_paths(rectangle, Path2D::make_rect(5, 0, 10, 10), BooleanOp::Union);
    PIKDRAW_ASSERT(exact.success && exact.status == BooleanStatus::Success);
    PIKDRAW_ASSERT(exact.approximation_error_bound_mm == 0.0);
}

PIKDRAW_TEST(general_path_boolean_preserves_even_odd_holes_and_large_origin) {
    Path2D ring;
    ring.set_winding_rule(WindingRule::EvenOdd);
    ring.move_to(900000, 900000);
    ring.line_to(900100, 900000);
    ring.line_to(900100, 900100);
    ring.line_to(900000, 900100);
    ring.close();
    ring.move_to(900025, 900025);
    ring.line_to(900075, 900025);
    ring.line_to(900075, 900075);
    ring.line_to(900025, 900075);
    ring.close();

    const auto covering = Path2D::make_rect(899990, 899990, 120, 120);
    const auto result = boolean_paths(ring, covering, BooleanOp::Intersection);
    PIKDRAW_ASSERT(result.success && result.status == BooleanStatus::Success);
    PIKDRAW_ASSERT(result.path.winding_rule() == WindingRule::NonZero);
    PIKDRAW_ASSERT_NEAR(std::abs(signed_area(result.path)), 7500.0, 0.01);
    size_t contours = 0;
    for (const auto& element : result.path.elements())
        if (element.verb == PathVerb::MoveTo) ++contours;
    PIKDRAW_ASSERT(contours == 2);
}

PIKDRAW_TEST(general_path_boolean_reports_empty_and_budget_failures) {
    const auto first = Path2D::make_rect(0, 0, 10, 10);
    const auto disjoint = Path2D::make_rect(20, 0, 10, 10);
    const auto empty = boolean_paths(first, disjoint, BooleanOp::Intersection);
    PIKDRAW_ASSERT(empty.success && empty.status == BooleanStatus::Empty && empty.path.empty());

    Path2D triangle;
    triangle.move_to(0, 0);
    triangle.line_to(10, 0);
    triangle.line_to(0, 10);
    triangle.close();
    BooleanOptions limited;
    limited.max_output_points = 2;
    const auto over_budget = boolean_paths(triangle, first, BooleanOp::Union, limited);
    PIKDRAW_ASSERT(!over_budget.success &&
                   over_budget.status == BooleanStatus::WorkBudgetExceeded);

    BooleanOptions impossible_tolerance;
    impossible_tolerance.flatten_tolerance_mm = 1e-12;
    impossible_tolerance.max_subdivision_depth = 1;
    const auto ellipse = Path2D::make_ellipse(5, 5, 5, 5);
    const auto tolerance_failure = boolean_paths(ellipse, first, BooleanOp::Intersection,
                                                  impossible_tolerance);
    PIKDRAW_ASSERT(!tolerance_failure.success &&
                   tolerance_failure.status == BooleanStatus::ToleranceNotMet);
}

PIKDRAW_TEST(boolean_boundary_is_not_misreported_as_union) {
    const auto result = boolean_rectangles(
        {0, 0, 10, 10}, {5, 0, 10, 10}, BooleanOp::Boundary);
    PIKDRAW_ASSERT(!result.success);
    PIKDRAW_ASSERT(result.status == BooleanStatus::Unsupported);
    PIKDRAW_ASSERT(result.path.empty());
    PIKDRAW_ASSERT(result.error_message != nullptr);
}

PIKDRAW_TEST(offset_options_and_collapsed_inset) {
    const auto rectangle = Path2D::make_rect(0, 0, 10, 10);
    OffsetOptions options;
    options.distance_mm = -6;
    const auto inset = offset_path(rectangle, options);
    PIKDRAW_ASSERT(inset.success && inset.path.empty());
    options.distance_mm = 1;
    options.join_type = JoinType::Round;
    PIKDRAW_ASSERT(!offset_path(rectangle, options).success);
    options.join_type = JoinType::Miter;
    options.distance_mm = std::numeric_limits<double>::quiet_NaN();
    PIKDRAW_ASSERT(!offset_path(rectangle, options).success);
    const auto invalid = boolean_rectangles({0, 0, -5, 2}, {0, 0, 10, 10}, BooleanOp::Union);
    PIKDRAW_ASSERT(!invalid.success);
    const auto empty = boolean_rectangles({}, {0, 0, 10, 10}, BooleanOp::Union);
    PIKDRAW_ASSERT(empty.success && empty.rectangles.size() == 1);
}
