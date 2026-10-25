#include "pikdraw/geometry/operations/boolean.hpp"

#include "pikdraw/geometry/flattening/flattening.hpp"
#include "pikdraw/geometry/primitives/validation.hpp"
#include "rectangle_subset.hpp"
#include <iterator>
#include <clipper2/clipper.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace pikdraw::geometry {
namespace {

using Clipper2Lib::ClipType;
using Clipper2Lib::Clipper64;
using Clipper2Lib::FillRule;
using Clipper2Lib::Path64;
using Clipper2Lib::Paths64;
using Clipper2Lib::Point64;
using Clipper2Lib::PolyPath64;
using Clipper2Lib::PolyTree64;

constexpr size_t kMaximumBooleanPoints = 1'000'000;
constexpr long double kMaximumClipperCoordinate = 1.0e15L;

struct FlattenedFill {
    std::vector<std::vector<Point2D>> loops;
    size_t point_count{0};
};

BooleanResult failure(BooleanStatus status, const char* message) {
    BooleanResult result;
    result.status = status;
    result.error_message = message;
    return result;
}

bool valid_winding_rule(WindingRule rule) noexcept {
    return rule == WindingRule::NonZero || rule == WindingRule::EvenOdd;
}

FillRule clipper_fill_rule(WindingRule rule) noexcept {
    return rule == WindingRule::EvenOdd ? FillRule::EvenOdd : FillRule::NonZero;
}

bool flatten_fill(const Path2D& path, const BooleanOptions& options,
                  size_t remaining_points, FlattenedFill& output,
                  BooleanStatus& status, const char*& error) {
    if (path.empty()) return true;
    FlatteningOptions flatten_options;
    flatten_options.tolerance_mm = options.flatten_tolerance_mm;
    flatten_options.max_subdivision_depth = options.max_subdivision_depth;
    flatten_options.max_output_points = remaining_points;
    const auto flattened = flatten_path_checked(path, flatten_options);
    if (flattened.status != FlatteningStatus::Success) {
        switch (flattened.status) {
        case FlatteningStatus::ToleranceNotMet:
            status = BooleanStatus::ToleranceNotMet;
            error = "Boolean flattening could not meet its requested error bound";
            break;
        case FlatteningStatus::WorkBudgetExceeded:
            status = BooleanStatus::WorkBudgetExceeded;
            error = "Boolean flattening exceeded its point budget";
            break;
        case FlatteningStatus::InvalidOptions:
        case FlatteningStatus::InvalidPath:
            status = BooleanStatus::InvalidInput;
            error = "Boolean input path could not be flattened";
            break;
        case FlatteningStatus::Success:
            break;
        }
        return false;
    }

    for (const auto& subpath : flattened.subpaths) {
        std::vector<Point2D> loop;
        loop.reserve(subpath.points.size());
        for (const auto& point : subpath.points) {
            if (validate_point(point) != GeometryStatus::Ok) {
                status = BooleanStatus::InvalidInput;
                error = "Boolean input coordinates exceed the supported canvas range";
                return false;
            }
            if (loop.empty() || point.x != loop.back().x || point.y != loop.back().y)
                loop.push_back(point);
        }
        if (loop.size() > 1 && loop.front().x == loop.back().x &&
            loop.front().y == loop.back().y)
            loop.pop_back();
        if (loop.size() < 3) continue;
        if (loop.size() > options.max_output_points - output.point_count) {
            status = BooleanStatus::WorkBudgetExceeded;
            error = "Boolean flattening exceeded its combined point budget";
            return false;
        }
        output.point_count += loop.size();
        output.loops.push_back(std::move(loop));
    }
    return true;
}

void include_bounds(const FlattenedFill& fill, double& min_x, double& min_y,
                    double& max_x, double& max_y, bool& any_point) {
    for (const auto& loop : fill.loops) {
        for (const auto& point : loop) {
            if (!any_point) {
                min_x = max_x = point.x;
                min_y = max_y = point.y;
                any_point = true;
            } else {
                min_x = std::min(min_x, point.x);
                min_y = std::min(min_y, point.y);
                max_x = std::max(max_x, point.x);
                max_y = std::max(max_y, point.y);
            }
        }
    }
}

bool quantize_fill(const FlattenedFill& source, double origin_x, double origin_y,
                   double grid_mm, Paths64& result) {
    const long double inverse_grid = 1.0L / static_cast<long double>(grid_mm);
    result.reserve(source.loops.size());
    for (const auto& loop : source.loops) {
        Path64 path;
        path.reserve(loop.size());
        for (const auto& point : loop) {
            const long double x = (static_cast<long double>(point.x) - origin_x) * inverse_grid;
            const long double y = (static_cast<long double>(point.y) - origin_y) * inverse_grid;
            if (!std::isfinite(x) || !std::isfinite(y) ||
                std::abs(x) > kMaximumClipperCoordinate ||
                std::abs(y) > kMaximumClipperCoordinate)
                return false;
            const Point64 quantized{static_cast<int64_t>(std::llround(x)),
                                    static_cast<int64_t>(std::llround(y))};
            if (path.empty() || quantized.x != path.back().x || quantized.y != path.back().y)
                path.push_back(quantized);
        }
        if (path.size() > 1 && path.front().x == path.back().x &&
            path.front().y == path.back().y)
            path.pop_back();
        if (path.size() < 3)
            return false;
        result.push_back(std::move(path));
    }
    return true;
}

bool normalize_fill(Paths64& paths, FillRule fill_rule) {
    if (paths.empty()) return true;
    Clipper64 clipper;
    clipper.AddSubject(paths);
    Paths64 normalized;
    if (!clipper.Execute(ClipType::Union, fill_rule, normalized)) return false;
    paths = std::move(normalized);
    return true;
}

ClipType clipper_operation(BooleanOp operation, bool& reverse) noexcept {
    reverse = false;
    switch (operation) {
    case BooleanOp::Union: return ClipType::Union;
    case BooleanOp::Intersection: return ClipType::Intersection;
    case BooleanOp::Difference: return ClipType::Difference;
    case BooleanOp::Xor: return ClipType::Xor;
    case BooleanOp::BackMinusFront:
        reverse = true;
        return ClipType::Difference;
    case BooleanOp::Boundary:
    case BooleanOp::Simplify:
        return ClipType::NoClip;
    }
    return ClipType::NoClip;
}

Path2D path_from_clipper(const Path64& loop, double origin_x, double origin_y,
                         double grid_mm) {
    Path2D path;
    path.set_winding_rule(WindingRule::NonZero);
    if (loop.size() < 3) return path;
    const auto to_point = [&](const Point64& point) {
        return Point2D{origin_x + static_cast<double>(point.x) * grid_mm,
                       origin_y + static_cast<double>(point.y) * grid_mm};
    };
    path.move_to(to_point(loop.front()));
    for (size_t index = 1; index < loop.size(); ++index)
        path.line_to(to_point(loop[index]));
    path.close();
    return path;
}

void append_path(Path2D& destination, const Path2D& source) {
    for (const auto& element : source.elements()) {
        if (element.verb == PathVerb::MoveTo) destination.move_to(element.pt);
        else if (element.verb == PathVerb::LineTo) destination.line_to(element.pt);
        else if (element.verb == PathVerb::Close) destination.close();
    }
}

Path2D component_path(const PolyPath64& outer, double origin_x,
                     double origin_y, double grid_mm) {
    Path2D component;
    component.set_winding_rule(WindingRule::NonZero);
    append_path(component, path_from_clipper(outer.Polygon(), origin_x, origin_y, grid_mm));
    for (size_t index = 0; index < outer.Count(); ++index) {
        const auto* child = outer.Child(index);
        if (child->IsHole()) {
            append_path(component, path_from_clipper(child->Polygon(), origin_x, origin_y, grid_mm));
        }
    }
    return component;
}

void append_components(const PolyPath64& parent, double origin_x,
                       double origin_y, double grid_mm,
                       std::vector<Path2D>& components) {
    for (size_t index = 0; index < parent.Count(); ++index) {
        const auto* child = parent.Child(index);
        if (child->IsHole()) continue;
        auto component = component_path(*child, origin_x, origin_y, grid_mm);
        if (!component.empty()) components.push_back(std::move(component));
        // A filled island nested inside a hole is another independent region.
        append_components(*child, origin_x, origin_y, grid_mm, components);
    }
}

void append_boundary_loops(const PolyPath64& parent, double origin_x,
                           double origin_y, double grid_mm, Path2D& path,
                           std::vector<Path2D>& sub_paths) {
    for (size_t index = 0; index < parent.Count(); ++index) {
        const auto* child = parent.Child(index);
        auto loop = path_from_clipper(child->Polygon(), origin_x, origin_y, grid_mm);
        if (!loop.empty()) {
            append_path(path, loop);
            sub_paths.push_back(std::move(loop));
        }
        append_boundary_loops(*child, origin_x, origin_y, grid_mm, path, sub_paths);
    }
}

} // namespace

BooleanResult boolean_paths(const Path2D& subject, const Path2D& clip,
                            BooleanOp op, const BooleanOptions& options) {
    const auto subject_rect = detail::axis_aligned_rectangle(subject);
    const auto clip_rect = detail::axis_aligned_rectangle(clip);
    if (subject_rect && clip_rect) return boolean_rectangles(*subject_rect, *clip_rect, op);

    bool reverse = false;
    const auto clip_type = clipper_operation(op, reverse);
    if (clip_type == ClipType::NoClip)
        return failure(BooleanStatus::Unsupported,
                       "Boundary and Simplify are unsupported for general polygon paths");
    if (!std::isfinite(options.flatten_tolerance_mm) || options.flatten_tolerance_mm <= 0.0 ||
        !std::isfinite(options.quantization_grid_mm) || options.quantization_grid_mm <= 0.0 ||
        options.max_subdivision_depth == 0 || options.max_subdivision_depth > 64 ||
        options.max_output_points == 0 || options.max_output_points > kMaximumBooleanPoints ||
        !valid_winding_rule(subject.winding_rule()) || !valid_winding_rule(clip.winding_rule()))
        return failure(BooleanStatus::InvalidInput, "Invalid Boolean tolerance, budget or fill rule");

    FlattenedFill subject_flat;
    FlattenedFill clip_flat;
    BooleanStatus flatten_status = BooleanStatus::InvalidInput;
    const char* flatten_error = nullptr;
    if (!flatten_fill(subject, options, options.max_output_points,
                      subject_flat, flatten_status, flatten_error))
        return failure(flatten_status, flatten_error);
    const size_t remaining = options.max_output_points - subject_flat.point_count;
    if (!flatten_fill(clip, options, remaining, clip_flat, flatten_status, flatten_error))
        return failure(flatten_status, flatten_error);

    double min_x = 0.0, min_y = 0.0, max_x = 0.0, max_y = 0.0;
    bool any_point = false;
    include_bounds(subject_flat, min_x, min_y, max_x, max_y, any_point);
    include_bounds(clip_flat, min_x, min_y, max_x, max_y, any_point);
    const double origin_x = any_point ? min_x * 0.5 + max_x * 0.5 : 0.0;
    const double origin_y = any_point ? min_y * 0.5 + max_y * 0.5 : 0.0;

    Paths64 subject_paths;
    Paths64 clip_paths;
    if (!quantize_fill(subject_flat, origin_x, origin_y,
                       options.quantization_grid_mm, subject_paths) ||
        !quantize_fill(clip_flat, origin_x, origin_y,
                       options.quantization_grid_mm, clip_paths))
        return failure(BooleanStatus::ToleranceNotMet,
                       "Boolean quantization collapsed a closed fill or exceeded its coordinate range");

    try {
        const auto subject_rule = clipper_fill_rule(subject.winding_rule());
        const auto clip_rule = clipper_fill_rule(clip.winding_rule());
        FillRule operation_rule = subject_rule;
        if (subject_rule != clip_rule) {
            if (!normalize_fill(subject_paths, subject_rule) ||
                !normalize_fill(clip_paths, clip_rule))
                return failure(BooleanStatus::InvalidInput,
                               "Clipper2 could not normalize different input fill rules");
            operation_rule = FillRule::NonZero;
        }

        Clipper64 clipper;
        if (reverse) {
            clipper.AddSubject(clip_paths);
            clipper.AddClip(subject_paths);
        } else {
            clipper.AddSubject(subject_paths);
            clipper.AddClip(clip_paths);
        }
        PolyTree64 solution;
        if (!clipper.Execute(clip_type, operation_rule, solution))
            return failure(BooleanStatus::InvalidInput, "Clipper2 rejected the Boolean polygon operation");

        size_t solution_points = 0;
        std::vector<const PolyPath64*> pending;
        for (size_t index = 0; index < solution.Count(); ++index)
            pending.push_back(solution.Child(index));
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            if (node->Polygon().size() > options.max_output_points - solution_points)
                return failure(BooleanStatus::WorkBudgetExceeded,
                               "Boolean result exceeded its output point budget");
            solution_points += node->Polygon().size();
            for (size_t index = 0; index < node->Count(); ++index)
                pending.push_back(node->Child(index));
        }

        BooleanResult result;
        result.success = true;
        result.status = solution.Count() == 0 ? BooleanStatus::Empty : BooleanStatus::Success;
        result.approximation_error_bound_mm = options.flatten_tolerance_mm +
            std::sqrt(2.0) * options.quantization_grid_mm * 0.5;
        result.path.set_winding_rule(WindingRule::NonZero);
        append_boundary_loops(solution, origin_x, origin_y,
                              options.quantization_grid_mm,
                              result.path, result.sub_paths);
        append_components(solution, origin_x, origin_y,
                          options.quantization_grid_mm, result.components);
        if (result.path.empty()) result.status = BooleanStatus::Empty;
        return result;
    } catch (const std::bad_alloc&) {
        return failure(BooleanStatus::WorkBudgetExceeded,
                       "Boolean polygon operation exhausted its memory budget");
    } catch (...) {
        return failure(BooleanStatus::InvalidInput, "Boolean polygon operation failed");
    }
}

} // namespace pikdraw::geometry
