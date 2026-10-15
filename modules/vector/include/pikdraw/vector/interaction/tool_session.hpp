#pragma once

#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/document/storage/save_recovery.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pikdraw::vector::interaction {

enum class ToolSessionState : uint8_t {
  Idle,
  AwaitFirstPoint,
  PreviewSegment,
  DragHandle,
  NumericInput,
  PanSuspended,
  RecoveryPending
};

enum class ToolSessionResultCode : uint8_t {
  Applied,
  Ignored,
  NeedsResolution,
  FinishRequested,
  Finished,
  Cancelled,
  Stale,
  InvalidInput,
  BudgetExceeded,
  NoHistory
};

struct ToolSessionResult {
  ToolSessionResultCode code{ToolSessionResultCode::Ignored};
  std::string message;

  [[nodiscard]] bool applied() const noexcept {
    return code == ToolSessionResultCode::Applied ||
           code == ToolSessionResultCode::Finished ||
           code == ToolSessionResultCode::Cancelled;
  }
};

struct ToolSessionLimits {
  size_t max_draft_bytes{8u * 1024u * 1024u};
  size_t max_history_bytes{6u * 1024u * 1024u};
  size_t max_session_bytes{16u * 1024u * 1024u};
  size_t max_history_entries{256};
  size_t max_checkpoint_bytes{document::kRecoveryDraftPayloadLimit};
};

struct DraftEdit {
  size_t offset{0};
  std::vector<uint8_t> removed;
  std::vector<uint8_t> inserted;
  bool can_finish{false};
};

struct ToolSessionToken {
  std::string document_id;
  uint64_t document_epoch{0};
  uint64_t base_revision{0};
  uint64_t session_generation{0};
  uint64_t preview_generation{0};
  uint64_t profile_generation{0};
};

struct GestureSample {
  geometry::Point2D anchor;
  geometry::Point2D raw_pointer;
  uint32_t modifier_mask{0};
  uint64_t profile_generation{0};
};

struct FinishedToolDraft {
  std::string tool_id;
  std::string document_id;
  uint64_t document_epoch{0};
  uint64_t base_revision{0};
  uint64_t session_generation{0};
  std::vector<uint8_t> payload;
};

class ToolSession {
public:
  [[nodiscard]] ToolSessionResult begin(std::string tool_id,
                                        document::SnapshotPtr base,
                                        ToolSessionLimits limits = {});
  [[nodiscard]] ToolSessionResult apply_edit(DraftEdit edit);
  [[nodiscard]] ToolSessionResult set_preview_payload(
      std::vector<uint8_t> payload);
  void clear_preview() noexcept;

  [[nodiscard]] ToolSessionResult begin_gesture(
      geometry::Point2D anchor, geometry::Point2D raw_pointer,
      uint32_t modifier_mask = 0);
  [[nodiscard]] std::optional<GestureSample> update_gesture(
      geometry::Point2D raw_pointer, uint32_t modifier_mask);
  [[nodiscard]] ToolSessionResult finish_gesture(DraftEdit edit);
  [[nodiscard]] ToolSessionResult cancel_gesture() noexcept;

  [[nodiscard]] ToolSessionResult begin_numeric_input() noexcept;
  [[nodiscard]] ToolSessionResult finish_numeric_input(DraftEdit edit,
                                                       bool valid);
  [[nodiscard]] ToolSessionResult cancel_numeric_input() noexcept;
  [[nodiscard]] ToolSessionResult begin_pan() noexcept;
  [[nodiscard]] ToolSessionResult end_pan() noexcept;

  [[nodiscard]] ToolSessionResult escape(bool shift_held);
  [[nodiscard]] ToolSessionResult undo_local();
  [[nodiscard]] ToolSessionResult redo_local();
  [[nodiscard]] ToolSessionResult finish(document::SnapshotPtr current,
                                         FinishedToolDraft &out_draft);
  void cancel() noexcept;

  [[nodiscard]] ToolSessionResult observe_snapshot(
      document::SnapshotPtr current) noexcept;
  void on_document_closed() noexcept;
  [[nodiscard]] ToolSessionResult on_focus_lost() noexcept;
  [[nodiscard]] ToolSessionResult on_capture_lost() noexcept;
  [[nodiscard]] ToolSessionResult set_profile_generation(
      uint64_t generation) noexcept;

  [[nodiscard]] document::RecoveryDraftRecord recovery_record(
      uint32_t base_document_crc32) const;
  [[nodiscard]] document::SaveResult write_recovery_checkpoint(
      const std::string &path, uint32_t base_document_crc32,
      const document::SaveOptions &options = {}) const;
  [[nodiscard]] ToolSessionResult stage_recovery(
      const document::RecoveryDraftRecord &record,
      document::SnapshotPtr current, uint32_t expected_base_crc32,
      std::string *out_error = nullptr);
  [[nodiscard]] ToolSessionResult stage_recovery_file(
      const std::string &path, document::SnapshotPtr current,
      uint32_t expected_base_crc32, std::string *out_error = nullptr);
  [[nodiscard]] ToolSessionResult accept_recovery();
  void discard_recovery() noexcept;

  [[nodiscard]] ToolSessionState state() const noexcept { return state_; }
  [[nodiscard]] bool active() const noexcept {
    return state_ != ToolSessionState::Idle &&
           state_ != ToolSessionState::RecoveryPending;
  }
  [[nodiscard]] bool can_finish() const noexcept { return can_finish_; }
  [[nodiscard]] const std::string &tool_id() const noexcept { return tool_id_; }
  [[nodiscard]] const std::vector<uint8_t> &draft_payload() const noexcept {
    return draft_payload_;
  }
  [[nodiscard]] const std::vector<uint8_t> &preview_payload() const noexcept {
    return preview_payload_;
  }
  [[nodiscard]] uint64_t generation() const noexcept {
    return session_generation_;
  }
  [[nodiscard]] uint64_t profile_generation() const noexcept {
    return profile_generation_;
  }
  [[nodiscard]] ToolSessionToken token() const;
  [[nodiscard]] bool accepts(const ToolSessionToken &token) const noexcept;

private:
  struct HistoryEntry {
    size_t offset{0};
    std::vector<uint8_t> removed;
    std::vector<uint8_t> inserted;
    bool before_can_finish{false};
    bool after_can_finish{false};
  };
  struct PendingRecovery {
    std::string tool_id;
    std::vector<uint8_t> payload;
    bool can_finish{false};
  };

  [[nodiscard]] ToolSessionResult apply_edit_internal(DraftEdit edit,
                                                       bool record_history);
  [[nodiscard]] bool apply_patch(size_t offset,
                                 const std::vector<uint8_t> &removed,
                                 const std::vector<uint8_t> &inserted,
                                 std::vector<uint8_t> &out) const;
  [[nodiscard]] bool matches_base(
      const document::SnapshotPtr &snapshot) const noexcept;
  [[nodiscard]] size_t history_bytes() const noexcept;
  void clear_gesture() noexcept;
  void apply_pending_profile() noexcept;
  void invalidate_stale() noexcept;

  ToolSessionState state_{ToolSessionState::Idle};
  ToolSessionState numeric_return_state_{ToolSessionState::PreviewSegment};
  ToolSessionState pan_return_state_{ToolSessionState::PreviewSegment};
  std::string tool_id_;
  document::SnapshotPtr base_snapshot_;
  ToolSessionLimits limits_;
  std::vector<uint8_t> draft_payload_;
  std::vector<uint8_t> preview_payload_;
  std::vector<HistoryEntry> undo_history_;
  std::vector<HistoryEntry> redo_history_;
  bool can_finish_{false};
  bool gesture_active_{false};
  geometry::Point2D gesture_anchor_{};
  geometry::Point2D raw_pointer_{};
  uint32_t modifier_mask_{0};
  uint64_t session_generation_{0};
  uint64_t preview_generation_{0};
  uint64_t profile_generation_{0};
  std::optional<uint64_t> pending_profile_generation_;
  std::optional<PendingRecovery> pending_recovery_;
};

} // namespace pikdraw::vector::interaction
