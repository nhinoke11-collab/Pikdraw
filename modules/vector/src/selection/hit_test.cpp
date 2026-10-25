#include "hit_geometry.hpp"
#include "pikdraw/document/hierarchy/frame_geometry.hpp"
#include "pikdraw/vector/gestures/edit_target.hpp"
#include "pikdraw/vector/selection/selection_model.hpp"
#include <cmath>
#include <limits>
namespace pikdraw::vector {
namespace {
double distance(geometry::Point2D p, geometry::Point2D a, geometry::Point2D b) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double length2 = dx * dx + dy * dy;
    const double t = length2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length2, 0.0, 1.0) : 0;
    return std::hypot(p.x - a.x - t * dx, p.y - a.y - t * dy);
}
struct PathHit {
    bool hit{false};
    bool curves{false};
    bool unsupported{false};
    bool budget_exceeded{false};
};
bool stroke_hit(geometry::Point2D world, geometry::Point2D local, geometry::Point2D a, geometry::Point2D b,
                const document::PathObject &object, double tolerance, double maximum_scale,
                bool &budget_exceeded) {
    const double radius = object.stroke_width_mm * .5;
    if (radius <= 0)
        return false;
    if (distance(local, a, b) <= radius)
        return true;
    if (tolerance <= 0 || distance(world, object.transform.map_point(a), object.transform.map_point(b)) >
                              radius * maximum_scale + tolerance)
        return false;
    const double error = std::min(.01, tolerance * .1);
    const double ratio = std::clamp(1 - error / (radius * maximum_scale), -1.0, 1.0);
    const double required = std::ceil(3.141592653589793 / std::max(std::acos(ratio), 1e-12));
    if (required > 4096) {
        budget_exceeded = true;
        return false;
    }
    const int steps = std::max(12, static_cast<int>(required));
    const auto edge = b - a;
    const double length = std::hypot(edge.x, edge.y);
    if (length > 0) {
        const geometry::Point2D normal{-edge.y / length * radius, edge.x / length * radius};
        for (const double sign : {-1.0, 1.0})
            if (distance(world, object.transform.map_point(a + normal * sign),
                         object.transform.map_point(b + normal * sign)) <= tolerance)
                return true;
    }
    // Affine-transformed round caps, with <=0.01mm world chord error.
    for (const auto center : {a, b}) {
        auto previous = object.transform.map_point({center.x + radius, center.y});
        for (int i = 1; i <= steps; ++i) {
            const double angle = 6.283185307179586 * i / steps;
            const auto next = object.transform.map_point(
                {center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)});
            if (distance(world, previous, next) <= tolerance + error)
                return true;
            previous = next;
        }
    }
    return false;
}
PathHit path_hit(geometry::Point2D world, const document::PathObject &object, double tolerance) {
    PathHit result;
    const auto inverse = object.transform.inverted();
    if (!inverse)
        return result;
    const auto &t = object.transform;
    const double frobenius = std::hypot(std::hypot(t.a, t.b), std::hypot(t.c, t.d));
    const auto local = inverse->map_point(world);
    const auto b = object.bounds();
    const double margin = tolerance + (object.has_stroke ? object.stroke_width_mm * frobenius * .5 : 0);
    if (world.x < b.x - margin || world.x > b.right() + margin || world.y < b.y - margin ||
        world.y > b.bottom() + margin)
        return result;
    for (const auto &e : object.path.elements()) {
        if (e.verb == geometry::PathVerb::ArcTo) {
            result.unsupported = true;
            return result; // no false arc/bounds hit
        }
        if (e.verb == geometry::PathVerb::QuadTo || e.verb == geometry::PathVerb::CubicTo)
            result.curves = true;
    }
    const double flatness =
        (tolerance > 0 ? std::min(.01, tolerance * .1) : .01) / std::max(frobenius, 1e-12);
    const auto paths = hit_polylines(object.path, flatness);
    if (!paths) {
        result.budget_exceeded = true;
        return result;
    }
    int winding = 0;
    bool odd = false;
    for (const auto &path : *paths) {
        if (path.points.size() < 2)
            continue;
        for (size_t i = 0; i < path.points.size(); ++i) {
            const auto a = path.points[i], b = path.points[(i + 1) % path.points.size()];
            const bool stroke_edge = i + 1 < path.points.size() || path.is_closed;
            if (stroke_edge && object.has_stroke &&
                stroke_hit(world, local, a, b, object, tolerance, frobenius, result.budget_exceeded))
                result.hit = true;
            // Filled paths implicitly close their final subpath.
            if (!object.has_fill)
                continue;
            if (distance(world, t.map_point(a), t.map_point(b)) <= tolerance)
                result.hit = true;
            if ((a.y > local.y) != (b.y > local.y)) {
                const double x = a.x + (local.y - a.y) * (b.x - a.x) / (b.y - a.y);
                if (x > local.x) {
                    odd = !odd;
                    winding += b.y > a.y ? 1 : -1;
                }
            }
        }
    }
    if (object.has_fill &&
        (object.path.winding_rule() == geometry::WindingRule::EvenOdd ? odd : winding != 0))
        result.hit = true;
    return result;
}
} // namespace
namespace {
std::optional<HitQuality> scene_hit(geometry::Point2D point,
    const document::DocumentSnapshot& snapshot, document::ObjectId id,
    double tolerance, bool text_allowed, HitDiagnostics* diagnostics,
    bool include_locked) {
    const auto* entry = snapshot.hierarchy().find(id);
    const auto* source = snapshot.objects().lookup_ref(id);
    if (!entry || !source) return std::nullopt;
    const bool eligible = include_locked
        ? entry->visible && entry->world.is_finite() && entry->world.is_invertible()
        : snapshot.hierarchy().editable(id);
    if (!eligible) return std::nullopt;
    if (entry->bounds) {
        if (const auto* path = std::get_if<document::PathObject>(source)) {
            const auto& transform = entry->world;
            const double scale = std::hypot(std::hypot(transform.a, transform.b),
                                            std::hypot(transform.c, transform.d));
            const double margin = tolerance +
                (path->has_stroke ? path->stroke_width_mm * scale * .5 : 0.0);
            if (entry->bounds->is_finite() && std::isfinite(margin)) {
                const auto& bounds = *entry->bounds;
                if (point.x < bounds.x - margin || point.x > bounds.right() + margin ||
                    point.y < bounds.y - margin || point.y > bounds.bottom() + margin)
                    return std::nullopt;
            }
        }
    }
    if (entry->bounds && !std::holds_alternative<document::PathObject>(*source)) {
        auto bounds = *entry->bounds;
        bounds.x -= tolerance; bounds.y -= tolerance;
        bounds.width += 2*tolerance; bounds.height += 2*tolerance;
        if (!bounds.contains(point)) return std::nullopt;
    }
    if (const auto* group = std::get_if<document::GroupObject>(source)) {
        if (group->clip) {
            const auto* mask = snapshot.objects().lookup_ref(group->clip->mask_object_id);
            auto geometry = mask ? document::frame_geometry(*mask) : std::nullopt;
            if (!geometry) return std::nullopt;
            geometry->transform = entry->world * geometry->transform;
            geometry->has_fill = true; geometry->has_stroke = false;
            if (!path_hit(point, *geometry, 0).hit) return std::nullopt;
        }
        for (auto it=group->children_ids.rbegin(); it!=group->children_ids.rend(); ++it) {
            if (group->clip && *it==group->clip->mask_object_id) continue;
            if (auto hit=scene_hit(point,snapshot,*it,tolerance,text_allowed,diagnostics,
                                   include_locked)) return hit;
        }
        if (group->clip) return HitQuality::SourceLines;
        return std::nullopt;
    }
    return std::visit([&](const auto& object)->std::optional<HitQuality>{
        using T=std::decay_t<decltype(object)>;
        if constexpr(std::is_same_v<T,document::PathObject>) {
            auto world=object; world.transform=entry->world;
            auto result=path_hit(point,world,tolerance);
            if(diagnostics && result.unsupported) ++diagnostics->unsupported_objects;
            if(diagnostics && result.budget_exceeded) ++diagnostics->approximation_budget_exceeded;
            if(result.hit) return result.curves?HitQuality::CurveApproximation:HitQuality::SourceLines;
        } else if constexpr(std::is_same_v<T,document::TextObject> ||
                            std::is_same_v<T,document::ImageObject>) {
            if constexpr(std::is_same_v<T,document::TextObject>)
                if(!text_allowed) return std::nullopt;
            auto inverse=entry->world.inverted();
            if(!inverse) return std::nullopt;
            geometry::Rect2D bounds;
            if constexpr(std::is_same_v<T,document::ImageObject>) bounds=object.bounds_rect;
            else { auto local=object; local.transform={}; bounds=local.bounds(); }
            bounds.x-=tolerance; bounds.y-=tolerance;
            bounds.width+=2*tolerance; bounds.height+=2*tolerance;
            if(!bounds.contains(inverse->map_point(point))) return std::nullopt;
            if constexpr(std::is_same_v<T,document::ImageObject>) {
                if(object.clip_path) {
                    document::PathObject clip; clip.path=*object.clip_path;
                    clip.transform=entry->world; clip.has_fill=true; clip.has_stroke=false;
                    if(!path_hit(point,clip,0).hit) return std::nullopt;
                }
                return HitQuality::ImageBounds;
            } else return HitQuality::TextApproximation;
        }
        return std::nullopt;
    },*source);
}
}
std::vector<document::ObjectId> SelectionModel::scope_roots(
    const document::DocumentSnapshot& snapshot, document::ObjectId scope, bool frame_only) {
    std::vector<document::ObjectId> ids;
    if(scope) {
        const auto* source=snapshot.objects().lookup_ref(scope);
        if(const auto* group=source?std::get_if<document::GroupObject>(source):nullptr)
            for(auto id:group->children_ids)
                if(frame_only ? (group->clip && id==group->clip->mask_object_id)
                              : (!group->clip || id!=group->clip->mask_object_id)) ids.push_back(id);
    } else for(const auto& layer:snapshot.page().layers)
        ids.insert(ids.end(),layer.object_ids.begin(),layer.object_ids.end());
    return ids;
}
std::optional<HitResult> SelectionModel::hit_test_detail(geometry::Point2D point,
    const document::DocumentSnapshot& snapshot,double tolerance,bool text_allowed,
    HitDiagnostics* diagnostics) {
    return hit_test_detail_candidates(point,snapshot,scope_roots(snapshot,0),
                                     tolerance,text_allowed,diagnostics);
}
std::optional<HitResult> SelectionModel::hit_test_visible_detail(
    geometry::Point2D point, const document::DocumentSnapshot& snapshot,
    double tolerance, bool text_allowed, HitDiagnostics* diagnostics) {
    if (diagnostics) *diagnostics = {};
    if (!finite_point(point) || !std::isfinite(tolerance) || tolerance < 0)
        return std::nullopt;
    const auto candidates = scope_roots(snapshot, 0);
    for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
        if (auto quality = scene_hit(point, snapshot, *it, tolerance,
                                     text_allowed, diagnostics, true))
            return HitResult{*it, *quality};
    }
    return std::nullopt;
}
std::optional<document::ObjectId> SelectionModel::hit_test_in_scope(
    geometry::Point2D point,const document::DocumentSnapshot& snapshot,
    document::ObjectId scope,double tolerance,bool frame_only) {
    const auto result=hit_test_detail_candidates(point,snapshot,scope_roots(snapshot,scope,frame_only),
                                                tolerance,true,nullptr);
    return result?std::optional(result->id):std::nullopt;
}
std::optional<HitResult> SelectionModel::hit_test_detail_candidates(
    geometry::Point2D point,const document::DocumentSnapshot& snapshot,
    std::span<const document::ObjectId> candidates,double tolerance,bool text_allowed,
    HitDiagnostics* diagnostics) {
    if(diagnostics) *diagnostics={};
    if(!finite_point(point)||!std::isfinite(tolerance)||tolerance<0) return std::nullopt;
    for(auto it=candidates.rbegin();it!=candidates.rend();++it)
        if(auto quality=scene_hit(point,snapshot,*it,tolerance,text_allowed,
                                  diagnostics,false))
            return HitResult{*it,*quality};
    return std::nullopt;
}
} // namespace pikdraw::vector
