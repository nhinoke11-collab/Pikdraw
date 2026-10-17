#include "pikdraw/document/model/gradient_paint.hpp"
#include "pikdraw/document/model/object.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::document;

void test_valid_defaults_and_effective_accessor() {
    reset_gradient_id_allocators_for_tests();

    geometry::Rect2D bounds{10.0, 20.0, 100.0, 60.0};
    auto linear = GradientPaint::make_default_linear(
        allocate_gradient_paint_id(),
        bounds,
        color::RGBColor{1.0, 0.0, 0.0, 1.0},
        color::RGBColor{0.0, 0.0, 1.0, 1.0});

    assert(linear.is_valid());
    assert(linear.stops.size() == 2);
    assert(linear.stops[0].offset == 0.0);
    assert(linear.stops[1].offset == 1.0);
    assert(linear.stops[1].midpoint_to_next == 0.5);

    auto radial = GradientPaint::make_default_radial(
        allocate_gradient_paint_id(),
        bounds);
    assert(radial.is_valid());
    assert(radial.stops.size() == 2);
    assert(radial.gradient_to_object.tx == 60.0); // 10 + 50
    assert(radial.gradient_to_object.ty == 50.0); // 20 + 30

    PathObject path;
    path.fill = color::RGBColor{0.5, 0.5, 0.5, 1.0};
    path.has_fill = true;
    path.fill_gradient = nullptr;

    auto eff1 = path.effective_fill_paint();
    assert(eff1.kind == FillPaintKind::Solid);
    assert(eff1.gradient == nullptr);

    path.fill_gradient = std::make_shared<const GradientPaint>(linear);
    auto eff2 = path.effective_fill_paint();
    assert(eff2.kind == FillPaintKind::Gradient);
    assert(eff2.gradient != nullptr);
    assert(eff2.gradient->id == linear.id);

    path.has_fill = false;
    auto eff3 = path.effective_fill_paint();
    assert(eff3.kind == FillPaintKind::None);
    // Remembered gradient and fill are preserved
    assert(eff3.gradient == nullptr);
    assert(path.fill_gradient != nullptr);

    std::cout << "[PASS] test_valid_defaults_and_effective_accessor\n";
}

void test_reversal_invariants_and_restoration() {
    reset_gradient_id_allocators_for_tests();

    GradientPaint p;
    p.id = allocate_gradient_paint_id();
    p.kind = GradientKind::Linear;
    p.spread = GradientSpread::Reflect;
    p.gradient_to_object = geometry::Transform2D::scale(2.0, 3.0);

    GradientStop s1{allocate_gradient_stop_id(), 0.0, color::RGBColor{1.0, 0.0, 0.0, 1.0}, 1.0, 0.3};
    GradientStop s2{allocate_gradient_stop_id(), 0.4, color::RGBColor{0.0, 1.0, 0.0, 1.0}, 0.8, 0.7};
    GradientStop s3{allocate_gradient_stop_id(), 1.0, color::RGBColor{0.0, 0.0, 1.0, 1.0}, 0.5, 0.5};
    p.stops = {s1, s2, s3};

    assert(p.is_valid());

    auto rev1 = p.with_reversed_stops();
    assert(rev1.is_valid());
    assert(rev1.stops.size() == 3);
    assert(std::abs(rev1.stops[0].offset - 0.0) < 1e-9); // 1 - 1.0
    assert(std::abs(rev1.stops[1].offset - 0.6) < 1e-9); // 1 - 0.4
    assert(std::abs(rev1.stops[2].offset - 1.0) < 1e-9); // 1 - 0.0

    // Check midpoints
    assert(std::abs(rev1.stops[0].midpoint_to_next - (1.0 - 0.7)) < 1e-9);
    assert(std::abs(rev1.stops[1].midpoint_to_next - (1.0 - 0.3)) < 1e-9);
    assert(std::abs(rev1.stops[2].midpoint_to_next - 0.5) < 1e-9);

    // Reversing twice restores exact payload
    auto rev2 = rev1.with_reversed_stops();
    assert(rev2.is_valid());
    assert(p.same_payload(rev2));

    // Test with hard edge (ties at offset 0.5)
    GradientPaint pt;
    pt.id = allocate_gradient_paint_id();
    pt.stops = {
        GradientStop{allocate_gradient_stop_id(), 0.0, color::RGBColor{1, 0, 0, 1}, 1.0, 0.5},
        GradientStop{allocate_gradient_stop_id(), 0.5, color::RGBColor{1, 1, 0, 1}, 1.0, 0.2},
        GradientStop{allocate_gradient_stop_id(), 0.5, color::RGBColor{0, 1, 0, 1}, 1.0, 0.8},
        GradientStop{allocate_gradient_stop_id(), 1.0, color::RGBColor{0, 0, 1, 1}, 1.0, 0.5}
    };
    assert(pt.is_valid());

    auto rev_tie = pt.with_reversed_stops();
    assert(rev_tie.is_valid());
    // Offsets must still be monotonic non-decreasing: 0.0, 0.5, 0.5, 1.0
    assert(rev_tie.stops[0].offset <= rev_tie.stops[1].offset);
    assert(rev_tie.stops[1].offset <= rev_tie.stops[2].offset);
    assert(rev_tie.stops[2].offset <= rev_tie.stops[3].offset);

    auto rev_tie2 = rev_tie.with_reversed_stops();
    assert(pt.same_payload(rev_tie2));

    std::cout << "[PASS] test_reversal_invariants_and_restoration\n";
}

void test_validation_boundary_rejections() {
    reset_gradient_id_allocators_for_tests();

    // 1 stop (less than 2) -> reject
    GradientPaint p1;
    p1.id = 1;
    p1.stops = {GradientStop{1, 0.0, color::RGBColor{1, 0, 0, 1}, 1.0, 0.5}};
    assert(!p1.is_valid());

    // Duplicate stop ID -> reject
    GradientPaint p2;
    p2.id = 1;
    p2.stops = {
        GradientStop{10, 0.0, color::RGBColor{1, 0, 0, 1}, 1.0, 0.5},
        GradientStop{10, 1.0, color::RGBColor{0, 1, 0, 1}, 1.0, 0.5}
    };
    assert(!p2.is_valid());

    // Decreasing offsets -> reject
    GradientPaint p3;
    p3.id = 1;
    p3.stops = {
        GradientStop{1, 0.8, color::RGBColor{1, 0, 0, 1}, 1.0, 0.5},
        GradientStop{2, 0.2, color::RGBColor{0, 1, 0, 1}, 1.0, 0.5}
    };
    assert(!p3.is_valid());

    // Terminal midpoint != 0.5 -> reject
    GradientPaint p4;
    p4.id = 1;
    p4.stops = {
        GradientStop{1, 0.0, color::RGBColor{1, 0, 0, 1}, 1.0, 0.5},
        GradientStop{2, 1.0, color::RGBColor{0, 1, 0, 1}, 1.0, 0.8}
    };
    assert(!p4.is_valid());

    // Stop opacity out of range -> reject
    GradientPaint p5;
    p5.id = 1;
    p5.stops = {
        GradientStop{1, 0.0, color::RGBColor{1, 0, 0, 1}, 1.5, 0.5},
        GradientStop{2, 1.0, color::RGBColor{0, 1, 0, 1}, 1.0, 0.5}
    };
    assert(!p5.is_valid());

    // Midpoint out of range (< 0.01) -> reject
    GradientPaint p6;
    p6.id = 1;
    p6.stops = {
        GradientStop{1, 0.0, color::RGBColor{1, 0, 0, 1}, 1.0, 0.005},
        GradientStop{2, 1.0, color::RGBColor{0, 1, 0, 1}, 1.0, 0.5}
    };
    assert(!p6.is_valid());

    // Zero paint ID -> reject
    GradientPaint p7;
    p7.id = 0;
    p7.stops = {
        GradientStop{1, 0.0, color::RGBColor{1, 0, 0, 1}, 1.0, 0.5},
        GradientStop{2, 1.0, color::RGBColor{0, 1, 0, 1}, 1.0, 0.5}
    };
    assert(!p7.is_valid());

    std::cout << "[PASS] test_validation_boundary_rejections\n";
}

void test_matrix_invertibility_guardrail() {
    // Identity is valid
    assert(validate_gradient_matrix(geometry::Transform2D::identity()));

    // Uniform / non-uniform scale
    assert(validate_gradient_matrix(geometry::Transform2D::scale(100.0, 50.0)));

    // Rotation
    assert(validate_gradient_matrix(geometry::Transform2D::rotation_degrees(45.0)));

    // Singular matrix (zeros)
    assert(!validate_gradient_matrix(geometry::Transform2D{0, 0, 0, 0, 0, 0}));

    // Rank 1 degenerate matrix: col0 = (1, 2), col1 = (2, 4) -> det = 1*4 - 2*2 = 0
    assert(!validate_gradient_matrix(geometry::Transform2D{1, 2, 2, 4, 10, 20}));

    // Determinant just below relative threshold 1e-12
    assert(!validate_gradient_matrix(geometry::Transform2D{1.0, 0.0, 0.0, 1e-13, 0.0, 0.0}));

    // Determinant above relative threshold 1e-12
    assert(validate_gradient_matrix(geometry::Transform2D{1.0, 0.0, 0.0, 1e-11, 0.0, 0.0}));

    // Non-finite coordinates
    assert(!validate_gradient_matrix(geometry::Transform2D{NAN, 0, 0, 1, 0, 0}));
    assert(!validate_gradient_matrix(geometry::Transform2D{1, 0, 0, 1, INFINITY, 0}));

    // Out of max coordinate bounds (> 1e9)
    assert(!validate_gradient_matrix(geometry::Transform2D{1e10, 0, 0, 1, 0, 0}));

    std::cout << "[PASS] test_matrix_invertibility_guardrail\n";
}

int main() {
    try {
        test_valid_defaults_and_effective_accessor();
        test_reversal_invariants_and_restoration();
        test_validation_boundary_rejections();
        test_matrix_invertibility_guardrail();
        std::cout << "All test_gradient_paint checks PASSED.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Exception in test_gradient_paint: " << e.what() << "\n";
        return 1;
    }
}
