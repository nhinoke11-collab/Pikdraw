#include "pen_tool_controller.hpp"

#include <cmath>
#include <utility>

namespace pikdraw::desktop::tools {
namespace {

using vector::tools::pen::PenDrawingResult;
using vector::tools::pen::PenDrawingResultCode;

PenDrawingResult make_pen_result(PenDrawingResultCode code,
                                 std::string message = {}) {
    return {code, std::move(message), std::nullopt, std::nullopt};
}

PenDrawingResult no_document_result() {
    return make_pen_result(PenDrawingResultCode::InvalidInput,
                           "Pen needs an open document with an editable layer");
}

} // namespace

std::optional<size_t> PenToolController::first_editable_layer(
    const document::SnapshotPtr& snapshot) const noexcept {
    if (!snapshot) return std::nullopt;
    const auto& layers = snapshot->page().layers;
    for (size_t index = 0; index < layers.size(); ++index) {
        if (layers[index].visible && !layers[index].locked) return index;
    }
    return std::nullopt;
}

PenDrawingResult PenToolController::activate() {
    if (active())
        return make_pen_result(PenDrawingResultCode::Ignored,
                               "The Pen session is already active");
    const auto snapshot = document_.current_snapshot();
    const auto layer = first_editable_layer(snapshot);
    if (!layer) return no_document_result();
    clear_pointer();
    return tool_.begin(snapshot, *layer);
}

PenDrawingResult PenToolController::resume(document::ObjectId object_id,
                                           document::SubpathId subpath_id,
                                           document::NodeId endpoint_id) {
    if (active())
        return make_pen_result(PenDrawingResultCode::Ignored,
                               "Finish or cancel the current Pen session before resuming a path");
    clear_pointer();
    return tool_.begin_resume(document_.current_snapshot(), object_id,
                              subpath_id, endpoint_id);
}

PenDrawingResult PenToolController::pointer_down(geometry::Point2D world) {
    if (!active())
        return make_pen_result(PenDrawingResultCode::Ignored,
                               "No Pen session is active");
    if (!world.is_finite())
        return make_pen_result(PenDrawingResultCode::InvalidInput,
                               "Pen pointer is not finite");
    const auto freshness = tool_.observe_snapshot(document_.current_snapshot());
    if (freshness.code == PenDrawingResultCode::Stale) {
        clear_pointer();
        return freshness;
    }
    pointer_down_ = world;
    pointer_current_ = world;
    pointer_modifiers_ = {};
    pointer_dragged_ = false;
    pointer_numeric_constraints_.reset();
    pointer_numeric_conflicted_ = false;
    PenPointerPreview preview;
    preview.anchor = world;
    pointer_preview_ = preview;
    return make_pen_result(PenDrawingResultCode::PreviewUpdated);
}

PenDrawingResult PenToolController::pointer_move(geometry::Point2D world,
                                                 bool dragged,
                                                 PenGestureModifiers modifiers) {
    if (!active())
        return make_pen_result(PenDrawingResultCode::Ignored,
                               "No Pen session is active");
    if (!world.is_finite())
        return make_pen_result(PenDrawingResultCode::InvalidInput,
                               "Pen pointer is not finite");
    const auto freshness = tool_.observe_snapshot(document_.current_snapshot());
    if (freshness.code == PenDrawingResultCode::Stale) {
        clear_pointer();
        return freshness;
    }
    pointer_current_ = world;
    pointer_modifiers_ = modifiers;
    if (pointer_down_ && dragged) pointer_dragged_ = true;
    if (pointer_down_) {
        if (pointer_dragged_ && pointer_numeric_constraints_)
            return recompute_numeric_handle_preview();
        PenPointerPreview preview;
        preview.anchor = *pointer_down_;
        preview.dragging = pointer_dragged_;
        preview.direction_locked = pointer_dragged_ &&
            direction_lock_active(pointer_modifiers_);
        if (preview.dragging) {
            const auto adjusted = handle_position(
                *pointer_down_, world, pointer_modifiers_);
            const auto handle = adjusted - *pointer_down_;
            preview.outgoing_handle = adjusted;
            if (pointer_modifiers_.alt) {
                preview.node_mode = document::PathNodeMode::Cusp;
            } else {
                preview.node_mode = document::PathNodeMode::Symmetric;
                preview.incoming_handle = *pointer_down_ - handle;
            }
        }
        pointer_preview_ = preview;
    } else {
        pointer_preview_.reset();
        const auto result = tool_.update_pointer(world);
        if (result.code == PenDrawingResultCode::InvalidInput) return result;
    }
    return make_pen_result(PenDrawingResultCode::PreviewUpdated);
}

PenDrawingResult PenToolController::pointer_up() {
    if (!active() || !pointer_down_)
        return make_pen_result(PenDrawingResultCode::Ignored,
                               "No Pen pointer gesture is active");
    if (pointer_numeric_conflicted_)
        return make_pen_result(PenDrawingResultCode::InvalidInput,
                               "Resolve the Pen handle numeric conflict before confirming");
    const geometry::Point2D anchor = tool_.source_point_from_world(*pointer_down_);
    vector::tools::pen::PenAnchorInput input{
        anchor, document::PathNodeMode::Corner, std::nullopt, std::nullopt};
    if (pointer_dragged_ && pointer_current_) {
        const std::optional<geometry::Point2D> adjusted =
            pointer_numeric_constraints_
                ? (pointer_preview_ ? pointer_preview_->outgoing_handle
                                    : std::optional<geometry::Point2D>{})
                : std::optional<geometry::Point2D>{handle_position(
                      *pointer_down_, *pointer_current_, pointer_modifiers_)};
        if (adjusted) {
            const auto current = tool_.source_point_from_world(*adjusted);
            const auto delta = current - anchor;
            const double length = std::hypot(delta.x, delta.y);
            if (std::isfinite(length) && length > 0.0) {
                input.mode = pointer_modifiers_.alt
                    ? document::PathNodeMode::Cusp
                    : document::PathNodeMode::Symmetric;
                input.outgoing_handle = current;
                if (!pointer_modifiers_.alt)
                    input.incoming_handle = anchor - delta;
            }
        }
    }
    clear_pointer();
    return tool_.confirm_anchor(std::move(input));
}

PenDrawingResult PenToolController::update_from_snap_query(
    geometry::Point2D raw_world,
    const vector::precision::snap::SnapQueryResult& query,
    const vector::precision::snap::SnapQueryStamp& expected_stamp,
    const vector::precision::snap::SnapCameraTransform& camera,
    const std::optional<vector::precision::snap::DirectionSnapResult>& direction,
    bool hard_direction_lock) {
    if (!active())
        return make_pen_result(PenDrawingResultCode::Ignored,
                               "No Pen session is active");
    if (pointer_down_)
        return make_pen_result(PenDrawingResultCode::Ignored,
                               "Endpoint snap does not run during a handle drag");
    const auto freshness = tool_.observe_snapshot(document_.current_snapshot());
    if (freshness.code == PenDrawingResultCode::Stale) {
        clear_pointer();
        return freshness;
    }
    return tool_.update_from_snap_query(raw_world, query, expected_stamp, camera,
                                        direction, hard_direction_lock);
}

PenDrawingResult PenToolController::update_numeric_candidate(
    std::string_view expression, vector::precision::numeric::ParseOptions options) {
    clear_pointer();
    return tool_.update_numeric_candidate(expression, options);
}

PenDrawingResult PenToolController::update_numeric_handle_constraints(
    PenHandleNumericConstraints constraints,
    vector::precision::numeric::ParseOptions options,
    std::optional<vector::precision::numeric::ConstructionFrame> frame) {
    if (!active())
        return make_pen_result(PenDrawingResultCode::Ignored,
                               "No Pen session is active");
    if (!pointer_down_ || !pointer_dragged_ || !pointer_current_)
        return make_pen_result(PenDrawingResultCode::InvalidInput,
                               "Pen handle dimensions require an active handle drag");
    const auto freshness = tool_.observe_snapshot(document_.current_snapshot());
    if (freshness.code == PenDrawingResultCode::Stale) {
        clear_pointer();
        return freshness;
    }
    if (!constraints.length && !constraints.angle) {
        pointer_numeric_constraints_.reset();
        pointer_numeric_frame_.reset();
        pointer_numeric_conflicted_ = false;
        return pointer_move(*pointer_current_, true, pointer_modifiers_);
    }
    pointer_numeric_constraints_ = std::move(constraints);
    pointer_numeric_options_ = options;
    pointer_numeric_frame_ = frame;
    return recompute_numeric_handle_preview();
}

bool PenToolController::set_gesture_preferences(
    PenGesturePreferences preferences) noexcept {
    if (!std::isfinite(preferences.direction_step_degrees) ||
        preferences.direction_step_degrees <= 0.0 ||
        preferences.direction_step_degrees > 180.0 ||
        preferences.profile > PenShortcutProfile::Illustrator)
        return false;
    gesture_preferences_ = preferences;
    return true;
}

bool PenToolController::direction_lock_active(
    PenGestureModifiers modifiers) const noexcept {
    if (gesture_preferences_.profile == PenShortcutProfile::Corel)
        return modifiers.control;
    return modifiers.shift;
}

geometry::Point2D PenToolController::handle_position(
    geometry::Point2D anchor, geometry::Point2D pointer,
    PenGestureModifiers modifiers) const noexcept {
    const auto delta = pointer - anchor;
    if (!direction_lock_active(modifiers)) return pointer;
    const double length = std::hypot(delta.x, delta.y);
    if (!std::isfinite(length) || length == 0.0) return pointer;
    const double step = gesture_preferences_.direction_step_degrees;
    const double angle = std::atan2(delta.y, delta.x) * (180.0 / std::acos(-1.0));
    const double snapped = std::round(angle / step) * step;
    const double radians = snapped * (std::acos(-1.0) / 180.0);
    return {std::fma(length, std::cos(radians), anchor.x),
            std::fma(length, std::sin(radians), anchor.y)};
}

PenDrawingResult PenToolController::recompute_numeric_handle_preview() {
    using vector::precision::numeric::parse_angle_degrees;
    using vector::precision::numeric::parse_length_mm;
    if (!pointer_down_ || !pointer_current_ || !pointer_numeric_constraints_) {
        pointer_numeric_conflicted_ = true;
        return make_pen_result(PenDrawingResultCode::InvalidInput,
                               "Pen handle dimensions have no active handle target");
    }

    const auto& constraints = *pointer_numeric_constraints_;
    if (!constraints.length && !constraints.angle) {
        pointer_numeric_conflicted_ = true;
        return make_pen_result(PenDrawingResultCode::InvalidInput,
                               "Enter a Pen handle length or angle");
    }

    std::optional<double> length_mm;
    if (constraints.length) {
        const auto parsed = parse_length_mm(*constraints.length,
                                            pointer_numeric_options_);
        if (!parsed) {
            pointer_numeric_conflicted_ = true;
            return make_pen_result(PenDrawingResultCode::InvalidInput,
                                   "Pen handle length is invalid");
        }
        if (*parsed.value < 0.0) {
            pointer_numeric_conflicted_ = true;
            return make_pen_result(PenDrawingResultCode::InvalidInput,
                                   "Pen handle length must be zero or greater");
        }
        length_mm = *parsed.value;
    }

    std::optional<double> angle_degrees;
    double reference_world_degrees = 0.0;
    if (constraints.angle) {
        const auto parsed = parse_angle_degrees(*constraints.angle,
                                                pointer_numeric_options_);
        if (!parsed) {
            pointer_numeric_conflicted_ = true;
            return make_pen_result(PenDrawingResultCode::InvalidInput,
                                   "Pen handle angle is invalid");
        }
        switch (constraints.angle_reference) {
        case vector::tools::pen::PenAngleReference::WorldX:
            break;
        case vector::tools::pen::PenAngleReference::WorldY:
            reference_world_degrees = 90.0;
            break;
        case vector::tools::pen::PenAngleReference::ConstructionFrameX:
            if (pointer_numeric_frame_) {
                reference_world_degrees =
                    pointer_numeric_frame_->x_axis_angle_degrees;
                if (!std::isfinite(reference_world_degrees)) {
                    pointer_numeric_conflicted_ = true;
                    return make_pen_result(PenDrawingResultCode::InvalidInput,
                                           "Pen handle construction frame is invalid");
                }
            }
            break;
        case vector::tools::pen::PenAngleReference::PreviousSegment: {
            const auto previous = tool_.previous_segment_angle_world();
            if (!previous) {
                pointer_numeric_conflicted_ = true;
                return make_pen_result(PenDrawingResultCode::InvalidInput,
                                       "Previous-segment handle angle is undefined");
            }
            reference_world_degrees = *previous;
            break;
        }
        default:
            pointer_numeric_conflicted_ = true;
            return make_pen_result(PenDrawingResultCode::InvalidInput,
                                   "Pen handle angle reference is invalid");
        }
        const double resolved_angle = *parsed.value + reference_world_degrees;
        if (!std::isfinite(resolved_angle)) {
            pointer_numeric_conflicted_ = true;
            return make_pen_result(PenDrawingResultCode::InvalidInput,
                                   "Pen handle world angle is outside the finite range");
        }
        angle_degrees = std::fmod(resolved_angle, 360.0);
    }

    constexpr double pi = 3.141592653589793238462643383279502884;
    const auto pointer_direction = handle_position(
        *pointer_down_, *pointer_current_, pointer_modifiers_);
    const auto pointer_offset = pointer_direction - *pointer_down_;
    const double pointer_radius = std::hypot(pointer_offset.x, pointer_offset.y);
    if (!pointer_offset.is_finite() || !std::isfinite(pointer_radius)) {
        pointer_numeric_conflicted_ = true;
        return make_pen_result(PenDrawingResultCode::InvalidInput,
                               "Pen handle pointer is outside the finite range");
    }

    geometry::Point2D handle_offset{};
    if (length_mm && angle_degrees) {
        const double radians = *angle_degrees * (pi / 180.0);
        handle_offset = {*length_mm * std::cos(radians),
                         *length_mm * std::sin(radians)};
    } else if (length_mm) {
        if (*length_mm > 0.0 && pointer_radius == 0.0) {
            pointer_numeric_conflicted_ = true;
            return make_pen_result(PenDrawingResultCode::InvalidInput,
                                   "Move away from the anchor to choose handle direction");
        }
        if (*length_mm > 0.0)
            handle_offset = pointer_offset * (*length_mm / pointer_radius);
    } else {
        if (pointer_radius == 0.0) {
            pointer_numeric_conflicted_ = true;
            return make_pen_result(PenDrawingResultCode::InvalidInput,
                                   "Move the pointer to choose handle length");
        }
        const double radians = *angle_degrees * (pi / 180.0);
        handle_offset = {pointer_radius * std::cos(radians),
                         pointer_radius * std::sin(radians)};
    }

    if (!handle_offset.is_finite()) {
        pointer_numeric_conflicted_ = true;
        return make_pen_result(PenDrawingResultCode::InvalidInput,
                               "Pen handle result is outside the finite range");
    }
    const double solved_length = std::hypot(handle_offset.x, handle_offset.y);
    if (!std::isfinite(solved_length)) {
        pointer_numeric_conflicted_ = true;
        return make_pen_result(PenDrawingResultCode::InvalidInput,
                               "Pen handle length is outside the finite range");
    }
    if (angle_degrees && solved_length > 0.0 &&
        direction_lock_active(pointer_modifiers_)) {
        if (pointer_radius == 0.0) {
            pointer_numeric_conflicted_ = true;
            return make_pen_result(PenDrawingResultCode::InvalidInput,
                                   "Move the pointer to resolve the locked handle direction");
        }
        const geometry::Point2D locked_unit{
            pointer_offset.x / pointer_radius, pointer_offset.y / pointer_radius};
        const geometry::Point2D numeric_unit{
            handle_offset.x / solved_length, handle_offset.y / solved_length};
        const double cross = std::fma(locked_unit.x, numeric_unit.y,
                                      -locked_unit.y * numeric_unit.x);
        const double dot = std::fma(locked_unit.x, numeric_unit.x,
                                    locked_unit.y * numeric_unit.y);
        if (!std::isfinite(cross) || !std::isfinite(dot) || dot <= 0.0 ||
            std::abs(cross) > 1e-10) {
            pointer_numeric_conflicted_ = true;
            return make_pen_result(PenDrawingResultCode::InvalidInput,
                                   "Pen handle angle conflicts with the locked direction");
        }
    }

    const geometry::Point2D anchor_world = *pointer_down_;
    const auto handle_world = anchor_world + handle_offset;
    const auto anchor_source = tool_.source_point_from_world(anchor_world);
    const auto handle_source = tool_.source_point_from_world(handle_world);
    if (!anchor_source.is_finite() || !handle_world.is_finite() ||
        !handle_source.is_finite() ||
        geometry::validate_point(handle_world) != geometry::GeometryStatus::Ok ||
        geometry::validate_point(handle_source) != geometry::GeometryStatus::Ok) {
        pointer_numeric_conflicted_ = true;
        return make_pen_result(PenDrawingResultCode::InvalidInput,
                               "Pen handle result is outside the supported coordinate range");
    }

    PenPointerPreview preview;
    preview.anchor = anchor_world;
    preview.dragging = true;
    preview.direction_locked = direction_lock_active(pointer_modifiers_);
    if (handle_offset.x != 0.0 || handle_offset.y != 0.0) {
        preview.node_mode = pointer_modifiers_.alt
            ? document::PathNodeMode::Cusp
            : document::PathNodeMode::Symmetric;
        preview.outgoing_handle = handle_world;
        if (!pointer_modifiers_.alt) {
            const auto incoming_source = anchor_source - (handle_source - anchor_source);
            const auto incoming_world = tool_.world_point_from_source(incoming_source);
            if (!incoming_source.is_finite() || !incoming_world.is_finite() ||
                geometry::validate_point(incoming_source) != geometry::GeometryStatus::Ok ||
                geometry::validate_point(incoming_world) != geometry::GeometryStatus::Ok) {
                pointer_numeric_conflicted_ = true;
                return make_pen_result(PenDrawingResultCode::InvalidInput,
                                       "Pen incoming handle is outside the supported coordinate range");
            }
            preview.incoming_handle = incoming_world;
        }
    }
    pointer_preview_ = std::move(preview);
    pointer_numeric_conflicted_ = false;
    return make_pen_result(PenDrawingResultCode::PreviewUpdated);
}

PenDrawingResult PenToolController::update_numeric_constraints(
    vector::tools::pen::PenNumericConstraints constraints,
    geometry::Point2D pointer_world,
    vector::precision::numeric::ParseOptions options,
    std::optional<vector::precision::numeric::ConstructionFrame> frame) {
    if (!active())
        return make_pen_result(PenDrawingResultCode::Ignored,
                               "No Pen session is active");
    const auto freshness = tool_.observe_snapshot(document_.current_snapshot());
    if (freshness.code == PenDrawingResultCode::Stale) {
        clear_pointer();
        return freshness;
    }
    clear_pointer();
    return tool_.update_numeric_constraints(std::move(constraints), pointer_world,
                                            options, frame);
}

PenDrawingResult PenToolController::confirm_candidate() {
    clear_pointer();
    return tool_.confirm_candidate();
}

PenDrawingResult PenToolController::finish_open() {
    clear_pointer();
    return tool_.finish_open(document_);
}

PenDrawingResult PenToolController::close() {
    clear_pointer();
    return tool_.close(document_);
}

PenDrawingResult PenToolController::undo_local() {
    clear_pointer();
    return tool_.undo_local();
}

PenDrawingResult PenToolController::redo_local() {
    clear_pointer();
    return tool_.redo_local();
}

vector::interaction::ToolSessionResult
PenToolController::suspend_for_pan() noexcept {
    clear_pointer();
    return tool_.suspend_for_pan();
}

vector::interaction::ToolSessionResult
PenToolController::resume_after_pan() noexcept {
    return tool_.resume_after_pan();
}

vector::interaction::ToolSessionResult PenToolController::on_focus_lost() noexcept {
    clear_pointer();
    return tool_.on_focus_lost();
}

vector::interaction::ToolSessionResult
PenToolController::on_capture_lost() noexcept {
    clear_pointer();
    return tool_.on_capture_lost();
}

void PenToolController::cancel() noexcept {
    clear_pointer();
    tool_.cancel_all();
}

void PenToolController::clear_pointer() noexcept {
    pointer_down_.reset();
    pointer_current_.reset();
    pointer_preview_.reset();
    pointer_numeric_constraints_.reset();
    pointer_numeric_frame_.reset();
    pointer_numeric_conflicted_ = false;
    pointer_modifiers_ = {};
    pointer_dragged_ = false;
}

} // namespace pikdraw::desktop::tools
