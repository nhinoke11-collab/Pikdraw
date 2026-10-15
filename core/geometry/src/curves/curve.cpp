#include "pikdraw/geometry/curves/curve.hpp"
#include <algorithm>
#include <cmath>

namespace pikdraw::geometry {

Rect2D CubicBezier::bounds() const {
    double min_x = std::min(p0.x, p3.x);
    double max_x = std::max(p0.x, p3.x);
    double min_y = std::min(p0.y, p3.y);
    double max_y = std::max(p0.y, p3.y);

    // Derivative: B'(t) = 3*(1-t)^2*(p1-p0) + 6*(1-t)*t*(p2-p1) + 3*t^2*(p3-p2)
    // = 3 * [ (p3 - 3*p2 + 3*p1 - p0)*t^2 + 2*(p2 - 2*p1 + p0)*t + (p1 - p0) ]
    // Solve quadratic equation for t in (0, 1)
    auto solve_extrema = [this](double v0, double v1, double v2, double v3, double& out_min, double& out_max, auto eval_coord) {
        const double a = -v0 + 3.0 * v1 - 3.0 * v2 + v3;
        const double b = 2.0 * (v0 - 2.0 * v1 + v2);
        const double c = v1 - v0;

        if (std::abs(a) < 1e-12) {
            if (std::abs(b) > 1e-12) {
                const double t = -c / b;
                if (t > 0.0 && t < 1.0) {
                    const double val = eval_coord(t);
                    out_min = std::min(out_min, val);
                    out_max = std::max(out_max, val);
                }
            }
            return;
        }

        const double disc = b * b - 4.0 * a * c;
        if (disc >= 0.0) {
            const double sqrt_disc = std::sqrt(disc);
            const double t1 = (-b + sqrt_disc) / (2.0 * a);
            const double t2 = (-b - sqrt_disc) / (2.0 * a);

            if (t1 > 0.0 && t1 < 1.0) {
                const double val = eval_coord(t1);
                out_min = std::min(out_min, val);
                out_max = std::max(out_max, val);
            }
            if (t2 > 0.0 && t2 < 1.0) {
                const double val = eval_coord(t2);
                out_min = std::min(out_min, val);
                out_max = std::max(out_max, val);
            }
        }
    };

    solve_extrema(p0.x, p1.x, p2.x, p3.x, min_x, max_x, [this](double t) { return point_at(t).x; });
    solve_extrema(p0.y, p1.y, p2.y, p3.y, min_y, max_y, [this](double t) { return point_at(t).y; });

    return Rect2D::from_ltrb(min_x, min_y, max_x, max_y);
}

Rect2D EllipticArc::bounds() const {
    const double min_x = std::min(p0.x, p1.x) - std::abs(rx);
    const double max_x = std::max(p0.x, p1.x) + std::abs(rx);
    const double min_y = std::min(p0.y, p1.y) - std::abs(ry);
    const double max_y = std::max(p0.y, p1.y) + std::abs(ry);
    return Rect2D::from_ltrb(min_x, min_y, max_x, max_y);
}

} // namespace pikdraw::geometry
