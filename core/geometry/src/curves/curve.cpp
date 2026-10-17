#include "pikdraw/geometry/curves/curve.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace pikdraw::geometry {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kTau = 2.0 * kPi;

double normalized_positive(double angle) {
    double result = std::fmod(angle, kTau);
    if (result < 0.0) result += kTau;
    return result;
}

bool angle_on_sweep(double angle, double start, double sweep) {
    if (std::abs(sweep) >= kTau) return true;
    constexpr double angular_slack = 32.0 * std::numeric_limits<double>::epsilon();
    if (sweep >= 0.0) {
        return normalized_positive(angle - start) <= sweep + angular_slack;
    }
    return normalized_positive(start - angle) <= -sweep + angular_slack;
}

void include_point(Point2D point, double& min_x, double& min_y, double& max_x, double& max_y) {
    min_x = std::min(min_x, point.x);
    min_y = std::min(min_y, point.y);
    max_x = std::max(max_x, point.x);
    max_y = std::max(max_y, point.y);
}

template <typename Evaluate>
void include_cubic_extrema(
    double p0, double p1, double p2, double p3,
    double& low, double& high, Evaluate evaluate) {
    const double d0 = p1 - p0;
    const double d1 = p2 - p1;
    const double d2 = p3 - p2;
    const double a = d0 - 2.0 * d1 + d2;
    const double b = 2.0 * (d1 - d0);
    const double c = d0;
    const double scale = std::max({std::abs(a), std::abs(b), std::abs(c)});
    const double epsilon = 16.0 * std::numeric_limits<double>::epsilon() * scale;

    auto consider = [&](double t) {
        if (t > 0.0 && t < 1.0) {
            const double value = evaluate(t);
            low = std::min(low, value);
            high = std::max(high, value);
        }
    };

    if (std::abs(a) <= epsilon) {
        if (std::abs(b) > epsilon) consider(-c / b);
        return;
    }

    double discriminant = std::fma(b, b, -4.0 * a * c);
    const double discriminant_scale = std::abs(b * b) + std::abs(4.0 * a * c);
    const double discriminant_slack = 32.0 * std::numeric_limits<double>::epsilon() * discriminant_scale;
    if (discriminant < -discriminant_slack) return;
    discriminant = std::max(0.0, discriminant);

    const double root = std::sqrt(discriminant);
    const double q = -0.5 * (b + std::copysign(root, b));
    if (q == 0.0) {
        consider(-b / (2.0 * a));
        return;
    }
    consider(q / a);
    consider(c / q);
}

} // namespace

Point2D CubicBezier::derivative_at(double t) const {
    const double u = 1.0 - t;
    return (p1 - p0) * (3.0 * u * u) +
           (p2 - p1) * (6.0 * u * t) +
           (p3 - p2) * (3.0 * t * t);
}

Rect2D CubicBezier::bounds() const {
    double min_x = std::min(p0.x, p3.x);
    double max_x = std::max(p0.x, p3.x);
    double min_y = std::min(p0.y, p3.y);
    double max_y = std::max(p0.y, p3.y);
    include_cubic_extrema(p0.x, p1.x, p2.x, p3.x, min_x, max_x,
                          [this](double t) { return point_at(t).x; });
    include_cubic_extrema(p0.y, p1.y, p2.y, p3.y, min_y, max_y,
                          [this](double t) { return point_at(t).y; });
    const double coordinate_scale = std::max({
        1.0, std::abs(p0.x), std::abs(p0.y), std::abs(p1.x), std::abs(p1.y),
        std::abs(p2.x), std::abs(p2.y), std::abs(p3.x), std::abs(p3.y)
    });
    const double padding = 64.0 * std::numeric_limits<double>::epsilon() * coordinate_scale;
    min_x = std::nextafter(min_x - padding, -std::numeric_limits<double>::infinity());
    min_y = std::nextafter(min_y - padding, -std::numeric_limits<double>::infinity());
    max_x = std::nextafter(max_x + padding, std::numeric_limits<double>::infinity());
    max_y = std::nextafter(max_y + padding, std::numeric_limits<double>::infinity());
    return Rect2D::from_ltrb(min_x, min_y, max_x, max_y);
}

Point2D EllipticArc::point_at(double t) const {
    const double angle = std::fma(sweep_angle_rad, t, start_angle_rad);
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    return {
        std::fma(axis_v.x, sine, std::fma(axis_u.x, cosine, center.x)),
        std::fma(axis_v.y, sine, std::fma(axis_u.y, cosine, center.y))
    };
}

Point2D EllipticArc::derivative_at(double t) const {
    const double angle = std::fma(sweep_angle_rad, t, start_angle_rad);
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    return {
        sweep_angle_rad * (-axis_u.x * sine + axis_v.x * cosine),
        sweep_angle_rad * (-axis_u.y * sine + axis_v.y * cosine)
    };
}

bool EllipticArc::is_finite() const {
    return center.is_finite() && axis_u.is_finite() && axis_v.is_finite() &&
           std::isfinite(start_angle_rad) && std::isfinite(sweep_angle_rad);
}

EllipticArc EllipticArc::sub_arc(double t0, double t1) const {
    return EllipticArc{center, axis_u, axis_v,
                       std::fma(sweep_angle_rad, t0, start_angle_rad),
                       sweep_angle_rad * (t1 - t0)};
}

EllipticArc EllipticArc::transformed(const Transform2D& transform) const {
    return EllipticArc{transform.map_point(center),
                       transform.map_vector(axis_u),
                       transform.map_vector(axis_v),
                       start_angle_rad, sweep_angle_rad};
}

Rect2D EllipticArc::bounds() const {
    const Point2D first = point_at(0.0);
    const Point2D last = point_at(1.0);
    if (sweep_angle_rad == 0.0) return Rect2D{first.x, first.y, 0.0, 0.0};
    double min_x = std::min(first.x, last.x);
    double max_x = std::max(first.x, last.x);
    double min_y = std::min(first.y, last.y);
    double max_y = std::max(first.y, last.y);

    const double x_extremum = std::atan2(axis_v.x, axis_u.x);
    const double y_extremum = std::atan2(axis_v.y, axis_u.y);
    for (double angle : std::array<double, 4>{
             x_extremum, x_extremum + kPi, y_extremum, y_extremum + kPi}) {
        if (angle_on_sweep(angle, start_angle_rad, sweep_angle_rad)) {
            include_point(point_at((angle - start_angle_rad) / sweep_angle_rad),
                          min_x, min_y, max_x, max_y);
        } else if (std::abs(sweep_angle_rad) >= kTau) {
            include_point(point_at(0.0), min_x, min_y, max_x, max_y);
        }
    }
    const double coordinate_scale = std::max({
        1.0, std::abs(center.x), std::abs(center.y), std::abs(axis_u.x), std::abs(axis_u.y),
        std::abs(axis_v.x), std::abs(axis_v.y), std::abs(min_x), std::abs(min_y),
        std::abs(max_x), std::abs(max_y)
    });
    const double padding = 64.0 * std::numeric_limits<double>::epsilon() * coordinate_scale;
    min_x = std::nextafter(min_x - padding, -std::numeric_limits<double>::infinity());
    min_y = std::nextafter(min_y - padding, -std::numeric_limits<double>::infinity());
    max_x = std::nextafter(max_x + padding, std::numeric_limits<double>::infinity());
    max_y = std::nextafter(max_y + padding, std::numeric_limits<double>::infinity());
    return Rect2D::from_ltrb(min_x, min_y, max_x, max_y);
}

ArcResolutionResult resolve_endpoint_arc(const EndpointArcInput& input) {
    if (!input.p0.is_finite() || !input.p1.is_finite() ||
        !std::isfinite(input.rx) || !std::isfinite(input.ry) ||
        !std::isfinite(input.rotation_deg)) {
        return {ArcResolutionKind::Invalid, {}};
    }

    double rx = std::abs(input.rx);
    double ry = std::abs(input.ry);
    if (input.p0 == input.p1) return {ArcResolutionKind::Empty, {}};
    if (rx == 0.0 || ry == 0.0) return {ArcResolutionKind::Line, {}};

    const double phi = input.rotation_deg * (kPi / 180.0);
    const double cosine = std::cos(phi);
    const double sine = std::sin(phi);
    const double half_dx = input.p0.x * 0.5 - input.p1.x * 0.5;
    const double half_dy = input.p0.y * 0.5 - input.p1.y * 0.5;
    const double x_prime = cosine * half_dx + sine * half_dy;
    const double y_prime = -sine * half_dx + cosine * half_dy;
    const double rx2 = rx * rx;
    const double ry2 = ry * ry;
    const double lambda = (x_prime * x_prime) / rx2 + (y_prime * y_prime) / ry2;
    if (!std::isfinite(lambda)) return {ArcResolutionKind::Invalid, {}};
    if (lambda > 1.0) {
        const double scale = std::sqrt(lambda);
        rx *= scale;
        ry *= scale;
    }

    const double scaled_rx2 = rx * rx;
    const double scaled_ry2 = ry * ry;
    const double denominator = scaled_rx2 * y_prime * y_prime + scaled_ry2 * x_prime * x_prime;
    if (!(denominator > 0.0) || !std::isfinite(denominator)) {
        return {ArcResolutionKind::Invalid, {}};
    }
    const double numerator = std::max(0.0,
        scaled_rx2 * scaled_ry2 - scaled_rx2 * y_prime * y_prime - scaled_ry2 * x_prime * x_prime);
    const double sign = (input.large_arc == input.sweep) ? -1.0 : 1.0;
    const double coefficient = sign * std::sqrt(numerator / denominator);
    const double center_x_prime = coefficient * (rx * y_prime / ry);
    const double center_y_prime = coefficient * (-ry * x_prime / rx);
    const Point2D midpoint{input.p0.x * 0.5 + input.p1.x * 0.5,
                           input.p0.y * 0.5 + input.p1.y * 0.5};
    const Point2D center{
        cosine * center_x_prime - sine * center_y_prime + midpoint.x,
        sine * center_x_prime + cosine * center_y_prime + midpoint.y
    };

    const double ux = (x_prime - center_x_prime) / rx;
    const double uy = (y_prime - center_y_prime) / ry;
    const double vx = (-x_prime - center_x_prime) / rx;
    const double vy = (-y_prime - center_y_prime) / ry;
    const double start = std::atan2(uy, ux);
    double sweep = std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    if (!input.sweep && sweep > 0.0) sweep -= kTau;
    if (input.sweep && sweep < 0.0) sweep += kTau;

    EllipticArc arc{center, {rx * cosine, rx * sine}, {-ry * sine, ry * cosine}, start, sweep};
    if (!arc.is_finite() || !arc.point_at(0.0).is_finite() || !arc.point_at(1.0).is_finite()) {
        return {ArcResolutionKind::Invalid, {}};
    }
    return {ArcResolutionKind::Arc, arc};
}

std::optional<EndpointArcParameters> encode_endpoint_arc(const EllipticArc& arc) {
    if (!arc.is_finite() || arc.sweep_angle_rad == 0.0) return std::nullopt;
    const double xx = std::fma(arc.axis_u.x, arc.axis_u.x, arc.axis_v.x * arc.axis_v.x);
    const double xy = std::fma(arc.axis_u.x, arc.axis_u.y, arc.axis_v.x * arc.axis_v.y);
    const double yy = std::fma(arc.axis_u.y, arc.axis_u.y, arc.axis_v.y * arc.axis_v.y);
    const double discriminant = std::hypot(xx - yy, 2.0 * xy);
    const double lambda_major = std::max(0.0, (xx + yy + discriminant) * 0.5);
    const double lambda_minor = std::max(0.0, (xx + yy - discriminant) * 0.5);
    const double major = std::sqrt(lambda_major);
    const double minor = std::sqrt(lambda_minor);
    const double determinant = arc.axis_u.x * arc.axis_v.y - arc.axis_u.y * arc.axis_v.x;
    if (!(major > 0.0) || !(minor > 0.0) || determinant == 0.0 ||
        !std::isfinite(major) || !std::isfinite(minor)) {
        return std::nullopt;
    }

    double rotation = 0.5 * std::atan2(2.0 * xy, xx - yy) * (180.0 / kPi);
    rotation = std::fmod(rotation, 180.0);
    const bool positive_orientation = std::signbit(determinant) == std::signbit(arc.sweep_angle_rad);
    return EndpointArcParameters{
        major,
        minor,
        rotation,
        std::abs(arc.sweep_angle_rad) > kPi,
        positive_orientation
    };
}

} // namespace pikdraw::geometry
