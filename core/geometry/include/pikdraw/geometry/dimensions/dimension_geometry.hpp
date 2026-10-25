#pragma once

#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/geometry/curves/path.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace pikdraw::geometry {

enum class LinearOrientation : uint8_t {
    Aligned,
    Horizontal,
    Vertical
};

enum class ArrowType : uint8_t {
    ClosedFilled,
    ArchitecturalTick,
    Dot,
    Open
};

enum class OrdinateAxis : uint8_t {
    X,
    Y
};

struct DimensionArrow {
    Point2D tip;
    Point2D direction; // normalized vector pointing from base towards tip
};

struct LinearDimensionResult {
    double measured_value_mm{0.0};
    Point2D def_point1;
    Point2D def_point2;
    Point2D dim_line_start;
    Point2D dim_line_end;
    Point2D ext1_start;
    Point2D ext1_end;
    Point2D ext2_start;
    Point2D ext2_end;
    DimensionArrow arrow1;
    DimensionArrow arrow2;
    Point2D text_position;
    double text_rotation_rad{0.0};
    bool valid{false};
};

struct AngularDimensionResult {
    double measured_value_rad{0.0};
    double measured_value_deg{0.0};
    Point2D apex;
    double radius{0.0};
    double start_angle_rad{0.0};
    double sweep_angle_rad{0.0};
    DimensionArrow arrow1;
    DimensionArrow arrow2;
    Point2D text_position;
    bool valid{false};
};

struct RadialDimensionResult {
    double measured_value_mm{0.0};
    bool is_diameter{false};
    Point2D center;
    Point2D surface_point;
    Point2D elbow_point;
    Point2D landing_end;
    DimensionArrow arrow;
    Point2D text_position;
    bool valid{false};
};

struct ArcLengthDimensionResult {
    double measured_value_mm{0.0};
    Point2D center;
    double radius{0.0};
    double start_angle_rad{0.0};
    double sweep_angle_rad{0.0};
    DimensionArrow arrow1;
    DimensionArrow arrow2;
    Point2D text_position;
    bool valid{false};
};

struct OrdinateDimensionResult {
    double measured_value_mm{0.0};
    OrdinateAxis axis{OrdinateAxis::X};
    Point2D feature_point;
    Point2D elbow_point;
    Point2D landing_end;
    Point2D text_position;
    bool valid{false};
};

struct MultiLeaderResult {
    std::vector<Point2D> arrow_heads;
    std::vector<DimensionArrow> arrows;
    Point2D elbow_point;
    Point2D landing_end;
    Point2D text_position;
    bool valid{false};
};

LinearDimensionResult compute_linear_dimension(
    Point2D p1,
    Point2D p2,
    Point2D placement,
    LinearOrientation orientation,
    double ext_offset_mm = 1.5,
    double ext_overhang_mm = 2.0,
    double arrow_size_mm = 2.5);

AngularDimensionResult compute_angular_dimension(
    Point2D ray1_pt1,
    Point2D ray1_pt2,
    Point2D ray2_pt1,
    Point2D ray2_pt2,
    Point2D placement,
    double arrow_size_mm = 2.5);

RadialDimensionResult compute_radial_dimension(
    Point2D center,
    double radius_mm,
    bool is_diameter,
    Point2D placement,
    double arrow_size_mm = 2.5,
    double landing_length_mm = 8.0);

ArcLengthDimensionResult compute_arc_length_dimension(
    Point2D center,
    double radius_mm,
    double start_angle_rad,
    double sweep_angle_rad,
    Point2D placement,
    double arrow_size_mm = 2.5);

OrdinateDimensionResult compute_ordinate_dimension(
    Point2D datum_origin,
    Point2D feature_point,
    OrdinateAxis axis,
    Point2D placement,
    double landing_length_mm = 8.0);

MultiLeaderResult compute_multi_leader(
    const std::vector<Point2D>& arrow_heads,
    Point2D placement,
    double landing_length_mm = 8.0,
    double arrow_size_mm = 2.5);

Path2D make_arrow_path(const DimensionArrow& arrow, ArrowType type, double size_mm);

} // namespace pikdraw::geometry
