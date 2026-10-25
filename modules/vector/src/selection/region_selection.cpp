#include "pikdraw/document/hierarchy/frame_geometry.hpp"
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
bool path_has_segments(const geometry::Path2D& path) {
    return std::any_of(path.elements().begin(), path.elements().end(), [](const auto& element) {
        return element.verb != geometry::PathVerb::MoveTo;
    });
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

std::optional<Rect2D> SelectionModel::bounds(const document::DocumentSnapshot& snapshot) const {
    std::optional<Rect2D> bounds;
    for(auto id:snapshot.hierarchy().roots(selected_ids_)) {
        const auto* entry=snapshot.hierarchy().find(id);
        if(!entry || !editable_target(snapshot,id) || !entry->bounds) continue;
        bounds=bounds?bounds->united(*entry->bounds):entry->bounds;
    }
    return bounds;
}
std::vector<document::ObjectId> SelectionModel::query_region(const Rect2D &region, RegionMode mode,
    const document::DocumentSnapshot &snapshot) {
    return query_region_in_scope(region, mode, snapshot, 0);
}
std::vector<document::ObjectId> SelectionModel::query_region_in_scope(const Rect2D &region,
    RegionMode mode, const document::DocumentSnapshot &snapshot, document::ObjectId scope) {
    return query_region_candidates(region, mode, snapshot, scope_roots(snapshot, scope));
}

std::vector<document::ObjectId> SelectionModel::query_region_candidates(
    const Rect2D &region, RegionMode mode,
    const document::DocumentSnapshot &snapshot,
    std::span<const document::ObjectId> candidates) {
    std::vector<document::ObjectId> result;
    if (!region.is_finite() || region.is_empty())
        return result;
    result.reserve(candidates.size());
    for (const auto id : candidates) {
        const auto *object = snapshot.objects().lookup_ref(id);
        if (!object || !editable_target(snapshot, id))
            continue;
        if (const auto* path = std::get_if<document::PathObject>(object)) {
            const auto* entry = snapshot.hierarchy().find(id);
            if (!entry || !entry->bounds)
                continue;
            auto broad_bounds = *entry->bounds;
            if (path->has_stroke) {
                const double radius = path->stroke_width_mm * .5;
                const double x_margin = radius * std::hypot(entry->world.a, entry->world.c);
                const double y_margin = radius * std::hypot(entry->world.b, entry->world.d);
                const double left = broad_bounds.x - x_margin;
                const double top = broad_bounds.y - y_margin;
                const double width = broad_bounds.width + 2.0 * x_margin;
                const double height = broad_bounds.height + 2.0 * y_margin;
                const Rect2D expanded{left, top, width, height};
                if (!expanded.is_finite()) continue;
                broad_bounds = expanded;
            }
            if (!broad_bounds.is_finite()) continue;
            // SceneHierarchy already stores immutable world geometry bounds. Rejecting disjoint
            // candidates here avoids recomputing path bounds and flattening off-screen paths.
            if (!broad_bounds.intersects(region))
                continue;

            bool selected = false;
            if ((path->has_fill || (path->has_stroke && path->stroke_width_mm > 0.0)) &&
                path_has_segments(path->path)) {
                if (mode == RegionMode::Window) {
                    selected = contains_rect(region, broad_bounds);
                } else if (contains_rect(region, *entry->bounds)) {
                    // Every non-empty segment is inside the selection region, so exact edge
                    // flattening cannot change the crossing result.
                    selected = true;
                } else {
                    auto world = *path;
                    world.transform = entry->world;
                    selected = path_crosses(world, region);
                }
            }
            if (selected) result.push_back(id);
            continue;
        }
        const bool selected = std::visit(
            [&](const auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, document::PathObject>) {
                    return false;
                } else if constexpr(std::is_same_v<T,document::GroupObject>) {
                    const auto* entry=snapshot.hierarchy().find(id);
                    if(!entry||!entry->bounds)return false;
                    if(mode==RegionMode::Window)return contains_rect(region,*entry->bounds);
                    if(value.clip) {
                        const auto* mask=snapshot.objects().lookup_ref(value.clip->mask_object_id);
                        auto geometry=mask?document::frame_geometry(*mask):std::nullopt;
                        if(!geometry)return false;
                        geometry->transform=entry->world*geometry->transform;
                        geometry->has_fill=true;geometry->has_stroke=false;
                        return path_crosses(*geometry,region);
                    }
                    return !query_region_candidates(region,mode,snapshot,value.children_ids).empty();
                } else if constexpr(std::is_same_v<T,document::ImageObject>) {
                    const auto* entry=snapshot.hierarchy().find(id);if(!entry)return false;
                    document::PathObject shape;
                    shape.path=value.clip_path?*value.clip_path:geometry::Path2D::make_rect(
                        value.bounds_rect.x,value.bounds_rect.y,value.bounds_rect.width,value.bounds_rect.height);
                    shape.transform=entry->world;shape.has_fill=true;shape.has_stroke=false;
                    const auto bounds=shape.bounds();
                    return mode==RegionMode::Window?contains_rect(region,bounds):path_crosses(shape,region);
                } else if constexpr(std::is_same_v<T,document::TextObject>) {
                    const auto* entry=snapshot.hierarchy().find(id);
                    if(!entry||!entry->bounds)return false;
                    return mode==RegionMode::Window?contains_rect(region,*entry->bounds):entry->bounds->intersects(region);
                }
                return false;
            },
            *object);
        if (selected)
            result.push_back(id);
    }
    return result;
}
} // namespace pikdraw::vector
