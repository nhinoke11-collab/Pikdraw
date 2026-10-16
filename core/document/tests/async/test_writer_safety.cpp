#include "pikdraw/document/document.hpp"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace pikdraw::document;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

CommandPtr add(ObjectId id) {
    PathObject path;
    path.id = id;
    path.path = pikdraw::geometry::Path2D::make_rect(0, 0, 5, 5);
    return std::make_unique<AddObjectCommand>(path);
}

AsyncProposal proposal(AsyncProposalMetadata metadata, ObjectId id) {
    AsyncProposal result;
    result.metadata = std::move(metadata);
    result.commands.push_back(add(id));
    return result;
}

void job_lifecycle() {
    Document doc("jobs");
    auto first = doc.begin_job(7);
    doc.cancel_job(7);
    check(doc.validate_and_commit_proposal(proposal(first, 1)) == ProposalStatus::JobCancelled,
          "cancelled job committed");
    auto replaced = doc.begin_job(7);
    auto current = doc.begin_job(7);
    check(doc.validate_and_commit_proposal(proposal(replaced, 1)) == ProposalStatus::JobCancelled,
          "superseded job committed");
    check(doc.validate_and_commit_proposal(proposal(current, 1)) == ProposalStatus::Accepted,
          "current job rejected");
    auto missing_target = doc.begin_job(9);
    missing_target.target_object_ids = {123456};
    check(doc.validate_and_commit_proposal(proposal(missing_target, 2)) == ProposalStatus::ExecutionFailed,
          "missing source target accepted");
    check(doc.undo() && doc.redo(), "job command must participate in history");
    check(doc.validate_and_commit_proposal(proposal(current, 2)) == ProposalStatus::StaleRevision,
          "proposal survived undo/redo");
    auto late = doc.begin_job(8);
    doc.close();
    check(doc.validate_and_commit_proposal(proposal(late, 3)) != ProposalStatus::Accepted,
          "closed document accepts a job");
    check(!doc.execute_command(add(4)).success, "closed document accepts a write");
    Document reopened(*doc.current_snapshot());
    check(reopened.epoch() != first.epoch && reopened.id() == doc.id(), "open reuses epoch or loses ID");
    check(reopened.current_snapshot()->contains_object(1) && !reopened.is_dirty(), "open loses data/dirty state");
    check(reopened.execute_command(add(3)).success && reopened.undo(), "reopened document is not editable");
}

void publication_and_owner() {
    Document doc("reader-writer");
    std::atomic<bool> stop{false};
    std::atomic<bool> coherent{true};
    std::atomic<size_t> reads{0};
    std::atomic<bool> unauthorized_rejected{false};
    std::thread reader([&] {
        unauthorized_rejected = !doc.execute_command(add(999)).success;
        while (!stop.load()) {
            const auto snapshot = doc.current_snapshot();
            if (snapshot->revision() != snapshot->object_count() ||
                snapshot->page().layers.front().object_ids.size() != snapshot->object_count())
                coherent = false;
            ++reads;
        }
    });
    while (reads.load() == 0) std::this_thread::yield();
    for (ObjectId id = 1; id <= 250; ++id)
        check(doc.execute_command(add(id)).success, "owner write failed");
    stop = true;
    reader.join();
    check(unauthorized_rejected && coherent && reads > 0, "publication or owner guard failed");
    const auto snapshot = doc.current_snapshot();
    check(snapshot->object_count() == 250, "snapshot lost committed objects");
    check(snapshot->document_id() == "reader-writer", "snapshot lost document identity");
    bool direct_rejected = false;
    try { doc.apply_mutation(doc.page(), doc.objects()); }
    catch (const std::logic_error&) { direct_rejected = true; }
    check(direct_rejected, "direct mutation bypasses command boundary");
}

void observers_and_saves() {
    Document doc("observers");
    bool rejected = false;
    size_t notifications = 0;
    doc.add_snapshot_listener([](SnapshotPtr) { throw std::runtime_error("observer error"); });
    doc.add_snapshot_listener([&](SnapshotPtr) {
        rejected = !doc.execute_command(add(99)).success;
        ++notifications;
    });
    check(doc.execute_command(add(1)).success, "observer exception escaped committed command");
    check(rejected && notifications == 1 && doc.revision() == 1, "reentrant observer write accepted");
    check(doc.listener_failure_count() == 1, "observer failures have no diagnostic count");
    doc.mark_saved(doc.epoch(), 1);
    check(!doc.is_dirty(), "save current revision");
    check(doc.execute_command(add(2)).success, "second edit");
    doc.mark_saved(doc.epoch(), 1);
    check(doc.is_dirty(), "late save cleared newer edits");
    doc.mark_saved(doc.epoch(), 2);
    doc.mark_saved(doc.epoch(), 1);
    check(!doc.is_dirty(), "out-of-order old save dirtied already saved current state");
    auto& writer = doc.writer();
    writer.begin_transaction("original transaction");
    check(writer.stage_command(add(3)).success, "stage");
    bool nested_rejected = false;
    try { writer.begin_transaction("would discard original"); }
    catch (const std::logic_error&) { nested_rejected = true; }
    check(nested_rejected && writer.commit_transaction().success, "nested begin discards staged commands");
    check(doc.current_snapshot()->contains_object(3), "original transaction lost");
    UndoHistory disabled(0);
    disabled.push_undo(add(4));
    check(!disabled.can_undo(), "zero-capacity history not disabled");
}

int main() {
    try {
        job_lifecycle();
        publication_and_owner();
        observers_and_saves();
        std::cout << "PASS job lifecycle, concurrent publication, owner guard, observers, saves, transaction\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
