#include "hit_geometry.hpp"
#include "pikdraw/vector/gestures/edit_target.hpp"
#include "pikdraw/vector/selection/selection_model.hpp"
#include <array>
#include <cmath>

namespace pikdraw::vector {
namespace {
using geometry::Point2D;
using geometry::Rect2D;

double cross(Point2D a, Point2D b, Point2D c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
double point_distance(Point2D p, Point2D a, Point2D b) {
    const auto d = b - a;
    const double length2 = d.x * d.x + d.y * d.y;
    const double t =
        length2 > 0 ? std::clamp(((p.x - a.x) * d.x + (p.y - a.y) * d.y) / length2, 0.0, 1.0) : 0;
    return p.distance_to(a + d * t);
}
bool segments_intersect(Point2D a, Point2D b, Point2D c, Point2D d) {
    const auto bounds = [](Point2D p, Point2D q) {
        return Rect2D{std::min(p.x, q.x), std::min(p.y, q.y), std::abs(p.x - q.x), std::abs(p.y - q.y)};
    };
    if (!bounds(a, b).intersects(bounds(c, d)))
        return false;
    const double ab_c = cross(a, b, c), ab_d = cross(a, b, d);
    const double cd_a = cross(c, d, a), cd_b = cross(c, d, b);
    return ((ab_c <= 0 && ab_d >= 0) || (ab_c >= 0 && ab_d <= 0)) &&
           ((cd_a <= 0 && cd_b >= 0) || (cd_a >= 0 && cd_b <= 0));
}
double segment_distance(Point2D a, Point2D b, Point2D c, Point2D d) {
    if (segments_intersect(a, b, c, d))
        return 0;
    return std::min(
        {point_distance(a, c, d), point_distance(b, c, d), point_distance(c, a, b), point_distance(d, a, b)});
}
std::array<Point2D, 4> corners(const Rect2D &r) {
    return {{{r.x, r.y}, {r.right(), r.y}, {r.right(), r.bottom()}, {r.x, r.bottom()}}};
}
bool contains_rect(const Rect2D &outer, const Rect2D &inner) {
    return outer.contains(inner.min_point()) && outer.contains(inner.max_point());
}
Rect2D painted_bounds(const document::PathObject &object) {
    auto bounds = object.bounds();
    if (object.has_stroke) {
        const double radius = object.stroke_width_mm * .5;
        const double x = radius * std::hypot(object.transform.a, object.transform.c);
        const double y = radius * std::hypot(object.transform.b, object.transform.d);
        bounds = {bounds.x - x, bounds.y - y, bounds.width + 2 * x, bounds.height + 2 * y};
    }
    return bounds;
}
bool path_crosses(const document::PathObject &object, const Rect2D &region) {
    const auto inverse = object.transform.inverted();
    if (!inverse)
        return false;
    const double scale = std::hypot(std::hypot(object.transform.a, object.transform.b),
                                    std::hypot(object.transform.c, object.transform.d));
    const auto paths = hit_polylines(object.path, .01 / std::max(scale, 1e-12));
    if (!paths)
        return false;
    const auto world_corners = corners(region);
    auto local_corners = world_corners;
    for (auto &point : local_corners)
        point = inverse->map_point(point);
    int winding = 0;
    bool odd = false;
    const auto sample = world_corners.front();
    for (const auto &path : *paths) {
        if (path.points.size() < 2)
            continue;
        for (size_t i = 0; i < path.points.size(); ++i) {
            const auto local_a = path.points[i];
            const auto local_b = path.points[(i + 1) % path.points.size()];
            const auto a = object.transform.map_point(local_a);
            const auto b = object.transform.map_point(local_b);
            if (object.has_fill) {
                if (region.contains(a))
                    return true;
                for (size_t edge = 0; edge < 4; ++edge)
                    if (segments_intersect(a, b, world_corners[edge], world_corners[(edge + 1) % 4]))
                        return true;
                if ((a.y > sample.y) != (b.y > sample.y)) {
                    const double x = a.x + (sample.y - a.y) * (b.x - a.x) / (b.y - a.y);
                    if (x > sample.x) {
                        odd = !odd;
                        winding += b.y > a.y ? 1 : -1;
                    }
                }
            }
            if (object.has_stroke && object.stroke_width_mm > 0 &&
                (i + 1 < path.points.size() || path.is_closed)) {
                if (region.contains(a))
                    return true;
                for (size_t edge = 0; edge < 4; ++edge)
                    if (segment_distance(local_a, local_b, local_corners[edge],
                                         local_corners[(edge + 1) % 4]) <= object.stroke_width_mm * .5)
                        return true;
            }
        }
    }
    return object.has_fill &&
           (object.path.winding_rule() == geometry::WindingRule::EvenOdd ? odd : winding != 0);
}
} // namespace

std::optional<Rect2D> SelectionModel::bounds(const document::DocumentSnapshot &snapshot) const {
    std::optional<Rect2D> result;
    for (const auto id : selected_ids_) {
        if (!editable_target(snapshot, id))
            continue;
        const auto *object = snapshot.objects().lookup_ref(id);
        std::visit(
            [&](const auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (!std::is_same_v<T, document::GroupObject>) {
                    const auto b = value.bounds();
                    result = result ? Rect2D::from_ltrb(std::min(result->x, b.x), std::min(result->y, b.y),
                                                        std::max(result->right(), b.right()),
                                                        std::max(result->bottom(), b.bottom()))
                                    : b;
                }
            },
            *object);
    }
    return result;
}

std::vector<document::ObjectId> SelectionModel::query_region(const Rect2D &region, RegionMode mode,
                                                             const document::DocumentSnapshot &snapshot) {
    std::vector<document::ObjectId> result;
    if (!region.is_finite() || region.is_empty())
        return result;
    for (const auto &layer : snapshot.page().layers) {
        if (!layer.visible || layer.locked)
            continue;
        for (const auto id : layer.object_ids) {
            if (!editable_target(snapshot, id))
                continue;
            const auto *object = snapshot.objects().lookup_ref(id);
            const bool selected = std::visit(
                [&](const auto &value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, document::PathObject>) {
                        if (!value.has_fill && (!value.has_stroke || value.stroke_width_mm == 0))
                            return false;
                        const auto b = painted_bounds(value);
                        if (!b.intersects(region))
                            return false;
                        if (mode == RegionMode::Window)
                            return contains_rect(region, b);
                        return path_crosses(value, region);
                    } else if constexpr (std::is_same_v<T, document::TextObject>) {
                        // Logical text bounds remain approximate, consistently with point picking.
                        const auto b = value.bounds();
                        return mode == RegionMode::Window ? contains_rect(region, b) : b.intersects(region);
                    }
                    return false;
                },
                *object);
            if (selected)
                result.push_back(id);
        }
    }
    return result;
}
} // namespace pikdraw::vector
