#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/model/object.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>

namespace pikdraw::vector {

class ShapeFactory {
  public:
    static constexpr int kMinimumPolygonSides = 3;
    static constexpr int kMaximumPolygonSides = 256;

    /// Resolve a drag rectangle shared by rectangle, ellipse and polygon tools.
    /// Shift keeps equal extents; Ctrl treats the initial point as the center.
    [[nodiscard]] static geometry::Rect2D constrained_drag_bounds(
        const geometry::Point2D &start, const geometry::Point2D &current,
        bool equal_sides, bool from_center) {
        if (!std::isfinite(start.x) || !std::isfinite(start.y) ||
            !std::isfinite(current.x) || !std::isfinite(current.y))
            throw std::invalid_argument("Invalid shape drag points");

        double dx = current.x - start.x;
        double dy = current.y - start.y;
        if (!std::isfinite(dx) || !std::isfinite(dy))
            throw std::invalid_argument("Shape drag delta overflow");
        if (equal_sides) {
            const double extent = std::max(std::abs(dx), std::abs(dy));
            const double x_direction = dx < 0.0 ? -1.0 : 1.0;
            const double y_direction = dy < 0.0 ? -1.0 : 1.0;
            dx = x_direction * extent;
            dy = y_direction * extent;
        }

        if (from_center) {
            const double half_width = std::abs(dx);
            const double half_height = std::abs(dy);
            const geometry::Rect2D bounds{start.x - half_width, start.y - half_height,
                                          half_width * 2.0, half_height * 2.0};
            if (!bounds.is_finite())
                throw std::invalid_argument("Centered shape bounds overflow");
            return bounds;
        }

        const geometry::Rect2D bounds{std::min(start.x, start.x + dx),
                                      std::min(start.y, start.y + dy),
                                      std::abs(dx), std::abs(dy)};
        if (!bounds.is_finite())
            throw std::invalid_argument("Shape bounds overflow");
        return bounds;
    }

    [[nodiscard]] static document::PathObject create_rectangle(
        const geometry::Rect2D &rect, color::ColorValue fill = color::RGBColor{0.14, 0.47, 0.82, 1.0},
        color::ColorValue stroke = color::RGBColor{0.0, 0.0, 0.0, 1.0}, double stroke_width_mm = 1.0) {
        if (!rect.is_finite() || rect.width <= 0 || rect.height <= 0 || !fill.is_valid() ||
            !stroke.is_valid() || !std::isfinite(stroke_width_mm) || stroke_width_mm < 0)
            throw std::invalid_argument("Invalid rectangle source parameters");
        document::PathObject obj;
        obj.id = document::generate_object_id();
        obj.name = "Rectangle " + std::to_string(obj.id);
        obj.path = geometry::Path2D::make_rect(rect.x, rect.y, rect.width, rect.height);
        obj.fill = std::move(fill);
        obj.stroke = std::move(stroke);
        obj.stroke_width_mm = stroke_width_mm;
        obj.has_fill = true;
        obj.has_stroke = true;
        return obj;
    }

    [[nodiscard]] static document::PathObject
    create_ellipse(const geometry::Point2D &center, double rx, double ry,
                   color::ColorValue fill = color::RGBColor{0.82, 0.24, 0.14, 1.0},
                   color::ColorValue stroke = color::RGBColor{0.0, 0.0, 0.0, 1.0},
                   double stroke_width_mm = 1.0) {
        if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(rx) ||
            !std::isfinite(ry) || rx <= 0 || ry <= 0 || !fill.is_valid() || !stroke.is_valid() ||
            !std::isfinite(stroke_width_mm) || stroke_width_mm < 0)
            throw std::invalid_argument("Invalid ellipse source parameters");
        document::PathObject obj;
        obj.id = document::generate_object_id();
        obj.name = "Ellipse " + std::to_string(obj.id);
        obj.path = geometry::Path2D::make_ellipse(center.x, center.y, rx, ry);
        obj.fill = std::move(fill);
        obj.stroke = std::move(stroke);
        obj.stroke_width_mm = stroke_width_mm;
        obj.has_fill = true;
        obj.has_stroke = true;
        return obj;
    }

    [[nodiscard]] static document::PathObject create_regular_polygon(
        const geometry::Point2D &center, double radius, int sides,
        double rotation_degrees = -90.0,
        color::ColorValue fill = color::RGBColor{0.14, 0.47, 0.82, 1.0},
        color::ColorValue stroke = color::RGBColor{0.0, 0.0, 0.0, 1.0},
        double stroke_width_mm = 1.0) {
        if (!std::isfinite(center.x) || !std::isfinite(center.y) ||
            !std::isfinite(radius) || radius <= 0.0 ||
            sides < kMinimumPolygonSides || sides > kMaximumPolygonSides ||
            !std::isfinite(rotation_degrees) || !fill.is_valid() || !stroke.is_valid() ||
            !std::isfinite(stroke_width_mm) || stroke_width_mm < 0.0)
            throw std::invalid_argument("Invalid regular polygon source parameters");

        document::PathObject obj;
        obj.id = document::generate_object_id();
        obj.name = "Polygon " + std::to_string(sides) + " sides " + std::to_string(obj.id);
        obj.path.set_winding_rule(geometry::WindingRule::NonZero);
        const double normalized_rotation = std::remainder(rotation_degrees, 360.0);
        const double rotation = normalized_rotation * std::numbers::pi / 180.0;
        const double side_count = static_cast<double>(sides);
        for (int side = 0; side < sides; ++side) {
            const double angle = rotation + 2.0 * std::numbers::pi *
                static_cast<double>(side) / side_count;
            const geometry::Point2D vertex{
                center.x + radius * std::cos(angle),
                center.y + radius * std::sin(angle)};
            if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y))
                throw std::invalid_argument("Regular polygon coordinates overflow");
            if (side == 0)
                obj.path.move_to(vertex);
            else
                obj.path.line_to(vertex);
        }
        obj.path.close();
        obj.fill = std::move(fill);
        obj.stroke = std::move(stroke);
        obj.stroke_width_mm = stroke_width_mm;
        obj.has_fill = true;
        obj.has_stroke = true;
        return obj;
    }
};

} // namespace pikdraw::vector
