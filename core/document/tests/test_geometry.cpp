#include "test_harness.hpp"
#include "pikdraw/geometry/point.hpp"
#include "pikdraw/geometry/transform.hpp"
#include "pikdraw/geometry/rect.hpp"
#include "pikdraw/geometry/curve.hpp"
#include "pikdraw/geometry/boolean_offset.hpp"

using namespace pikdraw::geometry;

PIK_TEST_CASE(vector_large_sign_relative) {
    // Contract vector: large-sign-relative
    // point: [20000.005, 500], matrix: identity, origin: [20000, 0]
    // expected_relative: [0.005, 500], tolerance: 1e-9
    const Point2D pt(20000.005, 500.0);
    const Transform2D mat = Transform2D::identity();
    const Point2D transformed = mat.apply(pt);
    const Point2D origin(20000.0, 0.0);
    const RenderPoint rel = to_render_local(transformed, origin);

    PIK_CHECK_NEAR(rel.x, 0.005f, 1e-5f);
    PIK_CHECK_NEAR(rel.y, 500.0f, 1e-5f);
}

PIK_TEST_CASE(vector_reflection) {
    // Contract vector: reflection
    // point: [2, 3], matrix: [-1, 0, 0, 1, 10, 0]
    // expected_world: [8, 3]
    const Point2D pt(2.0, 3.0);
    const Transform2D mat(-1.0, 0.0, 0.0, 1.0, 10.0, 0.0);
    const Point2D world = mat.apply(pt);

    PIK_CHECK_NEAR(world.x, 8.0, 1e-9);
    PIK_CHECK_NEAR(world.y, 3.0, 1e-9);
}

PIK_TEST_CASE(vector_nonuniform_scale_translate) {
    // Contract vector: nonuniform-scale-translate
    // point: [2, 3], matrix: [2, 0, 0, 0.5, 10, -4]
    // expected_world: [14, -2.5]
    const Point2D pt(2.0, 3.0);
    const Transform2D mat(2.0, 0.0, 0.0, 0.5, 10.0, -4.0);
    const Point2D world = mat.apply(pt);

    PIK_CHECK_NEAR(world.x, 14.0, 1e-9);
    PIK_CHECK_NEAR(world.y, -2.5, 1e-9);
}

PIK_TEST_CASE(transform_composition_and_inverse) {
    const auto t_trans = Transform2D::translation(10.0, -5.0);
    const auto t_scale = Transform2D::scale(2.0, 3.0);
    // compose(parent, local): applies local first
    const auto comp = Transform2D::compose(t_scale, t_trans);
    const Point2D pt(1.0, 1.0);
    // Local: (1 + 10, 1 - 5) = (11, -4)
    // Parent: (11 * 2, -4 * 3) = (22, -12)
    const Point2D res = comp.apply(pt);
    PIK_CHECK_NEAR(res.x, 22.0, 1e-9);
    PIK_CHECK_NEAR(res.y, -12.0, 1e-9);

    const auto inv_opt = comp.inverse();
    PIK_CHECK(inv_opt.has_value());
    const Point2D back = inv_opt->apply(res);
    PIK_CHECK_NEAR(back.x, pt.x, 1e-9);
    PIK_CHECK_NEAR(back.y, pt.y, 1e-9);
}

PIK_TEST_CASE(curve_source_preservation_and_discretization) {
    SourcePath path;
    path.move_to({0, 0});
    path.cubic_to({10, 20}, {30, 20}, {40, 0});
    path.close();

    // Verify source segments are strictly preserved
    PIK_CHECK_EQUAL(path.segments().size(), 3);
    PIK_CHECK(path.segments()[1].type == SegmentType::CubicTo);
    PIK_CHECK_NEAR(path.segments()[1].p1.x, 10.0, 1e-9);
    PIK_CHECK_NEAR(path.segments()[1].p2.x, 30.0, 1e-9);

    // Discretize for manufacturing with 0.01mm tolerance
    const auto approx = discretize_for_manufacturing(path, 0.01);
    PIK_CHECK(!approx.is_source_curve);
    PIK_CHECK_EQUAL(approx.tolerance_mm, 0.01);
    PIK_CHECK(approx.polyline.size() > 5);
}

PIK_TEST_CASE(rectangle_boolean_and_diagnostic) {
    const Rect2D r1 = Rect2D::from_xywh(0, 0, 10, 10);
    const Rect2D r2 = Rect2D::from_xywh(5, 0, 10, 10);

    TopologyDiagnostic diag;
    auto inter = RectangleBooleanEngine::intersect(r1, r2, &diag);
    PIK_CHECK(diag.ok());
    PIK_CHECK(inter.has_value());
    PIK_CHECK_NEAR(inter->width(), 5.0, 1e-9);
    PIK_CHECK_NEAR(inter->height(), 10.0, 1e-9);

    auto sub = RectangleBooleanEngine::subtract(r1, r2, &diag);
    PIK_CHECK(diag.ok());
    PIK_CHECK_EQUAL(sub.size(), 1);
    PIK_CHECK_NEAR(sub[0].width(), 5.0, 1e-9);

    // Unsupported complex topology produces explicit diagnostic
    SourcePath sp1, sp2, out;
    auto cdiag = evaluate_general_path_boolean(sp1, sp2, out);
    PIK_CHECK(cdiag.code == TopologyDiagnosticCode::UnsupportedTopology);
}
