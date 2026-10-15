#include "hit_geometry.hpp"
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
    for (const auto &e : object.path.elements()) {
        if (e.verb == geometry::PathVerb::ArcTo) {
            result.unsupported = true;
            return result; // no false arc/bounds hit
        }
        if (e.verb == geometry::PathVerb::QuadTo || e.verb == geometry::PathVerb::CubicTo)
            result.curves = true;
    }
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
std::optional<HitResult> SelectionModel::hit_test_detail(geometry::Point2D point,
                                                         const document::DocumentSnapshot &snapshot,
                                                         double tolerance, bool text_allowed,
                                                         HitDiagnostics *diagnostics) {
    if (diagnostics)
        *diagnostics = {};
    if (!finite_point(point) || !std::isfinite(tolerance) || tolerance < 0)
        return std::nullopt;
    for (auto layer = snapshot.page().layers.rbegin(); layer != snapshot.page().layers.rend(); ++layer) {
        if (!layer->visible || layer->locked)
            continue;
        for (auto id = layer->object_ids.rbegin(); id != layer->object_ids.rend(); ++id) {
            const auto *object = snapshot.objects().lookup_ref(*id);
            if (!object)
                continue;
            if (std::visit([](const auto &o) { return !o.visible || o.locked; }, *object))
                continue;
            const auto hit = std::visit(
                [&](const auto &o) -> std::optional<HitQuality> {
                    using T = std::decay_t<decltype(o)>;
                    if constexpr (std::is_same_v<T, document::PathObject>) {
                        const auto r = path_hit(point, o, tolerance);
                        if (diagnostics && r.unsupported)
                            ++diagnostics->unsupported_objects;
                        if (diagnostics && r.budget_exceeded)
                            ++diagnostics->approximation_budget_exceeded;
                        if (r.hit)
                            return r.curves ? HitQuality::CurveApproximation : HitQuality::SourceLines;
                    } else if constexpr (std::is_same_v<T, document::TextObject>) {
                        if (!text_allowed)
                            return std::nullopt;
                        const auto inv = o.transform.inverted();
                        if (!inv)
                            return std::nullopt;
                        auto identity = o;
                        identity.transform = {};
                        auto b = identity.bounds();
                        b.x -= tolerance;
                        b.y -= tolerance;
                        b.width += 2 * tolerance;
                        b.height += 2 * tolerance;
                        if (b.contains(inv->map_point(point)))
                            return HitQuality::TextApproximation;
                    } else if (diagnostics)
                        ++diagnostics->unsupported_objects;
                    return std::nullopt;
                },
                *object);
            if (hit)
                return HitResult{*id, *hit};
        }
    }
    return std::nullopt;
}
} // namespace pikdraw::vector
