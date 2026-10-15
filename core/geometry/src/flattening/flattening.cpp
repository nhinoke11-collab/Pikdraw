#include "pikdraw/geometry/flattening/flattening.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace pikdraw::geometry {
namespace {

double distance_to_segment(Point2D point, Point2D start, Point2D end) {
    const Point2D chord = end - start;
    const double length_squared = chord.distance_squared_to({0.0, 0.0});
    if (length_squared == 0.0) return point.distance_to(start);
    const double t = std::clamp(((point.x - start.x) * chord.x + (point.y - start.y) * chord.y) /
                                length_squared, 0.0, 1.0);
    return point.distance_to(start + chord * t);
}

double bezier_flatness(const QuadraticBezier& curve) {
    return distance_to_segment(curve.p1, curve.p0, curve.p2);
}

double bezier_flatness(const CubicBezier& curve) {
    return std::max(distance_to_segment(curve.p1, curve.p0, curve.p3),
                    distance_to_segment(curve.p2, curve.p0, curve.p3));
}

double ellipse_operator_norm(const EllipticArc& arc) {
    const double xx = arc.axis_u.x * arc.axis_u.x + arc.axis_v.x * arc.axis_v.x;
    const double xy = arc.axis_u.x * arc.axis_u.y + arc.axis_v.x * arc.axis_v.y;
    const double yy = arc.axis_u.y * arc.axis_u.y + arc.axis_v.y * arc.axis_v.y;
    const double largest_eigenvalue = (xx + yy + std::hypot(xx - yy, 2.0 * xy)) * 0.5;
    return std::sqrt(std::max(0.0, largest_eigenvalue));
}

double arc_flatness(const EllipticArc& arc, Point2D exact_start, Point2D exact_end) {
    const double sweep = std::abs(arc.sweep_angle_rad);
    if (sweep > 3.14159265358979323846) return std::numeric_limits<double>::infinity();
    const double sine = std::sin(sweep * 0.25);
    const double chord_error = ellipse_operator_norm(arc) * (2.0 * sine * sine);
    return chord_error + std::max(arc.point_at(0.0).distance_to(exact_start),
                                  arc.point_at(1.0).distance_to(exact_end));
}

bool emit_point(Point2D point, std::vector<Point2D>& output, size_t& emitted,
                size_t point_budget, FlatteningStatus& failure) {
    if (emitted >= point_budget) {
        failure = FlatteningStatus::WorkBudgetExceeded;
        return false;
    }
    output.push_back(point);
    ++emitted;
    return true;
}

bool flatten_quad(const QuadraticBezier& curve, double tolerance, size_t depth,
                  size_t max_depth, std::vector<Point2D>& output, size_t& emitted,
                  size_t point_budget, double& unmet, FlatteningStatus& failure) {
    const double error = bezier_flatness(curve);
    if (error <= tolerance) {
        return emit_point(curve.p2, output, emitted, point_budget, failure);
    }
    if (depth >= max_depth) {
        unmet = std::max(unmet, error);
        failure = FlatteningStatus::ToleranceNotMet;
        return false;
    }

    const Point2D p01 = (curve.p0 + curve.p1) * 0.5;
    const Point2D p12 = (curve.p1 + curve.p2) * 0.5;
    const Point2D midpoint = (p01 + p12) * 0.5;
    return flatten_quad({curve.p0, p01, midpoint}, tolerance, depth + 1, max_depth, output,
                        emitted, point_budget, unmet, failure) &&
           flatten_quad({midpoint, p12, curve.p2}, tolerance, depth + 1, max_depth, output,
                        emitted, point_budget, unmet, failure);
}

bool flatten_cubic(const CubicBezier& curve, double tolerance, size_t depth,
                   size_t max_depth, std::vector<Point2D>& output, size_t& emitted,
                   size_t point_budget, double& unmet, FlatteningStatus& failure) {
    const double error = bezier_flatness(curve);
    if (error <= tolerance) {
        return emit_point(curve.p3, output, emitted, point_budget, failure);
    }
    if (depth >= max_depth) {
        unmet = std::max(unmet, error);
        failure = FlatteningStatus::ToleranceNotMet;
        return false;
    }

    const Point2D p01 = (curve.p0 + curve.p1) * 0.5;
    const Point2D p12 = (curve.p1 + curve.p2) * 0.5;
    const Point2D p23 = (curve.p2 + curve.p3) * 0.5;
    const Point2D p012 = (p01 + p12) * 0.5;
    const Point2D p123 = (p12 + p23) * 0.5;
    const Point2D midpoint = (p012 + p123) * 0.5;
    return flatten_cubic({curve.p0, p01, p012, midpoint}, tolerance, depth + 1, max_depth, output,
                         emitted, point_budget, unmet, failure) &&
           flatten_cubic({midpoint, p123, p23, curve.p3}, tolerance, depth + 1, max_depth, output,
                         emitted, point_budget, unmet, failure);
}

bool flatten_arc(const EllipticArc& arc, Point2D exact_start, Point2D exact_end,
                 double tolerance, size_t depth, size_t max_depth,
                 std::vector<Point2D>& output, size_t& emitted, size_t point_budget,
                 double& unmet, FlatteningStatus& failure) {
    const double error = arc_flatness(arc, exact_start, exact_end);
    if (error <= tolerance) {
        return emit_point(exact_end, output, emitted, point_budget, failure);
    }
    if (depth >= max_depth) {
        unmet = std::max(unmet, error);
        failure = FlatteningStatus::ToleranceNotMet;
        return false;
    }

    const EllipticArc first = arc.sub_arc(0.0, 0.5);
    const EllipticArc second = arc.sub_arc(0.5, 1.0);
    const Point2D midpoint = first.point_at(1.0);
    return flatten_arc(first, exact_start, midpoint, tolerance, depth + 1, max_depth,
                       output, emitted, point_budget, unmet, failure) &&
           flatten_arc(second, midpoint, exact_end, tolerance, depth + 1, max_depth,
                       output, emitted, point_budget, unmet, failure);
}

} // namespace

FlatteningResult flatten_path_checked(const Path2D& path, const FlatteningOptions& options) {
    FlatteningResult result;
    result.status = FlatteningStatus::Success;
    if (!std::isfinite(options.tolerance_mm) || options.tolerance_mm <= 0.0 ||
        options.max_subdivision_depth > 32 || options.max_output_points == 0) {
        result.status = FlatteningStatus::InvalidOptions;
        return result;
    }

    FlattenedSubpath current;
    Point2D current_point{};
    Point2D subpath_start{};
    bool has_subpath = false;
    bool continuation_pending = false;
    size_t emitted_points = 0;
    auto fail = [&](FlatteningStatus status, size_t index) {
        result.status = status;
        result.element_index = index;
        result.subpaths.clear();
        return result;
    };
    auto finish_subpath = [&]() {
        if (!current.points.empty()) result.subpaths.push_back(std::move(current));
        current = FlattenedSubpath{};
    };
    auto begin_continuation = [&]() {
        if (continuation_pending) {
            if (!emit_point(current_point, current.points, emitted_points,
                            options.max_output_points, result.status)) return false;
            subpath_start = current_point;
            continuation_pending = false;
        }
        return true;
    };

    const auto& elements = path.elements();
    for (size_t i = 0; i < elements.size(); ++i) {
        const auto& element = elements[i];
        if (!element.pt.is_finite() || !element.ctrl1.is_finite() || !element.ctrl2.is_finite()) {
            return fail(FlatteningStatus::InvalidPath, i);
        }
        switch (element.verb) {
            case PathVerb::MoveTo:
                finish_subpath();
                current_point = element.pt;
                subpath_start = current_point;
                if (!emit_point(current_point, current.points, emitted_points,
                                options.max_output_points, result.status)) return fail(result.status, i);
                has_subpath = true;
                continuation_pending = false;
                break;
            case PathVerb::LineTo:
                if (!has_subpath) return fail(FlatteningStatus::InvalidPath, i);
                if (!begin_continuation() || !emit_point(element.pt, current.points, emitted_points,
                                                          options.max_output_points, result.status)) {
                    return fail(result.status, i);
                }
                current_point = element.pt;
                break;
            case PathVerb::QuadTo: {
                if (!has_subpath) return fail(FlatteningStatus::InvalidPath, i);
                if (!begin_continuation()) return fail(result.status, i);
                double unmet = 0.0;
                if (!flatten_quad({current_point, element.ctrl1, element.pt}, options.tolerance_mm,
                                  0, options.max_subdivision_depth, current.points, emitted_points,
                                  options.max_output_points, unmet, result.status)) {
                    result.unmet_error_bound_mm = unmet;
                    return fail(result.status, i);
                }
                current_point = element.pt;
                break;
            }
            case PathVerb::CubicTo: {
                if (!has_subpath) return fail(FlatteningStatus::InvalidPath, i);
                if (!begin_continuation()) return fail(result.status, i);
                double unmet = 0.0;
                if (!flatten_cubic({current_point, element.ctrl1, element.ctrl2, element.pt}, options.tolerance_mm,
                                   0, options.max_subdivision_depth, current.points, emitted_points,
                                   options.max_output_points, unmet, result.status)) {
                    result.unmet_error_bound_mm = unmet;
                    return fail(result.status, i);
                }
                current_point = element.pt;
                break;
            }
            case PathVerb::ArcTo: {
                if (!has_subpath) return fail(FlatteningStatus::InvalidPath, i);
                if (!begin_continuation()) return fail(result.status, i);
                const auto arc = resolve_endpoint_arc(EndpointArcInput{
                    current_point, element.rx, element.ry, element.rotation_deg,
                    element.large_arc, element.sweep, element.pt
                });
                if (arc.kind == ArcResolutionKind::Invalid) return fail(FlatteningStatus::InvalidPath, i);
                if (arc.kind == ArcResolutionKind::Arc) {
                    double unmet = 0.0;
                    if (!flatten_arc(arc.arc, current_point, element.pt, options.tolerance_mm, 0,
                                     options.max_subdivision_depth, current.points, emitted_points,
                                     options.max_output_points, unmet, result.status)) {
                        result.unmet_error_bound_mm = unmet;
                        return fail(result.status, i);
                    }
                } else if (arc.kind == ArcResolutionKind::Line) {
                    if (!emit_point(element.pt, current.points, emitted_points,
                                    options.max_output_points, result.status)) return fail(result.status, i);
                }
                current_point = element.pt;
                break;
            }
            case PathVerb::Close:
                if (has_subpath) {
                    current.is_closed = true;
                    current_point = subpath_start;
                    finish_subpath();
                    continuation_pending = true;
                }
                break;
        }
    }
    finish_subpath();
    return result;
}

std::vector<FlattenedSubpath> flatten_path(const Path2D& path, const FlatteningOptions& options) {
    auto result = flatten_path_checked(path, options);
    if (result.status != FlatteningStatus::Success) {
        throw std::runtime_error("flatten_path failed at element " + std::to_string(result.element_index) +
                                 " with status " + std::to_string(static_cast<int>(result.status)));
    }
    return std::move(result.subpaths);
}

} // namespace pikdraw::geometry
