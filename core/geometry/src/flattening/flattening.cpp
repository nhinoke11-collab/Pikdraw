#include "pikdraw/geometry/flattening/flattening.hpp"
#include <cmath>

namespace pikdraw::geometry {

namespace {

void subdivide_cubic(const CubicBezier& c, double tol, size_t depth, size_t max_depth, std::vector<Point2D>& out) {
    // Check distance of control points to chord (p0 -> p3)
    const Point2D d = c.p3 - c.p0;
    const double len_sq = d.distance_squared_to(Point2D{0, 0});

    double dist1 = 0.0;
    double dist2 = 0.0;
    if (len_sq > 1e-12) {
        dist1 = std::abs((c.p1.x - c.p0.x) * d.y - (c.p1.y - c.p0.y) * d.x) / std::sqrt(len_sq);
        dist2 = std::abs((c.p2.x - c.p0.x) * d.y - (c.p2.y - c.p0.y) * d.x) / std::sqrt(len_sq);
    } else {
        dist1 = c.p1.distance_to(c.p0);
        dist2 = c.p2.distance_to(c.p0);
    }

    if ((dist1 <= tol && dist2 <= tol) || depth >= max_depth) {
        out.push_back(c.p3);
        return;
    }

    // Midpoint de Casteljau subdivision
    const Point2D p01 = (c.p0 + c.p1) * 0.5;
    const Point2D p12 = (c.p1 + c.p2) * 0.5;
    const Point2D p23 = (c.p2 + c.p3) * 0.5;
    const Point2D p012 = (p01 + p12) * 0.5;
    const Point2D p123 = (p12 + p23) * 0.5;
    const Point2D mid = (p012 + p123) * 0.5;

    subdivide_cubic(CubicBezier{c.p0, p01, p012, mid}, tol, depth + 1, max_depth, out);
    subdivide_cubic(CubicBezier{mid, p123, p23, c.p3}, tol, depth + 1, max_depth, out);
}

void subdivide_quad(const QuadraticBezier& q, double tol, size_t depth, size_t max_depth, std::vector<Point2D>& out) {
    const Point2D d = q.p2 - q.p0;
    const double len_sq = d.distance_squared_to(Point2D{0, 0});
    double dist = 0.0;
    if (len_sq > 1e-12) {
        dist = std::abs((q.p1.x - q.p0.x) * d.y - (q.p1.y - q.p0.y) * d.x) / std::sqrt(len_sq);
    } else {
        dist = q.p1.distance_to(q.p0);
    }

    if (dist <= tol || depth >= max_depth) {
        out.push_back(q.p2);
        return;
    }

    const Point2D p01 = (q.p0 + q.p1) * 0.5;
    const Point2D p12 = (q.p1 + q.p2) * 0.5;
    const Point2D mid = (p01 + p12) * 0.5;

    subdivide_quad(QuadraticBezier{q.p0, p01, mid}, tol, depth + 1, max_depth, out);
    subdivide_quad(QuadraticBezier{mid, p12, q.p2}, tol, depth + 1, max_depth, out);
}

} // namespace

std::vector<FlattenedSubpath> flatten_path(const Path2D& path, const FlatteningOptions& options) {
    std::vector<FlattenedSubpath> result;
    FlattenedSubpath current;
    Point2D curr_pt{0.0, 0.0};

    for (const auto& el : path.elements()) {
        switch (el.verb) {
            case PathVerb::MoveTo:
                if (!current.points.empty()) {
                    result.push_back(std::move(current));
                    current = FlattenedSubpath{};
                }
                curr_pt = el.pt;
                current.points.push_back(curr_pt);
                break;
            case PathVerb::LineTo:
                curr_pt = el.pt;
                current.points.push_back(curr_pt);
                break;
            case PathVerb::QuadTo: {
                QuadraticBezier quad{curr_pt, el.ctrl1, el.pt};
                subdivide_quad(quad, options.tolerance_mm, 0, options.max_subdivision_depth, current.points);
                curr_pt = el.pt;
                break;
            }
            case PathVerb::CubicTo: {
                CubicBezier cubic{curr_pt, el.ctrl1, el.ctrl2, el.pt};
                subdivide_cubic(cubic, options.tolerance_mm, 0, options.max_subdivision_depth, current.points);
                curr_pt = el.pt;
                break;
            }
            case PathVerb::ArcTo:
                // Approximation fallback for ArcTo
                curr_pt = el.pt;
                current.points.push_back(curr_pt);
                break;
            case PathVerb::Close:
                current.is_closed = true;
                break;
        }
    }

    if (!current.points.empty()) {
        result.push_back(std::move(current));
    }
    return result;
}

} // namespace pikdraw::geometry
