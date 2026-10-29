#include "test_harness.hpp"
#include "pikdraw/geometry/operations/path_cut.hpp"
#include <cmath>

using namespace pikdraw::geometry;

PIKDRAW_TEST(knife_cut_rect) {
    auto rect = Path2D::make_rect(0.0, 0.0, 100.0, 50.0);
    // Vertical cut at x = 40.0
    auto result = cut_path_by_line(rect, {40.0, -10.0}, {40.0, 60.0});
    PIKDRAW_ASSERT(result.success);
    PIKDRAW_ASSERT(result.status == PathCutStatus::Success);
    PIKDRAW_ASSERT(result.pieces.size() >= 2);

    auto b0 = result.pieces[0].bounds();
    auto b1 = result.pieces[1].bounds();
    double min_w = std::min(b0.width, b1.width);
    double max_w = std::max(b0.width, b1.width);
    PIKDRAW_ASSERT_NEAR(min_w, 40.0, 0.1);
    PIKDRAW_ASSERT_NEAR(max_w, 60.0, 0.1);
}

PIKDRAW_TEST(knife_cut_concave_l_shape) {
    // L-shape polygon:
    // (0,0) -> (60,0) -> (60,20) -> (20,20) -> (20,60) -> (0,60) -> close
    Path2D l_shape;
    l_shape.move_to(0.0, 0.0);
    l_shape.line_to(60.0, 0.0);
    l_shape.line_to(60.0, 20.0);
    l_shape.line_to(20.0, 20.0);
    l_shape.line_to(20.0, 60.0);
    l_shape.line_to(0.0, 60.0);
    l_shape.close();

    // Cut vertically at x = 10 (cuts through the vertical arm)
    auto result = cut_path_by_line(l_shape, {10.0, -10.0}, {10.0, 70.0});
    PIKDRAW_ASSERT(result.success);
    PIKDRAW_ASSERT(result.pieces.size() >= 2);
}

PIKDRAW_TEST(knife_no_cut_outside) {
    auto rect = Path2D::make_rect(10.0, 10.0, 40.0, 40.0);
    // Line far to the right at x = 80
    auto result = cut_path_by_line(rect, {80.0, 0.0}, {80.0, 100.0});
    PIKDRAW_ASSERT(!result.success);
    PIKDRAW_ASSERT(result.status == PathCutStatus::NoCut);
}

PIKDRAW_TEST(knife_freehand_reports_its_topology_slit_bound) {
    const auto rect = Path2D::make_rect(0.0, 0.0, 100.0, 100.0);
    const auto result = cut_path_by_polyline(
        rect, {{50.0, -10.0}, {50.0, 50.0}, {50.0, 110.0}});
    PIKDRAW_ASSERT(result.success && result.status == PathCutStatus::Success);
    PIKDRAW_ASSERT(result.pieces.size() == 2);
    // Zero-gap freehand currently uses a narrow internal slit to make two
    // valid contours; the returned bound must include that half-width.
    PIKDRAW_ASSERT(result.approximation_error_bound_mm > 0.011);
}

PIKDRAW_TEST(eraser_cut) {
    auto rect = Path2D::make_rect(0.0, 0.0, 100.0, 50.0);
    // Stroke across middle horizontally: from (-5, 25) to (105, 25) with 4mm nib
    std::vector<Point2D> stroke{{-5.0, 25.0}, {105.0, 25.0}};
    auto result = erase_path(rect, stroke, 4.0, true);
    PIKDRAW_ASSERT(result.success);
    PIKDRAW_ASSERT(result.pieces.size() >= 2); // Split top and bottom
}

PIKDRAW_TEST(eraser_follows_bent_stroke_instead_of_its_chord) {
    const auto rect = Path2D::make_rect(0.0, 0.0, 100.0, 100.0);
    const auto result = erase_path(rect, {{10.0, 10.0}, {10.0, 90.0}, {90.0, 90.0}}, 4.0, true);
    PIKDRAW_ASSERT(result.success && result.pieces.size() == 1);
    const auto on_stroke = Path2D::make_rect(9.5, 49.5, 1.0, 1.0);
    const auto on_chord = Path2D::make_rect(49.5, 49.5, 1.0, 1.0);
    const auto erased = boolean_paths(result.pieces.front(), on_stroke, BooleanOp::Intersection);
    const auto retained = boolean_paths(result.pieces.front(), on_chord, BooleanOp::Intersection);
    PIKDRAW_ASSERT(erased.success && erased.status == BooleanStatus::Empty);
    PIKDRAW_ASSERT(retained.success && retained.status == BooleanStatus::Success);
}

PIKDRAW_TEST(eraser_preserves_hole_topology_and_rejects_noop) {
    const auto rect = Path2D::make_rect(0.0, 0.0, 100.0, 100.0);
    const auto hole = erase_path(rect, {{50.0, 50.0}}, 10.0, true);
    PIKDRAW_ASSERT(hole.success);
    PIKDRAW_ASSERT(hole.pieces.size() == 1);
    PIKDRAW_ASSERT(hole.approximation_error_bound_mm >= 0.019);

    size_t move_count = 0;
    size_t close_count = 0;
    for (const auto& element : hole.pieces.front().elements()) {
        if (element.verb == PathVerb::MoveTo) ++move_count;
        if (element.verb == PathVerb::Close) ++close_count;
    }
    PIKDRAW_ASSERT(move_count == 2);
    PIKDRAW_ASSERT(close_count == 2);

    const auto removed_probe = Path2D::make_rect(47.0, 47.0, 6.0, 6.0);
    const auto still_filled_probe = Path2D::make_rect(10.0, 10.0, 6.0, 6.0);
    const auto erased_area = boolean_paths(hole.pieces.front(), removed_probe,
                                           BooleanOp::Intersection);
    const auto remaining_area = boolean_paths(hole.pieces.front(), still_filled_probe,
                                              BooleanOp::Intersection);
    PIKDRAW_ASSERT(erased_area.success);
    PIKDRAW_ASSERT(erased_area.status == BooleanStatus::Empty);
    PIKDRAW_ASSERT(remaining_area.success);
    PIKDRAW_ASSERT(remaining_area.status == BooleanStatus::Success);

    const auto no_op = erase_path(rect, {{200.0, 200.0}}, 10.0, true);
    PIKDRAW_ASSERT(!no_op.success);
    PIKDRAW_ASSERT(no_op.status == PathCutStatus::NoCut);
}

PIKDRAW_TEST(eraser_removes_open_stroke_centerline_and_respects_nib_shape) {
    Path2D open_path;
    open_path.move_to(0.0, 0.0);
    open_path.line_to(100.0, 0.0);
    const auto result = erase_open_path_stroke(
        open_path, {{50.0, -10.0}, {50.0, 10.0}}, 4.0, 2.0, true);
    PIKDRAW_ASSERT(result.success && result.status == PathCutStatus::Success);
    PIKDRAW_ASSERT(result.pieces.size() == 2);
    PIKDRAW_ASSERT_NEAR(result.pieces[0].bounds().width, 47.0, 1e-6);
    PIKDRAW_ASSERT_NEAR(result.pieces[1].bounds().width, 47.0, 1e-6);
    PIKDRAW_ASSERT_NEAR(result.approximation_error_bound_mm, 0.01, 1e-12);

    const auto square = erase_open_path_stroke(
        open_path, {{50.0, 0.0}}, 4.0, 0.0, false);
    PIKDRAW_ASSERT(square.success && square.pieces.size() == 2);
    PIKDRAW_ASSERT_NEAR(square.pieces[0].bounds().width, 48.0, 1e-6);

    const auto noop = erase_open_path_stroke(
        open_path, {{50.0, 10.0}}, 4.0, 0.0, true);
    PIKDRAW_ASSERT(!noop.success && noop.status == PathCutStatus::NoCut);
}

PIKDRAW_TEST(trim_target_with_sources) {
    auto target = Path2D::make_rect(0.0, 0.0, 100.0, 100.0);
    auto cutter = Path2D::make_rect(20.0, 20.0, 40.0, 40.0);
    auto res = trim_target_with_sources(target, {cutter});
    PIKDRAW_ASSERT(res.success);
    PIKDRAW_ASSERT(res.status == BooleanStatus::Success);
    PIKDRAW_ASSERT(!res.path.empty());
    PIKDRAW_ASSERT(res.components.size() == 1);
    size_t contour_count = 0;
    for (const auto& element : res.components.front().elements())
        if (element.verb == PathVerb::MoveTo) ++contour_count;
    PIKDRAW_ASSERT(contour_count == 2); // Outer target and the preserved hole.
}

PIKDRAW_TEST(trim_returns_disconnected_regions_as_separate_components) {
    const auto target = Path2D::make_rect(0.0, 0.0, 100.0, 100.0);
    const auto source = Path2D::make_rect(45.0, 0.0, 10.0, 100.0);
    const auto result = trim_target_with_sources(target, {source});
    PIKDRAW_ASSERT(result.success && result.status == BooleanStatus::Success);
    PIKDRAW_ASSERT(result.components.size() == 2);
    PIKDRAW_ASSERT_NEAR(result.components[0].bounds().width, 45.0, 1e-8);
    PIKDRAW_ASSERT_NEAR(result.components[1].bounds().width, 45.0, 1e-8);
}

PIKDRAW_TEST(trim_rejects_non_overlapping_sources_as_no_change) {
    const auto target = Path2D::make_rect(0.0, 0.0, 100.0, 100.0);
    const auto source = Path2D::make_rect(200.0, 200.0, 20.0, 20.0);
    const auto result = trim_target_with_sources(target, {source});
    PIKDRAW_ASSERT(!result.success);
    PIKDRAW_ASSERT(result.status == BooleanStatus::NoChange);
}
