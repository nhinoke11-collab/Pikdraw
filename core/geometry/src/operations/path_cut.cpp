#include "pikdraw/geometry/operations/path_cut.hpp"

#include "pikdraw/geometry/curves/path.hpp"
#include "pikdraw/geometry/operations/boolean.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace pikdraw::geometry {

namespace {

Path2D make_circle_path(const Point2D& center, double radius, size_t segments = 16) {
    Path2D path;
    if (radius <= 0.0 || segments < 4) return path;
    const double step = 2.0 * std::numbers::pi / static_cast<double>(segments);
    path.move_to(center.x + radius, center.y);
    for (size_t i = 1; i < segments; ++i) {
        const double angle = static_cast<double>(i) * step;
        path.line_to(center.x + radius * std::cos(angle), center.y + radius * std::sin(angle));
    }
    path.close();
    return path;
}

size_t circle_segments_for_tolerance(double radius, double tolerance_mm,
                                     size_t minimum_segments) {
    if (!std::isfinite(radius) || !std::isfinite(tolerance_mm) ||
        radius <= 0.0 || tolerance_mm <= 0.0) return minimum_segments;
    const double ratio = std::clamp(tolerance_mm / radius, 0.0, 1.0);
    const double angle = 2.0 * std::acos(1.0 - ratio);
    if (!std::isfinite(angle) || angle <= 0.0) return 4096;
    const double requested = std::ceil(2.0 * std::numbers::pi / angle);
    if (requested >= 4096.0) return 4096;
    const auto required = static_cast<size_t>(requested);
    return std::clamp(std::max(required, minimum_segments), minimum_segments, size_t{4096});
}

double circle_sagitta_mm(double radius, size_t segments) {
    if (radius <= 0.0 || segments < 3) return 0.0;
    return radius * (1.0 - std::cos(std::numbers::pi / static_cast<double>(segments)));
}

Path2D make_square_path(const Point2D& center, double half_side) {
    Path2D path;
    path.move_to(center.x - half_side, center.y - half_side);
    path.line_to(center.x + half_side, center.y - half_side);
    path.line_to(center.x + half_side, center.y + half_side);
    path.line_to(center.x - half_side, center.y + half_side);
    path.close();
    return path;
}

void append_subpath(Path2D& dest, const Path2D& src) {
    for (const auto& el : src.elements()) {
        if (el.verb == PathVerb::MoveTo) dest.move_to(el.pt);
        else if (el.verb == PathVerb::LineTo) dest.line_to(el.pt);
        else if (el.verb == PathVerb::Close) dest.close();
    }
}

bool is_filled_closed_path(const Path2D& path) {
    bool has_subpath = false;
    bool active = false;
    bool closed = true;
    for (const auto& element : path.elements()) {
        if (element.verb == PathVerb::MoveTo) {
            if (active && !closed) return false;
            active = true;
            closed = false;
            has_subpath = true;
        } else if (element.verb == PathVerb::Close) {
            if (!active || closed) return false;
            closed = true;
        } else if (!active || closed) {
            return false;
        }
    }
    return has_subpath && active && closed;
}

bool finite_points(const std::vector<Point2D>& points) {
    return std::all_of(points.begin(), points.end(), [](const Point2D& point) {
        return std::isfinite(point.x) && std::isfinite(point.y);
    });
}

std::vector<Path2D> filled_components(BooleanResult&& result) {
    if (!result.components.empty()) return std::move(result.components);
    if (!result.path.empty()) return {std::move(result.path)};
    return {};
}

void append_paths(Path2D& destination, const Path2D& source) {
    for (const auto& element : source.elements()) {
        if (element.verb == PathVerb::MoveTo) destination.move_to(element.pt);
        else if (element.verb == PathVerb::LineTo) destination.line_to(element.pt);
        else if (element.verb == PathVerb::Close) destination.close();
    }
}

} // namespace

PathCutResult cut_path_by_line(
    const Path2D& subject,
    const Point2D& p1,
    const Point2D& p2,
    double gap_mm,
    const BooleanOptions& options
) {
    if (subject.empty()) {
        return {false, PathCutStatus::InvalidInput, {}, "Subject path is empty"};
    }

    if (!is_filled_closed_path(subject)) {
        return {false, PathCutStatus::Unsupported, {},
                "Knife currently supports closed filled paths only"};
    }
    if (!std::isfinite(p1.x) || !std::isfinite(p1.y) ||
        !std::isfinite(p2.x) || !std::isfinite(p2.y) ||
        !std::isfinite(gap_mm) || gap_mm < 0.0) {
        return {false, PathCutStatus::InvalidInput, {}, "Cut line or gap is not finite and non-negative"};
    }

    const double dx = p2.x - p1.x;
    const double dy = p2.y - p1.y;
    const double len = std::hypot(dx, dy);
    if (len < 1e-6) {
        return {false, PathCutStatus::InvalidInput, {}, "Cut line endpoints are coincident"};
    }

    const Point2D dir{dx / len, dy / len};
    const Point2D norm{-dir.y, dir.x};

    const auto bounds = subject.bounds();
    double span = std::hypot(bounds.width, bounds.height);
    if (span < 1.0) span = 100.0;
    const double R = span * 4.0 + 200.0;

    const double cx = bounds.x + bounds.width * 0.5;
    const double cy = bounds.y + bounds.height * 0.5;
    const double t_mid = (cx - p1.x) * dir.x + (cy - p1.y) * dir.y;
    const Point2D mid{p1.x + dir.x * t_mid, p1.y + dir.y * t_mid};

    const Point2D A{mid.x - dir.x * R, mid.y - dir.y * R};
    const Point2D B{mid.x + dir.x * R, mid.y + dir.y * R};

    if (gap_mm <= 0.0) {
        // Construct Left and Right half-plane polygons covering the entire bounds
        Path2D path_left;
        path_left.move_to(A);
        path_left.line_to(B);
        path_left.line_to(B.x + norm.x * R, B.y + norm.y * R);
        path_left.line_to(A.x + norm.x * R, A.y + norm.y * R);
        path_left.close();

        Path2D path_right;
        path_right.move_to(B);
        path_right.line_to(A);
        path_right.line_to(A.x - norm.x * R, A.y - norm.y * R);
        path_right.line_to(B.x - norm.x * R, B.y - norm.y * R);
        path_right.close();

        auto res_left = boolean_paths(subject, path_left, BooleanOp::Intersection, options);
        auto res_right = boolean_paths(subject, path_right, BooleanOp::Intersection, options);

        if (!res_left.success || !res_right.success) {
            return {false, PathCutStatus::InvalidInput, {}, "Clipping operation failed during line cut"};
        }

        if (res_left.status == BooleanStatus::Empty || res_right.status == BooleanStatus::Empty ||
            res_left.path.empty() || res_right.path.empty()) {
            return {false, PathCutStatus::NoCut, {}, "Line does not bisect the path"};
        }

        auto pieces = filled_components(std::move(res_left));
        auto right_pieces = filled_components(std::move(res_right));
        pieces.insert(pieces.end(), std::make_move_iterator(right_pieces.begin()),
                      std::make_move_iterator(right_pieces.end()));

        if (pieces.size() >= 2) {
            return {true, PathCutStatus::Success, std::move(pieces), "",
                    options.flatten_tolerance_mm +
                    std::sqrt(2.0) * options.quantization_grid_mm * 0.5};
        }
        return {false, PathCutStatus::NoCut, {}, "Line cut did not yield multiple pieces"};
    } else {
        // Gap > 0.0: subtract a slit of width gap_mm
        const double half_gap = gap_mm * 0.5;
        Path2D slit_path;
        slit_path.move_to(A.x - norm.x * half_gap, A.y - norm.y * half_gap);
        slit_path.line_to(B.x - norm.x * half_gap, B.y - norm.y * half_gap);
        slit_path.line_to(B.x + norm.x * half_gap, B.y + norm.y * half_gap);
        slit_path.line_to(A.x + norm.x * half_gap, A.y + norm.y * half_gap);
        slit_path.close();

        auto res = boolean_paths(subject, slit_path, BooleanOp::Difference, options);
        if (!res.success) {
            return {false, PathCutStatus::InvalidInput, {}, "Slit subtraction failed"};
        }
        if (res.status == BooleanStatus::Empty || res.path.empty()) {
            return {false, PathCutStatus::EmptyResult, {}, "Slit consumed the entire shape"};
        }

        auto pieces = filled_components(std::move(res));

        if (pieces.size() >= 2) {
            return {true, PathCutStatus::Success, std::move(pieces), "",
                    options.flatten_tolerance_mm +
                    std::sqrt(2.0) * options.quantization_grid_mm * 0.5};
        }
        return {false, PathCutStatus::NoCut, {}, "Slit cut did not divide the shape into multiple pieces"};
    }
}

PathCutResult cut_path_by_polyline(
    const Path2D& subject,
    const std::vector<Point2D>& polyline,
    double gap_mm,
    const BooleanOptions& options
) {
    if (polyline.size() < 2) {
        return {false, PathCutStatus::InvalidInput, {}, "Polyline requires at least 2 points"};
    }
    if (subject.empty() || !is_filled_closed_path(subject)) {
        return {false, PathCutStatus::Unsupported, {},
                "Knife currently supports closed filled paths only"};
    }
    if (!finite_points(polyline) || !std::isfinite(gap_mm) || gap_mm < 0.0) {
        return {false, PathCutStatus::InvalidInput, {}, "Polyline or gap is not finite and non-negative"};
    }
    if (polyline.size() == 2) {
        return cut_path_by_line(subject, polyline[0], polyline[1], gap_mm, options);
    }

    const double slit_width = (gap_mm > 0.0) ? gap_mm : 0.002;
    const double half_w = slit_width * 0.5;
    const size_t joint_segments = circle_segments_for_tolerance(
        half_w, options.flatten_tolerance_mm, 8);
    const double joint_approximation_error_mm = circle_sagitta_mm(half_w, joint_segments);

    Path2D slit_path;
    for (size_t i = 0; i + 1 < polyline.size(); ++i) {
        const auto& p0 = polyline[i];
        const auto& p1 = polyline[i + 1];
        const double dx = p1.x - p0.x;
        const double dy = p1.y - p0.y;
        const double len = std::hypot(dx, dy);
        if (len < 1e-6) continue;
        const Point2D norm{-dy / len * half_w, dx / len * half_w};

        Path2D seg_quad;
        seg_quad.move_to(p0.x - norm.x, p0.y - norm.y);
        seg_quad.line_to(p1.x - norm.x, p1.y - norm.y);
        seg_quad.line_to(p1.x + norm.x, p1.y + norm.y);
        seg_quad.line_to(p0.x + norm.x, p0.y + norm.y);
        seg_quad.close();
        append_subpath(slit_path, seg_quad);

        // Add circle joints at vertices to prevent micro-gaps
        auto joint = make_circle_path(p0, half_w, joint_segments);
        append_subpath(slit_path, joint);
        if (i + 2 == polyline.size()) {
            auto end_joint = make_circle_path(p1, half_w, joint_segments);
            append_subpath(slit_path, end_joint);
        }
    }

    auto res = boolean_paths(subject, slit_path, BooleanOp::Difference, options);
    if (!res.success) {
        return {false, PathCutStatus::InvalidInput, {}, "Polyline cut operation failed"};
    }
    if (res.status == BooleanStatus::Empty || res.path.empty()) {
        return {false, PathCutStatus::EmptyResult, {}, "Polyline cut emptied the shape"};
    }

    auto pieces = filled_components(std::move(res));

    if (pieces.size() >= 2) {
        return {true, PathCutStatus::Success, std::move(pieces), "",
                options.flatten_tolerance_mm +
                std::sqrt(2.0) * options.quantization_grid_mm * 0.5 +
                joint_approximation_error_mm +
                (gap_mm == 0.0 ? half_w : 0.0)};
    }
    return {false, PathCutStatus::NoCut, {}, "Polyline cut did not divide the shape into multiple pieces"};
}

PathCutResult erase_path(
    const Path2D& subject,
    const std::vector<Point2D>& stroke,
    double diameter_mm,
    bool round_nib,
    const BooleanOptions& options
) {
    if (subject.empty() || stroke.empty()) {
        return {false, PathCutStatus::InvalidInput, {}, "Subject path or stroke is empty"};
    }

    if (!is_filled_closed_path(subject)) {
        return {false, PathCutStatus::Unsupported, {},
                "Vector Eraser currently supports closed filled paths only"};
    }
    if (!finite_points(stroke) || !std::isfinite(diameter_mm) || diameter_mm <= 0.0) {
        return {false, PathCutStatus::InvalidInput, {},
                "Eraser stroke and diameter must be finite and diameter positive"};
    }

    const double dia = diameter_mm;
    const double radius = dia * 0.5;

    Path2D eraser_path;
    double nib_approximation_error_mm = 0.0;
    if (stroke.size() == 1) {
        if (round_nib) {
            const size_t segments = circle_segments_for_tolerance(
                radius, options.flatten_tolerance_mm, 20);
            eraser_path = make_circle_path(stroke.front(), radius, segments);
            nib_approximation_error_mm = circle_sagitta_mm(radius, segments);
        } else {
            eraser_path = make_square_path(stroke.front(), radius);
        }
    } else {
        for (size_t i = 0; i + 1 < stroke.size(); ++i) {
            const auto& p0 = stroke[i];
            const auto& p1 = stroke[i + 1];
            const double dx = p1.x - p0.x;
            const double dy = p1.y - p0.y;
            const double len = std::hypot(dx, dy);
            if (len < 1e-6) continue;
            const Point2D norm{-dy / len * radius, dx / len * radius};

            Path2D seg_quad;
            seg_quad.move_to(p0.x - norm.x, p0.y - norm.y);
            seg_quad.line_to(p1.x - norm.x, p1.y - norm.y);
            seg_quad.line_to(p1.x + norm.x, p1.y + norm.y);
            seg_quad.line_to(p0.x + norm.x, p0.y + norm.y);
            seg_quad.close();
            append_subpath(eraser_path, seg_quad);

            if (round_nib) {
                const size_t segments = circle_segments_for_tolerance(
                    radius, options.flatten_tolerance_mm, 12);
                append_subpath(eraser_path, make_circle_path(p0, radius, segments));
                nib_approximation_error_mm = std::max(
                    nib_approximation_error_mm, circle_sagitta_mm(radius, segments));
                if (i + 2 == stroke.size()) {
                    append_subpath(eraser_path, make_circle_path(p1, radius, segments));
                }
            } else {
                append_subpath(eraser_path, make_square_path(p0, radius));
                if (i + 2 == stroke.size()) {
                    append_subpath(eraser_path, make_square_path(p1, radius));
                }
            }
        }
    }

    if (eraser_path.empty()) {
        return {false, PathCutStatus::InvalidInput, {}, "Eraser stroke has no non-zero-length segments"};
    }

    auto overlap = boolean_paths(subject, eraser_path, BooleanOp::Intersection, options);
    if (!overlap.success) {
        return {false, PathCutStatus::InvalidInput, {},
                overlap.error_message ? overlap.error_message : "Eraser intersection failed"};
    }
    if (overlap.status == BooleanStatus::Empty || overlap.path.empty()) {
        return {false, PathCutStatus::NoCut, {}, "Eraser stroke does not overlap the path"};
    }

    auto res = boolean_paths(subject, eraser_path, BooleanOp::Difference, options);
    if (!res.success) {
        return {false, PathCutStatus::InvalidInput, {}, "Eraser subtraction failed"};
    }
    if (res.status == BooleanStatus::Empty || res.path.empty()) {
        return {true, PathCutStatus::EmptyResult, {}, "",
                options.flatten_tolerance_mm + nib_approximation_error_mm +
                std::sqrt(2.0) * options.quantization_grid_mm * 0.5};
    }

    auto pieces = filled_components(std::move(res));
    return {true, PathCutStatus::Success, std::move(pieces), "",
            options.flatten_tolerance_mm + nib_approximation_error_mm +
            std::sqrt(2.0) * options.quantization_grid_mm * 0.5};
}

BooleanResult trim_target_with_sources(
    const Path2D& target,
    const std::vector<Path2D>& sources,
    const BooleanOptions& options
) {
    if (target.empty() || sources.empty()) {
        BooleanResult res;
        res.success = false;
        res.status = BooleanStatus::InvalidInput;
        res.error_message = "Target or sources are empty";
        return res;
    }

    if (!is_filled_closed_path(target)) {
        BooleanResult res;
        res.success = false;
        res.status = BooleanStatus::Unsupported;
        res.error_message = "Trim currently supports closed filled paths only";
        return res;
    }

    std::vector<Path2D> current{target};
    bool changed = false;
    double approximation_error_bound_mm = 0.0;
    for (const auto& src : sources) {
        if (src.empty()) continue;
        if (!is_filled_closed_path(src)) {
            BooleanResult res;
            res.success = false;
            res.status = BooleanStatus::Unsupported;
            res.error_message = "Trim sources must be closed filled paths";
            return res;
        }

        std::vector<Path2D> next;
        for (const auto& region : current) {
            auto overlap = boolean_paths(region, src, BooleanOp::Intersection, options);
            if (!overlap.success) return overlap;
            approximation_error_bound_mm += overlap.approximation_error_bound_mm;
            if (overlap.status == BooleanStatus::Empty || overlap.path.empty()) {
                next.push_back(region);
                continue;
            }

            changed = true;
            auto difference = boolean_paths(region, src, BooleanOp::Difference, options);
            if (!difference.success) return difference;
            approximation_error_bound_mm += difference.approximation_error_bound_mm;
            auto remaining = filled_components(std::move(difference));
            next.insert(next.end(), std::make_move_iterator(remaining.begin()),
                        std::make_move_iterator(remaining.end()));
        }
        current = std::move(next);
        if (current.empty()) {
            BooleanResult res;
            res.success = true;
            res.status = BooleanStatus::Empty;
            res.approximation_error_bound_mm = approximation_error_bound_mm;
            return res;
        }
    }

    BooleanResult final_res;
    if (!changed) {
        final_res.success = false;
        final_res.status = BooleanStatus::NoChange;
        final_res.error_message = "No trim source overlaps the target";
        return final_res;
    }
    final_res.success = true;
    final_res.status = BooleanStatus::Success;
    final_res.approximation_error_bound_mm = approximation_error_bound_mm;
    final_res.components = std::move(current);
    for (const auto& component : final_res.components) append_paths(final_res.path, component);
    return final_res;
}

} // namespace pikdraw::geometry
