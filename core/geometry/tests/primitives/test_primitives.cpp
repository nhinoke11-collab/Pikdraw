#include "test_harness.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"
#include "pikdraw/geometry/primitives/validation.hpp"
#include <limits>

using namespace pikdraw::geometry;

PIKDRAW_TEST(point_arithmetic_and_distance) {
    Point2D p1{10.0, 20.0};
    Point2D p2{13.0, 24.0};

    Point2D sum = p1 + p2;
    PIKDRAW_ASSERT_NEAR(sum.x, 23.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(sum.y, 44.0, 1e-9);

    Point2D diff = p2 - p1;
    PIKDRAW_ASSERT_NEAR(diff.x, 3.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(diff.y, 4.0, 1e-9);

    PIKDRAW_ASSERT_NEAR(p1.distance_to(p2), 5.0, 1e-9);
    PIKDRAW_ASSERT(p1.approx_equal({10.0000001, 20.0000001}, 1e-6));
}

PIKDRAW_TEST(rect_operations) {
    Rect2D r1{10.0, 10.0, 50.0, 40.0};
    PIKDRAW_ASSERT_NEAR(r1.right(), 60.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(r1.bottom(), 50.0, 1e-9);

    PIKDRAW_ASSERT(r1.contains({20.0, 20.0}));
    PIKDRAW_ASSERT(!r1.contains({5.0, 5.0}));

    Rect2D r2{30.0, 30.0, 50.0, 50.0};
    PIKDRAW_ASSERT(r1.intersects(r2));

    Rect2D inter = r1.intersected(r2);
    PIKDRAW_ASSERT_NEAR(inter.x, 30.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(inter.y, 30.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(inter.width, 30.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(inter.height, 20.0, 1e-9);

    Rect2D un = r1.united(r2);
    PIKDRAW_ASSERT_NEAR(un.x, 10.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(un.y, 10.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(un.width, 70.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(un.height, 70.0, 1e-9);
}

PIKDRAW_TEST(transform_composition_and_inversion) {
    auto t1 = Transform2D::translation(100.0, 200.0);
    auto t2 = Transform2D::scale(2.0, 3.0);
    auto t3 = Transform2D::rotation_degrees(90.0);

    // Parent * Child
    auto combined = t1 * t2;
    Point2D pt{10.0, 10.0};
    Point2D mapped = combined.map_point(pt);

    // Child scale first: (20, 30), then translation: (120, 230)
    PIKDRAW_ASSERT_NEAR(mapped.x, 120.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(mapped.y, 230.0, 1e-9);

    auto inv = combined.inverted();
    PIKDRAW_ASSERT(inv.has_value());
    Point2D unmapped = inv->map_point(mapped);
    PIKDRAW_ASSERT_NEAR(unmapped.x, 10.0, limits::kTransformInversionToleranceMm);
    PIKDRAW_ASSERT_NEAR(unmapped.y, 10.0, limits::kTransformInversionToleranceMm);
}

PIKDRAW_TEST(numerical_validation_limits) {
    Point2D valid_pt{500000.0, -500000.0};
    PIKDRAW_ASSERT(validate_point(valid_pt) == GeometryStatus::Ok);

    Point2D out_pt{1500000.0, 0.0};
    PIKDRAW_ASSERT(validate_point(out_pt) == GeometryStatus::OutOfBounds);

    Point2D nan_pt{std::numeric_limits<double>::quiet_NaN(), 0.0};
    PIKDRAW_ASSERT(validate_point(nan_pt) == GeometryStatus::NonFiniteCoordinate);

    Point2D inf_pt{0.0, std::numeric_limits<double>::infinity()};
    PIKDRAW_ASSERT(validate_point(inf_pt) == GeometryStatus::NonFiniteCoordinate);

    Transform2D singular{1.0, 2.0, 2.0, 4.0, 0.0, 0.0}; // det = 1*4 - 2*2 = 0
    PIKDRAW_ASSERT(validate_transform(singular) == GeometryStatus::SingularTransform);
}
