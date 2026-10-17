#include "pikdraw/vector/tools/pen/pen_drawing_tool.hpp"

#include "pikdraw/document/commands/path_commands.hpp"
#include "pikdraw/geometry/primitives/validation.hpp"
#include "pen_drawing_tool_numeric.hpp"
#include "pen_drawing_tool_recovery.hpp"
#include "pen_drawing_tool_resume.hpp"
#include "pen_drawing_tool_snap.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>

namespace pikdraw::vector::tools::pen {
namespace {

bool valid_point(geometry::Point2D point) noexcept {
    return point.is_finite() &&
           geometry::validate_point(point) == geometry::GeometryStatus::Ok;
}

bool valid_anchor(const PenAnchorInput& input) noexcept {
    if (!valid_point(input.anchor) ||
        (input.incoming_handle && !valid_point(*input.incoming_handle)) ||
        (input.outgoing_handle && !valid_point(*input.outgoing_handle)))
        return false;

    auto mode = input.mode;
    const bool has_in = input.incoming_handle.has_value();
    const bool has_out = input.outgoing_handle.has_value();
    if (mode == document::PathNodeMode::Corner && (has_in || has_out))
        mode = document::PathNodeMode::Cusp;
    if (mode == document::PathNodeMode::AutoSmooth && (has_in || has_out))
        return false;
    if ((mode == document::PathNodeMode::Smooth ||
         mode == document::PathNodeMode::Symmetric) && (!has_in || !has_out))
        return false;

    if (mode != document::PathNodeMode::Smooth &&
        mode != document::PathNodeMode::Symmetric)
        return true;

    const auto incoming = *input.incoming_handle - input.anchor;
    const auto outgoing = *input.outgoing_handle - input.anchor;
    const double in_length = std::hypot(incoming.x, incoming.y);
    const double out_length = std::hypot(outgoing.x, outgoing.y);
    const double cross = std::fma(incoming.x, outgoing.y,
                                  -incoming.y * outgoing.x);
    const double scale = std::max(1.0, in_length * out_length);
    if (!std::isfinite(in_length) || !std::isfinite(out_length) ||
        !std::isfinite(cross) || std::abs(cross) > 1e-10 * scale ||
        incoming.x * outgoing.x + incoming.y * outgoing.y >= 0.0)
        return false;
    return mode != document::PathNodeMode::Symmetric ||
        std::abs(in_length - out_length) <=
            1e-10 * std::max({1.0, in_length, out_length});
}

document::PathNode make_node(const PenAnchorInput& input) {
    document::PathNode node;
    node.id = document::generate_path_element_id<document::NodeId>();
    node.anchor = input.anchor;
    node.mode = input.mode;
    if (node.mode == document::PathNodeMode::Corner &&
        (input.incoming_handle || input.outgoing_handle))
        node.mode = document::PathNodeMode::Cusp;
    if (input.incoming_handle) {
        node.has_incoming_handle = true;
        node.incoming_handle = *input.incoming_handle;
    }
    if (input.outgoing_handle) {
        node.has_outgoing_handle = true;
        node.outgoing_handle = *input.outgoing_handle;
    }
    return node;
}

document::EditableSegment make_segment(const PenAnchorInput& start,
                                       const PenAnchorInput& end) {
    document::EditableSegment segment;
    segment.id = document::generate_path_element_id<document::SegmentId>();
    segment.kind = (start.outgoing_handle || end.incoming_handle)
        ? document::PathSegmentKind::CubicBezier
        : document::PathSegmentKind::Line;
    return segment;
}

std::optional<std::string> validate_closing_edge(
    const document::EditableSubpath& source) {
    if (source.nodes.size() < 3) {
        const bool is_two_anchor_loop = source.nodes.size() == 2 &&
            source.segments.size() == 1 &&
            source.segments.front().kind == document::PathSegmentKind::CubicBezier &&
            source.nodes[0].has_outgoing_handle &&
            source.nodes[1].has_incoming_handle &&
            source.nodes[1].has_outgoing_handle &&
            source.nodes[0].has_incoming_handle;
        if (!is_two_anchor_loop)
            return "Close needs three anchors or two cubic anchors with handles on both sides";
    }
    if (source.nodes.back().anchor == source.nodes.front().anchor)
        return "Remove the duplicate endpoint and close at the first anchor instead";
    return std::nullopt;
}

} // namespace

PenDrawingResult PenDrawingTool::begin(document::SnapshotPtr base,
                                       size_t target_layer,
                                       interaction::ToolSessionLimits limits) {
    if (recovery_pending())
        return {PenDrawingResultCode::InvalidInput,
                "Accept or discard the staged Pen recovery before starting another draft",
                {}, {}};
    if (!base || base->document_id().empty() || target_layer >= base->page().layers.size())
        return {PenDrawingResultCode::InvalidInput,
                "Pen needs an open document and a valid target layer", {}, {}};
    if (base->page().layers[target_layer].locked)
        return {PenDrawingResultCode::InvalidInput, "The target layer is locked", {}, {}};
    const auto started = session_.begin("pen", base, limits);
    if (!started.applied())
        return {PenDrawingResultCode::InvalidInput, started.message, {}, {}};
    base_snapshot_ = std::move(base);
    limits_ = limits;
    target_layer_ = target_layer;
    clear_draft();
    subpath_.id = document::generate_path_element_id<document::SubpathId>();
    if (!subpath_.id.valid()) {
        cancel_all();
        return {PenDrawingResultCode::InvalidInput,
                "The path identity allocator is exhausted", {}, {}};
    }
    return {PenDrawingResultCode::Applied, {}, {}, {}};
}

PenDrawingResult PenDrawingTool::begin_resume(
    document::SnapshotPtr base, document::ObjectId object_id,
    document::SubpathId subpath_id, document::NodeId endpoint_id,
    interaction::ToolSessionLimits limits) {
    std::string error;
    const auto seed = detail::resolve_pen_resume_seed(
        base, object_id, subpath_id, endpoint_id, error);
    if (!seed)
        return {PenDrawingResultCode::InvalidInput, std::move(error), {}, {}};

    const auto started = begin(base, seed->layer_index, limits);
    if (started.code != PenDrawingResultCode::Applied) return started;
    resume_target_ = ResumeTarget{object_id, subpath_id, endpoint_id};
    source_from_world_ = seed->source_from_world;
    world_from_source_ = seed->world_from_source;
    subpath_ = {};
    subpath_.id = subpath_id;
    auto draft_endpoint = seed->endpoint;
    if (seed->started_at_first_endpoint) {
        std::swap(draft_endpoint.has_incoming_handle,
                  draft_endpoint.has_outgoing_handle);
        std::swap(draft_endpoint.incoming_handle, draft_endpoint.outgoing_handle);
    }
    subpath_.nodes.push_back(std::move(draft_endpoint));
    return {PenDrawingResultCode::Applied, {}, {}, {}};
}

bool PenDrawingTool::within_payload_budget(size_t anchor_count) const noexcept {
    return anchor_count <= limits_.max_draft_payload_bytes /
        (sizeof(PenAnchorInput) + sizeof(document::PathNode) +
         sizeof(document::EditableSegment));
}

PenDrawingResult PenDrawingTool::update_pointer(geometry::Point2D raw_world) {
    if (!active())
        return {PenDrawingResultCode::Ignored, "No Pen session is active", {}, {}};
    const auto source_point = source_from_world_.map_point(raw_world);
    if (!valid_point(raw_world) || !valid_point(source_point))
        return {PenDrawingResultCode::InvalidInput,
                "The Pen candidate is outside the finite document range", {}, {}};
    if (session_.state() == interaction::ToolSessionState::PanSuspended)
        return {PenDrawingResultCode::Ignored, "Pen preview is suspended for pan", {}, {}};
    if (numeric_constraints_) {
        numeric_pointer_world_ = raw_world;
        return evaluate_numeric_constraints();
    }
    numeric_pointer_world_.reset();
    numeric_conflicted_ = false;
    candidate_ = PenAnchorInput{source_point, document::PathNodeMode::Corner,
                                std::nullopt, std::nullopt};
    return {PenDrawingResultCode::PreviewUpdated, {}, {}, {}};
}

PenDrawingResult PenDrawingTool::confirm_candidate() {
    if (numeric_conflicted_)
        return {PenDrawingResultCode::InvalidInput,
                "Resolve the Pen numeric conflict before confirming this candidate", {}, {}};
    if (!candidate_)
        return {PenDrawingResultCode::InvalidInput,
                "Move the pointer or enter a coordinate before placing an anchor", {}, {}};
    return confirm_anchor(*candidate_);
}

PenDrawingResult PenDrawingTool::confirm_anchor(PenAnchorInput input) {
    if (!active())
        return {PenDrawingResultCode::Ignored, "No Pen session is active", {}, {}};
    if (numeric_conflicted_)
        return {PenDrawingResultCode::InvalidInput,
                "Resolve the Pen numeric conflict before placing an anchor", {}, {}};
    if (!valid_anchor(input))
        return {PenDrawingResultCode::InvalidInput,
                "The anchor or its handles violate the selected node type", {}, {}};
    if (numeric_constraints_ &&
        (!candidate_ || input.anchor != candidate_->anchor))
        return {PenDrawingResultCode::InvalidInput,
                "The active Pen numeric constraints do not match this anchor", {}, {}};
    const size_t new_count = subpath_.nodes.size() + 1;
    const size_t retained_history = undo_.size() + redo_.size();
    if (retained_history >= limits_.max_history_entries ||
        retained_history + 1 > limits_.max_history_payload_bytes /
                                   sizeof(AnchorEdit))
        return {PenDrawingResultCode::BudgetExceeded,
                "Pen local undo reached its configured history budget", {}, {}};
    if (!within_payload_budget(new_count))
        return {PenDrawingResultCode::BudgetExceeded,
                "Pen draft reached its configured byte budget", {}, {}};
    const size_t per_anchor = sizeof(PenAnchorInput) + sizeof(document::PathNode) +
                              sizeof(document::EditableSegment);
    if (new_count > limits_.max_session_payload_bytes / per_anchor ||
        retained_history + 1 >
            (limits_.max_session_payload_bytes - new_count * per_anchor) /
                sizeof(AnchorEdit))
        return {PenDrawingResultCode::BudgetExceeded,
                "Pen draft and history reached the configured session budget", {}, {}};
    if (!subpath_.nodes.empty() &&
        input.anchor == subpath_.nodes.back().anchor &&
        !(input.incoming_handle || subpath_.nodes.back().has_outgoing_handle))
        return {PenDrawingResultCode::InvalidInput,
                "A zero-length straight segment cannot be placed", {}, {}};
    if (!subpath_.nodes.empty() && input.anchor == subpath_.nodes.front().anchor)
        return {PenDrawingResultCode::InvalidInput,
                "Use Close Path at the first anchor instead of adding a duplicate node", {}, {}};

    document::PathNode node = make_node(input);
    if (!node.id.valid())
        return {PenDrawingResultCode::InvalidInput,
                "The node identity allocator is exhausted", {}, {}};
    std::optional<document::EditableSegment> segment;
    if (!subpath_.nodes.empty()) {
        segment = make_segment(
            PenAnchorInput{subpath_.nodes.back().anchor,
                           subpath_.nodes.back().mode,
                           subpath_.nodes.back().has_incoming_handle
                               ? std::optional{subpath_.nodes.back().incoming_handle}
                               : std::nullopt,
                           subpath_.nodes.back().has_outgoing_handle
                               ? std::optional{subpath_.nodes.back().outgoing_handle}
                               : std::nullopt},
            input);
        if (!segment->id.valid())
            return {PenDrawingResultCode::InvalidInput,
                    "The segment identity allocator is exhausted", {}, {}};
        const auto previous_control = subpath_.nodes.back().has_outgoing_handle
            ? subpath_.nodes.back().outgoing_handle
            : subpath_.nodes.back().anchor;
        const auto next_control = input.incoming_handle.value_or(input.anchor);
        if (segment->kind == document::PathSegmentKind::CubicBezier &&
            subpath_.nodes.back().anchor == input.anchor &&
            previous_control == subpath_.nodes.back().anchor &&
            next_control == input.anchor)
            return {PenDrawingResultCode::InvalidInput,
                    "A cubic segment with coincident controls has zero length", {}, {}};
    }

    AnchorEdit edit{input, segment, node};
    try {
        undo_.push_back(edit);
        subpath_.nodes.push_back(node);
        try {
            if (segment) subpath_.segments.push_back(*segment);
        } catch (...) {
            subpath_.nodes.pop_back();
            undo_.pop_back();
            throw;
        }
        redo_.clear();
    } catch (const std::exception&) {
        return {PenDrawingResultCode::BudgetExceeded,
                "Could not retain the new Pen anchor", {}, {}};
    }
    candidate_.reset();
    numeric_constraints_.reset();
    numeric_frame_.reset();
    numeric_pointer_world_.reset();
    numeric_options_ = {};
    numeric_conflicted_ = false;
    session_.clear_preview();
    return {PenDrawingResultCode::Applied, {}, {}, {}};
}

PenDrawingResult PenDrawingTool::undo_local() {
    if (!active() || undo_.empty())
        return {PenDrawingResultCode::Ignored, "There is no Pen anchor to undo", {}, {}};
    if (subpath_.nodes.empty())
        return {PenDrawingResultCode::InvalidInput,
                "Pen draft history does not match the current path", {}, {}};
    try {
        redo_.push_back(undo_.back());
    } catch (...) {
        return {PenDrawingResultCode::BudgetExceeded,
                "Could not retain Pen redo history", {}, {}};
    }
    if (subpath_.nodes.size() > 1) subpath_.segments.pop_back();
    subpath_.nodes.pop_back();
    undo_.pop_back();
    candidate_.reset();
    session_.clear_preview();
    return {PenDrawingResultCode::Applied, {}, {}, {}};
}

PenDrawingResult PenDrawingTool::redo_local() {
    if (!active() || redo_.empty())
        return {PenDrawingResultCode::Ignored, "There is no Pen anchor to redo", {}, {}};
    if (undo_.size() >= limits_.max_history_entries)
        return {PenDrawingResultCode::BudgetExceeded,
                "Pen local undo reached its configured entry budget", {}, {}};
    const AnchorEdit edit = redo_.back();
    if (!subpath_.nodes.empty() && !edit.incoming_segment)
        return {PenDrawingResultCode::InvalidInput,
                "Pen redo cannot restore a segment identity", {}, {}};
    try {
        undo_.push_back(edit);
        subpath_.nodes.push_back(edit.node);
        if (edit.incoming_segment) subpath_.segments.push_back(*edit.incoming_segment);
        redo_.pop_back();
    } catch (...) {
        if (!subpath_.nodes.empty() && subpath_.nodes.back().anchor == edit.anchor.anchor)
            subpath_.nodes.pop_back();
        if (!undo_.empty()) undo_.pop_back();
        return {PenDrawingResultCode::BudgetExceeded,
                "Could not restore the Pen anchor", {}, {}};
    }
    candidate_.reset();
    return {PenDrawingResultCode::Applied, {}, {}, {}};
}

bool PenDrawingTool::can_finish() const noexcept {
    return active() && subpath_.nodes.size() >= 2;
}

PenDrawingResult PenDrawingTool::finish_open(document::Document& document) {
    return finish_path(document, false);
}

PenDrawingResult PenDrawingTool::close(document::Document& document) {
    if (!active())
        return {PenDrawingResultCode::Ignored, "No Pen session is active", {}, {}};
    if (resume_target_)
        return {PenDrawingResultCode::InvalidInput,
                "A resumed endpoint extension stays open; close the path in a separate action", {}, {}};
    if (const auto error = validate_closing_edge(subpath_))
        return {PenDrawingResultCode::InvalidInput, *error, {}, {}};
    return finish_path(document, true);
}

PenDrawingResult PenDrawingTool::escape(document::Document& document) {
    if (!active()) return {PenDrawingResultCode::Ignored, "No Pen session is active", {}, {}};
    candidate_.reset();
    session_.clear_preview();
    if (can_finish()) return finish_open(document);
    cancel_all();
    return {PenDrawingResultCode::Cancelled,
            "The empty Pen draft was discarded without changing the document", {}, {}};
}

PenDrawingResult PenDrawingTool::finish_path(document::Document& document,
                                             bool closed) {
    if (!active() || subpath_.nodes.size() < 2)
        return {PenDrawingResultCode::InvalidInput,
                "A Pen path needs at least one confirmed segment", {}, {}};
    const auto current = document.current_snapshot();
    if (session_.observe_snapshot(current).code ==
        interaction::ToolSessionResultCode::Stale) {
        clear_draft();
        base_snapshot_.reset();
        return {PenDrawingResultCode::Stale,
                "The document changed; the Pen draft cannot be committed", {}, {}};
    }
    if (!current || !base_snapshot_ ||
        current->document_id() != base_snapshot_->document_id())
        return {PenDrawingResultCode::Stale,
                "The Pen draft belongs to another document", {}, {}};
    if (target_layer_ >= current->page().layers.size() ||
        current->page().layers[target_layer_].locked)
        return {PenDrawingResultCode::InvalidInput,
                "The target layer is missing or locked", {}, {}};

    if (resume_target_) {
        if (closed)
            return {PenDrawingResultCode::InvalidInput,
                    "A resumed endpoint extension cannot close the source path", {}, {}};
        std::vector<document::PathNode> extension_nodes;
        std::vector<document::PathSegmentKind> segment_kinds;
        try {
            extension_nodes.reserve(subpath_.nodes.size() - 1);
            segment_kinds.reserve(subpath_.segments.size());
            for (size_t index = 1; index < subpath_.nodes.size(); ++index) {
                auto node = subpath_.nodes[index];
                node.id = {};
                extension_nodes.push_back(std::move(node));
            }
            for (const auto& segment : subpath_.segments)
                segment_kinds.push_back(segment.kind);
        } catch (const std::exception&) {
            return {PenDrawingResultCode::BudgetExceeded,
                    "Could not prepare the resumed Pen extension", {}, {}};
        }
        const auto command_result = document.execute_command(
            std::make_unique<document::ExtendPathEndpointCommand>(
                resume_target_->object_id, resume_target_->subpath_id,
                resume_target_->endpoint_id, std::move(extension_nodes),
                std::move(segment_kinds)));
        if (!command_result.success)
            return {PenDrawingResultCode::CommandFailed,
                    command_result.error_message, {}, {}};
        const auto object_id = resume_target_->object_id;
        cancel_all();
        return {PenDrawingResultCode::Committed, {}, object_id, {}};
    }

    document::EditablePath source;
    auto committed_subpath = subpath_;
    committed_subpath.closed = closed;
    if (closed) {
        const auto& last = committed_subpath.nodes.back();
        const auto& first = committed_subpath.nodes.front();
        document::EditableSegment closing;
        closing.id = document::generate_path_element_id<document::SegmentId>();
        closing.kind = (last.has_outgoing_handle || first.has_incoming_handle)
            ? document::PathSegmentKind::CubicBezier
            : document::PathSegmentKind::Line;
        if (!closing.id.valid())
            return {PenDrawingResultCode::CommandFailed,
                    "The segment identity allocator is exhausted", {}, {}};
        committed_subpath.segments.push_back(closing);
    }
    source.subpaths.push_back(std::move(committed_subpath));
    if (const auto error = source.validation_error())
        return {PenDrawingResultCode::InvalidInput, *error, {}, {}};

    document::PathObject object;
    object.id = document::generate_object_id();
    if (object.id == document::kInvalidObjectId)
        return {PenDrawingResultCode::CommandFailed,
                "The object identity allocator is exhausted", {}, {}};
    object.name = "Pen Path";
    object.has_fill = closed;
    object.has_stroke = true;
    const auto object_id = object.id;
    try {
        object.set_authoring_source(std::move(source));
        const auto result = document.execute_command(
            std::make_unique<document::AddObjectCommand>(
                std::move(object), target_layer_));
        if (!result.success)
            return {PenDrawingResultCode::CommandFailed, result.error_message, {}, {}};
    } catch (const std::exception& error) {
        return {PenDrawingResultCode::CommandFailed, error.what(), {}, {}};
    }
    cancel_all();
    return {PenDrawingResultCode::Committed, {}, object_id, {}};
}

PenDrawingResult PenDrawingTool::observe_snapshot(document::SnapshotPtr current) {
    if (pending_recovery_) {
        if (!current || current->document_id() != pending_recovery_->document_id ||
            current->revision() != pending_recovery_->base_revision) {
            pending_recovery_.reset();
            return {PenDrawingResultCode::Stale,
                    "The document changed while Pen recovery was awaiting acceptance",
                    {}, {}};
        }
        return {PenDrawingResultCode::Applied, {}, {}, {}};
    }
    if (!active()) return {PenDrawingResultCode::Ignored, "No Pen session is active", {}, {}};
    const auto result = session_.observe_snapshot(std::move(current));
    if (result.code == interaction::ToolSessionResultCode::Stale) {
        clear_draft();
        base_snapshot_.reset();
        return {PenDrawingResultCode::Stale, result.message, {}, {}};
    }
    return {PenDrawingResultCode::Applied, {}, {}, {}};
}

interaction::ToolSessionResult PenDrawingTool::suspend_for_pan() noexcept {
    return session_.begin_pan();
}

interaction::ToolSessionResult PenDrawingTool::resume_after_pan() noexcept {
    return session_.end_pan();
}

interaction::ToolSessionResult PenDrawingTool::on_focus_lost() noexcept {
    candidate_.reset();
    return session_.on_focus_lost();
}

interaction::ToolSessionResult PenDrawingTool::on_capture_lost() noexcept {
    candidate_.reset();
    return session_.on_capture_lost();
}

void PenDrawingTool::cancel_all() noexcept {
    session_.cancel();
    base_snapshot_.reset();
    clear_draft();
    pending_recovery_.reset();
}

void PenDrawingTool::clear_draft() noexcept {
    subpath_ = {};
    candidate_.reset();
    resume_target_.reset();
    source_from_world_ = geometry::Transform2D::identity();
    world_from_source_ = geometry::Transform2D::identity();
    numeric_constraints_.reset();
    numeric_frame_.reset();
    numeric_pointer_world_.reset();
    numeric_options_ = {};
    numeric_conflicted_ = false;
    undo_.clear();
    redo_.clear();
}

} // namespace pikdraw::vector::tools::pen
