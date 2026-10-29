#include "pikdraw/geometry/operations/path_cut.hpp"

#include "pikdraw/geometry/flattening/flattening.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

namespace pikdraw::geometry {
namespace {

struct Interval {
    double first{0.0};
    double last{0.0};
};

double dot(Point2D a, Point2D b) noexcept { return a.x * b.x + a.y * b.y; }

bool clip_slab(double origin, double delta, double minimum, double maximum,
               double& first, double& last) noexcept {
    constexpr double kParallelEpsilon = 1e-14;
    if (std::abs(delta) <= kParallelEpsilon)
        return origin >= minimum && origin <= maximum;
    double a = (minimum - origin) / delta;
    double b = (maximum - origin) / delta;
    if (a > b) std::swap(a, b);
    first = std::max(first, a);
    last = std::min(last, b);
    return first <= last;
}

void add_circle_interval(Point2D a, Point2D b, Point2D center, double radius,
                         std::vector<Interval>& intervals) {
    const Point2D direction = b - a;
    const Point2D relative = a - center;
    const double aa = dot(direction, direction);
    const double cc = dot(relative, relative) - radius * radius;
    if (aa <= 1e-24) {
        if (cc <= 0.0) intervals.push_back({0.0, 1.0});
        return;
    }
    const double bb = 2.0 * dot(relative, direction);
    const double discriminant = bb * bb - 4.0 * aa * cc;
    if (discriminant < 0.0) {
        if (cc <= 0.0) intervals.push_back({0.0, 1.0});
        return;
    }
    const double root = std::sqrt(std::max(0.0, discriminant));
    double first = (-bb - root) / (2.0 * aa);
    double last = (-bb + root) / (2.0 * aa);
    first = std::max(0.0, first);
    last = std::min(1.0, last);
    if (first < last) intervals.push_back({first, last});
}

void add_square_interval(Point2D a, Point2D b, Point2D center, double radius,
                         std::vector<Interval>& intervals) {
    const Point2D direction = b - a;
    double first = 0.0;
    double last = 1.0;
    if (clip_slab(a.x - center.x, direction.x, -radius, radius, first, last) &&
        clip_slab(a.y - center.y, direction.y, -radius, radius, first, last) &&
        first < last) {
        intervals.push_back({first, last});
    }
}

void add_swept_piece_intervals(Point2D a, Point2D b, Point2D c, Point2D d,
                               double radius, bool round_nib,
                               std::vector<Interval>& intervals) {
    const Point2D nib_delta = d - c;
    const double nib_length = std::hypot(nib_delta.x, nib_delta.y);
    if (nib_length <= 1e-12) {
        if (round_nib) add_circle_interval(a, b, c, radius, intervals);
        else add_square_interval(a, b, c, radius, intervals);
        return;
    }

    const Point2D axis{nib_delta.x / nib_length, nib_delta.y / nib_length};
    const Point2D normal{-axis.y, axis.x};
    const Point2D source_delta = b - a;
    const Point2D relative = a - c;
    double first = 0.0;
    double last = 1.0;
    if (clip_slab(dot(relative, axis), dot(source_delta, axis), 0.0, nib_length,
                  first, last) &&
        clip_slab(dot(relative, normal), dot(source_delta, normal), -radius, radius,
                  first, last) &&
        first < last) {
        intervals.push_back({first, last});
    }
    if (round_nib) {
        add_circle_interval(a, b, c, radius, intervals);
        add_circle_interval(a, b, d, radius, intervals);
    } else {
        // The desktop footprint is a perpendicular rectangle with axis-aligned
        // square caps. Match those caps here so preview and commit agree.
        add_square_interval(a, b, c, radius, intervals);
        add_square_interval(a, b, d, radius, intervals);
    }
}

std::vector<Interval> merged_intervals(Point2D a, Point2D b,
                                       const std::vector<Point2D>& stroke,
                                       double radius, bool round_nib) {
    std::vector<Interval> intervals;
    if (stroke.size() == 1) {
        if (round_nib) add_circle_interval(a, b, stroke.front(), radius, intervals);
        else add_square_interval(a, b, stroke.front(), radius, intervals);
    } else {
        for (size_t index = 0; index + 1 < stroke.size(); ++index)
            add_swept_piece_intervals(a, b, stroke[index], stroke[index + 1],
                                      radius, round_nib, intervals);
    }
    std::sort(intervals.begin(), intervals.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });
    std::vector<Interval> merged;
    for (const auto interval : intervals) {
        const double first = std::clamp(interval.first, 0.0, 1.0);
        const double last = std::clamp(interval.last, 0.0, 1.0);
        if (!(first < last)) continue;
        if (!merged.empty() && first <= merged.back().last + 1e-12)
            merged.back().last = std::max(merged.back().last, last);
        else
            merged.push_back({first, last});
    }
    return merged;
}

Point2D interpolate(Point2D a, Point2D b, double t) noexcept {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

void append_unique(std::vector<Point2D>& points, Point2D point) {
    if (points.empty() || points.back().distance_to(point) > 1e-10)
        points.push_back(point);
}

std::vector<Path2D> cut_subpath(const FlattenedSubpath& subpath,
                                const std::vector<Point2D>& stroke,
                                double radius, bool round_nib,
                                bool& cut, size_t& work, size_t work_budget) {
    std::vector<Path2D> pieces;
    if (subpath.points.size() < 2) return pieces;
    std::vector<Point2D> points = subpath.points;
    if (subpath.is_closed && points.front().distance_to(points.back()) > 1e-10)
        points.push_back(points.front());

    std::vector<Point2D> current;
    auto flush = [&] {
        if (current.size() >= 2) {
            Path2D path;
            path.move_to(current.front());
            for (size_t index = 1; index < current.size(); ++index)
                path.line_to(current[index]);
            pieces.push_back(std::move(path));
        }
        current.clear();
    };
    for (size_t index = 0; index + 1 < points.size(); ++index) {
        if (++work > work_budget) {
            pieces.clear();
            return pieces;
        }
        const Point2D a = points[index];
        const Point2D b = points[index + 1];
        if (a.distance_to(b) <= 1e-12) continue;
        const auto removed = merged_intervals(a, b, stroke, radius, round_nib);
        if (removed.empty()) {
            if (current.empty()) current.push_back(a);
            else if (current.back().distance_to(a) > 1e-9) {
                flush();
                current.push_back(a);
            }
            append_unique(current, b);
            continue;
        }

        cut = true;
        double cursor = 0.0;
        for (const auto interval : removed) {
            if (interval.first > cursor + 1e-12) {
                const auto start = interpolate(a, b, cursor);
                const auto end = interpolate(a, b, interval.first);
                if (current.empty()) current.push_back(start);
                else if (current.back().distance_to(start) > 1e-9) {
                    flush();
                    current.push_back(start);
                }
                append_unique(current, end);
            }
            flush();
            cursor = std::max(cursor, interval.last);
        }
        if (cursor < 1.0 - 1e-12) {
            const auto start = interpolate(a, b, cursor);
            if (current.empty()) current.push_back(start);
            else if (current.back().distance_to(start) > 1e-9) {
                flush();
                current.push_back(start);
            }
            append_unique(current, b);
        }
    }
    flush();

    // A cut elsewhere on a closed path does not make its original start vertex
    // a new endpoint. Join the trailing and leading runs across that vertex.
    if (subpath.is_closed && pieces.size() >= 2 &&
        !subpath.points.empty() &&
        pieces.front().elements().front().pt.distance_to(subpath.points.front()) <= 1e-9 &&
        pieces.back().elements().back().pt.distance_to(subpath.points.front()) <= 1e-9) {
        const auto first_points = flatten_path(pieces.front(), {1e-6, 1, 100000}).front().points;
        const auto last_points = flatten_path(pieces.back(), {1e-6, 1, 100000}).front().points;
        Path2D joined;
        joined.move_to(last_points.front());
        for (size_t i = 1; i < last_points.size(); ++i) joined.line_to(last_points[i]);
        for (size_t i = 1; i < first_points.size(); ++i) joined.line_to(first_points[i]);
        pieces.front() = std::move(joined);
        pieces.pop_back();
    }
    return pieces;
}

} // namespace

PathCutResult erase_open_path_stroke(
    const Path2D& subject,
    const std::vector<Point2D>& stroke,
    double diameter_mm,
    double subject_stroke_width_mm,
    bool round_nib,
    const BooleanOptions& options
) {
    constexpr size_t kMaxInputStrokePoints = 4096;
    constexpr size_t kMaxPathElements = 250000;
    constexpr size_t kMaxSegmentPairs = 2000000;
    if (subject.empty() || stroke.empty() || subject.size() > kMaxPathElements ||
        stroke.size() > kMaxInputStrokePoints || !std::isfinite(diameter_mm) ||
        diameter_mm <= 0.0 || !std::isfinite(subject_stroke_width_mm) ||
        subject_stroke_width_mm < 0.0 || !std::isfinite(options.flatten_tolerance_mm) ||
        options.flatten_tolerance_mm <= 0.0 || options.max_output_points == 0) {
        return {false, PathCutStatus::InvalidInput, {}, "Invalid open-path eraser input"};
    }
    for (const auto point : stroke) {
        if (!point.is_finite())
            return {false, PathCutStatus::InvalidInput, {}, "Eraser stroke is not finite"};
    }
    const double radius = (diameter_mm + subject_stroke_width_mm) * 0.5;
    if (!std::isfinite(radius) || radius <= 0.0 || radius > kMaxCanvasDimensionMm)
        return {false, PathCutStatus::InvalidInput, {}, "Eraser footprint is outside supported range"};

    FlatteningOptions flatten_options;
    flatten_options.tolerance_mm = options.flatten_tolerance_mm;
    flatten_options.max_subdivision_depth = options.max_subdivision_depth;
    flatten_options.max_output_points = options.max_output_points;
    const auto flattened = flatten_path_checked(subject, flatten_options);
    if (flattened.status != FlatteningStatus::Success) {
        const auto status = flattened.status == FlatteningStatus::WorkBudgetExceeded
            ? PathCutStatus::WorkBudgetExceeded : PathCutStatus::Unsupported;
        return {false, status, {}, "Could not flatten source path within the declared bound",
                flattened.unmet_error_bound_mm};
    }
    size_t source_segments = 0;
    for (const auto& subpath : flattened.subpaths)
        source_segments += subpath.points.size() + (subpath.is_closed ? 1 : 0);
    const size_t stroke_segments = std::max<size_t>(1, stroke.size() - 1);
    if (source_segments > kMaxSegmentPairs / stroke_segments)
        return {false, PathCutStatus::WorkBudgetExceeded, {},
                "Open-path eraser exceeded its intersection work budget"};

    bool cut = false;
    size_t work = 0;
    std::vector<Path2D> pieces;
    for (const auto& subpath : flattened.subpaths) {
        auto subpath_pieces = cut_subpath(subpath, stroke, radius, round_nib,
                                          cut, work, kMaxSegmentPairs);
        if (work > kMaxSegmentPairs)
            return {false, PathCutStatus::WorkBudgetExceeded, {},
                    "Open-path eraser exceeded its intersection work budget"};
        pieces.insert(pieces.end(), std::make_move_iterator(subpath_pieces.begin()),
                      std::make_move_iterator(subpath_pieces.end()));
    }
    if (!cut)
        return {false, PathCutStatus::NoCut, {}, "Eraser stroke does not cross the path"};
    if (pieces.empty())
        return {true, PathCutStatus::EmptyResult, {}, {}, options.flatten_tolerance_mm};
    return {true, PathCutStatus::Success, std::move(pieces), {},
            options.flatten_tolerance_mm};
}

} // namespace pikdraw::geometry
