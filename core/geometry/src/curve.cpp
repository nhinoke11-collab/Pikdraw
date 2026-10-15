#include "pikdraw/geometry/curve.hpp"
#include <stdexcept>

namespace pikdraw::geometry {

Rect2D SourcePath::compute_bounds() const {
    if (segments.empty()) {
        return Rect2D(0, 0, 0, 0);
    }
    double min_x = 1e30, min_y = 1e30, max_x = -1e30, max_y = -1e30;
    auto update_pt = [&](const Point2D& p) {
        min_x = std::min(min_x, p.x);
        min_y = std::min(min_y, p.y);
        max_x = std::max(max_x, p.x);
        max_y = std::max(max_y, p.y);
    };

    for (const auto& s : segments) {
        switch (s.type) {
            case SegmentType::MoveTo:
                update_pt(s.p0);
                break;
            case SegmentType::LineTo:
                update_pt(s.p1);
                break;
            case SegmentType::CubicTo:
                // Fast bounding box covering control polygon
                update_pt(s.p1);
                update_pt(s.p2);
                update_pt(s.p3);
                break;
            case SegmentType::Close:
                break;
        }
    }
    if (min_x > max_x) return Rect2D(0, 0, 0, 0);
    return Rect2D(min_x, min_y, max_x, max_y);
}

SourcePath SourcePath::transformed(const Transform2D& t) const {
    SourcePath result;
    result.segments.reserve(segments.size());
    for (const auto& s : segments) {
        PathSegment ns;
        ns.type = s.type;
        switch (s.type) {
            case SegmentType::MoveTo:
                ns.p0 = t.apply(s.p0);
                break;
            case SegmentType::LineTo:
                ns.p1 = t.apply(s.p1);
                break;
            case SegmentType::CubicTo:
                ns.p1 = t.apply(s.p1);
                ns.p2 = t.apply(s.p2);
                ns.p3 = t.apply(s.p3);
                break;
            case SegmentType::Close:
                break;
        }
        result.segments.push_back(ns);
    }
    return result;
}

namespace {
void subdivide_cubic(
    const Point2D& p0, const Point2D& p1, const Point2D& p2, const Point2D& p3,
    double tol_sq, std::vector<Point2D>& out_pts, int depth = 0
) {
    if (depth > 24 || out_pts.size() >= 1000000) throw std::runtime_error("Manufacturing approximation budget exhausted");
    // Distance from control points to chord p0-p3
    const double dx = p3.x - p0.x;
    const double dy = p3.y - p0.y;
    const double len_sq = dx * dx + dy * dy;
    const auto distance_sq = [&](const Point2D& p) {
        const double t = len_sq == 0 ? 0 : std::clamp(((p.x-p0.x)*dx + (p.y-p0.y)*dy)/len_sq, 0.0, 1.0);
        const double x = p.x - (p0.x + t*dx), y = p.y - (p0.y + t*dy);
        return x*x + y*y;
    };
    // Distance to finite chord segment handles zero-chord loops and collinear overshoot.
    const bool flat = distance_sq(p1) <= tol_sq && distance_sq(p2) <= tol_sq;

    if (flat) {
        out_pts.push_back(p3);
        return;
    }

    // de Casteljau split at t=0.5
    const Point2D p01 = (p0 + p1) * 0.5;
    const Point2D p12 = (p1 + p2) * 0.5;
    const Point2D p23 = (p2 + p3) * 0.5;
    const Point2D p012 = (p01 + p12) * 0.5;
    const Point2D p123 = (p12 + p23) * 0.5;
    const Point2D p0123 = (p012 + p123) * 0.5;

    subdivide_cubic(p0, p01, p012, p0123, tol_sq, out_pts, depth + 1);
    subdivide_cubic(p0123, p123, p23, p3, tol_sq, out_pts, depth + 1);
}
} // namespace

ManufacturingApproximation discretize_for_manufacturing(
    const SourcePath& source,
    double tolerance_mm
) {
    if (!std::isfinite(tolerance_mm) || tolerance_mm < 1e-9 || tolerance_mm > 1e6)
        throw std::invalid_argument("Tolerance outside supported spike range [1e-9, 1e6] mm");
    if (source.segments.size() > 1000000) throw std::invalid_argument("Too many source segments");
    for (const auto& s : source.segments) {
        for (const auto& p : {s.p0, s.p1, s.p2, s.p3})
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1e7 || std::abs(p.y) > 1e7)
                throw std::invalid_argument("Source coordinate outside finite spike range");
    }
    ManufacturingApproximation approx;
    approx.tolerance_mm = tolerance_mm;
    approx.is_source_curve = false;

    const double tol_sq = tolerance_mm * tolerance_mm;
    Point2D cur_pt{0, 0};
    Point2D start{};
    bool started = false, closed = false;

    for (const auto& s : source.segments) {
        switch (s.type) {
            case SegmentType::MoveTo:
                if (started) throw std::invalid_argument("Multiple contours require a contour-aware result; unsupported");
                started = true;
                start = s.p0;
                cur_pt = s.p0;
                approx.polyline.push_back(cur_pt);
                break;
            case SegmentType::LineTo:
                if (!started || closed) throw std::invalid_argument("Line outside open contour");
                cur_pt = s.p1;
                approx.polyline.push_back(cur_pt);
                break;
            case SegmentType::CubicTo:
                if (!started || closed) throw std::invalid_argument("Curve outside open contour");
                subdivide_cubic(cur_pt, s.p1, s.p2, s.p3, tol_sq, approx.polyline);
                cur_pt = s.p3;
                break;
            case SegmentType::Close:
                if (!started || closed) throw std::invalid_argument("Invalid close");
                if (cur_pt.x != start.x || cur_pt.y != start.y) approx.polyline.push_back(start);
                cur_pt = start;
                closed = true;
                break;
        }
    }
    return approx;
}

} // namespace pikdraw::geometry
