#include "pikdraw/geometry/queries/curve_intersections.hpp"
#include "pikdraw/geometry/curves/curve.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <queue>
#include <type_traits>
#include <utility>
#include <vector>

namespace pikdraw::geometry {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kTau = 2.0 * kPi;

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

double cross(Point2D a, Point2D b) noexcept {
    return std::fma(a.x, b.y, -a.y * b.x);
}

double dot(Point2D a, Point2D b) noexcept {
    return std::fma(a.x, b.x, a.y * b.y);
}

double rect_diagonal(const Rect2D& r) noexcept {
    return std::hypot(r.width, r.height);
}

bool rects_intersect(const Rect2D& a, const Rect2D& b, double slack) noexcept {
    return a.left() - slack <= b.right() && a.right() + slack >= b.left() &&
           a.top() - slack <= b.bottom() && a.bottom() + slack >= b.top();
}

template <typename Func>
std::optional<double> find_scalar_root_brent(
    Func&& f, double x1, double x2, double tol = 1e-12, size_t max_iter = 60) {
    double f1 = f(x1);
    double f2 = f(x2);
    if (!std::isfinite(f1) || !std::isfinite(f2)) return std::nullopt;
    if (std::abs(f1) < 1e-15) return x1;
    if (std::abs(f2) < 1e-15) return x2;
    if (f1 * f2 > 0.0) return std::nullopt;

    double a = x1, b = x2, c = x2;
    double fa = f1, fb = f2, fc = f2;
    double d = 0.0, e = 0.0;

    for (size_t iter = 0; iter < max_iter; ++iter) {
        if ((fb > 0.0 && fc > 0.0) || (fb < 0.0 && fc < 0.0)) {
            c = a;
            fc = fa;
            d = b - a;
            e = d;
        }
        if (std::abs(fc) < std::abs(fb)) {
            a = b; b = c; c = a;
            fa = fb; fb = fc; fc = fa;
        }
        const double tol1 = 2.0 * std::numeric_limits<double>::epsilon() * std::abs(b) + 0.5 * tol;
        const double xm = 0.5 * (c - b);
        if (std::abs(xm) <= tol1 || std::abs(fb) < 1e-15) return b;

        if (std::abs(e) >= tol1 && std::abs(fa) > std::abs(fb)) {
            double s = fb / fa;
            double p, q;
            if (a == c) {
                p = 2.0 * xm * s;
                q = 1.0 - s;
            } else {
                q = fa / fc;
                double r = fb / fc;
                p = s * (2.0 * xm * q * (q - r) - (b - a) * (r - 1.0));
                q = (q - 1.0) * (r - 1.0) * (s - 1.0);
            }
            if (p > 0.0) q = -q;
            p = std::abs(p);
            if (2.0 * p < std::min(3.0 * xm * q - std::abs(tol1 * q), std::abs(e * q))) {
                e = d;
                d = p / q;
            } else {
                d = xm;
                e = d;
            }
        } else {
            d = xm;
            e = d;
        }
        a = b;
        fa = fb;
        if (std::abs(d) > tol1) b += d;
        else b += std::copysign(tol1, xm);
        fb = f(b);
        if (!std::isfinite(fb)) return std::nullopt;
    }
    return b;
}

} // namespace

Point2D find_analytic_center(const EllipticArc& arc) noexcept {
    return arc.center;
}

QuadrantPointsResult find_quadrant_points(const EllipticArc& arc) {
    QuadrantPointsResult result;
    if (!arc.is_finite() || arc.sweep_angle_rad == 0.0) {
        result.status = CurveQueryStatus::InvalidInput;
        return result;
    }

    const double sweep = arc.sweep_angle_rad;
    const double start = arc.start_angle_rad;
    const double end = start + sweep;
    const double min_angle = std::min(start, end);
    const double max_angle = std::max(start, end);

    const int k_min = static_cast<int>(std::floor((min_angle - 2.0 * kPi) / kTau)) - 1;
    const int k_max = static_cast<int>(std::ceil((max_angle + 2.0 * kPi) / kTau)) + 1;

    constexpr double kQuadrants[4] = {0.0, kPi * 0.5, kPi, kPi * 1.5};
    constexpr double kAngularSlack = 1e-9;

    std::vector<RelationPoint> points;
    for (int k = k_min; k <= k_max; ++k) {
        for (double q : kQuadrants) {
            const double target_angle = q + static_cast<double>(k) * kTau;
            const double t = (target_angle - start) / sweep;
            if (t >= -kAngularSlack && t <= 1.0 + kAngularSlack) {
                const double clamped_t = std::clamp(t, 0.0, 1.0);
                const Point2D pt = arc.point_at(clamped_t);
                points.push_back({pt, clamped_t, 0.0});
            }
        }
    }

    std::sort(points.begin(), points.end(), [](const RelationPoint& a, const RelationPoint& b) {
        return a.parameter < b.parameter;
    });

    std::vector<RelationPoint> deduped;
    for (const auto& p : points) {
        if (deduped.empty()) {
            deduped.push_back(p);
        } else if (std::abs(p.parameter - deduped.back().parameter) > 1e-6) {
            // Also ensure we don't duplicate the 0.0 point at 1.0 on a full circle
            if (p.point.distance_to(deduped.front().point) > 1e-4) {
                deduped.push_back(p);
            }
        }
    }

    if (deduped.empty()) {
        result.status = CurveQueryStatus::NoSolution;
    } else {
        result.status = CurveQueryStatus::ExactForPrimitive;
        result.points = std::move(deduped);
    }
    return result;
}

HalfLengthResult find_half_curve_length(
    const CurveSegment& curve, const CurveQueryOptions& options) {
    HalfLengthResult result;
    if (!valid_curve(curve)) {
        result.status = CurveQueryStatus::InvalidInput;
        return result;
    }

    const auto measured = measure_curve_length(curve, 1e-7);
    if (measured.status != GeometryOperationStatus::Success) {
        result.status = CurveQueryStatus::Indeterminate;
        return result;
    }

    result.total_length = measured.length;
    result.half_length = measured.length * 0.5;

    if (result.total_length < 1e-12) {
        result.parameter = 0.5;
        result.point = curve_point_at(curve, 0.5);
        result.residual_mm = 0.0;
        result.status = CurveQueryStatus::ExactForPrimitive;
        return result;
    }

    if (std::holds_alternative<LineSegment>(curve)) {
        result.parameter = 0.5;
        result.point = curve_point_at(curve, 0.5);
        result.residual_mm = 0.0;
        result.status = CurveQueryStatus::ExactForPrimitive;
        return result;
    }

    double t_low = 0.0;
    double t_high = 1.0;
    double best_t = 0.5;
    double best_residual = std::numeric_limits<double>::infinity();

    for (size_t iter = 0; iter < 48; ++iter) {
        const double t_mid = (t_low + t_high) * 0.5;
        const auto split = split_curve(curve, t_mid);
        if (split.status != GeometryOperationStatus::Success || !split.first) {
            break;
        }
        const auto half_measured = measure_curve_length(*split.first, 1e-7);
        if (half_measured.status != GeometryOperationStatus::Success) {
            break;
        }
        const double l1 = half_measured.length;
        const double res = std::abs(l1 - result.half_length);
        if (res < best_residual) {
            best_residual = res;
            best_t = t_mid;
        }
        if (res <= options.max_residual_mm || (t_high - t_low) < 1e-14) {
            break;
        }
        if (l1 < result.half_length) {
            t_low = t_mid;
        } else {
            t_high = t_mid;
        }
    }

    result.parameter = best_t;
    result.point = curve_point_at(curve, best_t);
    result.residual_mm = best_residual;
    result.status = best_residual <= options.max_residual_mm
        ? CurveQueryStatus::BoundedApproximation
        : CurveQueryStatus::Indeterminate;
    return result;
}

RelationQueryResult find_perpendicular_points(
    const CurveSegment& curve, Point2D external_anchor,
    const CurveQueryOptions& options) {
    RelationQueryResult result;
    if (!valid_curve(curve) || !external_anchor.is_finite()) {
        result.status = CurveQueryStatus::InvalidInput;
        return result;
    }

    if (const auto* line = std::get_if<LineSegment>(&curve)) {
        const Point2D d = line->p1 - line->p0;
        const double len_sq = dot(d, d);
        if (len_sq < 1e-14) {
            result.status = CurveQueryStatus::NoSolution;
            return result;
        }
        const double t = dot(external_anchor - line->p0, d) / len_sq;
        if (t >= -1e-7 && t <= 1.0 + 1e-7) {
            const double clamped_t = std::clamp(t, 0.0, 1.0);
            const Point2D pt = line->point_at(clamped_t);
            const double res = std::abs(dot(pt - external_anchor, d)) / std::sqrt(len_sq);
            if (res <= options.max_residual_mm) {
                result.status = CurveQueryStatus::ExactForPrimitive;
                result.points.push_back({pt, clamped_t, res});
                return result;
            }
        }
        result.status = CurveQueryStatus::NoSolution;
        return result;
    }

    auto g = [&](double t) {
        const Point2D pt = curve_point_at(curve, t);
        const Point2D deriv = curve_derivative_at(curve, t);
        return dot(pt - external_anchor, deriv);
    };

    constexpr size_t kSamples = 64;
    std::vector<RelationPoint> roots;

    for (size_t i = 0; i < kSamples; ++i) {
        const double t0 = static_cast<double>(i) / static_cast<double>(kSamples);
        const double t1 = static_cast<double>(i + 1) / static_cast<double>(kSamples);

        const auto root_opt = find_scalar_root_brent(g, t0, t1, 1e-12);
        if (root_opt && *root_opt >= 0.0 && *root_opt <= 1.0) {
            const double t_root = *root_opt;
            const Point2D pt = curve_point_at(curve, t_root);
            const Point2D deriv = curve_derivative_at(curve, t_root);
            const double deriv_len = std::hypot(deriv.x, deriv.y);
            if (deriv_len > 1e-7) {
                const double res = std::abs(dot(pt - external_anchor, deriv)) / deriv_len;
                if (res <= options.max_residual_mm) {
                    roots.push_back({pt, t_root, res});
                }
            }
        }
    }

    std::sort(roots.begin(), roots.end(), [](const RelationPoint& a, const RelationPoint& b) {
        return a.parameter < b.parameter;
    });

    std::vector<RelationPoint> deduped;
    for (const auto& r : roots) {
        if (deduped.empty() || std::abs(r.parameter - deduped.back().parameter) > 1e-4) {
            deduped.push_back(r);
        } else if (r.residual_mm < deduped.back().residual_mm) {
            deduped.back() = r;
        }
    }

    if (deduped.empty()) {
        result.status = CurveQueryStatus::NoSolution;
    } else {
        result.status = CurveQueryStatus::BoundedApproximation;
        result.points = std::move(deduped);
    }
    return result;
}

RelationQueryResult find_tangent_points(
    const CurveSegment& curve, Point2D external_anchor,
    const CurveQueryOptions& options) {
    RelationQueryResult result;
    if (!valid_curve(curve) || !external_anchor.is_finite()) {
        result.status = CurveQueryStatus::InvalidInput;
        return result;
    }

    if (std::holds_alternative<LineSegment>(curve)) {
        result.status = CurveQueryStatus::NoSolution;
        return result;
    }

    auto h = [&](double t) {
        const Point2D pt = curve_point_at(curve, t);
        const Point2D deriv = curve_derivative_at(curve, t);
        return cross(pt - external_anchor, deriv);
    };

    constexpr size_t kSamples = 64;
    std::vector<RelationPoint> roots;

    for (size_t i = 0; i < kSamples; ++i) {
        const double t0 = static_cast<double>(i) / static_cast<double>(kSamples);
        const double t1 = static_cast<double>(i + 1) / static_cast<double>(kSamples);

        const auto root_opt = find_scalar_root_brent(h, t0, t1, 1e-12);
        if (root_opt && *root_opt >= 0.0 && *root_opt <= 1.0) {
            const double t_root = *root_opt;
            const Point2D pt = curve_point_at(curve, t_root);
            const Point2D deriv = curve_derivative_at(curve, t_root);
            const double dist = pt.distance_to(external_anchor);
            const double deriv_len = std::hypot(deriv.x, deriv.y);
            if (dist > 1e-6 && deriv_len > 1e-7) {
                const double res = std::abs(cross(pt - external_anchor, deriv)) / deriv_len;
                if (res <= options.max_residual_mm) {
                    roots.push_back({pt, t_root, res});
                }
            }
        }
    }

    std::sort(roots.begin(), roots.end(), [](const RelationPoint& a, const RelationPoint& b) {
        return a.parameter < b.parameter;
    });

    std::vector<RelationPoint> deduped;
    for (const auto& r : roots) {
        if (deduped.empty() || std::abs(r.parameter - deduped.back().parameter) > 1e-4) {
            deduped.push_back(r);
        } else if (r.residual_mm < deduped.back().residual_mm) {
            deduped.back() = r;
        }
    }

    if (deduped.empty()) {
        result.status = CurveQueryStatus::NoSolution;
    } else {
        result.status = CurveQueryStatus::BoundedApproximation;
        result.points = std::move(deduped);
    }
    return result;
}

CurveIntersectionResult find_curve_intersections(
    const CurveSegment& first, const CurveSegment& second,
    const CurveQueryOptions& options) {
    CurveIntersectionResult result;
    if (!valid_curve(first) || !valid_curve(second)) {
        result.status = CurveQueryStatus::InvalidInput;
        return result;
    }

    const auto* line1 = std::get_if<LineSegment>(&first);
    const auto* line2 = std::get_if<LineSegment>(&second);
    if (line1 && line2) {
        const auto res = intersect_line_segments(*line1, *line2, options.tolerance_mm);
        if (res.kind == IntersectionKind::Point) {
            const double dist = line1->point_at(res.first_parameter).distance_to(
                line2->point_at(res.second_parameter));
            result.status = CurveQueryStatus::ExactForPrimitive;
            result.points.push_back({res.first, res.first_parameter, res.second_parameter, dist});
            return result;
        }
        if (res.kind == IntersectionKind::Overlap) {
            result.status = CurveQueryStatus::Overlap;
            result.first_overlap_interval = {res.first_parameter, res.first_overlap_end_parameter};
            result.second_overlap_interval = {res.second_parameter, res.second_overlap_end_parameter};
            return result;
        }
        result.status = CurveQueryStatus::NoSolution;
        return result;
    }

    struct PairJob {
        CurveSegment c1;
        double u0;
        double u1;
        CurveSegment c2;
        double v0;
        double v1;
        size_t depth;
    };

    std::queue<PairJob> queue;
    queue.push({first, 0.0, 1.0, second, 0.0, 1.0, 0});

    size_t work_units = 0;
    std::vector<IntersectionPoint> candidates;

    while (!queue.empty()) {
        if (++work_units > options.max_work_units) {
            result.status = CurveQueryStatus::BudgetExceeded;
            return result;
        }

        auto job = queue.front();
        queue.pop();

        const Rect2D b1 = curve_bounds(job.c1);
        const Rect2D b2 = curve_bounds(job.c2);
        if (!rects_intersect(b1, b2, options.tolerance_mm)) {
            continue;
        }

        const double diag1 = rect_diagonal(b1);
        const double diag2 = rect_diagonal(b2);

        const bool is_line1 = std::holds_alternative<LineSegment>(job.c1);
        const bool is_line2 = std::holds_alternative<LineSegment>(job.c2);

        constexpr double kSubdivideThreshold = 0.5; // mm
        const bool c1_small = is_line1 || diag1 <= kSubdivideThreshold;
        const bool c2_small = is_line2 || diag2 <= kSubdivideThreshold;

        if ((c1_small && c2_small) || job.depth >= 12 || job.depth >= options.max_subdivision_depth) {
            double u = (job.u0 + job.u1) * 0.5;
            double v = (job.v0 + job.v1) * 0.5;

            // Refine with 2D Newton-Raphson
            for (size_t iter = 0; iter < 6; ++iter) {
                const Point2D p1 = curve_point_at(first, u);
                const Point2D p2 = curve_point_at(second, v);
                const Point2D d1 = curve_derivative_at(first, u);
                const Point2D d2 = curve_derivative_at(second, v);

                const Point2D r = p1 - p2;
                const double det = -cross(d1, d2);
                if (std::abs(det) < 1e-12) break;

                const double du = cross(r, d2) / det;
                const double dv = -cross(d1, r) / det;
                u = std::clamp(u + du, std::max(0.0, job.u0 - 0.05), std::min(1.0, job.u1 + 0.05));
                v = std::clamp(v + dv, std::max(0.0, job.v0 - 0.05), std::min(1.0, job.v1 + 0.05));
            }

            u = std::clamp(u, 0.0, 1.0);
            v = std::clamp(v, 0.0, 1.0);
            const Point2D p1 = curve_point_at(first, u);
            const Point2D p2 = curve_point_at(second, v);
            const double residual = p1.distance_to(p2);

            if (residual <= options.max_residual_mm) {
                const Point2D pt = (p1 + p2) * 0.5;
                bool is_dup = false;
                for (auto& existing : candidates) {
                    if (existing.point.distance_to(pt) < options.tolerance_mm &&
                        std::abs(existing.first_parameter - u) < 1e-3 &&
                        std::abs(existing.second_parameter - v) < 1e-3) {
                        is_dup = true;
                        if (residual < existing.residual_mm) {
                            existing = {pt, u, v, residual};
                        }
                        break;
                    }
                }
                if (!is_dup) {
                    candidates.push_back({pt, u, v, residual});
                }
            }
            continue;
        }

        // Subdivide
        if (!is_line1 && (is_line2 || diag1 >= diag2)) {
            const auto split = split_curve(job.c1, 0.5);
            if (split.status == GeometryOperationStatus::Success && split.first && split.second) {
                const double umid = (job.u0 + job.u1) * 0.5;
                queue.push({*split.first, job.u0, umid, job.c2, job.v0, job.v1, job.depth + 1});
                queue.push({*split.second, umid, job.u1, job.c2, job.v0, job.v1, job.depth + 1});
            }
        } else {
            const auto split = split_curve(job.c2, 0.5);
            if (split.status == GeometryOperationStatus::Success && split.first && split.second) {
                const double vmid = (job.v0 + job.v1) * 0.5;
                queue.push({job.c1, job.u0, job.u1, *split.first, job.v0, vmid, job.depth + 1});
                queue.push({job.c1, job.u0, job.u1, *split.second, vmid, job.v1, job.depth + 1});
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const IntersectionPoint& a, const IntersectionPoint& b) {
                  return a.first_parameter < b.first_parameter;
              });

    if (candidates.empty()) {
        result.status = CurveQueryStatus::NoSolution;
    } else {
        result.status = CurveQueryStatus::BoundedApproximation;
        result.points = std::move(candidates);
    }
    return result;
}

} // namespace pikdraw::geometry
