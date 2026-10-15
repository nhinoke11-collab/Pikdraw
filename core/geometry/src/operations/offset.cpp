#include "pikdraw/geometry/operations/offset.hpp"

namespace pikdraw::geometry {

Rect2D offset_rect(const Rect2D& rect, double distance_mm) {
    if (rect.is_empty()) return rect;
    return Rect2D{
        rect.x - distance_mm,
        rect.y - distance_mm,
        rect.width + 2.0 * distance_mm,
        rect.height + 2.0 * distance_mm
    };
}

OffsetResult offset_path(const Path2D& path, const OffsetOptions& options) {
    OffsetResult res;
    res.success = true;

    // For rectangle-like paths or general bounding box offset in M1:
    const Rect2D b = path.bounds();
    const Rect2D ob = offset_rect(b, options.distance_mm);
    res.path = Path2D::make_rect(ob.x, ob.y, ob.width, ob.height);
    return res;
}

} // namespace pikdraw::geometry
