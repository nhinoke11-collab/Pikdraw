#include "test_harness.hpp"
#include "pikdraw/geometry/dimensions/dimension_geometry.hpp"

#include <cmath>

using namespace pikdraw::geometry;

static constexpr double kEps = 1e-6;

PIKDRAW_TEST(linear_dimensions_ac_d01) {
    const Point2D a{0.0, 0.0};
    const Point2D b{30.0, 40.0};

    // Aligned: hypot(30, 40) = 50 mm
    const auto aligned = compute_linear_dimension(a, b, {10.0, 30.0}, LinearOrientation::Aligned);
    PIKDRAW_ASSERT(aligned.valid);
    PIKDRAW_ASSERT_NEAR(aligned.measured_value_mm, 50.0, kEps);
    PIKDRAW_ASSERT_NEAR(aligned.def_point1.x, 0.0, kEps);
    PIKDRAW_ASSERT_NEAR(aligned.def_point1.y, 0.0, kEps);
    PIKDRAW_ASSERT_NEAR(aligned.def_point2.x, 30.0, kEps);
    PIKDRAW_ASSERT_NEAR(aligned.def_point2.y, 40.0, kEps);
    PIKDRAW_ASSERT_NEAR(aligned.text_rotation_rad, std::atan2(40.0, 30.0), kEps);

    // Horizontal: dx = 30 mm
    const auto horiz = compute_linear_dimension(a, b, {15.0, 50.0}, LinearOrientation::Horizontal);
    PIKDRAW_ASSERT(horiz.valid);
    PIKDRAW_ASSERT_NEAR(horiz.measured_value_mm, 30.0, kEps);
    PIKDRAW_ASSERT_NEAR(horiz.dim_line_start.y, 50.0, kEps);
    PIKDRAW_ASSERT_NEAR(horiz.dim_line_end.y, 50.0, kEps);
    PIKDRAW_ASSERT_NEAR(horiz.text_position.x, 15.0, kEps);
    PIKDRAW_ASSERT_NEAR(horiz.text_position.y, 50.0, kEps);
    PIKDRAW_ASSERT_NEAR(horiz.text_rotation_rad, 0.0, kEps);

    // Vertical: dy = 40 mm
    const auto vert = compute_linear_dimension(a, b, {-10.0, 20.0}, LinearOrientation::Vertical);
    PIKDRAW_ASSERT(vert.valid);
    PIKDRAW_ASSERT_NEAR(vert.measured_value_mm, 40.0, kEps);
    PIKDRAW_ASSERT_NEAR(vert.dim_line_start.x, -10.0, kEps);
    PIKDRAW_ASSERT_NEAR(vert.dim_line_end.x, -10.0, kEps);
    PIKDRAW_ASSERT_NEAR(vert.text_position.x, -10.0, kEps);
    PIKDRAW_ASSERT_NEAR(vert.text_position.y, 20.0, kEps);

    // Degenerate zero-length cases
    const auto degen_aligned = compute_linear_dimension(a, a, {10.0, 10.0}, LinearOrientation::Aligned);
    PIKDRAW_ASSERT(!degen_aligned.valid);

    const auto degen_horiz = compute_linear_dimension({10.0, 0.0}, {10.0, 50.0}, {10.0, 60.0}, LinearOrientation::Horizontal);
    PIKDRAW_ASSERT(!degen_horiz.valid);
}

PIKDRAW_TEST(radial_and_arc_length_ac_d04) {
    const Point2D center{100.0, 100.0};
    const double radius = 20.0;

    // Radius
    const auto rad = compute_radial_dimension(center, radius, false, {130.0, 100.0});
    PIKDRAW_ASSERT(rad.valid);
    PIKDRAW_ASSERT(!rad.is_diameter);
    PIKDRAW_ASSERT_NEAR(rad.measured_value_mm, 20.0, kEps);
    PIKDRAW_ASSERT_NEAR(rad.surface_point.x, 120.0, kEps);
    PIKDRAW_ASSERT_NEAR(rad.surface_point.y, 100.0, kEps);

    // Diameter
    const auto dia = compute_radial_dimension(center, radius, true, {100.0, 140.0});
    PIKDRAW_ASSERT(dia.valid);
    PIKDRAW_ASSERT(dia.is_diameter);
    PIKDRAW_ASSERT_NEAR(dia.measured_value_mm, 40.0, kEps);
    PIKDRAW_ASSERT_NEAR(dia.surface_point.x, 100.0, kEps);
    PIKDRAW_ASSERT_NEAR(dia.surface_point.y, 120.0, kEps);

    // Arc Length: quarter circle R=20, sweep = pi/2 -> length = 10 * pi
    constexpr double kPi = 3.14159265358979323846;
    const auto arc = compute_arc_length_dimension(center, radius, 0.0, kPi * 0.5, {130.0, 130.0});
    PIKDRAW_ASSERT(arc.valid);
    const double expected_len = radius * (kPi * 0.5);
    PIKDRAW_ASSERT_NEAR(arc.measured_value_mm, expected_len, kEps);
}

PIKDRAW_TEST(angular_dimension_ac_d05) {
    // Two lines meeting at origin (0, 0): +X axis and +Y axis (90 deg)
    const Point2D ray1_p1{0.0, 0.0};
    const Point2D ray1_p2{100.0, 0.0};
    const Point2D ray2_p1{0.0, 0.0};
    const Point2D ray2_p2{0.0, 100.0};

    const auto ang = compute_angular_dimension(ray1_p1, ray1_p2, ray2_p1, ray2_p2, {40.0, 40.0});
    PIKDRAW_ASSERT(ang.valid);
    PIKDRAW_ASSERT_NEAR(ang.measured_value_deg, 90.0, 1e-4);
    constexpr double kPi = 3.14159265358979323846;
    PIKDRAW_ASSERT_NEAR(ang.measured_value_rad, kPi * 0.5, 1e-4);

    // Parallel lines -> invalid
    const auto par = compute_angular_dimension({0.0, 0.0}, {100.0, 0.0}, {0.0, 50.0}, {100.0, 50.0}, {50.0, 25.0});
    PIKDRAW_ASSERT(!par.valid);
}

PIKDRAW_TEST(ordinate_dimension_ac_d06) {
    const Point2D datum{10.0, 20.0};
    const Point2D feature{50.0, 70.0};

    // Ordinate X
    const auto ord_x = compute_ordinate_dimension(datum, feature, OrdinateAxis::X, {50.0, 90.0});
    PIKDRAW_ASSERT(ord_x.valid);
    PIKDRAW_ASSERT_NEAR(ord_x.measured_value_mm, 40.0, kEps);

    // Ordinate Y
    const auto ord_y = compute_ordinate_dimension(datum, feature, OrdinateAxis::Y, {80.0, 70.0});
    PIKDRAW_ASSERT(ord_y.valid);
    PIKDRAW_ASSERT_NEAR(ord_y.measured_value_mm, 50.0, kEps);
}

PIKDRAW_TEST(multi_leader_and_arrow_paths) {
    const std::vector<Point2D> heads{{10.0, 20.0}, {15.0, 40.0}};
    const Point2D elbow{50.0, 30.0};

    const auto ml = compute_multi_leader(heads, elbow);
    PIKDRAW_ASSERT(ml.valid);
    PIKDRAW_ASSERT(ml.arrows.size() == 2);
    PIKDRAW_ASSERT_NEAR(ml.landing_end.x, 58.0, kEps);
    PIKDRAW_ASSERT_NEAR(ml.landing_end.y, 30.0, kEps);

    // Arrow paths
    DimensionArrow arrow{{100.0, 100.0}, {1.0, 0.0}};
    const auto closed_filled = make_arrow_path(arrow, ArrowType::ClosedFilled, 3.0);
    PIKDRAW_ASSERT(!closed_filled.empty());
    PIKDRAW_ASSERT(closed_filled.elements().size() == 4);

    const auto tick = make_arrow_path(arrow, ArrowType::ArchitecturalTick, 3.0);
    PIKDRAW_ASSERT(!tick.empty());

    const auto dot = make_arrow_path(arrow, ArrowType::Dot, 3.0);
    PIKDRAW_ASSERT(!dot.empty());
}
