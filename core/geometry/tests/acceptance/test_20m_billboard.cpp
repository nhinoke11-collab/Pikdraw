#include "test_harness.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"
#include "pikdraw/geometry/primitives/validation.hpp"
#include "pikdraw/geometry/curves/path.hpp"

using namespace pikdraw::geometry;

PIKDRAW_TEST(signage_20m_billboard_precision) {
    // 20-meter billboard: 20,000 mm width x 1,000 mm height
    const double board_width = 20000.0;
    const double board_height = 1000.0;

    Rect2D billboard{0.0, 0.0, board_width, board_height};
    PIKDRAW_ASSERT(billboard.is_finite());
    PIKDRAW_ASSERT_NEAR(billboard.right(), 20000.0, 1e-9);

    // Fine manufacturing notch: 0.05 mm notch at the far end x = 20,000 mm
    const double notch_width = 0.05;
    const double notch_depth = 2.0;
    Rect2D notch{board_width - notch_width, 500.0 - notch_depth * 0.5, notch_width, notch_depth};

    PIKDRAW_ASSERT(notch.is_finite());
    PIKDRAW_ASSERT_NEAR(notch.width, 0.05, 1e-9);
    PIKDRAW_ASSERT_NEAR(notch.right(), 20000.0, 1e-9);
    PIKDRAW_ASSERT(billboard.contains(notch.min_point()));
    PIKDRAW_ASSERT(billboard.contains(notch.max_point()));

    // Affine transform with translation and reflection at 20 meters
    auto trans = Transform2D::translation(20000.0, 1000.0);
    auto refl = Transform2D::scale(-1.0, 1.0); // reflection
    auto combined = trans * refl;

    Point2D far_point{board_width, board_height};
    Point2D mapped = combined.map_point(far_point);

    auto inv = combined.inverted();
    PIKDRAW_ASSERT(inv.has_value());
    Point2D unmapped = inv->map_point(mapped);

    PIKDRAW_ASSERT_NEAR(unmapped.x, far_point.x, limits::kTransformInversionToleranceMm);
    PIKDRAW_ASSERT_NEAR(unmapped.y, far_point.y, limits::kTransformInversionToleranceMm);
}
