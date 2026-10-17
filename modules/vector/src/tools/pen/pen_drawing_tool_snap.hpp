#pragma once

#include "pikdraw/vector/tools/pen/pen_drawing_tool.hpp"
#include "pikdraw/geometry/primitives/validation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace pikdraw::vector::tools::pen::detail {

inline bool valid_pen_snap_point(geometry::Point2D point) noexcept {
    return point.is_finite() &&
        geometry::validate_point(point) == geometry::GeometryStatus::Ok;
}

inline bool matches_pen_snap_document(
    const precision::snap::SnapQueryStamp& stamp,
    const interaction::ToolSessionToken& token) noexcept {
    return stamp.document_id == token.document_id &&
        stamp.document_epoch == token.document_epoch &&
        stamp.document_revision == token.base_revision &&
        stamp.tool_context == "pen" && stamp.query_generation != 0;
}

inline bool stale_pen_snap_status(
    precision::snap::SnapQueryStatus status) noexcept {
    using precision::snap::SnapQueryStatus;
    return status == SnapQueryStatus::StaleSource ||
        status == SnapQueryStatus::StaleCamera ||
        status == SnapQueryStatus::StaleConfig ||
        status == SnapQueryStatus::StaleCapabilities ||
        status == SnapQueryStatus::StaleToolContext ||
        status == SnapQueryStatus::StaleQuery;
}

inline bool valid_pen_snap_candidate(
    const precision::snap::SnapCandidate& candidate,
    const precision::snap::SnapQueryStamp& expected) noexcept {
    return candidate.stamp == expected &&
        candidate.source_revision == expected.document_revision &&
        precision::snap::is_supported_snap_candidate(candidate.key.kind) &&
        valid_pen_snap_point(candidate.world_point) &&
        std::isfinite(candidate.distance_dip) && candidate.distance_dip >= 0.0 &&
        std::isfinite(candidate.parameter_t);
}

inline bool lies_on_pen_locked_ray(
    geometry::Point2D point, geometry::Point2D anchor,
    double angle_degrees,
    const precision::snap::SnapCameraTransform& camera) noexcept {
    if (!valid_pen_snap_point(point) || !valid_pen_snap_point(anchor) ||
        !std::isfinite(angle_degrees) || !camera.valid())
        return false;

    constexpr double pi = 3.141592653589793238462643383279502884;
    const double radians = angle_degrees * pi / 180.0;
    const geometry::Point2D world_ray{std::cos(radians), std::sin(radians)};
    const geometry::Point2D delta_world = point - anchor;
    const double world_scale = std::max(std::abs(delta_world.x),
                                        std::abs(delta_world.y));
    if (!std::isfinite(world_scale)) return false;
    if (world_scale > 0.0) {
        const double forward = std::fma(delta_world.x / world_scale, world_ray.x,
                                        (delta_world.y / world_scale) * world_ray.y);
        if (!(forward >= 0.0)) return false;
    }

    const auto anchor_dip = camera.world_to_dip(anchor);
    const auto point_dip = camera.world_to_dip(point);
    const auto ray_dip = camera.world_vector_to_dip(world_ray);
    const geometry::Point2D delta_dip = point_dip - anchor_dip;
    const double ray_length = std::hypot(ray_dip.x, ray_dip.y);
    const double dip_scale = std::max(std::abs(delta_dip.x), std::abs(delta_dip.y));
    if (!delta_dip.is_finite() || !std::isfinite(ray_length) || ray_length == 0.0 ||
        !std::isfinite(dip_scale))
        return false;
    if (dip_scale == 0.0) return true;

    const geometry::Point2D unit_ray{ray_dip.x / ray_length,
                                     ray_dip.y / ray_length};
    const double scaled_cross = std::fma(delta_dip.x / dip_scale, unit_ray.y,
        -(delta_dip.y / dip_scale) * unit_ray.x);
    const double distance_dip = std::abs(scaled_cross) * dip_scale;
    const double tolerance_dip = 64.0 * std::numeric_limits<double>::epsilon() *
                                 std::max(1.0, dip_scale);
    return std::isfinite(distance_dip) && distance_dip <= tolerance_dip;
}

inline auto stable_pen_snap_key(
    const precision::snap::SnapCandidate& candidate) noexcept {
    const auto object = candidate.key.source_object_id.value_or(0);
    const auto element = candidate.key.source_node_or_segment_id.value_or(0);
    const auto cell_x = candidate.key.grid_cell ? candidate.key.grid_cell->x : 0;
    const auto cell_y = candidate.key.grid_cell ? candidate.key.grid_cell->y : 0;
    return std::tuple{object, element, static_cast<uint8_t>(candidate.key.kind),
                      candidate.key.grid_cell.has_value(), cell_x, cell_y,
                      candidate.parameter_t};
}

inline bool pen_snap_candidate_less(
    const precision::snap::SnapCandidate& first,
    const precision::snap::SnapCandidate& second) noexcept {
    if (first.per_tool_priority != second.per_tool_priority)
        return first.per_tool_priority < second.per_tool_priority;
    if (first.configured_kind_priority != second.configured_kind_priority)
        return first.configured_kind_priority < second.configured_kind_priority;
    if (first.distance_dip != second.distance_dip)
        return first.distance_dip < second.distance_dip;
    return stable_pen_snap_key(first) < stable_pen_snap_key(second);
}

} // namespace pikdraw::vector::tools::pen::detail

namespace pikdraw::vector::tools::pen {

PenDrawingResult PenDrawingTool::update_from_snap_query(
    geometry::Point2D raw_world,
    const precision::snap::SnapQueryResult& query,
    const precision::snap::SnapQueryStamp& expected_stamp,
    const precision::snap::SnapCameraTransform& camera,
    const std::optional<precision::snap::DirectionSnapResult>& direction,
    bool hard_direction_lock) {
    if (!active() || !detail::valid_pen_snap_point(raw_world))
        return {PenDrawingResultCode::InvalidInput,
                "Snap proposal needs an active Pen tool and a valid pointer point", {}, {}};
    if (session_.state() == interaction::ToolSessionState::PanSuspended)
        return {PenDrawingResultCode::Ignored,
                "Pen snap preview is suspended while panning", {}, {}};

    const auto token = session_.token();
    if (!detail::matches_pen_snap_document(expected_stamp, token) ||
        query.stamp != expected_stamp)
        return {PenDrawingResultCode::Stale,
                "Snap query does not match the active Pen document or generation", {}, {}};
    if (expected_stamp.camera_generation != camera.generation)
        return {PenDrawingResultCode::Stale,
                "Snap query was produced for a different camera generation", {}, {}};
    if (!camera.valid())
        return {PenDrawingResultCode::InvalidInput,
                "Snap query requires a valid camera transform", {}, {}};
    if (query.status != precision::snap::SnapQueryStatus::Success)
        return {detail::stale_pen_snap_status(query.status)
                    ? PenDrawingResultCode::Stale
                    : PenDrawingResultCode::InvalidInput,
                "Snap query did not produce a current Pen candidate set", {}, {}};

    const bool has_anchor = !subpath_.nodes.empty();
    if (has_anchor && direction && direction->stamp != expected_stamp)
        return {PenDrawingResultCode::Stale,
                "Direction constraint does not match the current snap query", {}, {}};
    if (has_anchor && direction &&
        direction->status != precision::snap::SnapQueryStatus::Success)
        return {detail::stale_pen_snap_status(direction->status)
                    ? PenDrawingResultCode::Stale
                    : PenDrawingResultCode::InvalidInput,
                "Direction constraint could not be resolved", {}, {}};
    if (has_anchor && direction &&
        (!detail::valid_pen_snap_point(direction->world_point) ||
         !std::isfinite(direction->world_angle_degrees)))
        return {PenDrawingResultCode::InvalidInput,
                "Direction constraint produced an invalid world result", {}, {}};

    const bool apply_hard_lock = has_anchor && (hard_direction_lock ||
        (direction && direction->mode == precision::snap::DirectionMode::Ortho));
    if (apply_hard_lock && (!direction || !direction->constrained))
        return {PenDrawingResultCode::InvalidInput,
                "The active hard direction constraint has no valid ray", {}, {}};
    const geometry::Point2D anchor = has_anchor
        ? world_from_source_.map_point(subpath_.nodes.back().anchor)
        : raw_world;
    if (has_anchor && !detail::valid_pen_snap_point(anchor))
        return {PenDrawingResultCode::InvalidInput,
                "The active Pen anchor cannot be transformed to world coordinates", {}, {}};

    // Numeric drivers own the candidate; snap acquisition only guides the raw pointer.
    if (numeric_constraints_) {
        numeric_pointer_world_ = raw_world;
        auto result = evaluate_numeric_constraints();
        if (result.code == PenDrawingResultCode::PreviewUpdated && apply_hard_lock &&
            candidate_ && !detail::lies_on_pen_locked_ray(
                world_from_source_.map_point(candidate_->anchor), anchor,
                direction->world_angle_degrees, camera)) {
            numeric_conflicted_ = true;
            result.code = PenDrawingResultCode::InvalidInput;
            result.message = "Pen numeric constraints conflict with the locked direction";
        }
        return result;
    }

    const precision::snap::SnapCandidate* retained = nullptr;
    const precision::snap::SnapCandidate* best = nullptr;
    for (const auto& candidate : query.candidates) {
        if (!detail::valid_pen_snap_candidate(candidate, expected_stamp)) continue;
        if (apply_hard_lock && !detail::lies_on_pen_locked_ray(
                candidate.world_point, anchor, direction->world_angle_degrees, camera))
            continue;
        if (query.chosen && candidate.key == query.chosen->key &&
            candidate.stamp == query.chosen->stamp)
            retained = &candidate;
        if (!best || detail::pen_snap_candidate_less(candidate, *best))
            best = &candidate;
    }
    if (retained) return update_pointer(retained->world_point);
    if (best) return update_pointer(best->world_point);
    if (has_anchor && direction && direction->constrained) {
        if (apply_hard_lock && !detail::lies_on_pen_locked_ray(
                direction->world_point, anchor, direction->world_angle_degrees, camera))
            return {PenDrawingResultCode::InvalidInput,
                    "Direction fallback point is outside the locked ray", {}, {}};
        return update_pointer(direction->world_point);
    }
    return update_pointer(raw_world);
}

} // namespace pikdraw::vector::tools::pen
