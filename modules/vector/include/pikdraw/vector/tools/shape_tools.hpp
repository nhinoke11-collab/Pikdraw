#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/commands/path_commands.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/document/model/path_adapter.hpp"
#include "pikdraw/document/model/object.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace pikdraw::vector {

enum class RectCornerType {
    Round = 0,     // Bo tròn lồi (convex fillet)
    Scallop = 1,   // Bo lõm / ngược (concave scalloped arc)
    Chamfer = 2    // Vát góc phẳng (straight bevel diagonal)
};

struct RectCornerSettings {
    RectCornerType type{RectCornerType::Round};
    double tl{0.0};
    double tr{0.0};
    double br{0.0};
    double bl{0.0};
    bool locked{true};
    bool scale_relative{false};
};

[[nodiscard]] inline std::string serialize_rect_corner_spec(const RectCornerSettings& s) {
    std::ostringstream ss;
    ss << "rect_corner:v1;type=" << static_cast<int>(s.type)
       << ";tl=" << s.tl << ";tr=" << s.tr << ";br=" << s.br << ";bl=" << s.bl
       << ";lock=" << (s.locked ? 1 : 0) << ";rel=" << (s.scale_relative ? 1 : 0);
    return ss.str();
}

[[nodiscard]] inline std::optional<RectCornerSettings> deserialize_rect_corner_spec(std::string_view spec) {
    if (!spec.starts_with("rect_corner:")) return std::nullopt;
    RectCornerSettings s;
    size_t pos = 0;
    while (pos < spec.size()) {
        auto next = spec.find(';', pos);
        auto token = spec.substr(pos, next == std::string_view::npos ? spec.size() - pos : next - pos);
        if (auto eq = token.find('='); eq != std::string_view::npos) {
            auto key = token.substr(0, eq);
            auto val = token.substr(eq + 1);
            try {
                if (key == "type") s.type = static_cast<RectCornerType>(std::stoi(std::string(val)));
                else if (key == "tl") s.tl = std::max(0.0, std::stod(std::string(val)));
                else if (key == "tr") s.tr = std::max(0.0, std::stod(std::string(val)));
                else if (key == "br") s.br = std::max(0.0, std::stod(std::string(val)));
                else if (key == "bl") s.bl = std::max(0.0, std::stod(std::string(val)));
                else if (key == "lock") s.locked = (std::stoi(std::string(val)) != 0);
                else if (key == "rel") s.scale_relative = (std::stoi(std::string(val)) != 0);
            } catch (...) {}
        }
        if (next == std::string_view::npos) break;
        pos = next + 1;
    }
    return s;
}

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

    /// Generates a Path2D for a rectangle with independent corner radii and corner types (Round, Scallop, Chamfer).
    [[nodiscard]] static geometry::Path2D generate_rectangle_path(
        const geometry::Rect2D& rect, const RectCornerSettings& settings) {
        if (!rect.is_finite() || rect.width <= 0.0 || rect.height <= 0.0)
            throw std::invalid_argument("Invalid rectangle bounds for path generation");

        const double x = rect.x;
        const double y = rect.y;
        const double w = rect.width;
        const double h = rect.height;

        double r_tl = std::max(0.0, settings.tl);
        double r_tr = std::max(0.0, settings.tr);
        double r_br = std::max(0.0, settings.br);
        double r_bl = std::max(0.0, settings.bl);

        // Proportional corner clamping (CSS3 / SVG / CorelDRAW standard)
        double factor = 1.0;
        if (r_tl + r_tr > w && (r_tl + r_tr) > 0.0) factor = std::min(factor, w / (r_tl + r_tr));
        if (r_bl + r_br > w && (r_bl + r_br) > 0.0) factor = std::min(factor, w / (r_bl + r_br));
        if (r_tl + r_bl > h && (r_tl + r_bl) > 0.0) factor = std::min(factor, h / (r_tl + r_bl));
        if (r_tr + r_br > h && (r_tr + r_br) > 0.0) factor = std::min(factor, h / (r_tr + r_br));
        r_tl *= factor;
        r_tr *= factor;
        r_br *= factor;
        r_bl *= factor;

        if (r_tl < 1e-4 && r_tr < 1e-4 && r_br < 1e-4 && r_bl < 1e-4) {
            return geometry::Path2D::make_rect(x, y, w, h);
        }

        constexpr double kappa = 0.5522847498307935;
        geometry::Path2D path;

        // Top-Left Corner
        if (r_tl > 1e-4) {
            path.move_to(x, y + r_tl);
            if (settings.type == RectCornerType::Round) {
                path.cubic_to(x, y + r_tl * (1.0 - kappa),
                              x + r_tl * (1.0 - kappa), y,
                              x + r_tl, y);
            } else if (settings.type == RectCornerType::Scallop) {
                path.cubic_to(x + r_tl * kappa, y + r_tl,
                              x + r_tl, y + r_tl * kappa,
                              x + r_tl, y);
            } else { // Chamfer
                path.line_to(x + r_tl, y);
            }
        } else {
            path.move_to(x, y);
        }

        // Top edge
        if (x + r_tl < x + w - r_tr) {
            path.line_to(x + w - r_tr, y);
        }

        // Top-Right Corner
        if (r_tr > 1e-4) {
            if (settings.type == RectCornerType::Round) {
                path.cubic_to(x + w - r_tr * (1.0 - kappa), y,
                              x + w, y + r_tr * (1.0 - kappa),
                              x + w, y + r_tr);
            } else if (settings.type == RectCornerType::Scallop) {
                path.cubic_to(x + w - r_tr, y + r_tr * kappa,
                              x + w - r_tr * kappa, y + r_tr,
                              x + w, y + r_tr);
            } else { // Chamfer
                path.line_to(x + w, y + r_tr);
            }
        } else {
            path.line_to(x + w, y);
        }

        // Right edge
        if (y + r_tr < y + h - r_br) {
            path.line_to(x + w, y + h - r_br);
        }

        // Bottom-Right Corner
        if (r_br > 1e-4) {
            if (settings.type == RectCornerType::Round) {
                path.cubic_to(x + w, y + h - r_br * (1.0 - kappa),
                              x + w - r_br * (1.0 - kappa), y + h,
                              x + w - r_br, y + h);
            } else if (settings.type == RectCornerType::Scallop) {
                path.cubic_to(x + w - r_br * kappa, y + h - r_br,
                              x + w - r_br, y + h - r_br * kappa,
                              x + w - r_br, y + h);
            } else { // Chamfer
                path.line_to(x + w - r_br, y + h);
            }
        } else {
            path.line_to(x + w, y + h);
        }

        // Bottom edge
        if (x + w - r_br > x + r_bl) {
            path.line_to(x + r_bl, y + h);
        }

        // Bottom-Left Corner
        if (r_bl > 1e-4) {
            if (settings.type == RectCornerType::Round) {
                path.cubic_to(x + r_bl * (1.0 - kappa), y + h,
                              x, y + h - r_bl * (1.0 - kappa),
                              x, y + h - r_bl);
            } else if (settings.type == RectCornerType::Scallop) {
                path.cubic_to(x + r_bl, y + h - r_bl * kappa,
                              x + r_bl * kappa, y + h - r_bl,
                              x, y + h - r_bl);
            } else { // Chamfer
                path.line_to(x, y + h - r_bl);
            }
        } else {
            path.line_to(x, y + h);
        }

        // Left edge
        if (y + h - r_bl > y + r_tl) {
            path.line_to(x, y + r_tl);
        }

        path.close();
        return path;
    }

    [[nodiscard]] static document::PathObject create_rounded_rectangle(
        const geometry::Rect2D &rect, const RectCornerSettings& settings,
        color::ColorValue fill = color::RGBColor{0.14, 0.47, 0.82, 1.0},
        color::ColorValue stroke = color::RGBColor{0.0, 0.0, 0.0, 1.0}, double stroke_width_mm = 0.0) {
        if (!rect.is_finite() || rect.width <= 0 || rect.height <= 0 || !fill.is_valid() ||
            !stroke.is_valid() || !std::isfinite(stroke_width_mm) || stroke_width_mm < 0)
            throw std::invalid_argument("Invalid rectangle source parameters");
        document::PathObject obj;
        obj.id = document::generate_object_id();
        obj.name = "Rectangle " + std::to_string(obj.id);
        obj.path = generate_rectangle_path(rect, settings);
        auto ep = document::editable_path_from_path2d(obj.path, nullptr);
        if (ep) {
            obj.set_authoring_source(std::move(*ep));
        }
        obj.contour_spec = serialize_rect_corner_spec(settings);
        obj.fill = std::move(fill);
        obj.stroke = std::move(stroke);
        obj.stroke_width_mm = stroke_width_mm;
        obj.has_fill = true;
        obj.has_stroke = true;
        return obj;
    }

    [[nodiscard]] static document::PathObject create_rectangle(
        const geometry::Rect2D &rect, color::ColorValue fill = color::RGBColor{0.14, 0.47, 0.82, 1.0},
        color::ColorValue stroke = color::RGBColor{0.0, 0.0, 0.0, 1.0}, double stroke_width_mm = 0.0) {
        return create_rounded_rectangle(rect, RectCornerSettings{}, std::move(fill), std::move(stroke), stroke_width_mm);
    }

    [[nodiscard]] static document::PathObject
    create_ellipse(const geometry::Point2D &center, double rx, double ry,
                   color::ColorValue fill = color::RGBColor{0.82, 0.24, 0.14, 1.0},
                   color::ColorValue stroke = color::RGBColor{0.0, 0.0, 0.0, 1.0},
                   double stroke_width_mm = 0.0) {
        if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(rx) ||
            !std::isfinite(ry) || rx <= 0 || ry <= 0 || !fill.is_valid() || !stroke.is_valid() ||
            !std::isfinite(stroke_width_mm) || stroke_width_mm < 0)
            throw std::invalid_argument("Invalid ellipse source parameters");
        document::PathObject obj;
        obj.id = document::generate_object_id();
        obj.name = "Ellipse " + std::to_string(obj.id);
        obj.path = geometry::Path2D::make_ellipse(center.x, center.y, rx, ry);
        auto ep = document::editable_path_from_path2d(obj.path, nullptr);
        if (ep) {
            obj.set_authoring_source(std::move(*ep));
        }
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
        double stroke_width_mm = 0.0) {
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
        auto ep = document::editable_path_from_path2d(obj.path, nullptr);
        if (ep) {
            obj.set_authoring_source(std::move(*ep));
        }
        obj.fill = std::move(fill);
        obj.stroke = std::move(stroke);
        obj.stroke_width_mm = stroke_width_mm;
        obj.has_fill = true;
        obj.has_stroke = true;
        return obj;
    }
};

/// Constructs an appropriate transformation command for an object.
/// For parametric rectangles with rounded/scalloped/chamfered corners:
/// if scaling is applied, absorbs the scale into rectangle bounds and regenerates
/// the path directly, ensuring that the corner radius is NOT affected (unless scale_relative is ON)
/// and corners remain 100% circular, undistorted, and isotropic (CorelDRAW standard).
[[nodiscard]] inline std::unique_ptr<document::DocumentCommand> make_transformed_object_command(
    document::ObjectId id,
    const document::DocumentSnapshot& snapshot,
    const geometry::Transform2D& delta) {
    if (!delta.is_finite())
        return nullptr;
    const auto* placement = snapshot.hierarchy().find(id);
    if (!placement) return nullptr;
    const auto inverse_parent = placement->parent_world.inverted();
    if (!inverse_parent) return nullptr;
    const auto local_delta = *inverse_parent * delta * placement->parent_world;

    const auto obj_opt = snapshot.objects().get(id);
    if (!obj_opt)
        return std::make_unique<document::TransformObjectCommand>(id, delta);

    if (const auto* path_obj = std::get_if<document::PathObject>(&*obj_opt)) {
        if (path_obj->contour_spec.starts_with("rect_corner:")) {
            auto rcs = deserialize_rect_corner_spec(path_obj->contour_spec);
            if (rcs) {
                const auto T_cand = local_delta * path_obj->transform;
                const double sx = std::hypot(T_cand.a, T_cand.b);
                const double sy = std::hypot(T_cand.c, T_cand.d);

                // Check for non-shearing transform where scale is present
                const double dot = T_cand.a * T_cand.c + T_cand.b * T_cand.d;
                if (sx > 1e-6 && sy > 1e-6 && std::abs(dot) < 1e-3 * sx * sy &&
                    (std::abs(sx - 1.0) > 1e-4 || std::abs(sy - 1.0) > 1e-4)) {
                    const auto local_bounds = path_obj->path.bounds();
                    if (local_bounds.is_finite() && local_bounds.width > 0.0 && local_bounds.height > 0.0) {
                        const geometry::Rect2D target_bounds{
                            local_bounds.x * sx,
                            local_bounds.y * sy,
                            local_bounds.width * sx,
                            local_bounds.height * sy
                        };

                        if (rcs->scale_relative) {
                            const double scale_factor = std::min(sx, sy);
                            rcs->tl *= scale_factor;
                            rcs->tr *= scale_factor;
                            rcs->br *= scale_factor;
                            rcs->bl *= scale_factor;
                        }
                        // When scale_relative is false: rcs->tl, tr, br, bl remain UNCHANGED!
                        // They are not affected by scale, exactly matching CorelDRAW / Illustrator behavior!

                        const auto new_path = ShapeFactory::generate_rectangle_path(target_bounds, *rcs);
                        auto ep = document::editable_path_from_path2d(new_path, nullptr);
                        if (ep) {
                            const std::string new_spec = serialize_rect_corner_spec(*rcs);
                            const geometry::Transform2D new_transform{
                                T_cand.a / sx, T_cand.b / sx,
                                T_cand.c / sy, T_cand.d / sy,
                                T_cand.tx, T_cand.ty
                            };
                            return std::make_unique<document::SetAuthoringSourceCommand>(
                                id, std::move(*ep), std::move(new_spec), new_transform);
                        }
                    }
                }
            }
        }
    }

    return std::make_unique<document::TransformObjectCommand>(id, local_delta);
}

} // namespace pikdraw::vector
