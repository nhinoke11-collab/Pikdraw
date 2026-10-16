#include "hit_geometry.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace pikdraw::vector {
namespace {
using geometry::Point2D;
double distance(Point2D p, Point2D a, Point2D b) {
    const auto d = b - a;
    const double length2 = d.x * d.x + d.y * d.y;
    const double t =
        length2 > 0 ? std::clamp(((p.x - a.x) * d.x + (p.y - a.y) * d.y) / length2, 0.0, 1.0) : 0;
    return (p - (a + d * t)).distance_to({});
}
bool append(std::vector<Point2D> &points, Point2D p, size_t &budget) {
    if (!budget)
        return false;
    --budget;
    points.push_back(p);
    return true;
}
bool cubic(std::array<Point2D, 4> p, double tolerance, size_t depth, std::vector<Point2D> &out,
           size_t &budget) {
    if (std::max(distance(p[1], p[0], p[3]), distance(p[2], p[0], p[3])) <= tolerance)
        return append(out, p[3], budget);
    if (depth == 20 || !budget)
        return false;
    const auto a = (p[0] + p[1]) * .5, b = (p[1] + p[2]) * .5, c = (p[2] + p[3]) * .5;
    const auto d = (a + b) * .5, e = (b + c) * .5, f = (d + e) * .5;
    return cubic({p[0], a, d, f}, tolerance, depth + 1, out, budget) &&
           cubic({f, e, c, p[3]}, tolerance, depth + 1, out, budget);
}
} // namespace
std::optional<std::vector<geometry::FlattenedSubpath>> hit_polylines(const geometry::Path2D &path,
                                                                     double tolerance) {
    std::vector<geometry::FlattenedSubpath> paths;
    geometry::FlattenedSubpath current;
    Point2D point{}, start{};
    // Preserve large linear source paths; bound extra interaction subdivision work.
    if (path.size() > 1000000)
        return std::nullopt;
    size_t budget = 16384 + path.size() * 2;
    for (const auto &e : path.elements()) {
        switch (e.verb) {
        case geometry::PathVerb::MoveTo:
            if (!current.points.empty())
                paths.push_back(std::move(current));
            current = {};
            point = start = e.pt;
            if (!append(current.points, point, budget))
                return std::nullopt;
            break;
        case geometry::PathVerb::LineTo:
            point = e.pt;
            if (!append(current.points, point, budget))
                return std::nullopt;
            break;
        case geometry::PathVerb::QuadTo:
            if (!cubic(
                    {point, point + (e.ctrl1 - point) * (2.0 / 3), e.pt + (e.ctrl1 - e.pt) * (2.0 / 3), e.pt},
                    tolerance, 0, current.points, budget))
                return std::nullopt;
            point = e.pt;
            break;
        case geometry::PathVerb::CubicTo:
            if (!cubic({point, e.ctrl1, e.ctrl2, e.pt}, tolerance, 0, current.points, budget))
                return std::nullopt;
            point = e.pt;
            break;
        case geometry::PathVerb::Close:
            if (!append(current.points, start, budget))
                return std::nullopt;
            point = start;
            // A continuation after Z starts at the closed subpath's origin.
            current.is_closed = true;
            paths.push_back(std::move(current));
            current = {};
            if (!append(current.points, start, budget))
                return std::nullopt;
            break;
        default:
            return std::nullopt;
        }
    }
    if (!current.points.empty())
        paths.push_back(std::move(current));
    return paths;
}
} // namespace pikdraw::vector
