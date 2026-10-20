#pragma once

#include "pikdraw/document/model/editable_path.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"
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

            // Must not have active Bézier handles
            if (curr_node.has_incoming_handle || curr_node.has_outgoing_handle)
                continue;

            // Segments connecting to this corner must be straight lines
            if (prev_i < sub.segments.size() && sub.segments[prev_i].kind != document::PathSegmentKind::Line)
                continue;
            if (i < sub.segments.size() && sub.segments[i].kind != document::PathSegmentKind::Line)
                continue;

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
        }
    }
    return widgets;
}

/// Constructs a new EditablePath where specified corner nodes are rounded with fillet curves.
/// When transform is supplied, fillet radii and tangent extents are computed in world millimeters
/// and pre-compensated into local space, guaranteeing true, isotropic, even circular corners
/// regardless of anisotropic scaling or rotation.
[[nodiscard]] inline document::EditablePath create_rounded_corners_path(
    const document::EditablePath& source,
    const std::vector<document::NodeId>& target_corners,
    double radius,
    const geometry::Transform2D& transform = geometry::Transform2D::identity()) {
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
            const auto c1_w = t1_w - u * k;
            const auto c2_w = t2_w - w * k;

            const auto t1 = use_tr ? inv_tr.map_point(t1_w) : t1_w;
            const auto t2 = use_tr ? inv_tr.map_point(t2_w) : t2_w;
            const auto c1 = use_tr ? inv_tr.map_point(c1_w) : c1_w;
            const auto c2 = use_tr ? inv_tr.map_point(c2_w) : c2_w;

            // First tangent node at T1 with outgoing handle C1 towards V
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

            // Second tangent node at T2 with incoming handle C2 from V
            document::PathNode node2;
            node2.id = document::generate_path_element_id<document::NodeId>();
            node2.anchor = t2;
            node2.mode = document::PathNodeMode::Cusp;
            node2.has_incoming_handle = true;
            node2.incoming_handle = c2;
            node2.has_outgoing_handle = false;
            new_sub.nodes.push_back(node2);

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

} // namespace pikdraw::vector::tools::node
