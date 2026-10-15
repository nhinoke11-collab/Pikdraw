#include "pikdraw/geometry/operations/boolean.hpp"
#include <vector>
#include <algorithm>

namespace pikdraw::geometry {

BooleanResult boolean_rectangles(const Rect2D& subject, const Rect2D& clip, BooleanOp op) {
    BooleanResult res;
    res.success = true;

    if (subject.is_empty() && clip.is_empty()) {
        return res;
    }

    if (op == BooleanOp::Intersection) {
        if (subject.intersects(clip)) {
            const Rect2D r = subject.intersected(clip);
            res.rectangles.push_back(r);
            res.path = Path2D::make_rect(r.x, r.y, r.width, r.height);
        }
        return res;
    }

    if (op == BooleanOp::Union) {
        if (!subject.intersects(clip)) {
            res.rectangles.push_back(subject);
            res.rectangles.push_back(clip);
        } else {
            // Check if one contains the other
            if (subject.contains(clip.min_point()) && subject.contains(clip.max_point())) {
                res.rectangles.push_back(subject);
            } else if (clip.contains(subject.min_point()) && clip.contains(subject.max_point())) {
                res.rectangles.push_back(clip);
            } else {
                // Decompose union into non-overlapping rectilinear tiles
                const Rect2D inter = subject.intersected(clip);
                res.rectangles.push_back(inter);

                // Add remaining non-overlapping parts
                const auto sub_diff = boolean_rectangles(subject, clip, BooleanOp::Difference);
                for (const auto& r : sub_diff.rectangles) res.rectangles.push_back(r);

                const auto clip_diff = boolean_rectangles(clip, subject, BooleanOp::Difference);
                for (const auto& r : clip_diff.rectangles) res.rectangles.push_back(r);
            }
        }

        // Build composite path
        Path2D p;
        for (const auto& r : res.rectangles) {
            Path2D rp = Path2D::make_rect(r.x, r.y, r.width, r.height);
            for (const auto& el : rp.elements()) {
                if (el.verb == PathVerb::MoveTo) p.move_to(el.pt);
                else if (el.verb == PathVerb::LineTo) p.line_to(el.pt);
                else if (el.verb == PathVerb::Close) p.close();
            }
        }
        res.path = p;
        return res;
    }

    if (op == BooleanOp::Difference) {
        if (!subject.intersects(clip)) {
            res.rectangles.push_back(subject);
            res.path = Path2D::make_rect(subject.x, subject.y, subject.width, subject.height);
            return res;
        }

        const Rect2D inter = subject.intersected(clip);
        // Slicing subject around inter
        // Top slice
        if (subject.y < inter.y) {
            res.rectangles.push_back(Rect2D{subject.x, subject.y, subject.width, inter.y - subject.y});
        }
        // Bottom slice
        if (subject.bottom() > inter.bottom()) {
            res.rectangles.push_back(Rect2D{subject.x, inter.bottom(), subject.width, subject.bottom() - inter.bottom()});
        }
        // Left slice
        if (subject.x < inter.x) {
            res.rectangles.push_back(Rect2D{subject.x, inter.y, inter.x - subject.x, inter.height});
        }
        // Right slice
        if (subject.right() > inter.right()) {
            res.rectangles.push_back(Rect2D{inter.right(), inter.y, subject.right() - inter.right(), inter.height});
        }

        Path2D p;
        for (const auto& r : res.rectangles) {
            Path2D rp = Path2D::make_rect(r.x, r.y, r.width, r.height);
            for (const auto& el : rp.elements()) {
                if (el.verb == PathVerb::MoveTo) p.move_to(el.pt);
                else if (el.verb == PathVerb::LineTo) p.line_to(el.pt);
                else if (el.verb == PathVerb::Close) p.close();
            }
        }
        res.path = p;
        return res;
    }

    res.success = false;
    res.error_message = "Unsupported Boolean operation on rectangles";
    return res;
}

BooleanResult boolean_paths(const Path2D& subject, const Path2D& clip, BooleanOp op) {
    // For M1 baseline, rectangle-backed boolean serves as the core kernel
    const Rect2D s_bounds = subject.bounds();
    const Rect2D c_bounds = clip.bounds();
    return boolean_rectangles(s_bounds, c_bounds, op);
}

} // namespace pikdraw::geometry
