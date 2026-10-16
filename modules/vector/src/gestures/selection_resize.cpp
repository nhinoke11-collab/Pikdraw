#include "pikdraw/vector/gestures/selection_resize.hpp"

namespace pikdraw::vector {
std::optional<geometry::Transform2D> selection_resize_transform(const geometry::Rect2D &bounds, int handle,
                                                                geometry::Point2D drag_delta,
                                                                bool preserve_aspect) {
    if (!bounds.is_finite() || handle < 0 || handle > 7 || !std::isfinite(drag_delta.x) ||
        !std::isfinite(drag_delta.y))
        return std::nullopt;
    constexpr int horizontal[] = {-1, 0, 1, 1, 1, 0, -1, -1};
    constexpr int vertical[] = {1, 1, 1, 0, -1, -1, -1, 0};
    const int hx = horizontal[handle], hy = vertical[handle];
    if ((hx != 0 && bounds.width <= 1e-9) || (hy != 0 && bounds.height <= 1e-9))
        return std::nullopt;
    const double anchor_x = hx < 0 ? bounds.right() : hx > 0 ? bounds.x : bounds.center().x;
    const double anchor_y = hy < 0 ? bounds.bottom() : hy > 0 ? bounds.y : bounds.center().y;
    double sx = hx == 0 ? 1 : 1 + drag_delta.x / (hx * bounds.width);
    double sy = hy == 0 ? 1 : 1 + drag_delta.y / (hy * bounds.height);
    if (preserve_aspect && hx != 0 && hy != 0) {
        const double magnitude = std::abs(sx - 1) > std::abs(sy - 1) ? std::abs(sx) : std::abs(sy);
        sx = std::copysign(magnitude, sx);
        sy = std::copysign(magnitude, sy);
    }
    // A zero-scale intermediate must never collapse editable source geometry.
    if (std::abs(sx) < 1e-6 || std::abs(sy) < 1e-6)
        return std::nullopt;
    auto result = geometry::Transform2D::translation(anchor_x, anchor_y) *
                  geometry::Transform2D::scale(sx, sy) *
                  geometry::Transform2D::translation(-anchor_x, -anchor_y);
    return result.is_finite() && result.is_invertible() ? std::optional(result) : std::nullopt;
}
} // namespace pikdraw::vector
