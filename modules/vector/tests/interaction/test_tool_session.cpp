#include "pikdraw/document/document.hpp"
#include "pikdraw/vector/interaction/tool_session.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace pikdraw;
using namespace pikdraw::vector::interaction;

namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

DraftEdit insert_byte(size_t offset, uint8_t value, bool can_finish) {
  return DraftEdit{offset, {}, {value}, can_finish};
}

void test_draft_lifecycle_and_local_history() {
  document::Document doc("tool-session-lifecycle");
  const auto base = doc.current_snapshot();
  ToolSession session;
  check(session.begin("polyline", base).applied(), "begin session");
  check(session.state() == ToolSessionState::AwaitFirstPoint,
        "session awaits first point");
  const auto old_token = session.token();
  check(session.apply_edit(insert_byte(0, 10, false)).applied(),
        "confirm first point");
  check(session.apply_edit(insert_byte(1, 20, true)).applied(),
        "confirm second point");
  check(session.can_finish() && session.draft_payload().size() == 2,
        "confirmed draft is finishable");
  check(session.set_preview_payload({10, 20, 99}).applied(),
        "update transient candidate");
  check(session.draft_payload() == std::vector<uint8_t>({10, 20}),
        "preview does not change confirmed draft");
  check(!session.accepts(old_token), "old preview token is stale");
  check(session.undo_local().applied() && !session.can_finish(),
        "draft undo removes only the last local edit");
  check(session.redo_local().applied() && session.can_finish(),
        "draft redo restores the local edit");
  check(session.escape(false).code == ToolSessionResultCode::FinishRequested,
        "Escape requests open-path finish");
  FinishedToolDraft finished;
  check(session.finish(base, finished).code == ToolSessionResultCode::Finished,
        "finish returns a commit proposal");
  check(finished.payload == std::vector<uint8_t>({10, 20}) &&
            finished.document_id == doc.id() && finished.base_revision == 0,
        "finish carries base identity and confirmed payload");
  check(doc.revision() == 0 && !doc.can_undo(),
        "tool preview never mutates document or its undo stack");
}

void test_escape_gesture_pan_and_profile_boundaries() {
  document::Document doc("tool-session-input");
  const auto base = doc.current_snapshot();
  ToolSession session;
  check(session.begin("pen", base).applied(), "begin pen session");
  check(session.escape(false).code == ToolSessionResultCode::Cancelled,
        "Escape cancels before the first point");
  check(session.begin("pen", base).applied(), "restart pen session");
  check(session.apply_edit(insert_byte(0, 1, false)).applied(),
        "confirm first anchor");
  check(session.begin_numeric_input().applied(), "open numeric input");
  check(session.finish_numeric_input(insert_byte(1, 2, true), false).code ==
            ToolSessionResultCode::InvalidInput,
        "invalid number is rejected");
  check(session.escape(false).code == ToolSessionResultCode::Cancelled &&
            session.draft_payload() == std::vector<uint8_t>({1}),
        "Escape in numeric input drops only the uncommitted value");
  check(session.begin_pan().applied(), "suspend draft for pan");
  check(session.end_pan().applied() &&
            session.state() == ToolSessionState::PreviewSegment,
        "pan resumes the same draft state");
  check(session.begin_pan().applied() &&
            session.on_capture_lost().applied() &&
            session.state() == ToolSessionState::PreviewSegment,
        "capture loss ends temporary pan without dropping the draft");
  check(session.set_preview_payload({1, 9}).applied(),
        "set a transient candidate before a gesture");
  check(session.begin_gesture({0, 0}, {4, 3}).applied(),
        "begin gesture with a visible candidate");
  const auto failed_gesture_token = session.token();
  check(session.finish_gesture(DraftEdit{0, {99}, {2}, true}).code ==
            ToolSessionResultCode::InvalidInput &&
            session.preview_payload().empty() &&
            session.draft_payload() == std::vector<uint8_t>({1}) &&
            !session.accepts(failed_gesture_token),
        "rejected gesture rolls back its preview without editing the draft");
  check(session.set_profile_generation(1).applied(),
        "install the initial modifier profile");
  check(session.begin_gesture({0, 0}, {4, 3}, 1).applied(),
        "begin pointer gesture");
  const auto first = session.update_gesture({5, 3}, 1);
  check(first && first->anchor == geometry::Point2D{0, 0},
        "gesture keeps the original anchor");
  check(session.set_profile_generation(2).applied() &&
            session.profile_generation() == 1,
        "profile change waits for gesture boundary");
  const auto released = session.update_gesture({7, 6}, 0);
  check(released && released->anchor == first->anchor &&
            released->raw_pointer == geometry::Point2D{7, 6},
        "modifier release recomputes from raw pointer and original anchor");
  const auto token = session.token();
  check(session.on_capture_lost().code == ToolSessionResultCode::Cancelled,
        "capture loss rolls back the active gesture");
  check(session.profile_generation() == 2 &&
            session.draft_payload() == std::vector<uint8_t>({1}) &&
            !session.accepts(token),
        "gesture rollback preserves confirmed draft and applies profile");
  check(session.begin_gesture({0, 0}, {2, 1}).applied(),
        "begin second gesture");
  check(session.on_focus_lost().code == ToolSessionResultCode::Cancelled &&
            session.state() == ToolSessionState::PreviewSegment,
        "focus loss clears transient drag state");
}

void test_stale_revision_and_close_invalidate_proposals() {
  document::Document doc("tool-session-stale");
  ToolSession session;
  check(session.begin("line", doc.current_snapshot()).applied(),
        "begin stale proposal session");
  check(session.apply_edit(insert_byte(0, 1, false)).applied() &&
            session.apply_edit(insert_byte(1, 2, true)).applied(),
        "confirm finishable draft");
  const auto token = session.token();
  document::PathObject object;
  object.id = document::generate_object_id();
  object.path.move_to({0, 0});
  object.path.line_to({5, 5});
  check(doc.execute_command(
            std::make_unique<document::AddObjectCommand>(object))
            .success,
        "change document after the draft base revision");
  FinishedToolDraft output;
  check(session.finish(doc.current_snapshot(), output).code ==
            ToolSessionResultCode::Stale &&
            session.state() == ToolSessionState::Idle &&
            !session.accepts(token),
        "finish rejects stale document revision and invalidates preview token");

  check(session.begin("line", doc.current_snapshot()).applied(),
        "begin session before close");
  const auto closing_token = session.token();
  session.on_document_closed();
  check(session.state() == ToolSessionState::Idle &&
            !session.accepts(closing_token),
        "closing the document invalidates outstanding input work");
}

void test_recovery_is_bounded_staged_and_explicit() {
  document::Document doc("tool-session-recovery");
  const auto base = doc.current_snapshot();
  ToolSession writer;
  check(writer.begin("polyline", base).applied(), "begin recovery source");
  check(writer.apply_edit(insert_byte(0, 3, false)).applied(),
        "add recovery anchor one");
  check(writer.apply_edit(insert_byte(1, 4, true)).applied(),
        "add recovery anchor two");
  const auto root = std::filesystem::temp_directory_path() /
                    ("pikdraw-tool-session-" + std::to_string(
                         std::chrono::steady_clock::now()
                             .time_since_epoch()
                             .count()));
  std::filesystem::create_directories(root);
  const auto path = (root / "draft.pdr").string();
  const auto saved = writer.write_recovery_checkpoint(path, 0x1234abcd);
  check(saved.success, "write bounded recovery record atomically");

  ToolSession restored;
  std::string error;
  check(restored.stage_recovery_file(path, base, 0x1234abcd, &error).applied(),
        "stage recovery matching document revision");
  check(restored.state() == ToolSessionState::RecoveryPending &&
            restored.draft_payload().empty() && doc.revision() == 0,
        "staged recovery is not auto-applied to document");
  check(restored.accept_recovery().applied() &&
            restored.draft_payload() == std::vector<uint8_t>({3, 4}),
        "explicit acceptance restores the editable draft");
  check(doc.revision() == 0 && !doc.can_undo(),
        "recovery remains separate from saved document history");

  document::Document other("different-document");
  ToolSession stale;
  const auto record = writer.recovery_record(0x1234abcd);
  check(stale.stage_recovery(record, other.current_snapshot(), 0x1234abcd,
                            &error)
                .code == ToolSessionResultCode::Stale,
        "recovery for another document is rejected");

  auto object = document::PathObject{};
  object.id = document::generate_object_id();
  object.path.move_to({0, 0});
  object.path.line_to({1, 1});
  check(doc.execute_command(
            std::make_unique<document::AddObjectCommand>(object))
            .success,
        "advance the checkpoint's source document");
  ToolSession stale_revision;
  check(stale_revision.stage_recovery(
            record, doc.current_snapshot(), 0x1234abcd, &error)
                .code == ToolSessionResultCode::Stale,
        "recovery for the same document at a different revision is rejected");
  std::filesystem::remove_all(root);
}

void test_session_and_checkpoint_budgets_fail_closed() {
  document::Document doc("tool-session-budget");
  ToolSession session;
  ToolSessionLimits limits;
  limits.max_draft_bytes = 2;
  limits.max_history_bytes = 2;
  limits.max_session_bytes = 4;
  check(session.begin("line", doc.current_snapshot(), limits).applied(),
        "begin bounded session");
  check(session.apply_edit(insert_byte(0, 1, false)).applied(),
        "insert within budget");
  check(session.apply_edit(insert_byte(1, 2, true)).applied(),
        "fill the configured budget");
  const auto over = session.apply_edit(insert_byte(2, 3, true));
  check(over.code == ToolSessionResultCode::BudgetExceeded &&
            session.draft_payload() == std::vector<uint8_t>({1, 2}),
        "oversized edit is rejected atomically");
  check(session.set_preview_payload({3}).code ==
            ToolSessionResultCode::BudgetExceeded &&
            session.preview_payload().empty(),
        "preview and history share the total session memory budget");
}
} // namespace

int main() {
  try {
    test_draft_lifecycle_and_local_history();
    test_escape_gesture_pan_and_profile_boundaries();
    test_stale_revision_and_close_invalidate_proposals();
    test_recovery_is_bounded_staged_and_explicit();
    test_session_and_checkpoint_budgets_fail_closed();
    std::cout << "ToolSession tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << "ToolSession test failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
