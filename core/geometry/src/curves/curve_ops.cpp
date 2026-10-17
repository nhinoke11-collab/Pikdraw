#include "pikdraw/geometry/curves/curve.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <type_traits>
#include <utility>

namespace pikdraw::geometry {
namespace {

bool valid_curve(const CurveSegment& curve) {
    return std::visit([](const auto& segment) {
        using T = std::decay_t<decltype(segment)>;
        if constexpr (std::is_same_v<T, LineSegment>) {
            return segment.p0.is_finite() && segment.p1.is_finite();
        } else if constexpr (std::is_same_v<T, QuadraticBezier>) {
            return segment.p0.is_finite() && segment.p1.is_finite() && segment.p2.is_finite();
        } else if constexpr (std::is_same_v<T, CubicBezier>) {
            return segment.p0.is_finite() && segment.p1.is_finite() &&
                   segment.p2.is_finite() && segment.p3.is_finite();
        } else {
            return segment.is_finite();
        }
    }, curve);
}

double distance_to_rect(Point2D point, const Rect2D& bounds) {
    const double dx = point.x < bounds.left() ? bounds.left() - point.x :
                      point.x > bounds.right() ? point.x - bounds.right() : 0.0;
    const double dy = point.y < bounds.top() ? bounds.top() - point.y :
                      point.y > bounds.bottom() ? point.y - bounds.bottom() : 0.0;
    return std::hypot(dx, dy);
}

Point2D point_on_line(const LineSegment& line, double t) {
    return line.p0 + (line.p1 - line.p0) * t;
}

double cross(Point2D a, Point2D b) {
    return std::fma(a.x, b.y, -a.y * b.x);
}

double dot(Point2D a, Point2D b) {
    return std::fma(a.x, b.x, a.y * b.y);
}

double parameter_on_line(const LineSegment& line, Point2D point) {
    const Point2D direction = line.p1 - line.p0;
    const double length_squared = dot(direction, direction);
    return length_squared == 0.0 ? 0.0 : dot(point - line.p0, direction) / length_squared;
}

} // namespace

Rect2D curve_bounds(const CurveSegment& curve) {
    return std::visit([](const auto& segment) { return segment.bounds(); }, curve);
}

Point2D curve_point_at(const CurveSegment& curve, double t) {
    return std::visit([t](const auto& segment) { return segment.point_at(t); }, curve);
}

Point2D curve_derivative_at(const CurveSegment& curve, double t) {
    return std::visit([t](const auto& segment) { return segment.derivative_at(t); }, curve);
}

CurveSplitResult split_curve(const CurveSegment& curve, double t) {
    CurveSplitResult result;
    if (!valid_curve(curve)) return result;
    if (!std::isfinite(t) || t < 0.0 || t > 1.0) {
        result.status = GeometryOperationStatus::InvalidParameter;
        return result;
    }

    result.status = GeometryOperationStatus::Success;
    std::visit([&](const auto& segment) {
        using T = std::decay_t<decltype(segment)>;
        if constexpr (std::is_same_v<T, LineSegment>) {
            const Point2D middle = segment.point_at(t);
            result.first = CurveSegment{LineSegment{segment.p0, middle}};
            result.second = CurveSegment{LineSegment{middle, segment.p1}};
        } else if constexpr (std::is_same_v<T, QuadraticBezier>) {
            const Point2D p01 = segment.p0 + (segment.p1 - segment.p0) * t;
            const Point2D p12 = segment.p1 + (segment.p2 - segment.p1) * t;
            const Point2D middle = p01 + (p12 - p01) * t;
            result.first = CurveSegment{QuadraticBezier{segment.p0, p01, middle}};
            result.second = CurveSegment{QuadraticBezier{middle, p12, segment.p2}};
        } else if constexpr (std::is_same_v<T, CubicBezier>) {
            const Point2D p01 = segment.p0 + (segment.p1 - segment.p0) * t;
            const Point2D p12 = segment.p1 + (segment.p2 - segment.p1) * t;
            const Point2D p23 = segment.p2 + (segment.p3 - segment.p2) * t;
            const Point2D p012 = p01 + (p12 - p01) * t;
            const Point2D p123 = p12 + (p23 - p12) * t;
            const Point2D middle = p012 + (p123 - p012) * t;
            result.first = CurveSegment{CubicBezier{segment.p0, p01, p012, middle}};
            result.second = CurveSegment{CubicBezier{middle, p123, p23, segment.p3}};
        } else {
            result.first = CurveSegment{segment.sub_arc(0.0, t)};
            result.second = CurveSegment{segment.sub_arc(t, 1.0)};
        }
    }, curve);
    return result;
}

CurveMeasureResult measure_curve_length(const CurveSegment& curve, double tolerance_mm, size_t max_depth) {
    CurveMeasureResult result;
    if (!valid_curve(curve)) return result;
    if (!std::isfinite(tolerance_mm) || tolerance_mm <= 0.0 || max_depth > 20) {
        result.status = GeometryOperationStatus::InvalidParameter;
        return result;
    }

    if (const auto* line = std::get_if<LineSegment>(&curve)) {
        result.status = GeometryOperationStatus::Success;
        result.length = line->p0.distance_to(line->p1);
        result.estimated_error = 0.0;
        return result;
    }

    auto speed = [&](double t) {
        const Point2D derivative = curve_derivative_at(curve, t);
        return std::hypot(derivative.x, derivative.y);
    };
    const double fa = speed(0.0);
    const double fm = speed(0.5);
    const double fb = speed(1.0);
    if (!std::isfinite(fa) || !std::isfinite(fm) || !std::isfinite(fb)) return result;
    const double whole = (fa + 4.0 * fm + fb) / 6.0;
    double total_error = 0.0;
    bool converged = true;

    auto integrate = [&](auto&& self, double a, double b, double left_value,
                         double middle_value, double right_value, double estimate,
                         double tolerance, size_t depth) -> double {
        const double middle = (a + b) * 0.5;
        const double left_middle = (a + middle) * 0.5;
        const double right_middle = (middle + b) * 0.5;
        const double fl = speed(left_middle);
        const double fr = speed(right_middle);
        if (!std::isfinite(fl) || !std::isfinite(fr)) {
            converged = false;
            return 0.0;
        }
        const double left = (middle - a) * (left_value + 4.0 * fl + middle_value) / 6.0;
        const double right = (b - middle) * (middle_value + 4.0 * fr + right_value) / 6.0;
        const double delta = left + right - estimate;
        const double error = std::abs(delta) / 15.0;
        if (error <= tolerance) {
            total_error += error;
            return left + right + delta / 15.0;
        }
        if (depth >= max_depth) {
            total_error += error;
            converged = false;
            return left + right + delta / 15.0;
        }
        return self(self, a, middle, left_value, fl, middle_value, left, tolerance * 0.5, depth + 1) +
               self(self, middle, b, middle_value, fr, right_value, right, tolerance * 0.5, depth + 1);
    };

    result.length = integrate(integrate, 0.0, 1.0, fa, fm, fb, whole, tolerance_mm, 0);
    result.estimated_error = total_error;
    result.status = converged ? GeometryOperationStatus::Success : GeometryOperationStatus::IterationLimit;
    if (!std::isfinite(result.length)) result.status = GeometryOperationStatus::InvalidInput;
    return result;
}

NearestPointResult nearest_point_on_curve(
    const CurveSegment& curve, Point2D query, double tolerance_mm, size_t max_nodes) {
    NearestPointResult result;
    if (!valid_curve(curve) || !query.is_finite()) return result;
    if (!std::isfinite(tolerance_mm) || tolerance_mm <= 0.0 ||
        max_nodes == 0 || max_nodes > 1000000) {
        result.status = GeometryOperationStatus::InvalidParameter;
        return result;
    }

    if (const auto* line = std::get_if<LineSegment>(&curve)) {
        const double t = std::clamp(parameter_on_line(*line, query), 0.0, 1.0);
        result.status = GeometryOperationStatus::Success;
        result.parameter = t;
        result.point = line->point_at(t);
        result.distance = result.point.distance_to(query);
        result.absolute_error_bound = 0.0;
        return result;
    }

    struct Entry {
        CurveSegment curve;
        double t0;
        double t1;
        double lower_bound;
    };
    struct LowerFirst {
        bool operator()(const Entry& left, const Entry& right) const {
            return left.lower_bound > right.lower_bound;
        }
    };
    std::priority_queue<Entry, std::vector<Entry>, LowerFirst> pending;
    const Rect2D root_bounds = curve_bounds(curve);
    if (!root_bounds.is_finite()) return result;
    pending.push({curve, 0.0, 1.0, distance_to_rect(query, root_bounds)});

    double best_distance = std::numeric_limits<double>::infinity();
    double best_parameter = 0.0;
    Point2D best_point{};
    size_t visited = 0;
    auto consider = [&](const CurveSegment& candidate, double local_t, double global_t) {
        const Point2D point = curve_point_at(candidate, local_t);
        if (!point.is_finite()) return false;
        const double distance = point.distance_to(query);
        if (distance < best_distance) {
            best_distance = distance;
            best_parameter = global_t;
            best_point = point;
        }
        return true;
    };

    if (!consider(curve, 0.0, 0.0) || !consider(curve, 0.5, 0.5) || !consider(curve, 1.0, 1.0)) return result;
    while (!pending.empty() && visited < max_nodes) {
        const double lower = pending.top().lower_bound;
        if (best_distance - lower <= tolerance_mm) break;
        Entry entry = pending.top();
        pending.pop();
        ++visited;
        if (entry.lower_bound >= best_distance) continue;

        const auto split = split_curve(entry.curve, 0.5);
        if (split.status != GeometryOperationStatus::Success) return result;
        const double middle = (entry.t0 + entry.t1) * 0.5;
        const Rect2D left_bounds = curve_bounds(*split.first);
        const Rect2D right_bounds = curve_bounds(*split.second);
        if (!left_bounds.is_finite() || !right_bounds.is_finite()) return result;
        if (!consider(*split.first, 0.0, entry.t0) || !consider(*split.first, 1.0, middle)) return result;
        const double left_lower = distance_to_rect(query, left_bounds);
        const double right_lower = distance_to_rect(query, right_bounds);
        if (left_lower < best_distance) pending.push({*split.first, entry.t0, middle, left_lower});
        if (right_lower < best_distance) pending.push({*split.second, middle, entry.t1, right_lower});
    }

    const double lower_bound = pending.empty() ? best_distance : pending.top().lower_bound;
    result.status = (!pending.empty() && best_distance - lower_bound > tolerance_mm)
        ? GeometryOperationStatus::IterationLimit : GeometryOperationStatus::Success;
    result.point = best_point;
    result.parameter = best_parameter;
    result.distance = best_distance;
    result.absolute_error_bound = std::max(0.0, best_distance - lower_bound);
    return result;
}

SegmentIntersectionResult intersect_line_segments(
    const LineSegment& first, const LineSegment& second, double tolerance_mm) {
    SegmentIntersectionResult result;
    if (!first.p0.is_finite() || !first.p1.is_finite() ||
        !second.p0.is_finite() || !second.p1.is_finite() ||
        !std::isfinite(tolerance_mm) || tolerance_mm <= 0.0) {
        result.kind = IntersectionKind::InvalidInput;
        return result;
    }

    const Point2D r = first.p1 - first.p0;
    const Point2D s = second.p1 - second.p0;
    const Point2D q_minus_p = second.p0 - first.p0;
    const double r_length = std::hypot(r.x, r.y);
    const double s_length = std::hypot(s.x, s.y);
    if (r_length <= tolerance_mm && s_length <= tolerance_mm) {
        if (first.p0.distance_to(second.p0) <= tolerance_mm) {
            result.kind = IntersectionKind::Point;
            result.first = (first.p0 + second.p0) * 0.5;
        }
        return result;
    }
    if (r_length <= tolerance_mm) {
        const double u = std::clamp(parameter_on_line(second, first.p0), 0.0, 1.0);
        if (point_on_line(second, u).distance_to(first.p0) <= tolerance_mm) {
            result.kind = IntersectionKind::Point;
            result.first = first.p0;
            result.second_parameter = u;
        }
        return result;
    }
    if (s_length <= tolerance_mm) {
        const double t = std::clamp(parameter_on_line(first, second.p0), 0.0, 1.0);
        if (point_on_line(first, t).distance_to(second.p0) <= tolerance_mm) {
            result.kind = IntersectionKind::Point;
            result.first = second.p0;
            result.first_parameter = t;
        }
        return result;
    }

    const double denominator = cross(r, s);
    const double parallel_tolerance = tolerance_mm * std::max(r_length, s_length);
    if (std::abs(denominator) <= parallel_tolerance) {
        if (std::abs(cross(q_minus_p, r)) > tolerance_mm * r_length) return result;

        const bool use_x = std::abs(r.x) >= std::abs(r.y);
        const double direction = use_x ? r.x : r.y;
        const double first_coordinate = use_x ? first.p0.x : first.p0.y;
        const double other_start = use_x ? second.p0.x : second.p0.y;
        const double other_end = use_x ? second.p1.x : second.p1.y;
        if (direction == 0.0) return result;
        const double t0 = (other_start - first_coordinate) / direction;
        const double t1 = (other_end - first_coordinate) / direction;
        const double low = std::max(0.0, std::min(t0, t1));
        const double high = std::min(1.0, std::max(t0, t1));
        const double parameter_tolerance = tolerance_mm / r_length;
        if (high < low - parameter_tolerance) return result;

        result.first_parameter = std::clamp(low, 0.0, 1.0);
        result.first_overlap_end_parameter = std::clamp(high, 0.0, 1.0);
        result.first = point_on_line(first, result.first_parameter);
        result.second = point_on_line(first, result.first_overlap_end_parameter);
        result.second_parameter = std::clamp(parameter_on_line(second, result.first), 0.0, 1.0);
        result.second_overlap_end_parameter = std::clamp(parameter_on_line(second, result.second), 0.0, 1.0);
        result.kind = high - low <= parameter_tolerance ? IntersectionKind::Point : IntersectionKind::Overlap;
        return result;
    }

    double t = cross(q_minus_p, s) / denominator;
    double u = cross(q_minus_p, r) / denominator;
    const double t_tolerance = tolerance_mm / r_length;
    const double u_tolerance = tolerance_mm / s_length;
    if (t < -t_tolerance || t > 1.0 + t_tolerance ||
        u < -u_tolerance || u > 1.0 + u_tolerance) return result;
    t = std::clamp(t, 0.0, 1.0);
    u = std::clamp(u, 0.0, 1.0);
    const Point2D on_first = point_on_line(first, t);
    const Point2D on_second = point_on_line(second, u);
    result.kind = IntersectionKind::Point;
    result.first = (on_first + on_second) * 0.5;
    result.first_parameter = t;
    result.second_parameter = u;
    return result;
}

} // namespace pikdraw::geometry
