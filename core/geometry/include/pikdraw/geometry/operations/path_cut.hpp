#pragma once

#include "pikdraw/geometry/curves/path.hpp"
#include "pikdraw/geometry/operations/boolean.hpp"
#include <string>
#include <vector>

namespace pikdraw::geometry {

enum class PathCutStatus {
    Success,
    NoCut,           // Line or gesture did not divide the path
    EmptyResult,     // Operation resulted in an empty shape
    InvalidInput,
    Unsupported,
    WorkBudgetExceeded
};

struct PathCutResult {
    bool success{false};
    PathCutStatus status{PathCutStatus::InvalidInput};
    std::vector<Path2D> pieces;
    std::string error_message;
    double approximation_error_bound_mm{0.0};
};

/// Cuts a closed Path2D by a straight line defined by p1 -> p2.
/// If gap_mm > 0.0, removes a slit of width gap_mm along the cutting line.
/// Returns pieces resulting from the cut. If the line does not bisect/divide the shape,
/// returns status NoCut and success false.
[[nodiscard]] PathCutResult cut_path_by_line(
    const Path2D& subject,
    const Point2D& p1,
    const Point2D& p2,
    double gap_mm = 0.0,
    const BooleanOptions& options = {}
);

/// Cuts a closed Path2D by a polyline (freehand or multi-segment knife cut).
[[nodiscard]] PathCutResult cut_path_by_polyline(
    const Path2D& subject,
    const std::vector<Point2D>& polyline,
    double gap_mm = 0.0,
    const BooleanOptions& options = {}
);

/// Erases regions of a path along an eraser stroke.
/// diameter_mm: diameter of the eraser nib in millimeters.
/// round_nib: true for circular nib, false for square nib.
[[nodiscard]] PathCutResult erase_path(
    const Path2D& subject,
    const std::vector<Point2D>& stroke,
    double diameter_mm = 2.0,
    bool round_nib = true,
    const BooleanOptions& options = {}
);

/// Trims a target path using one or more source paths (Target minus Sources).
[[nodiscard]] BooleanResult trim_target_with_sources(
    const Path2D& target,
    const std::vector<Path2D>& sources,
    const BooleanOptions& options = {}
);

} // namespace pikdraw::geometry
