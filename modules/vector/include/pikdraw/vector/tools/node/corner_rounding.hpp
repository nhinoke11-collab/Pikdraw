#pragma once

#include "pikdraw/document/model/editable_path.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <vector>

namespace pikdraw::vector::tools::node {

struct CornerWidget {
    document::SubpathId subpath_id{};
    document::NodeId node_id{};
    geometry::Point2D corner_anchor{};
    geometry::Point2D widget_position{};
    geometry::Point2D bisector{};
    double max_radius{0.0};
    double current_radius{0.0};
};

struct FilletPreviewSegment {
    geometry::Point2D p0;
    geometry::Point2D c1;
    geometry::Point2D c2;
    geometry::Point2D p3;
    bool is_cubic{true}; // true for Round (convex) & Scallop (concave); false for Chamfer (bevel line)
};

/// Detect sharp corners on straight segments eligible for Illustrator-style live corner rounding.
[[nodiscard]] inline std::vector<CornerWidget> detect_corner_widgets(
    const document::EditablePath& path, double zoom,
    const geometry::Transform2D& transform = geometry::Transform2D::identity()) {
    std::vector<CornerWidget> widgets;
    if (zoom < 1e-4) zoom = 1.0;

    for (const auto& sub : path.subpaths) {
        const size_t n = sub.nodes.size();
        if (n < 3) continue;

        for (size_t i = 0; i < n; ++i) {
            if (!sub.closed && (i == 0 || i == n - 1)) continue;

            const size_t prev_i = (i + n - 1) % n;
            const size_t next_i = (i + 1) % n;

            const auto& curr_node = sub.nodes[i];
            const auto& prev_node = sub.nodes[prev_i];
            const auto& next_node = sub.nodes[next_i];

            // 1. Check if curr_node is a sharp corner between two straight Line segments
            if (!curr_node.has_incoming_handle && !curr_node.has_outgoing_handle &&
                prev_i < sub.segments.size() && sub.segments[prev_i].kind == document::PathSegmentKind::Line &&
                i < sub.segments.size() && sub.segments[i].kind == document::PathSegmentKind::Line) {
                const auto v = transform.map_point(curr_node.anchor);
                const auto a = transform.map_point(prev_node.anchor);
                const auto b_pt = transform.map_point(next_node.anchor);

                const auto d1 = a - v;
                const auto d2 = b_pt - v;
                const double l1 = std::hypot(d1.x, d1.y);
                const double l2 = std::hypot(d2.x, d2.y);
                if (l1 < 0.5 || l2 < 0.5) continue;

                const auto u = geometry::Point2D{d1.x / l1, d1.y / l1};
                const auto w = geometry::Point2D{d2.x / l2, d2.y / l2};

                const double dot = std::clamp(u.x * w.x + u.y * w.y, -1.0, 1.0);
                const double angle = std::acos(dot);

                // Angle must be between 15° and 165°
                constexpr double kMinAngle = 15.0 * std::numbers::pi / 180.0;
                constexpr double kMaxAngle = 165.0 * std::numbers::pi / 180.0;
                if (angle < kMinAngle || angle > kMaxAngle) continue;

                auto bisector = geometry::Point2D{u.x + w.x, u.y + w.y};
                const double blen = std::hypot(bisector.x, bisector.y);
                if (blen < 1e-4) continue;
                bisector = geometry::Point2D{bisector.x / blen, bisector.y / blen};

                const double half_angle = angle * 0.5;
                const double tan_half = std::tan(half_angle);
                if (tan_half < 1e-4) continue;

                const double max_edge = std::min(l1, l2) * 0.49;
                const double max_r = max_edge * tan_half;

                // Screen distance: ~16 pixels into the corner along the bisector
                const double preferred_dist = 16.0 / zoom;
                const double widget_dist = std::clamp(preferred_dist, 2.0 / zoom, std::min(l1, l2) * 0.38);
                const auto widget_pos = v + bisector * widget_dist;

                widgets.push_back(CornerWidget{
                    sub.id,
                    curr_node.id,
                    v,
                    widget_pos,
                    bisector,
                    max_r,
                    0.0
                });
            } else if (!curr_node.has_incoming_handle && !next_node.has_outgoing_handle &&
                       prev_i < sub.segments.size() && sub.segments[prev_i].kind == document::PathSegmentKind::Line &&
                       next_i < sub.segments.size() && sub.segments[next_i].kind == document::PathSegmentKind::Line &&
                       n >= 4) {
                // 2. Check if segment i between curr_node and next_node is an existing filleted corner
                const size_t next2_i = (next_i + 1) % n;
                const auto& node_a = prev_node;
                const auto& node_b = sub.nodes[next2_i];
                const auto a_w = transform.map_point(node_a.anchor);
                const auto t1_w = transform.map_point(curr_node.anchor);
                const auto t2_w = transform.map_point(next_node.anchor);
                const auto b_w = transform.map_point(node_b.anchor);

                const auto d1 = t1_w - a_w;
                const auto d2 = b_w - t2_w;
                const double l1 = std::hypot(d1.x, d1.y);
                const double l2 = std::hypot(d2.x, d2.y);
                if (l1 > 0.5 && l2 > 0.5) {
                    const auto u1 = geometry::Point2D{d1.x / l1, d1.y / l1};
                    const auto u2 = geometry::Point2D{d2.x / l2, d2.y / l2};
                    const double det = u1.x * u2.y - u1.y * u2.x;
                    if (std::abs(det) > 1e-3) {
                        const auto delta = t2_w - t1_w;
                        const double s = (delta.x * u2.y - delta.y * u2.x) / det;
                        const double t = (u1.x * delta.y - u1.y * delta.x) / det;
                        if (s > 0.1 && t > 0.1 && s / t >= 0.2 && s / t <= 5.0) {
                            const auto v = t1_w + u1 * s;
                            const auto dir_a = geometry::Point2D{-u1.x, -u1.y};
                            const auto dir_b = u2;
                            const double dot = std::clamp(dir_a.x * dir_b.x + dir_a.y * dir_b.y, -1.0, 1.0);
                            const double angle = std::acos(dot);
                            constexpr double kMinAngle = 15.0 * std::numbers::pi / 180.0;
                            constexpr double kMaxAngle = 165.0 * std::numbers::pi / 180.0;
                            if (angle >= kMinAngle && angle <= kMaxAngle) {
                                auto bisector = geometry::Point2D{dir_a.x + dir_b.x, dir_a.y + dir_b.y};
                                const double blen = std::hypot(bisector.x, bisector.y);
                                if (blen > 1e-4) {
                                    bisector = geometry::Point2D{bisector.x / blen, bisector.y / blen};
                                    const double half_angle = angle * 0.5;
                                    const double tan_half = std::tan(half_angle);
                                    if (tan_half > 1e-4) {
                                        const double cur_r = s * tan_half;
                                        const double max_edge = std::min(l1 + s, l2 + t) * 0.49;
                                        const double max_r = max_edge * tan_half;
                                        const double preferred_dist = 14.0 / zoom;
                                        const double widget_dist = std::clamp(preferred_dist + cur_r * 0.75, 2.0 / zoom, max_edge * 0.95);
                                        const auto widget_pos = v + bisector * widget_dist;
                                        widgets.push_back(CornerWidget{
                                            sub.id,
                                            curr_node.id,
                                            v,
                                            widget_pos,
                                            bisector,
                                            max_r,
                                            cur_r
                                        });
                                        ++i; // skip next_node since it forms this fillet arc
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return widgets;
}

/// Restores the sharp unfilleted corner vertex V in the baseline path when dragging an existing filleted corner.
inline void restore_sharp_corner_in_baseline(
    document::EditablePath& path,
    const CornerWidget& widget,
    const geometry::Transform2D& inv_transform) {
    for (auto& sub : path.subpaths) {
        if (sub.id != widget.subpath_id) continue;
        const size_t n = sub.nodes.size();
        for (size_t i = 0; i < n; ++i) {
            if (sub.nodes[i].id == widget.node_id) {
                const size_t next_i = (i + 1) % n;
                sub.nodes[i].anchor = inv_transform.map_point(widget.corner_anchor);
                sub.nodes[i].has_incoming_handle = false;
                sub.nodes[i].has_outgoing_handle = false;
                sub.nodes[i].mode = document::PathNodeMode::Corner;
                if (n > 3) {
                    if (i < sub.segments.size()) {
                        sub.segments.erase(sub.segments.begin() + i);
                    }
                    sub.nodes.erase(sub.nodes.begin() + next_i);
                }
                return;
            }
        }
    }
}

/// Constructs a new EditablePath where specified corner nodes are rounded with fillet curves (Round, Scallop, Chamfer).
/// When transform is supplied, fillet radii and tangent extents are computed in world millimeters
/// and pre-compensated into local space, guaranteeing true, isotropic, even circular corners
/// regardless of anisotropic scaling or rotation.
[[nodiscard]] inline document::EditablePath create_rounded_corners_path(
    const document::EditablePath& source,
    const std::vector<document::NodeId>& target_corners,
    double radius,
    const geometry::Transform2D& transform = geometry::Transform2D::identity(),
    vector::RectCornerType corner_type = vector::RectCornerType::Round) {
    if (radius <= 0.05) return source;

    const auto inv_tr = transform.inverted().value_or(geometry::Transform2D::identity());
    const bool use_tr = transform.is_invertible();

    document::EditablePath result;
    result.winding_rule = source.winding_rule;

    for (const auto& sub : source.subpaths) {
        document::EditableSubpath new_sub;
        new_sub.id = sub.id;
        new_sub.closed = sub.closed;

        const size_t n = sub.nodes.size();
        if (n < 3) {
            result.subpaths.push_back(sub);
            continue;
        }

        for (size_t i = 0; i < n; ++i) {
            const size_t prev_i = (i + n - 1) % n;
            const size_t next_i = (i + 1) % n;

            const auto& curr = sub.nodes[i];
            const auto& prev = sub.nodes[prev_i];
            const auto& next = sub.nodes[next_i];

            const bool is_target = target_corners.empty() ||
                (std::find(target_corners.begin(), target_corners.end(), curr.id) != target_corners.end());

            const bool can_round = is_target &&
                (!curr.has_incoming_handle && !curr.has_outgoing_handle) &&
                (prev_i < sub.segments.size() && sub.segments[prev_i].kind == document::PathSegmentKind::Line) &&
                (i < sub.segments.size() && sub.segments[i].kind == document::PathSegmentKind::Line) &&
                (sub.closed || (i > 0 && i < n - 1));

            if (!can_round) {
                new_sub.nodes.push_back(curr);
                if (i < sub.segments.size()) {
                    new_sub.segments.push_back(sub.segments[i]);
                }
                continue;
            }

            const auto v = use_tr ? transform.map_point(curr.anchor) : curr.anchor;
            const auto a = use_tr ? transform.map_point(prev.anchor) : prev.anchor;
            const auto b = use_tr ? transform.map_point(next.anchor) : next.anchor;

            const auto d1 = a - v;
            const auto d2 = b - v;
            const double l1 = std::hypot(d1.x, d1.y);
            const double l2 = std::hypot(d2.x, d2.y);

            if (l1 < 0.5 || l2 < 0.5) {
                new_sub.nodes.push_back(curr);
                if (i < sub.segments.size()) new_sub.segments.push_back(sub.segments[i]);
                continue;
            }

            const auto u = geometry::Point2D{d1.x / l1, d1.y / l1};
            const auto w = geometry::Point2D{d2.x / l2, d2.y / l2};

            const double dot = std::clamp(u.x * w.x + u.y * w.y, -1.0, 1.0);
            const double angle = std::acos(dot);
            const double half_angle = angle * 0.5;
            const double tan_half = std::tan(half_angle);

            if (tan_half < 1e-4) {
                new_sub.nodes.push_back(curr);
                if (i < sub.segments.size()) new_sub.segments.push_back(sub.segments[i]);
                continue;
            }

            const double max_t = std::min(l1, l2) * 0.49;
            const double t = std::min(radius / tan_half, max_t);
            const double actual_r = t * tan_half;

            const double sweep = std::numbers::pi - angle;
            const double k = (4.0 / 3.0) * std::tan(sweep * 0.25) * actual_r;

            const auto t1_w = v + u * t;
            const auto t2_w = v + w * t;

            const auto t1 = use_tr ? inv_tr.map_point(t1_w) : t1_w;
            const auto t2 = use_tr ? inv_tr.map_point(t2_w) : t2_w;

            if (corner_type == vector::RectCornerType::Chamfer) {
                // Bevel diagonal straight segment
                document::PathNode node1;
                node1.id = document::generate_path_element_id<document::NodeId>();
                node1.anchor = t1;
                node1.mode = document::PathNodeMode::Cusp;
                node1.has_incoming_handle = false;
                node1.has_outgoing_handle = false;
                new_sub.nodes.push_back(node1);

                document::EditableSegment fillet_seg;
                fillet_seg.id = document::generate_path_element_id<document::SegmentId>();
                fillet_seg.kind = document::PathSegmentKind::Line;
                new_sub.segments.push_back(fillet_seg);

                document::PathNode node2;
                node2.id = document::generate_path_element_id<document::NodeId>();
                node2.anchor = t2;
                node2.mode = document::PathNodeMode::Cusp;
                node2.has_incoming_handle = false;
                node2.has_outgoing_handle = false;
                new_sub.nodes.push_back(node2);
            } else {
                // Round (convex) or Scallop (concave)
                const auto c1_w = (corner_type == vector::RectCornerType::Scallop)
                    ? (t1_w + w * k)
                    : (t1_w - u * k);
                const auto c2_w = (corner_type == vector::RectCornerType::Scallop)
                    ? (t2_w + u * k)
                    : (t2_w - w * k);

                const auto c1 = use_tr ? inv_tr.map_point(c1_w) : c1_w;
                const auto c2 = use_tr ? inv_tr.map_point(c2_w) : c2_w;

                // First tangent node at T1 with outgoing handle C1
                document::PathNode node1;
                node1.id = document::generate_path_element_id<document::NodeId>();
                node1.anchor = t1;
                node1.mode = document::PathNodeMode::Cusp;
                node1.has_incoming_handle = false;
                node1.has_outgoing_handle = true;
                node1.outgoing_handle = c1;
                new_sub.nodes.push_back(node1);

                // Cubic Bézier segment connecting T1 to T2
                document::EditableSegment fillet_seg;
                fillet_seg.id = document::generate_path_element_id<document::SegmentId>();
                fillet_seg.kind = document::PathSegmentKind::CubicBezier;
                new_sub.segments.push_back(fillet_seg);

                // Second tangent node at T2 with incoming handle C2
                document::PathNode node2;
                node2.id = document::generate_path_element_id<document::NodeId>();
                node2.anchor = t2;
                node2.mode = document::PathNodeMode::Cusp;
                node2.has_incoming_handle = true;
                node2.incoming_handle = c2;
                node2.has_outgoing_handle = false;
                new_sub.nodes.push_back(node2);
            }

            // Line segment continuing from T2 to the next node
            document::EditableSegment line_seg;
            line_seg.id = document::generate_path_element_id<document::SegmentId>();
            line_seg.kind = document::PathSegmentKind::Line;
            new_sub.segments.push_back(line_seg);
        }

        result.subpaths.push_back(std::move(new_sub));
    }

    return result;
}

/// Generates world-coordinate preview segments (Round, Scallop, or Chamfer) exclusively for the corners being rounded.
[[nodiscard]] inline std::vector<FilletPreviewSegment> generate_fillet_preview_segments(
    const document::EditablePath& source,
    const std::vector<document::NodeId>& target_corners,
    double radius,
    const geometry::Transform2D& transform = geometry::Transform2D::identity(),
    vector::RectCornerType corner_type = vector::RectCornerType::Round) {
    std::vector<FilletPreviewSegment> previews;
    if (radius <= 0.05) return previews;

    for (const auto& sub : source.subpaths) {
        const size_t n = sub.nodes.size();
        if (n < 3) continue;

        for (size_t i = 0; i < n; ++i) {
            const size_t prev_i = (i + n - 1) % n;
            const size_t next_i = (i + 1) % n;

            const auto& curr = sub.nodes[i];
            const auto& prev = sub.nodes[prev_i];
            const auto& next = sub.nodes[next_i];

            const bool is_target = target_corners.empty() ||
                (std::find(target_corners.begin(), target_corners.end(), curr.id) != target_corners.end());

            const bool can_round = is_target &&
                (!curr.has_incoming_handle && !curr.has_outgoing_handle) &&
                (prev_i < sub.segments.size() && sub.segments[prev_i].kind == document::PathSegmentKind::Line) &&
                (i < sub.segments.size() && sub.segments[i].kind == document::PathSegmentKind::Line) &&
                (sub.closed || (i > 0 && i < n - 1));

            if (!can_round) continue;

            const auto v = transform.map_point(curr.anchor);
            const auto a = transform.map_point(prev.anchor);
            const auto b = transform.map_point(next.anchor);

            const auto d1 = a - v;
            const auto d2 = b - v;
            const double l1 = std::hypot(d1.x, d1.y);
            const double l2 = std::hypot(d2.x, d2.y);
            if (l1 < 0.5 || l2 < 0.5) continue;

            const auto u = geometry::Point2D{d1.x / l1, d1.y / l1};
            const auto w = geometry::Point2D{d2.x / l2, d2.y / l2};

            const double dot = std::clamp(u.x * w.x + u.y * w.y, -1.0, 1.0);
            const double angle = std::acos(dot);
            const double half_angle = angle * 0.5;
            const double tan_half = std::tan(half_angle);
            if (tan_half < 1e-4) continue;

            const double max_t = std::min(l1, l2) * 0.49;
            const double t = std::min(radius / tan_half, max_t);
            const double actual_r = t * tan_half;

            const double sweep = std::numbers::pi - angle;
            const double k = (4.0 / 3.0) * std::tan(sweep * 0.25) * actual_r;

            const auto t1_w = v + u * t;
            const auto t2_w = v + w * t;

            if (corner_type == vector::RectCornerType::Chamfer) {
                previews.push_back(FilletPreviewSegment{
                    t1_w, t1_w, t2_w, t2_w, false
                });
            } else {
                const auto c1_w = (corner_type == vector::RectCornerType::Scallop)
                    ? (t1_w + w * k)
                    : (t1_w - u * k);
                const auto c2_w = (corner_type == vector::RectCornerType::Scallop)
                    ? (t2_w + u * k)
                    : (t2_w - w * k);
                previews.push_back(FilletPreviewSegment{
                    t1_w, c1_w, c2_w, t2_w, true
                });
            }
        }
    }
    return previews;
}

/// Detect the 4 persistent parametric corner widgets for a Rectangle (CorelDRAW / Illustrator standard).
/// Even after corners are rounded, scalloped, or chamfered, these 4 widgets remain at the corners
/// and allow continuous, real-time inward/outward dragging directly on canvas.
[[nodiscard]] inline std::vector<CornerWidget> detect_rectangle_corner_widgets(
    const geometry::Rect2D& rect,
    const RectCornerSettings& settings,
    double zoom,
    const geometry::Transform2D& transform = geometry::Transform2D::identity(),
    const std::vector<document::NodeId>& fallback_node_ids = {}) {
    if (!rect.is_finite() || rect.width <= 0.0 || rect.height <= 0.0) return {};
    if (zoom < 1e-4) zoom = 1.0;

    const auto local_tl = geometry::Point2D{rect.x, rect.y};
    const auto local_tr = geometry::Point2D{rect.x + rect.width, rect.y};
    const auto local_br = geometry::Point2D{rect.x + rect.width, rect.y + rect.height};
    const auto local_bl = geometry::Point2D{rect.x, rect.y + rect.height};
    const auto local_center = geometry::Point2D{rect.x + rect.width * 0.5, rect.y + rect.height * 0.5};

    const auto v_tl = transform.map_point(local_tl);
    const auto v_tr = transform.map_point(local_tr);
    const auto v_br = transform.map_point(local_br);
    const auto v_bl = transform.map_point(local_bl);
    const auto v_center = transform.map_point(local_center);

    const double sx = std::hypot(transform.a, transform.b);
    const double sy = std::hypot(transform.c, transform.d);
    const double world_scale = (sx > 1e-6 && sy > 1e-6) ? (sx + sy) * 0.5 : 1.0;

    const double max_r = std::min(rect.width, rect.height) * 0.499 * world_scale;
    const std::array<geometry::Point2D, 4> vertices{v_tl, v_tr, v_br, v_bl};
    const std::array<double, 4> radii{
        settings.tl * world_scale,
        settings.tr * world_scale,
        settings.br * world_scale,
        settings.bl * world_scale
    };

    std::vector<CornerWidget> widgets;
    widgets.reserve(4);

    for (size_t i = 0; i < 4; ++i) {
        const auto& v = vertices[i];
        const auto dir = v_center - v;
        const double len = std::hypot(dir.x, dir.y);
        if (len < 1e-4) continue;
        const auto bisector = geometry::Point2D{dir.x / len, dir.y / len};

        const double preferred_offset = 14.0 / zoom;
        const double min_offset = std::clamp(preferred_offset, 2.0 / zoom, max_r * 0.35);
        const double r = std::clamp(radii[i], 0.0, max_r);
        const double dist = std::clamp(min_offset + r * 0.75, min_offset, max_r * 0.95);
        const auto widget_pos = v + bisector * dist;

        const auto nid = (i < fallback_node_ids.size())
            ? fallback_node_ids[i]
            : document::generate_path_element_id<document::NodeId>();

        widgets.push_back(CornerWidget{
            document::SubpathId{},
            nid,
            v,
            widget_pos,
            bisector,
            max_r / world_scale,
            r / world_scale
        });
    }

    return widgets;
}

/// Generates world-space fillet preview segments for a parametric rectangle during live corner dragging.
[[nodiscard]] inline std::vector<FilletPreviewSegment> generate_rectangle_fillet_preview_segments(
    const geometry::Rect2D& rect,
    const RectCornerSettings& settings,
    double radius,
    const geometry::Transform2D& transform,
    RectCornerType corner_type,
    bool round_all = true,
    const std::optional<geometry::Point2D>& active_corner_vertex = std::nullopt) {
    (void)settings;
    if (!rect.is_finite() || rect.width <= 0.0 || rect.height <= 0.0 || radius <= 0.05) return {};

    const double r = std::min(radius, std::min(rect.width, rect.height) * 0.499);

    struct CornerGeom {
        geometry::Point2D v;
        geometry::Point2D u;
        geometry::Point2D w;
    };
    const std::array<CornerGeom, 4> corners{
        CornerGeom{ {rect.x, rect.y}, {1.0, 0.0}, {0.0, 1.0} },                              // TL
        CornerGeom{ {rect.x + rect.width, rect.y}, {-1.0, 0.0}, {0.0, 1.0} },               // TR
        CornerGeom{ {rect.x + rect.width, rect.y + rect.height}, {-1.0, 0.0}, {0.0, -1.0} },// BR
        CornerGeom{ {rect.x, rect.y + rect.height}, {1.0, 0.0}, {0.0, -1.0} }               // BL
    };

    constexpr double kappa = 0.5522847498307935;
    std::vector<FilletPreviewSegment> previews;

    for (const auto& c : corners) {
        const auto v_w = transform.map_point(c.v);
        if (active_corner_vertex && !round_all) {
            if (v_w.distance_to(*active_corner_vertex) > 1.0) continue;
        }

        const auto u_vec = transform.map_point(c.v + c.u) - transform.map_point(c.v);
        const auto w_vec = transform.map_point(c.v + c.w) - transform.map_point(c.v);
        const double u_len = std::hypot(u_vec.x, u_vec.y);
        const double w_len = std::hypot(w_vec.x, w_vec.y);
        if (u_len < 1e-4 || w_len < 1e-4) continue;
        const auto u_norm = geometry::Point2D{u_vec.x / u_len, u_vec.y / u_len};
        const auto w_norm = geometry::Point2D{w_vec.x / w_len, w_vec.y / w_len};

        const double actual_r_u = r * u_len;
        const double actual_r_w = r * w_len;

        const auto t1_w = v_w + u_norm * actual_r_u;
        const auto t2_w = v_w + w_norm * actual_r_w;

        if (corner_type == RectCornerType::Chamfer) {
            previews.push_back(FilletPreviewSegment{
                t1_w, t1_w, t2_w, t2_w, false
            });
        } else if (corner_type == RectCornerType::Scallop) {
            // Concave circular arc bowing inward into the shape
            const auto c1_w = t1_w + w_norm * (actual_r_w * kappa);
            const auto c2_w = t2_w + u_norm * (actual_r_u * kappa);
            previews.push_back(FilletPreviewSegment{
                t1_w, c1_w, c2_w, t2_w, true
            });
        } else {
            // Round: Convex circular arc bowing outward towards corner vertex
            const auto c1_w = t1_w + (v_w - t1_w) * kappa;
            const auto c2_w = t2_w + (v_w - t2_w) * kappa;
            previews.push_back(FilletPreviewSegment{
                t1_w, c1_w, c2_w, t2_w, true
            });
        }
    }

    return previews;
}

} // namespace pikdraw::vector::tools::node
