#include "pikdraw/vector/interaction/tool_session.hpp"
#include <algorithm>
#include <limits>
#include <utility>

namespace pikdraw::vector::interaction {

size_t ToolSession::history_bytes() const noexcept {
  size_t bytes = 0;
  const auto add = [&](const HistoryEntry &entry) {
    if (entry.removed.size() > std::numeric_limits<size_t>::max() -
                                   entry.inserted.size()) {
      bytes = std::numeric_limits<size_t>::max();
      return;
    }
    const size_t entry_bytes = entry.removed.size() + entry.inserted.size();
    if (entry_bytes > std::numeric_limits<size_t>::max() - bytes)
      bytes = std::numeric_limits<size_t>::max();
    else
      bytes += entry_bytes;
  };
  for (const auto &entry : undo_history_)
    add(entry);
  for (const auto &entry : redo_history_)
    add(entry);
  return bytes;
}

ToolSessionResult ToolSession::undo_local() {
  if (!active())
    return {ToolSessionResultCode::Ignored, "No active draft"};
  if (state_ != ToolSessionState::PreviewSegment &&
      state_ != ToolSessionState::AwaitFirstPoint)
    return {ToolSessionResultCode::Ignored,
            "Draft history changes only between input operations"};
  if (undo_history_.empty())
    return {ToolSessionResultCode::NoHistory, "No local draft edit to undo"};
  const auto &entry = undo_history_.back();
  std::vector<uint8_t> next;
  if (!apply_patch(entry.offset, entry.inserted, entry.removed, next))
    return {ToolSessionResultCode::InvalidInput,
            "Draft history no longer matches the confirmed payload"};
  const size_t retained_history = history_bytes();
  if (next.size() > limits_.max_session_bytes ||
      retained_history > limits_.max_session_bytes - next.size())
    return {ToolSessionResultCode::BudgetExceeded,
            "Undo would exceed the total draft session byte budget"};
  try {
    redo_history_.push_back(entry);
  } catch (...) {
    return {ToolSessionResultCode::BudgetExceeded,
            "Could not reserve draft redo history"};
  }
  draft_payload_.swap(next);
  can_finish_ = entry.before_can_finish;
  undo_history_.pop_back();
  preview_payload_.clear();
  ++preview_generation_;
  state_ = draft_payload_.empty() ? ToolSessionState::AwaitFirstPoint
                                  : ToolSessionState::PreviewSegment;
  return {ToolSessionResultCode::Applied, {}};
}

ToolSessionResult ToolSession::redo_local() {
  if (!active())
    return {ToolSessionResultCode::Ignored, "No active draft"};
  if (state_ != ToolSessionState::PreviewSegment &&
      state_ != ToolSessionState::AwaitFirstPoint)
    return {ToolSessionResultCode::Ignored,
            "Draft history changes only between input operations"};
  if (redo_history_.empty())
    return {ToolSessionResultCode::NoHistory, "No local draft edit to redo"};
  const auto &entry = redo_history_.back();
  std::vector<uint8_t> next;
  if (!apply_patch(entry.offset, entry.removed, entry.inserted, next))
    return {ToolSessionResultCode::InvalidInput,
            "Draft redo no longer matches the confirmed payload"};
  const size_t retained_history = history_bytes();
  if (next.size() > limits_.max_session_bytes ||
      retained_history > limits_.max_session_bytes - next.size())
    return {ToolSessionResultCode::BudgetExceeded,
            "Redo would exceed the total draft session byte budget"};
  try {
    undo_history_.push_back(entry);
  } catch (...) {
    return {ToolSessionResultCode::BudgetExceeded,
            "Could not reserve draft undo history"};
  }
  draft_payload_.swap(next);
  can_finish_ = entry.after_can_finish;
  redo_history_.pop_back();
  preview_payload_.clear();
  ++preview_generation_;
  state_ = ToolSessionState::PreviewSegment;
  return {ToolSessionResultCode::Applied, {}};
}

} // namespace pikdraw::vector::interaction
