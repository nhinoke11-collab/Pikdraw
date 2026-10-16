#include "pikdraw/geometry/curves/path.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace pikdraw::geometry {

PathBoundsResult Path2D::bounds_checked() const {
    PathBoundsResult result;
    result.status = GeometryOperationStatus::Success;
    if (elements_.empty()) return result;

    bool has_bounds = false;
    bool has_subpath = false;
    Point2D current{0.0, 0.0};
    Point2D subpath_start{0.0, 0.0};
    double min_x = 0.0;
    double min_y = 0.0;
    double max_x = 0.0;
    double max_y = 0.0;

    auto fail = [&](size_t index) {
        result.status = GeometryOperationStatus::InvalidInput;
        result.element_index = index;
        return result;
    };
    auto expand = [&](const Rect2D& bounds) {
        if (!bounds.is_finite() || bounds.width < 0.0 || bounds.height < 0.0) return false;
        if (!has_bounds) {
            min_x = bounds.left();
            min_y = bounds.top();
            max_x = bounds.right();
            max_y = bounds.bottom();
            has_bounds = true;
        } else {
            min_x = std::min(min_x, bounds.left());
            min_y = std::min(min_y, bounds.top());
            max_x = std::max(max_x, bounds.right());
            max_y = std::max(max_y, bounds.bottom());
        }
        return true;
    };

    for (size_t i = 0; i < elements_.size(); ++i) {
        const auto& element = elements_[i];
        if (!element.pt.is_finite() || !element.ctrl1.is_finite() || !element.ctrl2.is_finite()) {
            return fail(i);
        }
        switch (element.verb) {
            case PathVerb::MoveTo:
                current = element.pt;
                subpath_start = current;
                has_subpath = true;
                if (!expand(Rect2D{current.x, current.y, 0.0, 0.0})) return fail(i);
                break;
            case PathVerb::LineTo:
                if (!has_subpath || !expand(LineSegment{current, element.pt}.bounds())) return fail(i);
                current = element.pt;
                break;
            case PathVerb::QuadTo:
                if (!has_subpath || !expand(QuadraticBezier{current, element.ctrl1, element.pt}.bounds())) return fail(i);
                current = element.pt;
                break;
            case PathVerb::CubicTo:
                if (!has_subpath || !expand(CubicBezier{current, element.ctrl1, element.ctrl2, element.pt}.bounds())) return fail(i);
                current = element.pt;
                break;
            case PathVerb::ArcTo: {
                if (!has_subpath) return fail(i);
                const auto resolved = resolve_endpoint_arc(EndpointArcInput{
                    current, element.rx, element.ry, element.rotation_deg,
                    element.large_arc, element.sweep, element.pt
                });
                if (resolved.kind == ArcResolutionKind::Invalid) return fail(i);
                if (resolved.kind == ArcResolutionKind::Arc) {
                    if (!expand(resolved.arc.bounds()) ||
                        !expand(Rect2D{current.x, current.y, 0.0, 0.0}) ||
                        !expand(Rect2D{element.pt.x, element.pt.y, 0.0, 0.0})) return fail(i);
                } else if (resolved.kind == ArcResolutionKind::Line) {
                    if (!expand(LineSegment{current, element.pt}.bounds())) return fail(i);
                }
                current = element.pt;
                break;
            }
            case PathVerb::Close:
                if (has_subpath) {
                    if (!expand(LineSegment{current, subpath_start}.bounds())) return fail(i);
                    current = subpath_start;
                }
                break;
        }
    }

    if (has_bounds) result.bounds = Rect2D::from_ltrb(min_x, min_y, max_x, max_y);
    return result;
}

Rect2D Path2D::bounds() const {
    const auto result = bounds_checked();
    if (result.status == GeometryOperationStatus::Success) return result.bounds;

    // Compatibility fallback for existing consumers; this is not a validity proof.
    double min_x = 0.0;
    double min_y = 0.0;
    double max_x = 0.0;
    double max_y = 0.0;
    auto include = [&](Point2D point) {
        if (!point.is_finite()) return;
        min_x = std::min(min_x, point.x);
        min_y = std::min(min_y, point.y);
        max_x = std::max(max_x, point.x);
        max_y = std::max(max_y, point.y);
    };
    for (const auto& element : elements_) {
        include(element.pt);
        include(element.ctrl1);
        include(element.ctrl2);
        if (element.verb == PathVerb::ArcTo && std::isfinite(element.rx) && std::isfinite(element.ry)) {
            const double radius = std::max(std::abs(element.rx), std::abs(element.ry));
            include({element.pt.x - radius, element.pt.y - radius});
            include({element.pt.x + radius, element.pt.y + radius});
        }
    }
    return Rect2D::from_ltrb(min_x, min_y, max_x, max_y);
}

PathTransformResult Path2D::transformed_checked(const Transform2D& transform) const {
    PathTransformResult result;
    if (!transform.is_finite()) return result;

    result.status = GeometryOperationStatus::Success;
    result.path.set_winding_rule(winding_rule_);
    Point2D current{0.0, 0.0};
    Point2D subpath_start{0.0, 0.0};
    bool has_subpath = false;

    for (size_t i = 0; i < elements_.size(); ++i) {
        const auto& element = elements_[i];
        PathElement mapped = element;
        switch (element.verb) {
            case PathVerb::MoveTo:
                mapped.pt = transform.map_point(element.pt);
                current = element.pt;
                subpath_start = current;
                has_subpath = true;
                break;
            case PathVerb::LineTo:
                if (!has_subpath) {
                    result.status = GeometryOperationStatus::InvalidInput;
                    result.element_index = i;
                    return result;
                }
                mapped.pt = transform.map_point(element.pt);
                current = element.pt;
                break;
            case PathVerb::QuadTo:
                if (!has_subpath) {
                    result.status = GeometryOperationStatus::InvalidInput;
                    result.element_index = i;
                    return result;
                }
                mapped.ctrl1 = transform.map_point(element.ctrl1);
                mapped.pt = transform.map_point(element.pt);
                current = element.pt;
                break;
            case PathVerb::CubicTo:
                if (!has_subpath) {
                    result.status = GeometryOperationStatus::InvalidInput;
                    result.element_index = i;
                    return result;
                }
                mapped.ctrl1 = transform.map_point(element.ctrl1);
                mapped.ctrl2 = transform.map_point(element.ctrl2);
                mapped.pt = transform.map_point(element.pt);
                current = element.pt;
                break;
            case PathVerb::ArcTo: {
                if (!has_subpath) {
                    result.status = GeometryOperationStatus::InvalidInput;
                    result.element_index = i;
                    return result;
                }
                const auto resolved = resolve_endpoint_arc(EndpointArcInput{
                    current, element.rx, element.ry, element.rotation_deg,
                    element.large_arc, element.sweep, element.pt
                });
                if (resolved.kind == ArcResolutionKind::Invalid) {
                    result.status = GeometryOperationStatus::InvalidInput;
                    result.element_index = i;
                    return result;
                }
                mapped.pt = transform.map_point(element.pt);
                if (resolved.kind == ArcResolutionKind::Arc) {
                    const EllipticArc mapped_arc = resolved.arc.transformed(transform);
                    const auto parameters = encode_endpoint_arc(mapped_arc);
                    if (!parameters) {
                        result.status = GeometryOperationStatus::InvalidParameter;
                        result.element_index = i;
                        return result;
                    }
                    mapped.rx = parameters->rx;
                    mapped.ry = parameters->ry;
                    mapped.rotation_deg = parameters->rotation_deg;
                    mapped.large_arc = parameters->large_arc;
                    mapped.sweep = parameters->sweep;
                }
                current = element.pt;
                break;
            }
            case PathVerb::Close:
                if (has_subpath) current = subpath_start;
                break;
        }
        if (!mapped.pt.is_finite() || !mapped.ctrl1.is_finite() || !mapped.ctrl2.is_finite()) {
            result.status = GeometryOperationStatus::InvalidInput;
            result.element_index = i;
            return result;
        }
        result.path.elements_.push_back(mapped);
    }
    return result;
}

Path2D Path2D::transformed(const Transform2D& transform) const {
    auto result = transformed_checked(transform);
    if (result.status != GeometryOperationStatus::Success) {
        throw std::domain_error("Path2D::transformed rejected geometry at element " +
                                std::to_string(result.element_index));
    }
    return std::move(result.path);
}

Path2D Path2D::make_rect(double x, double y, double w, double h) {
    Path2D path;
    path.move_to(x, y);
    path.line_to(x + w, y);
    path.line_to(x + w, y + h);
    path.line_to(x, y + h);
    path.close();
    return path;
}

Path2D Path2D::make_ellipse(double cx, double cy, double rx, double ry) {
    rx = std::abs(rx);
    ry = std::abs(ry);
    constexpr double kappa = 0.5522847498307935;
    const double kx = rx * kappa;
    const double ky = ry * kappa;
    Path2D path;
    path.move_to(cx + rx, cy);
    path.cubic_to(cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry);
    path.cubic_to(cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy);
    path.cubic_to(cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry);
    path.cubic_to(cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy);
    path.close();
    return path;
}

} // namespace pikdraw::geometry
