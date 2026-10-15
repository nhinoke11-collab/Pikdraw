#include "test_harness.hpp"
#include "pikdraw/geometry/curves/curve.hpp"
#include "pikdraw/geometry/curves/path.hpp"
#include "pikdraw/geometry/flattening/flattening.hpp"

using namespace pikdraw::geometry;

PIKDRAW_TEST(line_segment_evaluation) {
    LineSegment line{Point2D{0.0, 0.0}, Point2D{100.0, 50.0}};
    Point2D mid = line.point_at(0.5);
    PIKDRAW_ASSERT_NEAR(mid.x, 50.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(mid.y, 25.0, 1e-9);

    Rect2D b = line.bounds();
    PIKDRAW_ASSERT_NEAR(b.width, 100.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(b.height, 50.0, 1e-9);
}

PIKDRAW_TEST(bezier_curves_and_bounds) {
    // S-curve cubic bezier
    CubicBezier cubic{
        Point2D{0.0, 0.0},
        Point2D{0.0, 100.0},
        Point2D{100.0, 0.0},
        Point2D{100.0, 100.0}
    };

    Point2D p0 = cubic.point_at(0.0);
    Point2D p1 = cubic.point_at(1.0);
    PIKDRAW_ASSERT_NEAR(p0.x, 0.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(p0.y, 0.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(p1.x, 100.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(p1.y, 100.0, 1e-9);

    Rect2D b = cubic.bounds();
    PIKDRAW_ASSERT(b.left() <= 0.0);
    PIKDRAW_ASSERT(b.right() >= 100.0);
}

PIKDRAW_TEST(path_construction_and_flattening) {
    Path2D path;
    path.move_to(0.0, 0.0);
    path.line_to(100.0, 0.0);
    path.line_to(100.0, 100.0);
    path.close();

    PIKDRAW_ASSERT_NEAR(path.bounds().width, 100.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(path.bounds().height, 100.0, 1e-9);

    // Transform path
    auto t = Transform2D::translation(50.0, 50.0);
    Path2D transformed = path.transformed(t);
    PIKDRAW_ASSERT_NEAR(transformed.bounds().x, 50.0, 1e-9);
    PIKDRAW_ASSERT_NEAR(transformed.bounds().y, 50.0, 1e-9);

    // Flatten ellipse
    Path2D ellipse = Path2D::make_ellipse(0.0, 0.0, 50.0, 25.0);
    FlatteningOptions opts;
    opts.tolerance_mm = 0.05;
    auto flattened = flatten_path(ellipse, opts);
    PIKDRAW_ASSERT(!flattened.empty());
    PIKDRAW_ASSERT(flattened[0].points.size() > 10);
}
