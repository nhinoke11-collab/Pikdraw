#include "pikdraw/geometry/curves/path.hpp"

namespace pikdraw::geometry {

Rect2D Path2D::bounds() const {
    if (elements_.empty()) {
        return Rect2D{};
    }

    double min_x = 0.0;
    double max_x = 0.0;
    double min_y = 0.0;
    double max_y = 0.0;
    bool first = true;
    Point2D current_pt{0.0, 0.0};

    auto expand = [&](const Rect2D& b) {
        if (first) {
            min_x = b.left();
            max_x = b.right();
            min_y = b.top();
            max_y = b.bottom();
            first = false;
        } else {
            min_x = std::min(min_x, b.left());
            max_x = std::max(max_x, b.right());
            min_y = std::min(min_y, b.top());
            max_y = std::max(max_y, b.bottom());
        }
    };

    for (const auto& el : elements_) {
        switch (el.verb) {
            case PathVerb::MoveTo:
                current_pt = el.pt;
                expand(Rect2D{current_pt.x, current_pt.y, 0.0, 0.0});
                break;
            case PathVerb::LineTo: {
                LineSegment seg{current_pt, el.pt};
                expand(seg.bounds());
                current_pt = el.pt;
                break;
            }
            case PathVerb::QuadTo: {
                QuadraticBezier quad{current_pt, el.ctrl1, el.pt};
                expand(quad.bounds());
                current_pt = el.pt;
                break;
            }
            case PathVerb::CubicTo: {
                CubicBezier cubic{current_pt, el.ctrl1, el.ctrl2, el.pt};
                expand(cubic.bounds());
                current_pt = el.pt;
                break;
            }
            case PathVerb::ArcTo: {
                EllipticArc arc{current_pt, el.rx, el.ry, el.rotation_deg, el.large_arc, el.sweep, el.pt};
                expand(arc.bounds());
                current_pt = el.pt;
                break;
            }
            case PathVerb::Close:
                break;
        }
    }
    return first ? Rect2D{} : Rect2D::from_ltrb(min_x, min_y, max_x, max_y);
}

Path2D Path2D::transformed(const Transform2D& t) const {
    Path2D res;
    res.set_winding_rule(winding_rule_);
    for (const auto& el : elements_) {
        PathElement new_el = el;
        new_el.pt = t.map_point(el.pt);
        new_el.ctrl1 = t.map_point(el.ctrl1);
        new_el.ctrl2 = t.map_point(el.ctrl2);
        res.elements_.push_back(new_el);
    }
    return res;
}

Path2D Path2D::make_rect(double x, double y, double w, double h) {
    Path2D p;
    p.move_to(x, y);
    p.line_to(x + w, y);
    p.line_to(x + w, y + h);
    p.line_to(x, y + h);
    p.close();
    return p;
}

Path2D Path2D::make_ellipse(double cx, double cy, double rx, double ry) {
    // 4 cubic beziers approximating an ellipse with kappa = 0.5522847498307935
    constexpr double k = 0.5522847498307935;
    const double kx = rx * k;
    const double ky = ry * k;

    Path2D p;
    p.move_to(cx + rx, cy);
    p.cubic_to(cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry);
    p.cubic_to(cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy);
    p.cubic_to(cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry);
    p.cubic_to(cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy);
    p.close();
    return p;
}

} // namespace pikdraw::geometry
