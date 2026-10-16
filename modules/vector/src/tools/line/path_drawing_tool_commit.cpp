#include "pikdraw/vector/tools/line/path_drawing_tool.hpp"
#include "path_object_builder.hpp"

#include "pikdraw/document/commands/command.hpp"

#include <exception>

namespace pikdraw::vector::tools::line {

PathDrawingResult PathDrawingTool::finish_path(document::Document& document,
                                              bool closed,
                                              bool continue_tool) {
    if (!active() || !can_finish())
        return {PathDrawingResultCode::InvalidInput,
                "At least one valid segment is required before finishing", {}};
    const auto current = document.current_snapshot();
    const auto freshness = session_.observe_snapshot(current);
    if (freshness.code == interaction::ToolSessionResultCode::Stale) {
        clear_draft();
        base_snapshot_.reset();
        return {PathDrawingResultCode::Stale,
                "Document changed during the path draft; the stale draft was discarded", {}};
    }
    if (!current || !base_snapshot_ || current->document_id() != base_snapshot_->document_id())
        return {PathDrawingResultCode::Stale,
                "The path draft belongs to another document", {}};
    if (target_layer_ >= current->page().layers.size() ||
        current->page().layers[target_layer_].locked)
        return {PathDrawingResultCode::InvalidInput,
                "The target layer is missing or locked", {}};

    detail::PathObjectBuildResult built;
    try {
        built = detail::build_path_object(kind_, points_, closed);
    } catch (const std::exception& error) {
        return {PathDrawingResultCode::CommandFailed, error.what(), {}};
    }
    if (!built)
        return {PathDrawingResultCode::InvalidInput, std::move(built.error), {}};
    const auto object_id = built.object.id;
    document::CommandResult command;
    try {
        command = document.execute_command(
            std::make_unique<document::AddObjectCommand>(
                std::move(built.object), target_layer_));
    } catch (const std::exception& error) {
        return {PathDrawingResultCode::CommandFailed, error.what(), {}};
    }
    if (!command.success)
        return {PathDrawingResultCode::CommandFailed, command.error_message, {}};

    session_.cancel();
    clear_draft();
    base_snapshot_.reset();
    if (continue_tool) {
        const auto restarted = restart(document.current_snapshot());
        if (restarted.code != PathDrawingResultCode::Applied)
            return {PathDrawingResultCode::Committed,
                    "Path was committed; the drawing tool could not start a fresh draft",
                    object_id};
    }
    return {PathDrawingResultCode::Committed, {}, object_id};
}

PathDrawingResult PathDrawingTool::enter(document::Document& document) {
    if (!active())
        return {PathDrawingResultCode::Ignored, "No path drawing is active", {}};
    if (candidate_ && candidate_is_numeric_)
        return confirm_candidate(document);
    if (kind_ == PathDrawingKind::Polyline && can_finish())
        return finish_path(document, false, true);
    if (kind_ == PathDrawingKind::Line)
        return {PathDrawingResultCode::Ignored,
                "Line commits as an independent object after its endpoint is placed", {}};
    return {PathDrawingResultCode::InvalidInput,
            "A Polyline needs at least one valid segment before Enter can finish", {}};
}

PathDrawingResult PathDrawingTool::close(document::Document& document) {
    if (!active() || kind_ != PathDrawingKind::Polyline)
        return {PathDrawingResultCode::InvalidInput,
                "Only an active Polyline can be closed", {}};
    candidate_.reset();
    candidate_is_numeric_ = false;
    session_.clear_preview();
    return finish_path(document, true, true);
}

PathDrawingResult PathDrawingTool::escape(document::Document& document) {
    if (!active())
        return {PathDrawingResultCode::Ignored, "No path drawing is active", {}};
    candidate_.reset();
    candidate_is_numeric_ = false;
    session_.clear_preview();
    if (can_finish()) return finish_path(document, false, false);
    cancel_all();
    return {PathDrawingResultCode::Cancelled,
            "The unfinished path had no segment and was discarded", {}};
}

PathDrawingResult PathDrawingTool::backspace() {
    if (!active())
        return {PathDrawingResultCode::Ignored, "No path drawing is active", {}};
    if (candidate_) {
        candidate_.reset();
        candidate_is_numeric_ = false;
        session_.clear_preview();
        return {PathDrawingResultCode::Applied,
                "The unconfirmed endpoint candidate was removed", {}};
    }
    if (points_.empty())
        return {PathDrawingResultCode::Ignored, "The path draft is already empty", {}};
    if (kind_ == PathDrawingKind::Polyline && points_.size() == 2) {
        const auto edit = session_.apply_edit(
            interaction::DraftEdit{0, {1}, {0}, false});
        if (!edit.applied())
            return {PathDrawingResultCode::InvalidInput, edit.message, {}};
    }
    points_.pop_back();
    if (points_.empty()) {
        const auto base = base_snapshot_;
        session_.cancel();
        clear_draft();
        base_snapshot_.reset();
        return restart(base);
    }
    return {PathDrawingResultCode::Applied,
            "The last confirmed path point was removed", {}};
}

PathDrawingResult PathDrawingTool::observe_snapshot(document::SnapshotPtr current) {
    if (!active())
        return {PathDrawingResultCode::Ignored, "No path drawing is active", {}};
    const auto result = session_.observe_snapshot(std::move(current));
    if (result.code == interaction::ToolSessionResultCode::Stale) {
        clear_draft();
        base_snapshot_.reset();
        return {PathDrawingResultCode::Stale, result.message, {}};
    }
    return {PathDrawingResultCode::Applied, {}, {}};
}

interaction::ToolSessionResult PathDrawingTool::suspend_for_pan() noexcept {
    return session_.begin_pan();
}

interaction::ToolSessionResult PathDrawingTool::resume_after_pan() noexcept {
    return session_.end_pan();
}

interaction::ToolSessionResult PathDrawingTool::on_focus_lost() noexcept {
    candidate_.reset();
    candidate_is_numeric_ = false;
    session_.clear_preview();
    return session_.on_focus_lost();
}

interaction::ToolSessionResult PathDrawingTool::on_capture_lost() noexcept {
    candidate_.reset();
    candidate_is_numeric_ = false;
    session_.clear_preview();
    return session_.on_capture_lost();
}

void PathDrawingTool::cancel_all() noexcept {
    session_.cancel();
    clear_draft();
    base_snapshot_.reset();
}

bool PathDrawingTool::can_finish() const noexcept {
    return active() && points_.size() >= 2;
}

PathDrawingResult PathDrawingTool::restart(document::SnapshotPtr base) {
    return begin(kind_, std::move(base), target_layer_, limits_);
}

void PathDrawingTool::clear_draft() noexcept {
    points_.clear();
    candidate_.reset();
    candidate_is_numeric_ = false;
    session_.clear_preview();
}

} // namespace pikdraw::vector::tools::line
