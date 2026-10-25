#include "pikdraw/geometry/operations/boolean.hpp"
#include <vector>
#include <algorithm>
#include <map>
#include "rectangle_subset.hpp"

namespace pikdraw::geometry {

namespace {

struct GridEdge {
    size_t x0;
    size_t y0;
    size_t x1;
    size_t y1;
    bool used{false};
};

int edge_direction(const GridEdge& edge) noexcept {
    if (edge.x1 > edge.x0) return 0; // east
    if (edge.y1 > edge.y0) return 1; // south
    if (edge.x1 < edge.x0) return 2; // west
    return 3;                        // north
}

double contour_signed_area(const Path2D& path) noexcept {
    double twice_area = 0.0;
    Point2D start{};
    Point2D previous{};
    bool active = false;
    for (const auto& element : path.elements()) {
        if (element.verb == PathVerb::MoveTo) {
            start = previous = element.pt;
            active = true;
        } else if (element.verb == PathVerb::LineTo && active) {
            twice_area += previous.x * element.pt.y - element.pt.x * previous.y;
            previous = element.pt;
        } else if (element.verb == PathVerb::Close && active) {
            twice_area += previous.x * start.y - start.x * previous.y;
            active = false;
        }
    }
    return twice_area * 0.5;
}

void append_path(Path2D& destination, const Path2D& source) {
    for (const auto& element : source.elements()) {
        if (element.verb == PathVerb::MoveTo) destination.move_to(element.pt);
        else if (element.verb == PathVerb::LineTo) destination.line_to(element.pt);
        else if (element.verb == PathVerb::Close) destination.close();
    }
}

BooleanResult boolean_rectangle_regions(const Rect2D& subject, const Rect2D& clip, BooleanOp op) {
    BooleanResult result;
    result.success = true;
    result.status = BooleanStatus::Success;

    std::vector<double> xs{subject.x, subject.right(), clip.x, clip.right()};
    std::vector<double> ys{subject.y, subject.bottom(), clip.y, clip.bottom()};
    std::sort(xs.begin(), xs.end());
    std::sort(ys.begin(), ys.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());

    const size_t columns = xs.size() - 1;
    const size_t rows = ys.size() - 1;
    std::vector<bool> filled(columns * rows, false);
    auto inside = [](const Rect2D& rect, double x, double y) {
        return x >= rect.left() && x < rect.right() && y >= rect.top() && y < rect.bottom();
    };
    auto index = [columns](size_t x, size_t y) { return y * columns + x; };

    for (size_t y = 0; y < rows; ++y) {
        for (size_t x = 0; x < columns; ++x) {
            const double mx = (xs[x] + xs[x + 1]) * 0.5;
            const double my = (ys[y] + ys[y + 1]) * 0.5;
            const bool in_subject = inside(subject, mx, my);
            const bool in_clip = inside(clip, mx, my);
            bool include = false;
            switch (op) {
            case BooleanOp::Union: include = in_subject || in_clip; break;
            case BooleanOp::Intersection: include = in_subject && in_clip; break;
            case BooleanOp::Difference: include = in_subject && !in_clip; break;
            case BooleanOp::Xor: include = in_subject != in_clip; break;
            case BooleanOp::BackMinusFront: include = in_clip && !in_subject; break;
            default:
                result.success = false;
                result.status = BooleanStatus::Unsupported;
                result.error_message = "Unsupported Boolean operation on rectangles";
                return result;
            }
            filled[index(x, y)] = include;
            if (include)
                result.rectangles.push_back({xs[x], ys[y], xs[x + 1] - xs[x], ys[y + 1] - ys[y]});
        }
    }

    std::vector<GridEdge> edges;
    std::map<std::pair<size_t, size_t>, std::vector<size_t>> outgoing;
    auto add_edge = [&](size_t x0, size_t y0, size_t x1, size_t y1) {
        const size_t edge_index = edges.size();
        edges.push_back({x0, y0, x1, y1, false});
        outgoing[{x0, y0}].push_back(edge_index);
    };
    auto is_filled = [&](size_t x, size_t y) {
        return x < columns && y < rows && filled[index(x, y)];
    };

    for (size_t y = 0; y < rows; ++y) {
        for (size_t x = 0; x < columns; ++x) {
            if (!is_filled(x, y)) continue;
            if (y == 0 || !is_filled(x, y - 1)) add_edge(x, y, x + 1, y);
            if (x + 1 == columns || !is_filled(x + 1, y)) add_edge(x + 1, y, x + 1, y + 1);
            if (y + 1 == rows || !is_filled(x, y + 1)) add_edge(x + 1, y + 1, x, y + 1);
            if (x == 0 || !is_filled(x - 1, y)) add_edge(x, y + 1, x, y);
        }
    }

    struct BoundaryLoop {
        Path2D path;
        double signed_area{0.0};
        Rect2D bounds;
    };
    std::vector<BoundaryLoop> loops;
    for (size_t seed = 0; seed < edges.size(); ++seed) {
        if (edges[seed].used) continue;
        const size_t start_x = edges[seed].x0;
        const size_t start_y = edges[seed].y0;
        Path2D loop;
        loop.set_winding_rule(WindingRule::NonZero);
        loop.move_to({xs[start_x], ys[start_y]});
        size_t current = seed;
        bool closed = false;

        for (size_t steps = 0; steps <= edges.size(); ++steps) {
            auto& edge = edges[current];
            if (edge.used) break;
            edge.used = true;
            loop.line_to({xs[edge.x1], ys[edge.y1]});
            if (edge.x1 == start_x && edge.y1 == start_y) {
                loop.close();
                closed = true;
                break;
            }

            const auto next_it = outgoing.find({edge.x1, edge.y1});
            if (next_it == outgoing.end()) break;
            const int incoming = edge_direction(edge);
            size_t next = edges.size();
            constexpr int turn_preference[]{1, 0, 3, 2}; // right, straight, left, reverse
            for (const int turn : turn_preference) {
                const int desired = (incoming + turn) % 4;
                for (const size_t candidate : next_it->second) {
                    if (!edges[candidate].used && edge_direction(edges[candidate]) == desired) {
                        next = candidate;
                        break;
                    }
                }
                if (next != edges.size()) break;
            }
            if (next == edges.size()) break;
            current = next;
        }
        if (!closed) {
            result.success = false;
            result.status = BooleanStatus::InvalidInput;
            result.rectangles.clear();
            result.path = {};
            result.components.clear();
            result.error_message = "Could not trace Boolean rectangle boundary";
            return result;
        }
        loops.push_back({loop, contour_signed_area(loop), loop.bounds()});
    }

    Path2D boundary;
    boundary.set_winding_rule(WindingRule::NonZero);
    for (const auto& loop : loops) append_path(boundary, loop.path);
    for (size_t index = 0; index < loops.size(); ++index) {
        if (loops[index].signed_area <= 0.0) continue;
        Path2D component = loops[index].path;
        for (size_t hole_index = 0; hole_index < loops.size(); ++hole_index) {
            if (loops[hole_index].signed_area >= 0.0) continue;
            const auto& hole_bounds = loops[hole_index].bounds;
            const Point2D center{hole_bounds.x + hole_bounds.width * 0.5,
                                 hole_bounds.y + hole_bounds.height * 0.5};
            if (center.x > loops[index].bounds.left() &&
                center.x < loops[index].bounds.right() &&
                center.y > loops[index].bounds.top() &&
                center.y < loops[index].bounds.bottom()) {
                append_path(component, loops[hole_index].path);
            }
        }
        result.components.push_back(std::move(component));
    }
    // A valid rectangle Boolean always has at least one positive outer loop,
    // but retain a conservative fallback if a future edge convention changes.
    if (result.components.empty()) {
        for (const auto& loop : loops) result.components.push_back(loop.path);
    }
    result.path = std::move(boundary);
    if (result.path.empty()) result.status = BooleanStatus::Empty;
    return result;
}

} // namespace

BooleanResult boolean_rectangles(const Rect2D& subject, const Rect2D& clip, BooleanOp op) {
    if (!subject.is_finite() || !clip.is_finite() || subject.width < 0 || subject.height < 0 ||
        clip.width < 0 || clip.height < 0) {
        BooleanResult res;
        res.error_message = "Invalid rectangle bounds";
        return res;
    }
    if (op == BooleanOp::Boundary) {
        BooleanResult res;
        res.status = BooleanStatus::Unsupported;
        res.error_message = "Boundary extraction is unsupported";
        return res;
    }
    if (op == BooleanOp::Simplify) {
        auto inter = boolean_rectangles(subject, clip, BooleanOp::Intersection);
        auto sub_diff = boolean_rectangles(subject, clip, BooleanOp::Difference);
        auto clip_diff = boolean_rectangles(clip, subject, BooleanOp::Difference);
        BooleanResult res;
        if (!inter.success || !sub_diff.success || !clip_diff.success) {
            res.status = BooleanStatus::InvalidInput;
            res.error_message = "Could not simplify rectangle paths";
            return res;
        }
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
        res.status = res.path.empty() ? BooleanStatus::Empty : BooleanStatus::Success;
        return res;
    }

    switch (op) {
    case BooleanOp::Union:
    case BooleanOp::Intersection:
    case BooleanOp::Difference:
    case BooleanOp::Xor:
    case BooleanOp::BackMinusFront:
        break;
    case BooleanOp::Boundary:
    case BooleanOp::Simplify:
        break;
    default: {
        BooleanResult res;
        res.status = BooleanStatus::Unsupported;
        res.error_message = "Unsupported Boolean operation on rectangles";
        return res;
    }
    }

    if (subject.is_empty() || clip.is_empty()) {
        BooleanResult res;
        res.success = true;
        res.status = BooleanStatus::Empty;
        const Rect2D* remaining = nullptr;
        switch (op) {
        case BooleanOp::Union:
        case BooleanOp::Xor:
            remaining = subject.is_empty() ? &clip : &subject;
            break;
        case BooleanOp::Difference:
            if (!subject.is_empty()) remaining = &subject;
            break;
        case BooleanOp::BackMinusFront:
            if (!clip.is_empty()) remaining = &clip;
            break;
        default:
            break;
        }
        if (remaining) {
            res.rectangles.push_back(*remaining);
            res.path = Path2D::make_rect(remaining->x, remaining->y, remaining->width, remaining->height);
            res.status = BooleanStatus::Success;
        }
        return res;
    }

    return boolean_rectangle_regions(subject, clip, op);
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
