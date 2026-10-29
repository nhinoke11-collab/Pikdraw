#include "pikdraw/geometry/curves/construction.hpp"

#include "pikdraw/geometry/primitives/validation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <numbers>

namespace pikdraw::geometry {
namespace {
constexpr double kTau = 2.0 * std::numbers::pi;

double positive_sweep(double from, double to) noexcept {
    double result = std::fmod(to - from, kTau);
    if (result < 0.0) result += kTau;
    return result;
}

bool valid_circle(const CircleSolution& circle) noexcept {
    return validate_point(circle.center) == GeometryStatus::Ok &&
        std::isfinite(circle.radius) && circle.radius > 0.0 &&
        validate_point({circle.center.x + circle.radius, circle.center.y}) == GeometryStatus::Ok &&
        validate_point({circle.center.x - circle.radius, circle.center.y}) == GeometryStatus::Ok &&
        validate_point({circle.center.x, circle.center.y + circle.radius}) == GeometryStatus::Ok &&
        validate_point({circle.center.x, circle.center.y - circle.radius}) == GeometryStatus::Ok;
}

std::optional<CircleSolution> circumcircle(Point2D a, Point2D b, Point2D c) noexcept {
    if (validate_point(a) != GeometryStatus::Ok || validate_point(b) != GeometryStatus::Ok ||
        validate_point(c) != GeometryStatus::Ok) return std::nullopt;
    const double scale = std::max({std::abs(b.x - a.x), std::abs(b.y - a.y),
                                   std::abs(c.x - a.x), std::abs(c.y - a.y)});
    if (!std::isfinite(scale) || scale <= 0.0) return std::nullopt;
    const double bx = (b.x - a.x) / scale, by = (b.y - a.y) / scale;
    const double cx = (c.x - a.x) / scale, cy = (c.y - a.y) / scale;
    const double determinant = 2.0 * (bx * cy - by * cx);
    if (!std::isfinite(determinant) || std::abs(determinant) <= 128.0 *
        std::numeric_limits<double>::epsilon()) return std::nullopt;
    const double b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
    const double ox = (b2 * cy - c2 * by) / determinant;
    const double oy = (bx * c2 - cx * b2) / determinant;
    const Point2D center{a.x + ox * scale, a.y + oy * scale};
    const double radius = std::hypot(ox, oy) * scale;
    CircleSolution result{center, radius};
    if (!valid_circle(result)) return std::nullopt;
    return result;
}
} // namespace

Point2D ArcSolution::start_point() const noexcept {
    return {center.x + radius * std::cos(start_angle_rad),
            center.y + radius * std::sin(start_angle_rad)};
}

Point2D ArcSolution::end_point() const noexcept {
    const double angle = start_angle_rad + sweep_angle_rad;
    return {center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)};
}

std::optional<CircleSolution> circle_from_center_radius(Point2D center, double radius) noexcept {
    CircleSolution result{center, radius};
    return valid_circle(result) ? std::optional(result) : std::nullopt;
}

std::optional<CircleSolution> circle_from_diameter_points(Point2D first, Point2D second) noexcept {
    if (validate_point(first) != GeometryStatus::Ok || validate_point(second) != GeometryStatus::Ok)
        return std::nullopt;
    const Point2D center{std::midpoint(first.x, second.x), std::midpoint(first.y, second.y)};
    return circle_from_center_radius(center, first.distance_to(second) * 0.5);
}

std::optional<CircleSolution> circle_through_three_points(
    Point2D first, Point2D second, Point2D third) noexcept {
    return circumcircle(first, second, third);
}

std::optional<ArcSolution> arc_through_three_points(
    Point2D start, Point2D through, Point2D end) noexcept {
    const auto circle = circumcircle(start, through, end);
    if (!circle) return std::nullopt;
    const double a0 = std::atan2(start.y - circle->center.y, start.x - circle->center.x);
    const double am = std::atan2(through.y - circle->center.y, through.x - circle->center.x);
    const double a1 = std::atan2(end.y - circle->center.y, end.x - circle->center.x);
    const double ccw_to_end = positive_sweep(a0, a1);
    const double ccw_to_through = positive_sweep(a0, am);
    const double sweep = ccw_to_through <= ccw_to_end ? ccw_to_end : ccw_to_end - kTau;
    if (!std::isfinite(sweep) || std::abs(sweep) <= 128.0 * std::numeric_limits<double>::epsilon() ||
        std::abs(sweep) >= kTau) return std::nullopt;
    return ArcSolution{circle->center, circle->radius, a0, sweep};
}

Path2D make_analytic_circle_path(const CircleSolution& circle) {
    if (!valid_circle(circle)) throw std::invalid_argument("Invalid circle construction");
    Path2D path;
    path.move_to(circle.center.x + circle.radius, circle.center.y);
    path.arc_to(circle.radius, circle.radius, 0.0, false, true,
                circle.center.x - circle.radius, circle.center.y);
    path.arc_to(circle.radius, circle.radius, 0.0, false, true,
                circle.center.x + circle.radius, circle.center.y);
    path.close();
    return path;
}

Path2D make_analytic_arc_path(const ArcSolution& arc) {
    if (!std::isfinite(arc.radius) || arc.radius <= 0.0 ||
        !std::isfinite(arc.start_angle_rad) || !std::isfinite(arc.sweep_angle_rad) ||
        std::abs(arc.sweep_angle_rad) <= 128.0 * std::numeric_limits<double>::epsilon() ||
        std::abs(arc.sweep_angle_rad) >= kTau ||
        !valid_circle({arc.center, arc.radius}))
        throw std::invalid_argument("Invalid arc construction");
    const auto start = arc.start_point();
    const auto end = arc.end_point();
    if (validate_point(start) != GeometryStatus::Ok || validate_point(end) != GeometryStatus::Ok)
        throw std::invalid_argument("Arc endpoints exceed document limits");
    Path2D path;
    path.move_to(start);
    path.arc_to(arc.radius, arc.radius, 0.0, std::abs(arc.sweep_angle_rad) > std::numbers::pi,
                arc.sweep_angle_rad > 0.0, end.x, end.y);
    return path;
}

} // namespace pikdraw::geometry
