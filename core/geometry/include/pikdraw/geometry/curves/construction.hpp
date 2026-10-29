#pragma once

#include "pikdraw/geometry/curves/path.hpp"
#include <optional>

namespace pikdraw::geometry {

struct CircleSolution {
    Point2D center{};
    double radius{0.0};
};

struct ArcSolution {
    Point2D center{};
    double radius{0.0};
    double start_angle_rad{0.0};
    double sweep_angle_rad{0.0};
    [[nodiscard]] Point2D start_point() const noexcept;
    [[nodiscard]] Point2D end_point() const noexcept;
};

[[nodiscard]] std::optional<CircleSolution> circle_from_center_radius(
    Point2D center, double radius) noexcept;
[[nodiscard]] std::optional<CircleSolution> circle_from_diameter_points(
    Point2D first, Point2D second) noexcept;
[[nodiscard]] std::optional<CircleSolution> circle_through_three_points(
    Point2D first, Point2D second, Point2D third) noexcept;
[[nodiscard]] std::optional<ArcSolution> arc_through_three_points(
    Point2D start, Point2D through, Point2D end) noexcept;

// Produces endpoint-arc path elements. A full circle is represented by two
// semicircular arcs so its endpoints remain distinct in editable path data.
[[nodiscard]] Path2D make_analytic_circle_path(const CircleSolution& circle);
[[nodiscard]] Path2D make_analytic_arc_path(const ArcSolution& arc);

} // namespace pikdraw::geometry
