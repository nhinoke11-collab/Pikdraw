#include "pikdraw/vector/interaction/tool_session.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace pikdraw::vector::interaction {
namespace {
constexpr size_t kMaxToolIdBytes = 128;

bool same_bytes_at(const std::vector<uint8_t> &payload, size_t offset,
                   const std::vector<uint8_t> &expected) {
  if (offset > payload.size() || expected.size() > payload.size() - offset)
    return false;
  return std::equal(expected.begin(), expected.end(),
                    payload.begin() + static_cast<std::ptrdiff_t>(offset));
}

} // namespace

ToolSessionResult ToolSession::begin(std::string tool_id,
                                     document::SnapshotPtr base,
                                     ToolSessionLimits limits) {
  if (active() || state_ == ToolSessionState::RecoveryPending)
    return {ToolSessionResultCode::NeedsResolution,
            "Finish or cancel the current draft before starting another tool"};
  if (!base || base->document_id().empty() || base->epoch() == 0 ||
      tool_id.empty() || tool_id.size() > kMaxToolIdBytes ||
      limits.max_draft_bytes == 0 || limits.max_history_entries == 0 ||
      limits.max_session_bytes == 0 ||
      limits.max_draft_bytes > limits.max_session_bytes ||
      limits.max_history_bytes > limits.max_session_bytes ||
      limits.max_checkpoint_bytes > document::kRecoveryDraftPayloadLimit ||
      limits.max_checkpoint_bytes < 17)
    return {ToolSessionResultCode::InvalidInput,
            "Tool session requires a valid document, tool ID and limits"};
  if (session_generation_ == std::numeric_limits<uint64_t>::max())
    return {ToolSessionResultCode::InvalidInput,
            "Tool session generation is exhausted"};
  ++session_generation_;
  preview_generation_ = 1;
  tool_id_ = std::move(tool_id);
  base_snapshot_ = std::move(base);
  limits_ = limits;
  draft_payload_.clear();
  preview_payload_.clear();
  undo_history_.clear();
  redo_history_.clear();
  can_finish_ = false;
  gesture_active_ = false;
  modifier_mask_ = 0;
  pending_profile_generation_.reset();
  pending_recovery_.reset();
  state_ = ToolSessionState::AwaitFirstPoint;
  return {ToolSessionResultCode::Applied, {}};
}

ToolSessionResult ToolSession::apply_edit(DraftEdit edit) {
  if (state_ != ToolSessionState::AwaitFirstPoint &&
      state_ != ToolSessionState::PreviewSegment)
    return {ToolSessionResultCode::Ignored,
            "Draft edits are accepted only between gestures"};
  return apply_edit_internal(std::move(edit), true);
}

ToolSessionResult ToolSession::apply_edit_internal(DraftEdit edit,
                                                    bool record_history) {
  if (edit.offset > draft_payload_.size() ||
      edit.removed.size() > draft_payload_.size() - edit.offset ||
      !same_bytes_at(draft_payload_, edit.offset, edit.removed))
    return {ToolSessionResultCode::InvalidInput,
            "Draft edit does not match the confirmed source"};
  const size_t retained_bytes = draft_payload_.size() - edit.removed.size();
  if (edit.inserted.size() > limits_.max_draft_bytes ||
      retained_bytes > limits_.max_draft_bytes - edit.inserted.size())
    return {ToolSessionResultCode::BudgetExceeded,
            "Draft payload exceeds the session byte budget"};
  std::vector<uint8_t> next_payload;
  if (!apply_patch(edit.offset, edit.removed, edit.inserted, next_payload))
    return {ToolSessionResultCode::InvalidInput,
            "Draft edit does not match the confirmed source"};
  if (next_payload.size() > limits_.max_draft_bytes)
    return {ToolSessionResultCode::BudgetExceeded,
            "Draft payload exceeds the session byte budget"};
  if (edit.removed.empty() && edit.inserted.empty())
    return {ToolSessionResultCode::Ignored, "Draft edit has no effect"};

  HistoryEntry entry{edit.offset, std::move(edit.removed),
                     std::move(edit.inserted), can_finish_, edit.can_finish};
  size_t projected_history_bytes = history_bytes();
  if (record_history) {
    size_t discarded_redo_bytes = 0;
    for (const auto &redo : redo_history_)
      discarded_redo_bytes += redo.removed.size() + redo.inserted.size();
    const size_t new_entry_bytes = entry.removed.size() + entry.inserted.size();
    const size_t current_history_bytes = history_bytes();
    if (current_history_bytes < discarded_redo_bytes ||
        new_entry_bytes > limits_.max_history_bytes ||
        current_history_bytes - discarded_redo_bytes >
            limits_.max_history_bytes - new_entry_bytes ||
        undo_history_.size() >= limits_.max_history_entries)
      return {ToolSessionResultCode::BudgetExceeded,
              "Draft local history exceeds its configured budget"};
    projected_history_bytes = current_history_bytes - discarded_redo_bytes +
                              new_entry_bytes;
  }
  if (next_payload.size() > limits_.max_session_bytes ||
      projected_history_bytes >
          limits_.max_session_bytes - next_payload.size())
    return {ToolSessionResultCode::BudgetExceeded,
            "Draft and local history exceed the session memory budget"};
  if (record_history) {
    try {
      undo_history_.push_back(std::move(entry));
    } catch (...) {
      return {ToolSessionResultCode::BudgetExceeded,
              "Could not reserve draft undo history"};
    }
    redo_history_.clear();
  }
  draft_payload_.swap(next_payload);
  can_finish_ = edit.can_finish;
  preview_payload_.clear();
  state_ = ToolSessionState::PreviewSegment;
  ++preview_generation_;
  return {ToolSessionResultCode::Applied, {}};
}

bool ToolSession::apply_patch(size_t offset,
                              const std::vector<uint8_t> &removed,
                              const std::vector<uint8_t> &inserted,
                              std::vector<uint8_t> &out) const {
  if (offset > draft_payload_.size() ||
      removed.size() > draft_payload_.size() - offset ||
      !same_bytes_at(draft_payload_, offset, removed) ||
      inserted.size() > limits_.max_draft_bytes)
    return false;
  const size_t retained = draft_payload_.size() - removed.size();
  if (inserted.size() > limits_.max_draft_bytes - retained)
    return false;
  try {
    out = draft_payload_;
    out.erase(out.begin() + static_cast<std::ptrdiff_t>(offset),
              out.begin() + static_cast<std::ptrdiff_t>(offset + removed.size()));
    out.insert(out.begin() + static_cast<std::ptrdiff_t>(offset),
               inserted.begin(), inserted.end());
  } catch (...) {
    out.clear();
    return false;
  }
  return true;
}

ToolSessionResult ToolSession::set_preview_payload(
    std::vector<uint8_t> payload) {
  if (!active() || state_ == ToolSessionState::NumericInput ||
      state_ == ToolSessionState::PanSuspended)
    return {ToolSessionResultCode::Ignored,
            "Transient preview is unavailable in the current state"};
  if (payload.size() > limits_.max_draft_bytes)
    return {ToolSessionResultCode::BudgetExceeded,
            "Transient preview exceeds the session byte budget"};
  const size_t history = history_bytes();
  if (draft_payload_.size() > limits_.max_session_bytes ||
      payload.size() > limits_.max_session_bytes - draft_payload_.size() ||
      history > limits_.max_session_bytes - draft_payload_.size() -
                    payload.size())
    return {ToolSessionResultCode::BudgetExceeded,
            "Draft, preview and local history exceed the session memory budget"};
  preview_payload_ = std::move(payload);
  ++preview_generation_;
  return {ToolSessionResultCode::Applied, {}};
}

void ToolSession::clear_preview() noexcept {
  if (!preview_payload_.empty()) {
    preview_payload_.clear();
    ++preview_generation_;
  }
}

ToolSessionResult ToolSession::begin_gesture(geometry::Point2D anchor,
                                            geometry::Point2D raw_pointer,
                                            uint32_t modifier_mask) {
  if (state_ != ToolSessionState::PreviewSegment || gesture_active_ ||
      !anchor.is_finite() || !raw_pointer.is_finite())
    return {ToolSessionResultCode::InvalidInput,
            "A pointer gesture needs a valid preview draft and finite points"};
  gesture_anchor_ = anchor;
  raw_pointer_ = raw_pointer;
  modifier_mask_ = modifier_mask;
  gesture_active_ = true;
  state_ = ToolSessionState::DragHandle;
  ++preview_generation_;
  return {ToolSessionResultCode::Applied, {}};
}

std::optional<GestureSample> ToolSession::update_gesture(
    geometry::Point2D raw_pointer, uint32_t modifier_mask) {
  if (!gesture_active_ || state_ != ToolSessionState::DragHandle ||
      !raw_pointer.is_finite())
    return std::nullopt;
  raw_pointer_ = raw_pointer;
  modifier_mask_ = modifier_mask;
  ++preview_generation_;
  return GestureSample{gesture_anchor_, raw_pointer_, modifier_mask_,
                       profile_generation_};
}

ToolSessionResult ToolSession::finish_gesture(DraftEdit edit) {
  if (!gesture_active_ || state_ != ToolSessionState::DragHandle)
    return {ToolSessionResultCode::Ignored, "No pointer gesture is active"};
  gesture_active_ = false;
  state_ = ToolSessionState::PreviewSegment;
  auto result = apply_edit_internal(std::move(edit), true);
  if (!result.applied()) {
    preview_payload_.clear();
    ++preview_generation_;
    state_ = ToolSessionState::PreviewSegment;
  }
  apply_pending_profile();
  modifier_mask_ = 0;
  return result;
}

void ToolSession::clear_gesture() noexcept {
  gesture_active_ = false;
  modifier_mask_ = 0;
  preview_payload_.clear();
  apply_pending_profile();
}

ToolSessionResult ToolSession::cancel_gesture() noexcept {
  if (!gesture_active_ || state_ != ToolSessionState::DragHandle)
    return {ToolSessionResultCode::Ignored, "No pointer gesture is active"};
  clear_gesture();
  state_ = ToolSessionState::PreviewSegment;
  ++preview_generation_;
  return {ToolSessionResultCode::Cancelled, "Unconfirmed gesture was rolled back"};
}

ToolSessionResult ToolSession::begin_numeric_input() noexcept {
  if (state_ != ToolSessionState::PreviewSegment)
    return {ToolSessionResultCode::Ignored,
            "Numeric input requires a live draft candidate"};
  numeric_return_state_ = state_;
  state_ = ToolSessionState::NumericInput;
  ++preview_generation_;
  return {ToolSessionResultCode::Applied, {}};
}

ToolSessionResult ToolSession::finish_numeric_input(DraftEdit edit,
                                                    bool valid) {
  if (state_ != ToolSessionState::NumericInput)
    return {ToolSessionResultCode::Ignored, "Numeric input is not active"};
  if (!valid)
    return {ToolSessionResultCode::InvalidInput,
            "Invalid numeric entry was not applied"};
  state_ = numeric_return_state_;
  auto result = apply_edit_internal(std::move(edit), true);
  if (!result.applied())
    state_ = ToolSessionState::NumericInput;
  return result;
}

ToolSessionResult ToolSession::cancel_numeric_input() noexcept {
  if (state_ != ToolSessionState::NumericInput)
    return {ToolSessionResultCode::Ignored, "Numeric input is not active"};
  state_ = numeric_return_state_;
  ++preview_generation_;
  return {ToolSessionResultCode::Cancelled,
          "Numeric candidate was discarded; the draft is unchanged"};
}

ToolSessionResult ToolSession::begin_pan() noexcept {
  if (state_ != ToolSessionState::AwaitFirstPoint &&
      state_ != ToolSessionState::PreviewSegment &&
      state_ != ToolSessionState::NumericInput &&
      state_ != ToolSessionState::DragHandle)
    return {ToolSessionResultCode::Ignored,
            "Pan suspension is unavailable in the current state"};
  pan_return_state_ = state_;
  state_ = ToolSessionState::PanSuspended;
  ++preview_generation_;
  return {ToolSessionResultCode::Applied, {}};
}

ToolSessionResult ToolSession::end_pan() noexcept {
  if (state_ != ToolSessionState::PanSuspended)
    return {ToolSessionResultCode::Ignored, "Pan is not suspending a tool"};
  state_ = pan_return_state_;
  ++preview_generation_;
  return {ToolSessionResultCode::Applied, {}};
}

ToolSessionResult ToolSession::escape(bool shift_held) {
  if (state_ == ToolSessionState::Idle)
    return {ToolSessionResultCode::Ignored, "No draft is active"};
  if (shift_held || state_ == ToolSessionState::RecoveryPending) {
    cancel();
    return {ToolSessionResultCode::Cancelled, "Draft session was cancelled"};
  }
  if (state_ == ToolSessionState::NumericInput)
    return cancel_numeric_input();
  if (state_ == ToolSessionState::DragHandle)
    return cancel_gesture();
  if (state_ == ToolSessionState::PanSuspended)
    return end_pan();
  if (state_ == ToolSessionState::PreviewSegment && can_finish_)
    return {ToolSessionResultCode::FinishRequested,
            "Finish the confirmed path as open geometry"};
  cancel();
  return {ToolSessionResultCode::Cancelled,
          "Draft without finishable geometry was discarded"};
}

bool ToolSession::matches_base(
    const document::SnapshotPtr &snapshot) const noexcept {
  return snapshot && base_snapshot_ &&
         snapshot->document_id() == base_snapshot_->document_id() &&
         snapshot->epoch() == base_snapshot_->epoch() &&
         snapshot->revision() == base_snapshot_->revision();
}

ToolSessionResult ToolSession::finish(document::SnapshotPtr current,
                                      FinishedToolDraft &out_draft) {
  if (!active() || state_ != ToolSessionState::PreviewSegment ||
      !can_finish_)
    return {ToolSessionResultCode::InvalidInput,
            "A valid confirmed path is required to finish the session"};
  if (!matches_base(current)) {
    invalidate_stale();
    return {ToolSessionResultCode::Stale,
            "Document identity, epoch or revision changed during the draft"};
  }
  out_draft = FinishedToolDraft{tool_id_, base_snapshot_->document_id(),
                                base_snapshot_->epoch(),
                                base_snapshot_->revision(), session_generation_,
                                std::move(draft_payload_)};
  cancel();
  return {ToolSessionResultCode::Finished, {}};
}

void ToolSession::cancel() noexcept {
  state_ = ToolSessionState::Idle;
  tool_id_.clear();
  base_snapshot_.reset();
  draft_payload_.clear();
  preview_payload_.clear();
  undo_history_.clear();
  redo_history_.clear();
  can_finish_ = false;
  pending_recovery_.reset();
  clear_gesture();
  pending_profile_generation_.reset();
  if (session_generation_ != std::numeric_limits<uint64_t>::max())
    ++session_generation_;
  preview_generation_ = 0;
}

ToolSessionResult ToolSession::observe_snapshot(
    document::SnapshotPtr current) noexcept {
  if (!active() && state_ != ToolSessionState::RecoveryPending)
    return {ToolSessionResultCode::Ignored, "No live draft to validate"};
  if (!matches_base(current)) {
    invalidate_stale();
    return {ToolSessionResultCode::Stale,
            "Document changed; the old draft can no longer be committed"};
  }
  return {ToolSessionResultCode::Ignored, "Draft still matches its base revision"};
}

void ToolSession::on_document_closed() noexcept {
  if (active() || state_ == ToolSessionState::RecoveryPending)
    invalidate_stale();
}

void ToolSession::invalidate_stale() noexcept {
  state_ = ToolSessionState::Idle;
  tool_id_.clear();
  base_snapshot_.reset();
  draft_payload_.clear();
  preview_payload_.clear();
  undo_history_.clear();
  redo_history_.clear();
  can_finish_ = false;
  pending_recovery_.reset();
  clear_gesture();
  pending_profile_generation_.reset();
  if (session_generation_ != std::numeric_limits<uint64_t>::max())
    ++session_generation_;
  preview_generation_ = 0;
}

ToolSessionResult ToolSession::on_focus_lost() noexcept {
  if (state_ == ToolSessionState::DragHandle)
    return cancel_gesture();
  if (state_ == ToolSessionState::NumericInput)
    return cancel_numeric_input();
  if (state_ == ToolSessionState::PanSuspended) {
    const auto suspended_state = pan_return_state_;
    const auto pan_result = end_pan();
    if (suspended_state == ToolSessionState::DragHandle)
      return cancel_gesture();
    if (suspended_state == ToolSessionState::NumericInput)
      return cancel_numeric_input();
    return pan_result;
  }
  modifier_mask_ = 0;
  apply_pending_profile();
  ++preview_generation_;
  return active() ? ToolSessionResult{ToolSessionResultCode::Applied,
                                      "Transient input state was cleared"}
                  : ToolSessionResult{ToolSessionResultCode::Ignored,
                                     "No active draft"};
}

ToolSessionResult ToolSession::on_capture_lost() noexcept {
  if (state_ == ToolSessionState::DragHandle)
    return cancel_gesture();
  if (state_ == ToolSessionState::PanSuspended) {
    const auto suspended_state = pan_return_state_;
    const auto pan_result = end_pan();
    if (suspended_state == ToolSessionState::DragHandle)
      return cancel_gesture();
    return pan_result;
  }
  modifier_mask_ = 0;
  apply_pending_profile();
  ++preview_generation_;
  return active() ? ToolSessionResult{ToolSessionResultCode::Applied,
                                      "Unconfirmed pointer state was cleared"}
                  : ToolSessionResult{ToolSessionResultCode::Ignored,
                                     "No active draft"};
}

ToolSessionResult ToolSession::set_profile_generation(
    uint64_t generation) noexcept {
  if (generation < profile_generation_ ||
      (pending_profile_generation_ &&
       generation < *pending_profile_generation_))
    return {ToolSessionResultCode::InvalidInput,
            "Modifier profile generations must be monotonic"};
  if (gesture_active_) {
    pending_profile_generation_ = generation;
    return {ToolSessionResultCode::Applied,
            "Profile change is queued until the gesture boundary"};
  }
  profile_generation_ = generation;
  ++preview_generation_;
  return {ToolSessionResultCode::Applied, {}};
}

void ToolSession::apply_pending_profile() noexcept {
  if (!pending_profile_generation_)
    return;
  profile_generation_ = *pending_profile_generation_;
  pending_profile_generation_.reset();
  ++preview_generation_;
}

ToolSessionToken ToolSession::token() const {
  if (!base_snapshot_)
    return {};
  return ToolSessionToken{base_snapshot_->document_id(), base_snapshot_->epoch(),
                          base_snapshot_->revision(), session_generation_,
                          preview_generation_, profile_generation_};
}

bool ToolSession::accepts(const ToolSessionToken &candidate) const noexcept {
  if (!active() || !base_snapshot_)
    return false;
  return candidate.document_id == base_snapshot_->document_id() &&
         candidate.document_epoch == base_snapshot_->epoch() &&
         candidate.base_revision == base_snapshot_->revision() &&
         candidate.session_generation == session_generation_ &&
         candidate.preview_generation == preview_generation_ &&
         candidate.profile_generation == profile_generation_;
}

} // namespace pikdraw::vector::interaction
