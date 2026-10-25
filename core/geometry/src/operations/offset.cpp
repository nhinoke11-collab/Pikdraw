#include "pikdraw/geometry/operations/offset.hpp"
#include "pikdraw/geometry/flattening/flattening.hpp"
#include "rectangle_subset.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace pikdraw::geometry {

Rect2D offset_rect(const Rect2D& rect, double distance_mm) {
    if (!rect.is_finite() || !std::isfinite(distance_mm) || rect.width < 0 || rect.height < 0)
        throw std::invalid_argument("Non-finite or negative rectangle offset input");
    if (rect.is_empty()) return rect;
    const Rect2D result{
        rect.x - distance_mm,
        rect.y - distance_mm,
        rect.width + 2.0 * distance_mm,
        rect.height + 2.0 * distance_mm
    };
    if (!result.is_finite()) throw std::overflow_error("Rectangle offset overflow");
    return result.is_empty() ? Rect2D{} : result;
}

OffsetResult offset_path(const Path2D& path, const OffsetOptions& options) {
    OffsetResult res;
    const auto rect = detail::axis_aligned_rectangle(path);
    if (!rect) {
        res.error_message = "General path offset is unsupported; only exact axis-aligned rectangles are implemented";
        return res;
    }
    if (!std::isfinite(options.distance_mm) || !std::isfinite(options.miter_limit) ||
        !std::isfinite(options.arc_tolerance_mm) || options.arc_tolerance_mm <= 0 ||
        options.join_type != JoinType::Miter || options.miter_limit < std::sqrt(2.0)) {
        res.error_message = "Unsupported join or invalid offset parameters";
        return res;
    }
    Rect2D expanded;
    try { expanded = offset_rect(*rect, options.distance_mm); }
    catch (const std::exception&) {
        res.error_message = "Rectangle offset overflow or invalid input";
        return res;
    }
    if (!expanded.is_empty())
        res.path = Path2D::make_rect(expanded.x, expanded.y, expanded.width, expanded.height);
    res.success = true;
    return res;
}

namespace {

struct PolygonLoop {
    std::vector<Point2D> pts;
    bool is_hole{false};
    double signed_area{0.0};
};

double compute_signed_area(const std::vector<Point2D>& pts) {
    if (pts.size() < 3) return 0.0;
    double area = 0.0;
    const size_t n = pts.size();
    for (size_t i = 0; i < n; ++i) {
        const size_t next = (i + 1 == n) ? 0 : i + 1;
        area += pts[i].x * pts[next].y - pts[next].x * pts[i].y;
    }
    return area * 0.5;
}

bool point_in_polygon(const Point2D& pt, const std::vector<Point2D>& poly) {
    if (poly.size() < 3) return false;
    bool inside = false;
    const size_t n = poly.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        if (((poly[i].y > pt.y) != (poly[j].y > pt.y)) &&
            (pt.x < (poly[j].x - poly[i].x) * (pt.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)) {
            inside = !inside;
        }
    }
    return inside;
}

bool segments_cross_strictly(const Point2D& a, const Point2D& b,
                            const Point2D& c, const Point2D& d) {
    if (std::hypot(a.x - c.x, a.y - c.y) < 1e-5 ||
        std::hypot(a.x - d.x, a.y - d.y) < 1e-5 ||
        std::hypot(b.x - c.x, b.y - c.y) < 1e-5 ||
        std::hypot(b.x - d.x, b.y - d.y) < 1e-5)
        return false;

    auto cross = [](const Point2D& p, const Point2D& q, const Point2D& r) {
        return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
    };

    const double cp1 = cross(a, b, c);
    const double cp2 = cross(a, b, d);
    const double cp3 = cross(c, d, a);
    const double cp4 = cross(c, d, b);

    const double eps = 1e-9;
    const bool ab_straddles_cd = ((cp1 > eps && cp2 < -eps) || (cp1 < -eps && cp2 > eps));
    const bool cd_straddles_ab = ((cp3 > eps && cp4 < -eps) || (cp3 < -eps && cp4 > eps));

    return ab_straddles_cd && cd_straddles_ab;
}

bool loop_has_self_intersection(const std::vector<Point2D>& pts) {
    const size_t n = pts.size();
    if (n < 4) return false;

    size_t checks = 0;
    constexpr size_t kMaxChecks = 15000;

    for (size_t i = 0; i < n; ++i) {
        const size_t i_next = (i + 1 == n) ? 0 : i + 1;
        const Point2D& p1 = pts[i];
        const Point2D& p2 = pts[i_next];

        for (size_t j = i + 1; j < n; ++j) {
            const size_t j_next = (j + 1 == n) ? 0 : j + 1;
            if (i == j || i == j_next || i_next == j || i_next == j_next)
                continue;

            const Point2D& p3 = pts[j];
            const Point2D& p4 = pts[j_next];

            if (std::max(p1.x, p2.x) < std::min(p3.x, p4.x) - 1e-7 ||
                std::max(p3.x, p4.x) < std::min(p1.x, p2.x) - 1e-7 ||
                std::max(p1.y, p2.y) < std::min(p3.y, p4.y) - 1e-7 ||
                std::max(p3.y, p4.y) < std::min(p1.y, p2.y) - 1e-7)
                continue;

            if (++checks > kMaxChecks) {
                // Safeguard against unbounded UI freeze on very dense polygons
                return false;
            }

            if (segments_cross_strictly(p1, p2, p3, p4))
                return true;
        }
    }
    return false;
}

std::vector<Point2D> clean_polygon_points(const std::vector<Point2D>& raw) {
    std::vector<Point2D> clean;
    clean.reserve(raw.size());
    for (const auto& pt : raw) {
        if (!clean.empty() && std::hypot(pt.x - clean.back().x, pt.y - clean.back().y) < 1e-8) {
            continue;
        }
        clean.push_back(pt);
    }
    // Also remove wrap-around duplicate
    if (clean.size() >= 2 && std::hypot(clean.front().x - clean.back().x, clean.front().y - clean.back().y) < 1e-8) {
        clean.pop_back();
    }
    return clean;
}

std::vector<Point2D> offset_single_loop(const PolygonLoop& loop, double d_signed,
                                        JoinType join_type, double miter_limit,
                                        double arc_tolerance_mm) {
    const auto& pts = loop.pts;
    const size_t n = pts.size();
    if (n < 3) return {};

    std::vector<Point2D> u(n);
    std::vector<Point2D> normal(n);

    for (size_t i = 0; i < n; ++i) {
        const size_t next = (i + 1 == n) ? 0 : i + 1;
        const double dx = pts[next].x - pts[i].x;
        const double dy = pts[next].y - pts[i].y;
        const double len = std::hypot(dx, dy);
        if (len < 1e-9) {
            u[i] = {1.0, 0.0};
            normal[i] = {0.0, -1.0};
        } else {
            u[i] = {dx / len, dy / len};
            normal[i] = {dy / len, -dx / len};
        }
    }

    std::vector<Point2D> result;
    result.reserve(n * 4);

    for (size_t i = 0; i < n; ++i) {
        const size_t prev = (i == 0) ? n - 1 : i - 1;
        const Point2D& v = pts[i];

        const Point2D p1{v.x + d_signed * normal[prev].x, v.y + d_signed * normal[prev].y};
        const Point2D p2{v.x + d_signed * normal[i].x, v.y + d_signed * normal[i].y};

        const double cross = u[prev].x * u[i].y - u[prev].y * u[i].x;

        if (std::abs(cross) < 1e-7) {
            result.push_back(p1);
            continue;
        }

        const bool is_convex = (d_signed > 0) ? (cross > 0) : (cross < 0);

        if (is_convex) {
            // Convex corner
            if (join_type == JoinType::Miter) {
                const double t1 = ((p2.x - p1.x) * u[i].y - (p2.y - p1.y) * u[i].x) / cross;
                const Point2D miter_pt{p1.x + t1 * u[prev].x, p1.y + t1 * u[prev].y};
                const double miter_dist = std::hypot(miter_pt.x - v.x, miter_pt.y - v.y);
                const double miter_ratio = miter_dist / std::abs(d_signed);

                if (miter_ratio <= miter_limit && std::isfinite(miter_pt.x) && std::isfinite(miter_pt.y)) {
                    result.push_back(miter_pt);
                } else {
                    // Bevel fallback
                    result.push_back(p1);
                    result.push_back(p2);
                }
            } else if (join_type == JoinType::Round) {
                result.push_back(p1);
                const double r = std::abs(d_signed);
                const double theta1 = std::atan2(p1.y - v.y, p1.x - v.x);
                const double theta2 = std::atan2(p2.y - v.y, p2.x - v.x);
                double delta = theta2 - theta1;

                if (d_signed > 0) {
                    while (delta < 0.0) delta += 2.0 * M_PI;
                } else {
                    while (delta > 0.0) delta -= 2.0 * M_PI;
                }

                const double chord_budget = std::max(0.001, arc_tolerance_mm);
                const double ratio = std::clamp(1.0 - chord_budget / r, -1.0, 1.0);
                const double max_step_angle = std::max(0.05, 2.0 * std::acos(ratio));
                int arc_steps = std::clamp(static_cast<int>(std::ceil(std::abs(delta) / max_step_angle)), 2, 64);

                for (int s = 1; s < arc_steps; ++s) {
                    const double angle = theta1 + delta * (static_cast<double>(s) / arc_steps);
                    result.push_back(Point2D{v.x + r * std::cos(angle), v.y + r * std::sin(angle)});
                }
                result.push_back(p2);
            } else {
                // Bevel
                result.push_back(p1);
                result.push_back(p2);
            }
        } else {
            // Reflex corner -> edges intersect
            const double t1 = ((p2.x - p1.x) * u[i].y - (p2.y - p1.y) * u[i].x) / cross;
            const Point2D reflex_pt{p1.x + t1 * u[prev].x, p1.y + t1 * u[prev].y};
            if (std::isfinite(reflex_pt.x) && std::isfinite(reflex_pt.y)) {
                result.push_back(reflex_pt);
            } else {
                result.push_back(p1);
            }
        }
    }

    const auto cleaned = clean_polygon_points(result);
    const double area = compute_signed_area(cleaned);

    if (loop.is_hole) {
        // Hole collapsed if area becomes positive or zero
        if (area >= -1e-7) return {};
    } else {
        // Outer loop collapsed if area becomes negative or zero
        if (area <= 1e-7) return {};
    }

    return cleaned;
}

Path2D polygon_to_path2d(const std::vector<Point2D>& pts) {
    Path2D path;
    if (pts.empty()) return path;
    path.move_to(pts[0]);
    for (size_t i = 1; i < pts.size(); ++i) {
        path.line_to(pts[i]);
    }
    path.close();
    return path;
}

} // namespace

ContourResult compute_contour(const ContourRequest& request) {
    ContourResult result;

    if (!std::isfinite(request.spacing_mm) || request.spacing_mm <= 0.0 || request.spacing_mm > 100000.0) {
        result.status = OffsetStatus::InvalidInput;
        result.error_message = "Spacing must be positive and finite (0 < d <= 100000 mm)";
        return result;
    }
    if (!std::isfinite(request.miter_limit) || request.miter_limit < 1.0 || request.miter_limit > 100.0) {
        result.status = OffsetStatus::InvalidInput;
        result.error_message = "Miter limit must be finite and between 1.0 and 100.0";
        return result;
    }
    if (!std::isfinite(request.arc_tolerance_mm) ||
        request.arc_tolerance_mm < kContourMinimumArcToleranceMm || request.arc_tolerance_mm > 1.0) {
        result.status = OffsetStatus::InvalidInput;
        result.error_message = "Contour arc tolerance must be finite and between 0.02 and 1 mm";
        return result;
    }

    int requested_steps = request.steps;
    if (request.direction == ContourDirection::ToCenter) {
        requested_steps = 99;
    } else {
        if (requested_steps < 1 || requested_steps > 99) {
            result.status = OffsetStatus::InvalidInput;
            result.error_message = "Steps must be between 1 and 99";
            return result;
        }
    }

    if (request.path.empty()) {
        result.status = OffsetStatus::Empty;
        return result;
    }

    // Check if input path is closed
    bool has_subpath = false;
    Point2D subpath_start{};
    Point2D current{};
    bool subpath_closed = false;

    for (const auto& elem : request.path.elements()) {
        if (elem.verb == PathVerb::MoveTo) {
            if (has_subpath && !subpath_closed) {
                // Previous subpath was not closed
                if (std::hypot(current.x - subpath_start.x, current.y - subpath_start.y) > 0.05) {
                    result.status = OffsetStatus::InvalidInput;
                    result.error_message = "Contour offset requires closed source paths";
                    return result;
                }
            }
            subpath_start = elem.pt;
            current = elem.pt;
            has_subpath = true;
            subpath_closed = false;
        } else if (elem.verb == PathVerb::Close) {
            subpath_closed = true;
            current = subpath_start;
        } else {
            current = elem.pt;
        }
    }
    if (has_subpath && !subpath_closed) {
        if (std::hypot(current.x - subpath_start.x, current.y - subpath_start.y) > 0.05) {
            result.status = OffsetStatus::InvalidInput;
            result.error_message = "Contour offset requires closed source paths";
            return result;
        }
    }

    // Special exact case: single axis-aligned rectangle with Miter join
    const auto exact_rect = detail::axis_aligned_rectangle(request.path);
    if (exact_rect && request.join_type == JoinType::Miter) {
        result.status = OffsetStatus::Success;
        for (int k = 1; k <= requested_steps; ++k) {
            ContourStep step;
            step.step_index = k;
            step.distance_mm = k * request.spacing_mm;

            if (request.direction == ContourDirection::Outside) {
                const Rect2D r = offset_rect(*exact_rect, step.distance_mm);
                Path2D p = Path2D::make_rect(r.x, r.y, r.width, r.height);
                step.loops.push_back(p);
                step.combined_path = p;
                result.steps.push_back(std::move(step));
            } else {
                // Inside or ToCenter
                const double new_w = exact_rect->width - 2.0 * step.distance_mm;
                const double new_h = exact_rect->height - 2.0 * step.distance_mm;
                if (new_w <= 0.0 || new_h <= 0.0) {
                    step.is_empty = true;
                    if (request.direction == ContourDirection::ToCenter) {
                        // Collapse reached
                        break;
                    }
                    result.steps.push_back(std::move(step));
                } else {
                    const Rect2D r = offset_rect(*exact_rect, -step.distance_mm);
                    Path2D p = Path2D::make_rect(r.x, r.y, r.width, r.height);
                    step.loops.push_back(p);
                    step.combined_path = p;
                    result.steps.push_back(std::move(step));
                }
            }
        }
        if (request.direction == ContourDirection::ToCenter && result.steps.size() == 99) {
            result.truncated = true;
        }
        if (result.steps.empty() || std::all_of(result.steps.begin(), result.steps.end(),
                                               [](const auto& s) { return s.is_empty; })) {
            result.status = OffsetStatus::Empty;
        }
        return result;
    }

    // General polygon / path offset
    const double tol = std::min(request.arc_tolerance_mm, 1.0);
    const auto flat_res = flatten_path_checked(request.path, {tol, 16});
    if (flat_res.status != FlatteningStatus::Success) {
        result.status = OffsetStatus::InvalidInput;
        result.error_message = "Flattening input path failed";
        return result;
    }

    std::vector<PolygonLoop> raw_loops;
    for (const auto& sp : flat_res.subpaths) {
        auto clean = clean_polygon_points(sp.points);
        if (clean.size() < 3) continue;

        if (loop_has_self_intersection(clean)) {
            result.status = OffsetStatus::InvalidInput;
            result.error_message = "Self-intersecting closed source is not supported";
            return result;
        }

        PolygonLoop loop;
        loop.pts = std::move(clean);
        loop.signed_area = compute_signed_area(loop.pts);
        raw_loops.push_back(std::move(loop));
    }

    if (raw_loops.empty()) {
        result.status = OffsetStatus::Empty;
        return result;
    }

    // Classify each loop as Outer or Hole based on nesting level
    for (size_t i = 0; i < raw_loops.size(); ++i) {
        const Point2D test_pt = raw_loops[i].pts.front();
        size_t nesting = 0;
        for (size_t j = 0; j < raw_loops.size(); ++j) {
            if (i == j) continue;
            if (point_in_polygon(test_pt, raw_loops[j].pts)) {
                ++nesting;
            }
        }
        raw_loops[i].is_hole = (nesting % 2 == 1);

        // Normalize orientation:
        // Outer loop -> CCW (positive area)
        // Hole loop  -> CW (negative area)
        if (!raw_loops[i].is_hole && raw_loops[i].signed_area < 0) {
            std::reverse(raw_loops[i].pts.begin(), raw_loops[i].pts.end());
            raw_loops[i].signed_area = -raw_loops[i].signed_area;
        } else if (raw_loops[i].is_hole && raw_loops[i].signed_area > 0) {
            std::reverse(raw_loops[i].pts.begin(), raw_loops[i].pts.end());
            raw_loops[i].signed_area = -raw_loops[i].signed_area;
        }
    }

    result.status = OffsetStatus::Success;

    for (int k = 1; k <= requested_steps; ++k) {
        ContourStep step;
        step.step_index = k;
        step.distance_mm = k * request.spacing_mm;

        const double d_signed = (request.direction == ContourDirection::Outside)
            ? step.distance_mm
            : -step.distance_mm;

        Path2D combined;
        bool step_has_valid_loop = false;

        for (const auto& loop : raw_loops) {
            auto offset_pts = offset_single_loop(loop, d_signed, request.join_type,
                                                request.miter_limit, request.arc_tolerance_mm);
            if (offset_pts.empty()) continue;

            Path2D loop_path = polygon_to_path2d(offset_pts);
            step.loops.push_back(loop_path);

            // Append into combined
            combined.move_to(offset_pts[0]);
            for (size_t p = 1; p < offset_pts.size(); ++p) {
                combined.line_to(offset_pts[p]);
            }
            combined.close();
            step_has_valid_loop = true;
        }

        if (!step_has_valid_loop) {
            step.is_empty = true;
            if (request.direction == ContourDirection::ToCenter) {
                // Collapsed to center
                break;
            }
            result.steps.push_back(std::move(step));
        } else {
            step.combined_path = std::move(combined);
            result.steps.push_back(std::move(step));
        }
    }

    if (request.direction == ContourDirection::ToCenter && result.steps.size() == 99) {
        result.truncated = true;
    }

    if (result.steps.empty() || std::all_of(result.steps.begin(), result.steps.end(),
                                           [](const auto& s) { return s.is_empty; })) {
        result.status = OffsetStatus::Empty;
    }

    return result;
}

} // namespace pikdraw::geometry
