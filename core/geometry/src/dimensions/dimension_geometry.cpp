#include "pikdraw/geometry/dimensions/dimension_geometry.hpp"

#include <algorithm>
#include <cmath>

namespace pikdraw::geometry {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

Point2D normalize_vec(Point2D v) {
    const double len = std::hypot(v.x, v.y);
    if (len < 1e-12) return Point2D{1.0, 0.0};
    return Point2D{v.x / len, v.y / len};
}

double normalize_angle_2pi(double a) {
    while (a < 0.0) a += 2.0 * kPi;
    while (a >= 2.0 * kPi) a -= 2.0 * kPi;
    return a;
}

} // namespace

Path2D make_arrow_path(const DimensionArrow& arrow, ArrowType type, double size_mm) {
    Path2D path;
    const double size = std::max(0.5, size_mm);

    switch (type) {
    case ArrowType::ClosedFilled: {
        const Point2D dir = normalize_vec(arrow.direction);
        const Point2D perp{-dir.y, dir.x};
        const Point2D base = arrow.tip - dir * size;
        const double half_w = size * 0.35;
        const Point2D corner1 = base + perp * half_w;
        const Point2D corner2 = base - perp * half_w;

        path.move_to(arrow.tip);
        path.line_to(corner1);
        path.line_to(corner2);
        path.close();
        break;
    }
    case ArrowType::ArchitecturalTick: {
        constexpr double kTickAngle = kPi / 4.0; // 45 deg
        const Point2D tick_dir{std::cos(kTickAngle), std::sin(kTickAngle)};
        const Point2D p1 = arrow.tip - tick_dir * (size * 0.5);
        const Point2D p2 = arrow.tip + tick_dir * (size * 0.5);

        path.move_to(p1);
        path.line_to(p2);
        break;
    }
    case ArrowType::Dot: {
        const double r = size * 0.25;
        path = Path2D::make_ellipse(arrow.tip.x, arrow.tip.y, r, r);
        break;
    }
    case ArrowType::Open: {
        const Point2D dir = normalize_vec(arrow.direction);
        const Point2D perp{-dir.y, dir.x};
        const Point2D base = arrow.tip - dir * size;
        const double half_w = size * 0.55;
        path.move_to(base + perp * half_w);
        path.line_to(arrow.tip);
        path.line_to(base - perp * half_w);
        break;
    }
    }

    return path;
}

LinearDimensionResult compute_linear_dimension(
    Point2D p1,
    Point2D p2,
    Point2D placement,
    LinearOrientation orientation,
    double ext_offset_mm,
    double ext_overhang_mm,
    double arrow_size_mm) {
    (void)arrow_size_mm;
    LinearDimensionResult res;
    res.def_point1 = p1;
    res.def_point2 = p2;

    if (orientation == LinearOrientation::Aligned) {
        const Point2D v = p2 - p1;
        const double len = std::hypot(v.x, v.y);
        if (len < 1e-9) return res;

        const Point2D u = v / len;
        const Point2D n{-u.y, u.x};
        const double d = (placement.x - p1.x) * n.x + (placement.y - p1.y) * n.y;

        const Point2D d1 = p1 + n * d;
        const Point2D d2 = p2 + n * d;
        const double s = (d >= 0.0) ? 1.0 : -1.0;

        res.dim_line_start = d1;
        res.dim_line_end = d2;
        res.ext1_start = p1 + n * (ext_offset_mm * s);
        res.ext1_end = d1 + n * (ext_overhang_mm * s);
        res.ext2_start = p2 + n * (ext_offset_mm * s);
        res.ext2_end = d2 + n * (ext_overhang_mm * s);

        res.arrow1.tip = d1;
        res.arrow1.direction = u;
        res.arrow2.tip = d2;
        res.arrow2.direction = Point2D{-u.x, -u.y};

        res.text_position = (d1 + d2) * 0.5;
        double rot = std::atan2(u.y, u.x);
        if (rot > kPi * 0.5) rot -= kPi;
        else if (rot <= -kPi * 0.5) rot += kPi;
        res.text_rotation_rad = rot;

        res.measured_value_mm = len;
        res.valid = true;
    } else if (orientation == LinearOrientation::Horizontal) {
        const double dx = std::abs(p2.x - p1.x);
        if (dx < 1e-9) return res;

        const Point2D d1{p1.x, placement.y};
        const Point2D d2{p2.x, placement.y};
        const double s1 = (placement.y >= p1.y) ? 1.0 : -1.0;
        const double s2 = (placement.y >= p2.y) ? 1.0 : -1.0;

        res.dim_line_start = d1;
        res.dim_line_end = d2;
        res.ext1_start = Point2D{p1.x, p1.y + ext_offset_mm * s1};
        res.ext1_end = Point2D{p1.x, placement.y + ext_overhang_mm * s1};
        res.ext2_start = Point2D{p2.x, p2.y + ext_offset_mm * s2};
        res.ext2_end = Point2D{p2.x, placement.y + ext_overhang_mm * s2};

        const Point2D u = (p2.x >= p1.x) ? Point2D{1.0, 0.0} : Point2D{-1.0, 0.0};
        res.arrow1.tip = d1;
        res.arrow1.direction = u;
        res.arrow2.tip = d2;
        res.arrow2.direction = Point2D{-u.x, -u.y};

        res.text_position = Point2D{(p1.x + p2.x) * 0.5, placement.y};
        res.text_rotation_rad = 0.0;
        res.measured_value_mm = dx;
        res.valid = true;
    } else { // Vertical
        const double dy = std::abs(p2.y - p1.y);
        if (dy < 1e-9) return res;

        const Point2D d1{placement.x, p1.y};
        const Point2D d2{placement.x, p2.y};
        const double s1 = (placement.x >= p1.x) ? 1.0 : -1.0;
        const double s2 = (placement.x >= p2.x) ? 1.0 : -1.0;

        res.dim_line_start = d1;
        res.dim_line_end = d2;
        res.ext1_start = Point2D{p1.x + ext_offset_mm * s1, p1.y};
        res.ext1_end = Point2D{placement.x + ext_overhang_mm * s1, p1.y};
        res.ext2_start = Point2D{p2.x + ext_offset_mm * s2, p2.y};
        res.ext2_end = Point2D{placement.x + ext_overhang_mm * s2, p2.y};

        const Point2D u = (p2.y >= p1.y) ? Point2D{0.0, 1.0} : Point2D{0.0, -1.0};
        res.arrow1.tip = d1;
        res.arrow1.direction = u;
        res.arrow2.tip = d2;
        res.arrow2.direction = Point2D{-u.x, -u.y};

        res.text_position = Point2D{placement.x, (p1.y + p2.y) * 0.5};
        res.text_rotation_rad = kPi * 0.5;
        res.measured_value_mm = dy;
        res.valid = true;
    }

    return res;
}

AngularDimensionResult compute_angular_dimension(
    Point2D ray1_pt1,
    Point2D ray1_pt2,
    Point2D ray2_pt1,
    Point2D ray2_pt2,
    Point2D placement,
    double arrow_size_mm) {
    (void)arrow_size_mm;
    AngularDimensionResult res;

    const Point2D v1 = ray1_pt2 - ray1_pt1;
    const Point2D v2 = ray2_pt2 - ray2_pt1;
    const double det = v1.x * v2.y - v1.y * v2.x;
    if (std::abs(det) < 1e-9) return res; // parallel or degenerate

    const double dx = ray2_pt1.x - ray1_pt1.x;
    const double dy = ray2_pt1.y - ray1_pt1.y;
    const double t = (dx * v2.y - dy * v2.x) / det;
    const Point2D apex = ray1_pt1 + v1 * t;

    const double R = std::max(5.0, apex.distance_to(placement));
    const Point2D u1 = normalize_vec(v1);
    const Point2D u2 = normalize_vec(v2);

    double a1 = normalize_angle_2pi(std::atan2(u1.y, u1.x));
    double a2 = normalize_angle_2pi(std::atan2(u2.y, u2.x));

    double sweep = normalize_angle_2pi(a2 - a1);
    if (sweep > kPi) {
        // Use acute/obtuse inner angle
        std::swap(a1, a2);
        sweep = 2.0 * kPi - sweep;
    }

    res.apex = apex;
    res.radius = R;
    res.start_angle_rad = a1;
    res.sweep_angle_rad = sweep;
    res.measured_value_rad = sweep;
    res.measured_value_deg = sweep * 180.0 / kPi;

    const double mid_angle = a1 + sweep * 0.5;
    res.text_position = apex + Point2D{std::cos(mid_angle), std::sin(mid_angle)} * R;

    // Arrow 1 at a1 pointing in sweep direction (CCW)
    res.arrow1.tip = apex + Point2D{std::cos(a1), std::sin(a1)} * R;
    res.arrow1.direction = Point2D{-std::sin(a1), std::cos(a1)};

    // Arrow 2 at a2 pointing against sweep direction (CW)
    res.arrow2.tip = apex + Point2D{std::cos(a2), std::sin(a2)} * R;
    res.arrow2.direction = Point2D{std::sin(a2), -std::cos(a2)};

    res.valid = true;
    return res;
}

RadialDimensionResult compute_radial_dimension(
    Point2D center,
    double radius_mm,
    bool is_diameter,
    Point2D placement,
    double arrow_size_mm,
    double landing_length_mm) {
    (void)arrow_size_mm;
    RadialDimensionResult res;
    radius_mm = std::abs(radius_mm);
    if (radius_mm < 1e-6) return res;

    const double theta = std::atan2(placement.y - center.y, placement.x - center.x);
    const Point2D surf = center + Point2D{std::cos(theta), std::sin(theta)} * radius_mm;

    const double dist = center.distance_to(placement);
    const double elbow_dist = std::max(dist, radius_mm + 5.0);
    const Point2D elbow = center + Point2D{std::cos(theta), std::sin(theta)} * elbow_dist;

    const double sign_x = (std::cos(theta) >= 0.0) ? 1.0 : -1.0;
    const Point2D landing_end = elbow + Point2D{landing_length_mm * sign_x, 0.0};

    res.center = center;
    res.surface_point = surf;
    res.elbow_point = elbow;
    res.landing_end = landing_end;
    res.arrow.tip = surf;
    res.arrow.direction = Point2D{std::cos(theta), std::sin(theta)};
    res.text_position = landing_end + Point2D{sign_x * 2.0, 0.0};
    res.is_diameter = is_diameter;
    res.measured_value_mm = is_diameter ? (2.0 * radius_mm) : radius_mm;
    res.valid = true;
    return res;
}

ArcLengthDimensionResult compute_arc_length_dimension(
    Point2D center,
    double radius_mm,
    double start_angle_rad,
    double sweep_angle_rad,
    Point2D placement,
    double arrow_size_mm) {
    (void)arrow_size_mm;
    ArcLengthDimensionResult res;
    radius_mm = std::abs(radius_mm);
    sweep_angle_rad = std::abs(sweep_angle_rad);
    if (radius_mm < 1e-6 || sweep_angle_rad < 1e-6) return res;

    const double R_dim = std::max(5.0, center.distance_to(placement));
    const double mid_angle = start_angle_rad + sweep_angle_rad * 0.5;

    res.center = center;
    res.radius = R_dim;
    res.start_angle_rad = start_angle_rad;
    res.sweep_angle_rad = sweep_angle_rad;
    res.measured_value_mm = radius_mm * sweep_angle_rad;
    res.text_position = center + Point2D{std::cos(mid_angle), std::sin(mid_angle)} * R_dim;

    const double a1 = start_angle_rad;
    const double a2 = start_angle_rad + sweep_angle_rad;
    res.arrow1.tip = center + Point2D{std::cos(a1), std::sin(a1)} * R_dim;
    res.arrow1.direction = Point2D{-std::sin(a1), std::cos(a1)};
    res.arrow2.tip = center + Point2D{std::cos(a2), std::sin(a2)} * R_dim;
    res.arrow2.direction = Point2D{std::sin(a2), -std::cos(a2)};

    res.valid = true;
    return res;
}

OrdinateDimensionResult compute_ordinate_dimension(
    Point2D datum_origin,
    Point2D feature_point,
    OrdinateAxis axis,
    Point2D placement,
    double landing_length_mm) {
    OrdinateDimensionResult res;
    res.axis = axis;
    res.feature_point = feature_point;

    const double val = (axis == OrdinateAxis::X)
        ? (feature_point.x - datum_origin.x)
        : (feature_point.y - datum_origin.y);
    res.measured_value_mm = val;

    res.elbow_point = placement;
    const double sign_x = (placement.x >= feature_point.x) ? 1.0 : -1.0;
    res.landing_end = placement + Point2D{landing_length_mm * sign_x, 0.0};
    res.text_position = res.landing_end + Point2D{sign_x * 2.0, 0.0};
    res.valid = true;
    return res;
}

MultiLeaderResult compute_multi_leader(
    const std::vector<Point2D>& arrow_heads,
    Point2D placement,
    double landing_length_mm,
    double arrow_size_mm) {
    (void)arrow_size_mm;
    MultiLeaderResult res;
    if (arrow_heads.empty()) return res;

    res.arrow_heads = arrow_heads;
    res.elbow_point = placement;
    const double sign_x = 1.0; // standard landing to right
    res.landing_end = placement + Point2D{landing_length_mm * sign_x, 0.0};
    res.text_position = res.landing_end + Point2D{2.0, 0.0};

    res.arrows.reserve(arrow_heads.size());
    for (const auto& head : arrow_heads) {
        DimensionArrow arrow;
        arrow.tip = head;
        arrow.direction = normalize_vec(head - placement);
        res.arrows.push_back(arrow);
    }

    res.valid = true;
    return res;
}

} // namespace pikdraw::geometry
