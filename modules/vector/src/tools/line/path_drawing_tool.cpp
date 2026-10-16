#include "pikdraw/vector/tools/line/path_drawing_tool.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <tuple>

namespace pikdraw::vector::tools::line {
namespace {

using precision::snap::DirectionSnapResult;
using precision::snap::SnapCandidate;
using precision::snap::SnapQueryStamp;

bool valid_point(geometry::Point2D point) noexcept {
    return point.is_finite() &&
           geometry::validate_point(point) == geometry::GeometryStatus::Ok;
}

bool matches_document(const SnapQueryStamp& stamp,
                      const interaction::ToolSessionToken& token,
                      std::string_view tool_context) noexcept {
    return stamp.document_id == token.document_id &&
           stamp.document_epoch == token.document_epoch &&
           stamp.document_revision == token.base_revision &&
           stamp.tool_context == tool_context && stamp.query_generation != 0;
}

auto stable_key(const SnapCandidate& candidate) noexcept {
    const auto object = candidate.key.source_object_id.value_or(0);
    const auto element = candidate.key.source_node_or_segment_id.value_or(0);
    const auto cell_x = candidate.key.grid_cell ? candidate.key.grid_cell->x : 0;
    const auto cell_y = candidate.key.grid_cell ? candidate.key.grid_cell->y : 0;
    return std::tuple{object, element, static_cast<uint8_t>(candidate.key.kind),
                      candidate.key.grid_cell.has_value(), cell_x, cell_y,
                      candidate.parameter_t};
}

bool candidate_less(const SnapCandidate& first,
                    const SnapCandidate& second) noexcept {
    if (first.per_tool_priority != second.per_tool_priority)
        return first.per_tool_priority < second.per_tool_priority;
    if (first.configured_kind_priority != second.configured_kind_priority)
        return first.configured_kind_priority < second.configured_kind_priority;
    if (first.distance_dip != second.distance_dip)
        return first.distance_dip < second.distance_dip;
    return stable_key(first) < stable_key(second);
}

bool candidate_is_valid(const SnapCandidate& candidate,
                        const SnapQueryStamp& expected) noexcept {
    return candidate.stamp == expected &&
           candidate.source_revision == expected.document_revision &&
           precision::snap::is_supported_snap_candidate(candidate.key.kind) &&
           valid_point(candidate.world_point) &&
           std::isfinite(candidate.distance_dip) && candidate.distance_dip >= 0.0 &&
           std::isfinite(candidate.parameter_t);
}

bool is_stale_status(precision::snap::SnapQueryStatus status) noexcept {
    using precision::snap::SnapQueryStatus;
    return status == SnapQueryStatus::StaleSource ||
           status == SnapQueryStatus::StaleCamera ||
           status == SnapQueryStatus::StaleConfig ||
           status == SnapQueryStatus::StaleCapabilities ||
           status == SnapQueryStatus::StaleToolContext ||
           status == SnapQueryStatus::StaleQuery;
}

bool lies_on_locked_ray(geometry::Point2D point,
                        geometry::Point2D anchor,
                        double angle_degrees,
                        const precision::snap::SnapCameraTransform& camera) noexcept {
    if (!valid_point(point) || !valid_point(anchor) ||
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

std::array<uint8_t, sizeof(double) * 2> preview_bytes(
    geometry::Point2D point) noexcept {
    std::array<uint8_t, sizeof(double) * 2> bytes{};
    std::memcpy(bytes.data(), &point.x, sizeof(point.x));
    std::memcpy(bytes.data() + sizeof(point.x), &point.y, sizeof(point.y));
    return bytes;
}

} // namespace

PathDrawingResult PathDrawingTool::begin(
    PathDrawingKind kind, document::SnapshotPtr base, size_t target_layer,
    interaction::ToolSessionLimits limits) {
    if (active())
        return {PathDrawingResultCode::InvalidInput,
                "Finish or cancel the active path before starting another", {}};
    if (kind > PathDrawingKind::Polyline || !base ||
        base->document_id().empty() || base->epoch() == 0 ||
        target_layer >= base->page().layers.size())
        return {PathDrawingResultCode::InvalidInput,
                "Path drawing requires a valid document and target layer", {}};
    if (base->page().layers[target_layer].locked)
        return {PathDrawingResultCode::InvalidInput,
                "The target layer is locked", {}};
    kind_ = kind;
    target_layer_ = target_layer;
    limits_ = limits;
    base_snapshot_ = std::move(base);
    clear_draft();
    const auto name = kind_ == PathDrawingKind::Line ? "line" : "polyline";
    const auto result = session_.begin(name, base_snapshot_, limits_);
    if (!result.applied()) {
        base_snapshot_.reset();
        return {PathDrawingResultCode::InvalidInput, result.message, {}};
    }
    return {PathDrawingResultCode::Applied, {}, {}};
}

PathDrawingResult PathDrawingTool::set_candidate(geometry::Point2D point,
                                                bool numeric) {
    if (!active() || session_.state() == interaction::ToolSessionState::PanSuspended)
        return {PathDrawingResultCode::Ignored,
                "A path candidate requires an active, unsuspended tool", {}};
    if (!valid_point(point))
        return {PathDrawingResultCode::InvalidInput,
                "Path candidate is non-finite or outside the document range", {}};
    const auto bytes = preview_bytes(point);
    const auto result = session_.set_preview_payload(
        std::vector<uint8_t>(bytes.begin(), bytes.end()));
    if (!result.applied())
        return {PathDrawingResultCode::InvalidInput, result.message, {}};
    candidate_ = point;
    candidate_is_numeric_ = numeric;
    return {PathDrawingResultCode::PreviewUpdated, {}, {}};
}

PathDrawingResult PathDrawingTool::update_pointer(geometry::Point2D raw_world) {
    return set_candidate(raw_world, false);
}

PathDrawingResult PathDrawingTool::update_from_snap_query(
    geometry::Point2D raw_world,
    const precision::snap::SnapQueryResult& query,
    const precision::snap::SnapQueryStamp& expected_stamp,
    const precision::snap::SnapCameraTransform& camera,
    const std::optional<DirectionSnapResult>& direction,
    bool hard_direction_lock) {
    if (!active() || !valid_point(raw_world))
        return {PathDrawingResultCode::InvalidInput,
                "Snap proposal needs an active tool and a valid pointer point", {}};
    const auto token = session_.token();
    const std::string_view context = kind_ == PathDrawingKind::Line ? "line" : "polyline";
    if (!matches_document(expected_stamp, token, context) ||
        query.stamp != expected_stamp)
        return {PathDrawingResultCode::Stale,
                "Snap query does not match the active document or tool generation", {}};
    if (query.status != precision::snap::SnapQueryStatus::Success)
        return {is_stale_status(query.status) ? PathDrawingResultCode::Stale
                                            : PathDrawingResultCode::InvalidInput,
                "Snap query did not produce a current candidate set", {}};

    const bool has_anchor = !points_.empty();
    if (has_anchor && direction && direction->stamp != expected_stamp)
        return {PathDrawingResultCode::Stale,
                "Direction constraint does not match the current snap query", {}};
    if (has_anchor && direction &&
        direction->status != precision::snap::SnapQueryStatus::Success)
        return {is_stale_status(direction->status) ? PathDrawingResultCode::Stale
                                                  : PathDrawingResultCode::InvalidInput,
                "Direction constraint could not be resolved", {}};
    if (has_anchor && direction && !valid_point(direction->world_point))
        return {PathDrawingResultCode::InvalidInput,
                "Direction constraint produced an invalid world point", {}};
    const bool apply_hard_lock = has_anchor && (hard_direction_lock ||
        (direction && direction->mode == precision::snap::DirectionMode::Ortho));
    if (apply_hard_lock && (!direction || !direction->constrained))
        return {PathDrawingResultCode::InvalidInput,
                "The active hard direction constraint has no valid ray", {}};

    const geometry::Point2D anchor = points_.empty() ? raw_world : points_.back();
    const SnapCandidate* retained = nullptr;
    const SnapCandidate* best = nullptr;
    for (const auto& candidate : query.candidates) {
        if (!candidate_is_valid(candidate, expected_stamp)) continue;
        if (apply_hard_lock && !lies_on_locked_ray(candidate.world_point, anchor,
                direction->world_angle_degrees, camera))
            continue;
        if (query.chosen && candidate.key == query.chosen->key &&
            candidate.stamp == query.chosen->stamp)
            retained = &candidate;
        if (!best || candidate_less(candidate, *best)) best = &candidate;
    }
    if (retained) return set_candidate(retained->world_point, false);
    if (best) return set_candidate(best->world_point, false);
    if (has_anchor && direction && direction->constrained)
        return set_candidate(direction->world_point, false);
    return set_candidate(raw_world, false);
}

PathDrawingResult PathDrawingTool::update_numeric_candidate(
    std::string_view expression, precision::numeric::ParseOptions options,
    std::optional<precision::numeric::ConstructionFrame> frame) {
    if (!active())
        return {PathDrawingResultCode::Ignored, "No path drawing is active", {}};
    const auto parsed = precision::numeric::parse_point_input(expression, options);
    if (!parsed) {
        PathDrawingResult result{PathDrawingResultCode::InvalidInput,
                                 "Numeric point expression is invalid", {}};
        result.numeric_diagnostic = parsed.diagnostic;
        return result;
    }
    if (points_.empty() &&
        parsed.value->kind != precision::numeric::PointInputKind::AbsoluteWorld)
        return {PathDrawingResultCode::InvalidInput,
                "The first numeric point must use absolute #x;y coordinates", {}};
    const auto anchor = points_.empty() ? geometry::Point2D{} : points_.back();
    const auto evaluated = precision::numeric::evaluate_point_input(
        *parsed.value, anchor, frame);
    if (!evaluated) {
        PathDrawingResult result{PathDrawingResultCode::InvalidInput,
                                 "Numeric point could not be evaluated at the current anchor", {}};
        result.numeric_diagnostic = evaluated.diagnostic;
        return result;
    }
    return set_candidate(*evaluated.value, true);
}

PathDrawingResult PathDrawingTool::confirm_candidate(document::Document& document) {
    if (!active() || !candidate_)
        return {PathDrawingResultCode::Ignored,
                "There is no active point candidate to confirm", {}};
    if (session_.state() == interaction::ToolSessionState::PanSuspended ||
        session_.state() == interaction::ToolSessionState::NumericInput)
        return {PathDrawingResultCode::Ignored,
                "The current tool state cannot confirm a point", {}};
    const auto freshness = session_.observe_snapshot(document.current_snapshot());
    if (freshness.code == interaction::ToolSessionResultCode::Stale) {
        clear_draft();
        base_snapshot_.reset();
        return {PathDrawingResultCode::Stale,
                "Document changed during the path draft; the stale candidate was discarded", {}};
    }
    if (!valid_point(*candidate_))
        return {PathDrawingResultCode::InvalidInput,
                "Path candidate is outside the supported document range", {}};
    if (!points_.empty() && points_.back() == *candidate_)
        return {PathDrawingResultCode::InvalidInput,
                "A zero-length segment was rejected", {}};
    if (points_.size() >= limits_.max_draft_payload_bytes / sizeof(geometry::Point2D))
        return {PathDrawingResultCode::BudgetExceeded,
                "The path draft reached its configured point budget", {}};

    if (kind_ == PathDrawingKind::Line && points_.size() == 1) {
        try {
            points_.push_back(*candidate_);
        } catch (...) {
            return {PathDrawingResultCode::InvalidInput,
                    "Could not allocate the Line endpoint", {}};
        }
        const auto committed = finish_path(document, false, true);
        if (!committed.committed() && committed.code != PathDrawingResultCode::Stale) {
            points_.pop_back();
            return committed;
        }
        return committed;
    }

    if (points_.empty()) {
        const auto edit = session_.apply_edit(interaction::DraftEdit{0, {}, {0}, false});
        if (!edit.applied())
            return {PathDrawingResultCode::InvalidInput, edit.message, {}};
    } else if (kind_ == PathDrawingKind::Polyline && points_.size() == 1) {
        const auto edit = session_.apply_edit(interaction::DraftEdit{0, {0}, {1}, true});
        if (!edit.applied())
            return {PathDrawingResultCode::InvalidInput, edit.message, {}};
    }
    try {
        points_.push_back(*candidate_);
    } catch (...) {
        if (points_.size() == 1 && kind_ == PathDrawingKind::Polyline)
            (void)session_.undo_local();
        return {PathDrawingResultCode::InvalidInput,
                "Could not allocate a path point", {}};
    }
    candidate_.reset();
    candidate_is_numeric_ = false;
    session_.clear_preview();
    return {PathDrawingResultCode::Applied, {}, {}};
}

} // namespace pikdraw::vector::tools::line
