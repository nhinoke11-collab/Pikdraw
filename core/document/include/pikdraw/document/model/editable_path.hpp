#pragma once

#include "pikdraw/document/model/object_id.hpp"
#include "pikdraw/geometry/curves/path.hpp"
#include "pikdraw/geometry/primitives/validation.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pikdraw::document {

template <class Tag>
class PathElementId {
public:
    constexpr PathElementId() noexcept = default;
    explicit constexpr PathElementId(uint64_t value) noexcept : value_(value) {}
    [[nodiscard]] constexpr uint64_t value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool valid() const noexcept { return value_ != 0; }
    friend constexpr bool operator==(PathElementId, PathElementId) noexcept = default;
    friend constexpr auto operator<=>(PathElementId, PathElementId) noexcept = default;

private:
    uint64_t value_{0};
};

struct SubpathIdTag;
struct NodeIdTag;
struct SegmentIdTag;
using SubpathId = PathElementId<SubpathIdTag>;
using NodeId = PathElementId<NodeIdTag>;
using SegmentId = PathElementId<SegmentIdTag>;

template <class Id>
[[nodiscard]] inline Id generate_path_element_id() noexcept {
    return Id{generate_object_id()};
}

template <class Id>
inline void reserve_path_element_id(Id id) noexcept {
    reserve_object_id(id.value());
}

enum class PathNodeMode { Corner, Cusp, Smooth, Symmetric, AutoSmooth };
enum class PathSegmentKind { Line, QuadraticBezier, CubicBezier, EllipticArc };

struct PathNode {
    NodeId id{};
    geometry::Point2D anchor{};
    PathNodeMode mode{PathNodeMode::Corner};
    bool has_incoming_handle{false};
    geometry::Point2D incoming_handle{};
    bool has_outgoing_handle{false};
    geometry::Point2D outgoing_handle{};
    double auto_smooth_tension{1.0};
};

struct EditableSegment {
    SegmentId id{};
    PathSegmentKind kind{PathSegmentKind::Line};
    geometry::Point2D quadratic_control{};
    geometry::EllipticArc arc{};
};

struct EditableSubpath {
    SubpathId id{};
    std::vector<PathNode> nodes;
    // Segment i runs from node i to node i+1; the last closed segment wraps to node 0.
    std::vector<EditableSegment> segments;
    bool closed{false};
};

class EditablePath {
public:
    geometry::WindingRule winding_rule{geometry::WindingRule::NonZero};
    std::vector<EditableSubpath> subpaths;

    [[nodiscard]] std::optional<std::string> validation_error() const {
        if (subpaths.empty()) return "Editable path must contain at least one subpath";
        std::unordered_set<uint64_t> ids;
        for (const auto& subpath : subpaths) {
            if (!subpath.id.valid() || !ids.insert(subpath.id.value()).second)
                return "Subpath IDs must be nonzero and unique";
            if (subpath.nodes.empty()) return "Subpath must contain an anchor node";
            if (subpath.closed && subpath.nodes.size() < 2)
                return "Closed subpath requires at least two distinct nodes";
            const size_t expected_segments = subpath.closed
                ? subpath.nodes.size() : subpath.nodes.size() - 1;
            if (subpath.segments.size() != expected_segments)
                return "Subpath segment count does not match its open/closed topology";
            for (const auto& node : subpath.nodes) {
                if (!node.id.valid() || !ids.insert(node.id.value()).second)
                    return "Node IDs must be nonzero and unique";
                if (geometry::validate_point(node.anchor) != geometry::GeometryStatus::Ok ||
                    !std::isfinite(node.auto_smooth_tension) ||
                    node.auto_smooth_tension < 0.0 || node.auto_smooth_tension > 1.0)
                    return "Node coordinates or auto-smooth tension are invalid";
                if ((node.has_incoming_handle && geometry::validate_point(node.incoming_handle) !=
                     geometry::GeometryStatus::Ok) ||
                    (node.has_outgoing_handle && geometry::validate_point(node.outgoing_handle) !=
                     geometry::GeometryStatus::Ok))
                    return "Bezier handle coordinates are invalid or outside the document range";
                if (node.mode == PathNodeMode::Corner &&
                    (node.has_incoming_handle || node.has_outgoing_handle))
                    return "Corner nodes cannot carry handle relations";
                if (node.mode == PathNodeMode::AutoSmooth &&
                    (node.has_incoming_handle || node.has_outgoing_handle))
                    return "Auto-smooth nodes derive handles from neighboring anchors";
                if ((node.mode == PathNodeMode::Smooth || node.mode == PathNodeMode::Symmetric) &&
                    (!node.has_incoming_handle || !node.has_outgoing_handle))
                    return "Smooth nodes require both incoming and outgoing handles";
                if ((node.mode == PathNodeMode::Smooth || node.mode == PathNodeMode::Symmetric) &&
                    node.has_incoming_handle && node.has_outgoing_handle) {
                    const auto incoming = node.incoming_handle - node.anchor;
                    const auto outgoing = node.outgoing_handle - node.anchor;
                    const double cross = incoming.x * outgoing.y - incoming.y * outgoing.x;
                    const double incoming_length = incoming.distance_to({0.0, 0.0});
                    const double outgoing_length = outgoing.distance_to({0.0, 0.0});
                    const double angular_scale = std::max(1.0, incoming_length * outgoing_length);
                    if (std::abs(cross) > 1e-10 * angular_scale ||
                        incoming.x * outgoing.x + incoming.y * outgoing.y >= 0.0)
                        return "Smooth handles must be opposing and collinear";
                    if (node.mode == PathNodeMode::Symmetric &&
                        std::abs(incoming_length - outgoing_length) >
                            1e-10 * std::max({1.0, incoming_length, outgoing_length}))
                        return "Symmetric handles must have equal lengths";
                }
            }
            for (size_t i = 0; i < subpath.segments.size(); ++i) {
                const auto& segment = subpath.segments[i];
                if (!segment.id.valid() || !ids.insert(segment.id.value()).second)
                    return "Segment IDs must be nonzero and unique";
                if (segment.kind == PathSegmentKind::QuadraticBezier &&
                    geometry::validate_point(segment.quadratic_control) != geometry::GeometryStatus::Ok)
                    return "Quadratic control point is invalid or outside the document range";
                if (segment.kind == PathSegmentKind::EllipticArc) {
                    if (!segment.arc.is_finite() ||
                        geometry::validate_rect(segment.arc.bounds()) != geometry::GeometryStatus::Ok ||
                        !geometry::encode_endpoint_arc(segment.arc))
                        return "Elliptic arc source must be finite, bounded and non-degenerate";
                    const auto start = subpath.nodes[i].anchor;
                    const auto end = subpath.nodes[(i + 1) % subpath.nodes.size()].anchor;
                    const auto endpoint_matches = [](geometry::Point2D a, geometry::Point2D b) {
                        const double scale = std::max({1.0, std::abs(a.x), std::abs(a.y),
                                                       std::abs(b.x), std::abs(b.y)});
                        const double tolerance = 1e-10 +
                            128.0 * std::numeric_limits<double>::epsilon() * scale;
                        return a.distance_to(b) <= tolerance;
                    };
                    if (!endpoint_matches(segment.arc.point_at(0.0), start) ||
                        !endpoint_matches(segment.arc.point_at(1.0), end))
                        return "Elliptic arc endpoints must match their source node anchors";
                }
            }
        }
        return std::nullopt;
    }

};

[[nodiscard]] std::pair<geometry::Point2D, geometry::Point2D> editable_path_handles_for(
    const EditableSubpath& subpath, size_t node_index);
[[nodiscard]] geometry::Path2D editable_path_to_path2d(const EditablePath& source);
[[nodiscard]] std::optional<EditablePath> editable_path_from_path2d(
    const geometry::Path2D& source, std::string* error = nullptr);
[[nodiscard]] bool editable_path_matches_adapter(
    const EditablePath& source, const geometry::Path2D& adapter);

} // namespace pikdraw::document
