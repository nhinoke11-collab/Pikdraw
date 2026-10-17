#include "test_harness.hpp"
#include "pikdraw/geometry/operations/boolean.hpp"
#include "pikdraw/geometry/operations/offset.hpp"
#include <limits>

using namespace pikdraw::geometry;

PIKDRAW_TEST(no_fabricated_path_boolean_or_offset) {
    Path2D triangle;
    triangle.move_to(0, 0);
    triangle.line_to(10, 0);
    triangle.line_to(0, 10);
    triangle.close();
    const auto rectangle = Path2D::make_rect(0, 0, 10, 10);
    const auto ellipse = Path2D::make_ellipse(5, 5, 5, 5);
    for (const auto& path : {triangle, ellipse}) {
        const auto boolean = boolean_paths(path, rectangle, BooleanOp::Intersection);
        PIKDRAW_ASSERT(!boolean.success && boolean.path.empty() && boolean.error_message);
        OffsetOptions options;
        options.distance_mm = 1;
        const auto offset = offset_path(path, options);
        PIKDRAW_ASSERT(!offset.success && offset.path.empty() && offset.error_message);
    }
    PIKDRAW_ASSERT(boolean_paths(rectangle, rectangle, BooleanOp::Intersection).success);
}

PIKDRAW_TEST(boolean_boundary_is_not_misreported_as_union) {
    const auto result = boolean_rectangles(
        {0, 0, 10, 10}, {5, 0, 10, 10}, BooleanOp::Boundary);
    PIKDRAW_ASSERT(!result.success);
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
