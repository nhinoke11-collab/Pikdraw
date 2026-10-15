#pragma once

#include "pikdraw/document/model/object.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include <memory>
#include <string>

namespace pikdraw::vector {

class ShapeFactory {
public:
    [[nodiscard]] static document::PathObject create_rectangle(
        const geometry::Rect2D& rect,
        color::ColorValue fill = color::RGBColor{0.14, 0.47, 0.82, 1.0},
        color::ColorValue stroke = color::RGBColor{0.0, 0.0, 0.0, 1.0},
        double stroke_width_mm = 1.0) {
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

    [[nodiscard]] static document::PathObject create_ellipse(
        const geometry::Point2D& center,
        double rx, double ry,
        color::ColorValue fill = color::RGBColor{0.82, 0.24, 0.14, 1.0},
        color::ColorValue stroke = color::RGBColor{0.0, 0.0, 0.0, 1.0},
        double stroke_width_mm = 1.0) {
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
};

} // namespace pikdraw::vector
