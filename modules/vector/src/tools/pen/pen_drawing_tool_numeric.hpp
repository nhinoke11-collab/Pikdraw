#pragma once

#include "pikdraw/vector/tools/pen/pen_drawing_tool.hpp"
#include "pikdraw/geometry/primitives/validation.hpp"
#include "pen_drawing_tool_resume.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace pikdraw::vector::tools::pen::detail {

struct ParsedPenNumericConstraints {
    std::optional<double> length_mm;
    std::optional<double> angle_degrees;
    std::optional<double> delta_x_mm;
    std::optional<double> delta_y_mm;
};

struct PenNumericSolveResult {
    geometry::Point2D offset{};
    const char* error{nullptr};
};

inline double pen_numeric_tolerance(double a, double b = 0.0) noexcept {
    return 1e-8 + 1e-12 * std::max({1.0, std::abs(a), std::abs(b)});
}

inline bool pen_numeric_close(double a, double b) noexcept {
    return std::isfinite(a) && std::isfinite(b) &&
        std::abs(a - b) <= pen_numeric_tolerance(a, b);
}

inline double pen_numeric_length(geometry::Point2D point) noexcept {
    return std::hypot(point.x, point.y);
}

inline bool valid_pen_numeric_point(geometry::Point2D point) noexcept {
    return point.is_finite() &&
        geometry::validate_point(point) == geometry::GeometryStatus::Ok;
}

inline geometry::Point2D rotate_pen_numeric_vector(
    geometry::Point2D point, double angle_degrees) noexcept {
    const double radians = angle_degrees * (std::acos(-1.0) / 180.0);
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    return {std::fma(point.x, cosine, -point.y * sine),
            std::fma(point.x, sine, point.y * cosine)};
}

inline double normalize_pen_angle(double angle) noexcept {
    double result = std::fmod(angle, 360.0);
    if (result < 0.0) result += 360.0;
    if (result >= 360.0) result -= 360.0;
    return result;
}

inline bool parse_pen_numeric_constraints(
    const PenNumericConstraints& input,
    precision::numeric::ParseOptions options,
    ParsedPenNumericConstraints& parsed,
    precision::numeric::Diagnostic& diagnostic,
    std::string& error) {
    using precision::numeric::parse_angle_degrees;
    using precision::numeric::parse_length_mm;
    if (input.length) {
        const auto value = parse_length_mm(*input.length, options);
        if (!value) {
            diagnostic = value.diagnostic;
            error = "Pen length field is invalid";
            return false;
        }
        if (*value.value <= 0.0) {
            error = "Pen segment length must be greater than zero";
            return false;
        }
        parsed.length_mm = *value.value;
    }
    if (input.angle) {
        const auto value = parse_angle_degrees(*input.angle, options);
        if (!value) {
            diagnostic = value.diagnostic;
            error = "Pen angle field is invalid";
            return false;
        }
        parsed.angle_degrees = normalize_pen_angle(*value.value);
    }
    if (input.delta_x) {
        const auto value = parse_length_mm(*input.delta_x, options);
        if (!value) {
            diagnostic = value.diagnostic;
            error = "Pen delta-X field is invalid";
            return false;
        }
        parsed.delta_x_mm = *value.value;
    }
    if (input.delta_y) {
        const auto value = parse_length_mm(*input.delta_y, options);
        if (!value) {
            diagnostic = value.diagnostic;
            error = "Pen delta-Y field is invalid";
            return false;
        }
        parsed.delta_y_mm = *value.value;
    }
    if (!parsed.length_mm && !parsed.angle_degrees &&
        !parsed.delta_x_mm && !parsed.delta_y_mm) {
        error = "Enter a Pen length, angle, or coordinate delta";
        return false;
    }
    return true;
}

inline PenNumericSolveResult solve_pen_numeric_constraints(
    const ParsedPenNumericConstraints& drivers,
    geometry::Point2D pointer_offset, double angle_degrees) noexcept {
    if (!pointer_offset.is_finite() ||
        (drivers.length_mm && !std::isfinite(*drivers.length_mm)) ||
        (drivers.angle_degrees && !std::isfinite(angle_degrees)) ||
        (drivers.delta_x_mm && !std::isfinite(*drivers.delta_x_mm)) ||
        (drivers.delta_y_mm && !std::isfinite(*drivers.delta_y_mm)))
        return {{}, "Pen numeric input is outside the finite range"};

    const double pointer_length = pen_numeric_length(pointer_offset);
    const bool has_direction = std::isfinite(pointer_length) && pointer_length > 0.0;
    double unit_x = 0.0;
    double unit_y = 0.0;
    if (drivers.angle_degrees) {
        const double radians = normalize_pen_angle(angle_degrees) *
            (std::acos(-1.0) / 180.0);
        unit_x = std::cos(radians);
        unit_y = std::sin(radians);
    }

    geometry::Point2D offset{};
    if (drivers.length_mm && drivers.angle_degrees) {
        offset = {*drivers.length_mm * unit_x, *drivers.length_mm * unit_y};
    } else if (drivers.angle_degrees) {
        if (!has_direction)
            return {{}, "Move the pointer to choose a radius for the locked angle"};
        offset = {pointer_length * unit_x, pointer_length * unit_y};
    } else if (drivers.length_mm) {
        if (!has_direction)
            return {{}, "Move the pointer to choose a direction for the locked length"};
        offset = {pointer_offset.x * (*drivers.length_mm / pointer_length),
                  pointer_offset.y * (*drivers.length_mm / pointer_length)};
    } else {
        offset = pointer_offset;
    }

    if (drivers.delta_x_mm && drivers.delta_y_mm) {
        offset = {*drivers.delta_x_mm, *drivers.delta_y_mm};
    } else if (drivers.delta_x_mm && drivers.angle_degrees &&
               !drivers.length_mm) {
        if (std::abs(unit_x) > 1e-12) {
            const double radius = *drivers.delta_x_mm / unit_x;
            if (radius < -pen_numeric_tolerance(radius))
                return {{}, "Delta-X conflicts with the locked angle"};
            offset = {std::max(0.0, radius) * unit_x,
                      std::max(0.0, radius) * unit_y};
        } else {
            if (!pen_numeric_close(*drivers.delta_x_mm, 0.0))
                return {{}, "Delta-X conflicts with a vertical locked angle"};
            if (!has_direction)
                return {{}, "Move the pointer to choose a radius for the locked angle"};
            offset = {pointer_length * unit_x, pointer_length * unit_y};
        }
    } else if (drivers.delta_y_mm && drivers.angle_degrees &&
               !drivers.length_mm) {
        if (std::abs(unit_y) > 1e-12) {
            const double radius = *drivers.delta_y_mm / unit_y;
            if (radius < -pen_numeric_tolerance(radius))
                return {{}, "Delta-Y conflicts with the locked angle"};
            offset = {std::max(0.0, radius) * unit_x,
                      std::max(0.0, radius) * unit_y};
        } else {
            if (!pen_numeric_close(*drivers.delta_y_mm, 0.0))
                return {{}, "Delta-Y conflicts with a horizontal locked angle"};
            if (!has_direction)
                return {{}, "Move the pointer to choose a radius for the locked angle"};
            offset = {pointer_length * unit_x, pointer_length * unit_y};
        }
    } else if (drivers.delta_x_mm && drivers.length_mm &&
               !drivers.angle_degrees) {
        const double remainder = *drivers.length_mm * *drivers.length_mm -
            *drivers.delta_x_mm * *drivers.delta_x_mm;
        const double tolerance = pen_numeric_tolerance(
            *drivers.length_mm * *drivers.length_mm,
            *drivers.delta_x_mm * *drivers.delta_x_mm);
        if (remainder < -tolerance)
            return {{}, "Delta-X is longer than the locked Pen length"};
        const double y = std::sqrt(std::max(0.0, remainder));
        if (y > 0.0 && pointer_offset.y == 0.0)
            return {{}, "Move above or below the anchor to choose a delta-Y branch"};
        offset = {*drivers.delta_x_mm,
                  y == 0.0 ? 0.0 : std::copysign(y, pointer_offset.y)};
    } else if (drivers.delta_y_mm && drivers.length_mm &&
               !drivers.angle_degrees) {
        const double remainder = *drivers.length_mm * *drivers.length_mm -
            *drivers.delta_y_mm * *drivers.delta_y_mm;
        const double tolerance = pen_numeric_tolerance(
            *drivers.length_mm * *drivers.length_mm,
            *drivers.delta_y_mm * *drivers.delta_y_mm);
        if (remainder < -tolerance)
            return {{}, "Delta-Y is longer than the locked Pen length"};
        const double x = std::sqrt(std::max(0.0, remainder));
        if (x > 0.0 && pointer_offset.x == 0.0)
            return {{}, "Move left or right of the anchor to choose a delta-X branch"};
        offset = {x == 0.0 ? 0.0 : std::copysign(x, pointer_offset.x),
                  *drivers.delta_y_mm};
    } else if (drivers.delta_x_mm) {
        offset.x = *drivers.delta_x_mm;
    } else if (drivers.delta_y_mm) {
        offset.y = *drivers.delta_y_mm;
    }

    if (!offset.is_finite())
        return {{}, "Pen numeric solution is outside the finite range"};
    const double length = pen_numeric_length(offset);
    if (length == 0.0)
        return {{}, "A zero-length Pen segment cannot be placed"};
    if (drivers.length_mm &&
        !pen_numeric_close(length, *drivers.length_mm))
        return {{}, "Pen coordinate fields conflict with the locked length"};
    if (drivers.delta_x_mm &&
        !pen_numeric_close(offset.x, *drivers.delta_x_mm))
        return {{}, "Pen coordinate fields conflict with delta-X"};
    if (drivers.delta_y_mm &&
        !pen_numeric_close(offset.y, *drivers.delta_y_mm))
        return {{}, "Pen coordinate fields conflict with delta-Y"};
    if (drivers.angle_degrees) {
        const double cross = std::fma(offset.x, unit_y, -offset.y * unit_x);
        const double dot = std::fma(offset.x, unit_x, offset.y * unit_y);
        if (!std::isfinite(cross) || !std::isfinite(dot) || dot <= 0.0 ||
            std::abs(cross) > pen_numeric_tolerance(length))
            return {{}, "Pen coordinate fields conflict with the locked angle"};
    }
    return {offset, nullptr};
}

inline geometry::Point2D forward_pen_segment_tangent(
    const document::EditableSubpath& subpath, size_t segment_index) noexcept {
    if (segment_index >= subpath.segments.size() ||
        segment_index + 1 >= subpath.nodes.size())
        return {};
    const auto& start = subpath.nodes[segment_index];
    const auto& end = subpath.nodes[segment_index + 1];
    if (subpath.segments[segment_index].kind ==
        document::PathSegmentKind::Line)
        return end.anchor - start.anchor;

    geometry::Point2D tangent{};
    if (end.has_incoming_handle) tangent = end.anchor - end.incoming_handle;
    if (tangent.x == 0.0 && tangent.y == 0.0 && start.has_outgoing_handle)
        tangent = end.anchor - start.outgoing_handle;
    if (tangent.x == 0.0 && tangent.y == 0.0)
        tangent = end.anchor - start.anchor;
    return tangent;
}

inline geometry::Point2D forward_pen_start_tangent(
    const document::EditableSubpath& subpath) noexcept {
    if (subpath.nodes.size() < 2 || subpath.segments.empty()) return {};
    const auto& start = subpath.nodes.front();
    const auto& end = subpath.nodes[1];
    if (subpath.segments.front().kind == document::PathSegmentKind::Line)
        return end.anchor - start.anchor;

    geometry::Point2D tangent{};
    if (start.has_outgoing_handle)
        tangent = start.outgoing_handle - start.anchor;
    if (tangent.x == 0.0 && tangent.y == 0.0 && end.has_incoming_handle)
        tangent = end.incoming_handle - start.anchor;
    if (tangent.x == 0.0 && tangent.y == 0.0)
        tangent = end.anchor - start.anchor;
    return tangent;
}

inline std::optional<double> pen_previous_segment_angle_world(
    const document::EditableSubpath& source, bool extending_from_start,
    const geometry::Transform2D& world_from_source) noexcept {
    if (source.nodes.size() < 2 ||
        source.segments.size() + 1 != source.nodes.size())
        return std::nullopt;
    geometry::Point2D tangent{};
    if (extending_from_start) {
        tangent = forward_pen_start_tangent(source);
        tangent = {-tangent.x, -tangent.y};
    } else {
        tangent = forward_pen_segment_tangent(source, source.segments.size() - 1);
    }
    tangent = world_from_source.map_vector(tangent);
    if (!tangent.is_finite() || (tangent.x == 0.0 && tangent.y == 0.0))
        return std::nullopt;
    const double angle = std::atan2(tangent.y, tangent.x) * (180.0 / std::acos(-1.0));
    if (!std::isfinite(angle)) return std::nullopt;
    return normalize_pen_angle(angle);
}

} // namespace pikdraw::vector::tools::pen::detail

namespace pikdraw::vector::tools::pen {

PenDrawingResult PenDrawingTool::update_numeric_candidate(
    std::string_view expression, precision::numeric::ParseOptions options,
    std::optional<precision::numeric::ConstructionFrame> frame) {
    if (!active())
        return {PenDrawingResultCode::Ignored, "No Pen session is active", {}, {}};
    numeric_constraints_.reset();
    numeric_frame_.reset();
    numeric_pointer_world_.reset();
    numeric_options_ = {};
    numeric_conflicted_ = true;
    const auto source_anchor = subpath_.nodes.empty()
        ? geometry::Point2D{} : subpath_.nodes.back().anchor;
    const auto world_anchor = world_from_source_.map_point(source_anchor);
    const auto parsed = precision::numeric::parse_point_input(expression, options);
    if (!parsed)
        return {PenDrawingResultCode::InvalidInput,
                "The Pen coordinate is invalid", {}, parsed.diagnostic};
    if (subpath_.nodes.empty() &&
        parsed.value->kind != precision::numeric::PointInputKind::AbsoluteWorld)
        return {PenDrawingResultCode::InvalidInput,
                "The first numeric Pen anchor needs absolute world coordinates", {}, {}};
    const auto evaluated = precision::numeric::evaluate_point_text(
        expression, world_anchor, frame, options);
    if (!evaluated)
        return {PenDrawingResultCode::InvalidInput,
                "The Pen coordinate is invalid", {}, evaluated.diagnostic};
    const auto source_candidate = source_from_world_.map_point(*evaluated.value);
    if (!detail::valid_pen_numeric_point(source_candidate))
        return {PenDrawingResultCode::InvalidInput,
                "The Pen coordinate is outside the transformed source range", {}, {}};
    candidate_ = PenAnchorInput{source_candidate, document::PathNodeMode::Corner,
                                std::nullopt, std::nullopt};
    numeric_conflicted_ = false;
    return {PenDrawingResultCode::PreviewUpdated, {}, {}, {}};
}

std::optional<double> PenDrawingTool::previous_segment_angle_world() const noexcept {
    if (subpath_.nodes.size() >= 2)
        return detail::pen_previous_segment_angle_world(
            subpath_, false, world_from_source_);
    if (!resume_target_ || !base_snapshot_) return std::nullopt;

    const auto object_value = base_snapshot_->get_object(resume_target_->object_id);
    if (!object_value || !std::holds_alternative<document::PathObject>(*object_value))
        return std::nullopt;
    const auto& object = std::get<document::PathObject>(*object_value);
    if (!object.authoring_source) return std::nullopt;
    const auto found = std::find_if(object.authoring_source->subpaths.begin(),
        object.authoring_source->subpaths.end(), [&](const auto& candidate) {
            return candidate.id == resume_target_->subpath_id;
        });
    if (found == object.authoring_source->subpaths.end()) return std::nullopt;
    const bool at_start = !found->nodes.empty() &&
        found->nodes.front().id == resume_target_->endpoint_id;
    const bool at_end = !found->nodes.empty() &&
        found->nodes.back().id == resume_target_->endpoint_id;
    if (!at_start && !at_end) return std::nullopt;
    return detail::pen_previous_segment_angle_world(
        *found, at_start, world_from_source_);
}

PenDrawingResult PenDrawingTool::update_numeric_constraints(
    PenNumericConstraints constraints, geometry::Point2D pointer_world,
    precision::numeric::ParseOptions options,
    std::optional<precision::numeric::ConstructionFrame> frame) {
    if (!active())
        return {PenDrawingResultCode::Ignored, "No Pen session is active", {}, {}};
    if (session_.state() == interaction::ToolSessionState::PanSuspended)
        return {PenDrawingResultCode::Ignored,
                "Pen numeric input is suspended while panning", {}, {}};
    numeric_constraints_ = std::move(constraints);
    numeric_options_ = options;
    numeric_frame_ = frame;
    if (!pointer_world.is_finite()) {
        numeric_pointer_world_.reset();
        numeric_conflicted_ = true;
        return {PenDrawingResultCode::InvalidInput,
                "Pen pointer is outside the finite document range", {}, {}};
    }
    numeric_pointer_world_ = pointer_world;
    return evaluate_numeric_constraints();
}

PenDrawingResult PenDrawingTool::evaluate_numeric_constraints() {
    if (!active() || !numeric_constraints_ || !numeric_pointer_world_ ||
        subpath_.nodes.empty()) {
        numeric_conflicted_ = true;
        return {PenDrawingResultCode::InvalidInput,
                "Place the first Pen anchor before locking numeric drivers", {}, {}};
    }

    detail::ParsedPenNumericConstraints parsed;
    precision::numeric::Diagnostic diagnostic{};
    std::string error;
    if (!detail::parse_pen_numeric_constraints(
            *numeric_constraints_, numeric_options_, parsed, diagnostic, error)) {
        numeric_conflicted_ = true;
        PenDrawingResult result{PenDrawingResultCode::InvalidInput,
                                std::move(error), {}, {}};
        if (diagnostic.code != precision::numeric::ErrorCode::None)
            result.numeric_diagnostic = diagnostic;
        return result;
    }

    const double frame_angle = numeric_frame_
        ? numeric_frame_->x_axis_angle_degrees : 0.0;
    if (!std::isfinite(frame_angle)) {
        numeric_conflicted_ = true;
        return {PenDrawingResultCode::InvalidInput,
                "The Pen construction frame is invalid", {},
                precision::numeric::Diagnostic{
                    precision::numeric::ErrorCode::InvalidFrame, 0}};
    }
    double reference_world = 0.0;
    switch (numeric_constraints_->angle_reference) {
    case PenAngleReference::WorldX:
        break;
    case PenAngleReference::WorldY:
        reference_world = 90.0;
        break;
    case PenAngleReference::ConstructionFrameX:
        reference_world = frame_angle;
        break;
    case PenAngleReference::PreviousSegment: {
        const auto angle = previous_segment_angle_world();
        if (!angle) {
            numeric_conflicted_ = true;
            return {PenDrawingResultCode::InvalidInput,
                    "Previous-segment angle is undefined at this anchor", {},
                    precision::numeric::Diagnostic{
                        precision::numeric::ErrorCode::InvalidAnchor, 0}};
        }
        reference_world = *angle;
        break;
    }
    default:
        numeric_conflicted_ = true;
        return {PenDrawingResultCode::InvalidInput,
                "The Pen angle reference is invalid", {}, {}};
    }

    if (parsed.angle_degrees)
        parsed.angle_degrees = detail::normalize_pen_angle(
            *parsed.angle_degrees + reference_world - frame_angle);
    const auto source_anchor = subpath_.nodes.back().anchor;
    const auto world_anchor = world_from_source_.map_point(source_anchor);
    const auto pointer_offset = detail::rotate_pen_numeric_vector(
        *numeric_pointer_world_ - world_anchor, -frame_angle);
    if (!world_anchor.is_finite() || !pointer_offset.is_finite()) {
        numeric_conflicted_ = true;
        return {PenDrawingResultCode::InvalidInput,
                "The Pen candidate is outside the finite coordinate range", {}, {}};
    }

    const auto solved = detail::solve_pen_numeric_constraints(
        parsed, pointer_offset, parsed.angle_degrees.value_or(0.0));
    if (solved.error) {
        numeric_conflicted_ = true;
        return {PenDrawingResultCode::InvalidInput, solved.error, {}, {}};
    }
    const auto world_candidate = world_anchor + detail::rotate_pen_numeric_vector(
        solved.offset, frame_angle);
    const auto source_candidate = source_from_world_.map_point(world_candidate);
    if (!world_candidate.is_finite() || !source_candidate.is_finite() ||
        geometry::validate_point(world_candidate) != geometry::GeometryStatus::Ok ||
        geometry::validate_point(source_candidate) != geometry::GeometryStatus::Ok) {
        numeric_conflicted_ = true;
        return {PenDrawingResultCode::InvalidInput,
                "The Pen numeric solution is outside the supported coordinate range", {},
                precision::numeric::Diagnostic{
                    precision::numeric::ErrorCode::Overflow, 0}};
    }
    candidate_ = PenAnchorInput{source_candidate, document::PathNodeMode::Corner,
                                std::nullopt, std::nullopt};
    numeric_conflicted_ = false;
    return {PenDrawingResultCode::PreviewUpdated, {}, {}, {}};
}

} // namespace pikdraw::vector::tools::pen
