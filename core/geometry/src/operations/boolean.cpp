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

    if (op == BooleanOp::BackMinusFront) {
        return boolean_rectangles(clip, subject, BooleanOp::Difference);
    }

    if (op == BooleanOp::Boundary) {
        return boolean_rectangles(subject, clip, BooleanOp::Union);
    }

    if (op == BooleanOp::Xor) {
        auto sub_diff = boolean_rectangles(subject, clip, BooleanOp::Difference);
        auto clip_diff = boolean_rectangles(clip, subject, BooleanOp::Difference);
        for (const auto& r : clip_diff.rectangles) {
            sub_diff.rectangles.push_back(r);
        }
        for (const auto& el : clip_diff.path.elements()) {
            if (el.verb == PathVerb::MoveTo) sub_diff.path.move_to(el.pt);
            else if (el.verb == PathVerb::LineTo) sub_diff.path.line_to(el.pt);
            else if (el.verb == PathVerb::Close) sub_diff.path.close();
        }
        return sub_diff;
    }

    if (op == BooleanOp::Simplify) {
        auto inter = boolean_rectangles(subject, clip, BooleanOp::Intersection);
        auto sub_diff = boolean_rectangles(subject, clip, BooleanOp::Difference);
        auto clip_diff = boolean_rectangles(clip, subject, BooleanOp::Difference);
        BooleanResult res;
        res.success = true;
        for (const auto& r : inter.rectangles) res.rectangles.push_back(r);
        for (const auto& r : sub_diff.rectangles) res.rectangles.push_back(r);
        for (const auto& r : clip_diff.rectangles) res.rectangles.push_back(r);
        for (const auto& r : res.rectangles) {
            res.sub_paths.push_back(Path2D::make_rect(r.x, r.y, r.width, r.height));
        }
        if (!res.rectangles.empty()) {
            res.path = res.sub_paths.front();
        }
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

std::pair<bool, std::vector<Path2D>> split_rect_by_line(
    const Rect2D& rect,
    const Point2D& p1,
    const Point2D& p2
) {
    if (rect.is_empty()) return {false, {}};
    double dx = p2.x - p1.x;
    double dy = p2.y - p1.y;
    double len_sq = dx * dx + dy * dy;
    if (len_sq < 1e-8) return {false, {}};

    // Line equation: A*x + B*y + C = 0
    double A = dy;
    double B = -dx;
    double C = -(A * p1.x + B * p1.y);

    Point2D corners[4] = {
        {rect.x, rect.y},
        {rect.right(), rect.y},
        {rect.right(), rect.bottom()},
        {rect.x, rect.bottom()}
    };

    struct InterInfo {
        int edge_index;
        Point2D pt;
    };
    std::vector<InterInfo> inters;

    for (int i = 0; i < 4; ++i) {
        Point2D e1 = corners[i];
        Point2D e2 = corners[(i + 1) % 4];
        double v1 = A * e1.x + B * e1.y + C;
        double v2 = A * e2.x + B * e2.y + C;

        if ((v1 < -1e-6 && v2 > 1e-6) || (v1 > 1e-6 && v2 < -1e-6)) {
            double t = -v1 / (v2 - v1);
            if (t >= 0.0 && t <= 1.0) {
                Point2D ip{e1.x + t * (e2.x - e1.x), e1.y + t * (e2.y - e1.y)};
                inters.push_back({i, ip});
            }
        }
    }

    if (inters.size() != 2) {
        return {false, {}};
    }

    Path2D path1;
    path1.move_to(inters[0].pt);
    int cur_corner = (inters[0].edge_index + 1) % 4;
    while (cur_corner != (inters[1].edge_index + 1) % 4) {
        path1.line_to(corners[cur_corner]);
        cur_corner = (cur_corner + 1) % 4;
    }
    path1.line_to(inters[1].pt);
    path1.close();

    Path2D path2;
    path2.move_to(inters[1].pt);
    cur_corner = (inters[1].edge_index + 1) % 4;
    while (cur_corner != (inters[0].edge_index + 1) % 4) {
        path2.line_to(corners[cur_corner]);
        cur_corner = (cur_corner + 1) % 4;
    }
    path2.line_to(inters[0].pt);
    path2.close();

    return {true, {path1, path2}};
}

} // namespace pikdraw::geometry
