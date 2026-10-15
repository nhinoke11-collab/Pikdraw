#pragma once

#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"
#include "pikdraw/geometry/curves/curve.hpp"
#include <cstddef>
#include <vector>

namespace pikdraw::geometry {

/// Winding rule according to Contract C1
enum class WindingRule {
    NonZero = 0,
    EvenOdd = 1
};

/// Path element verb
enum class PathVerb {
    MoveTo,
    LineTo,
    QuadTo,
    CubicTo,
    ArcTo,
    Close
};

/// Element in a path definition
struct PathElement {
    PathVerb verb{PathVerb::MoveTo};
    Point2D pt{0.0, 0.0};
    Point2D ctrl1{0.0, 0.0};
    Point2D ctrl2{0.0, 0.0};
    // For ArcTo
    double rx{0.0};
    double ry{0.0};
    double rotation_deg{0.0};
    bool large_arc{false};
    bool sweep{false};
};

struct PathBoundsResult;
struct PathTransformResult;

/// Composite 2D vector path
class Path2D {
public:
    Path2D() = default;

    void move_to(double x, double y) {
        elements_.push_back(PathElement{PathVerb::MoveTo, Point2D{x, y}});
    }

    void move_to(const Point2D& p) { move_to(p.x, p.y); }

    void line_to(double x, double y) {
        elements_.push_back(PathElement{PathVerb::LineTo, Point2D{x, y}});
    }

    void line_to(const Point2D& p) { line_to(p.x, p.y); }

    void quad_to(double cx, double cy, double x, double y) {
        PathElement el{PathVerb::QuadTo, Point2D{x, y}};
        el.ctrl1 = Point2D{cx, cy};
        elements_.push_back(el);
    }

    void cubic_to(double c1x, double c1y, double c2x, double c2y, double x, double y) {
        PathElement el{PathVerb::CubicTo, Point2D{x, y}};
        el.ctrl1 = Point2D{c1x, c1y};
        el.ctrl2 = Point2D{c2x, c2y};
        elements_.push_back(el);
    }

    void arc_to(double rx, double ry, double rot_deg, bool large_arc, bool sweep, double x, double y) {
        PathElement el{PathVerb::ArcTo, Point2D{x, y}};
        el.rx = rx;
        el.ry = ry;
        el.rotation_deg = rot_deg;
        el.large_arc = large_arc;
        el.sweep = sweep;
        elements_.push_back(el);
    }

    void close() {
        elements_.push_back(PathElement{PathVerb::Close});
    }

    [[nodiscard]] const std::vector<PathElement>& elements() const noexcept {
        return elements_;
    }

    [[nodiscard]] bool empty() const noexcept {
        return elements_.empty();
    }

    [[nodiscard]] size_t size() const noexcept {
        return elements_.size();
    }

    void clear() {
        elements_.clear();
    }

    [[nodiscard]] WindingRule winding_rule() const noexcept {
        return winding_rule_;
    }

    void set_winding_rule(WindingRule rule) noexcept {
        winding_rule_ = rule;
    }

    [[nodiscard]] Rect2D bounds() const;
    [[nodiscard]] PathBoundsResult bounds_checked() const;
    [[nodiscard]] Path2D transformed(const Transform2D& t) const;
    [[nodiscard]] PathTransformResult transformed_checked(const Transform2D& t) const;

    static Path2D make_rect(double x, double y, double w, double h);
    static Path2D make_ellipse(double cx, double cy, double rx, double ry);

private:
    std::vector<PathElement> elements_;
    WindingRule winding_rule_{WindingRule::NonZero};
};

struct PathBoundsResult {
    GeometryOperationStatus status{GeometryOperationStatus::InvalidInput};
    Rect2D bounds{};
    size_t element_index{0};
};

struct PathTransformResult {
    GeometryOperationStatus status{GeometryOperationStatus::InvalidInput};
    Path2D path{};
    size_t element_index{0};
};

} // namespace pikdraw::geometry
