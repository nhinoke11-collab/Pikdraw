#pragma once

#include "pikdraw/document/model/object.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/commands/transaction_writer.hpp"
#include "pikdraw/document/history/undo_history.hpp"
#include "pikdraw/document/async/async_proposal.hpp"
#include <string>
#include <memory>
#include <functional>
#include <vector>

namespace pikdraw::document {

using SnapshotListener = std::function<void(SnapshotPtr)>;

/// Primary Document facade coordinating transactions, snapshots, undo/redo, and dirty state
class Document {
public:
    explicit Document(std::string document_id = "doc-default", Page page = Page::make_default());
    ~Document() = default;

    [[nodiscard]] const std::string& id() const noexcept { return document_id_; }
    [[nodiscard]] uint64_t epoch() const noexcept { return epoch_; }
    [[nodiscard]] uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] SnapshotPtr current_snapshot() const noexcept { return current_snapshot_; }
    [[nodiscard]] const Page& page() const noexcept { return current_page_; }
    [[nodiscard]] const PersistentObjectMap& objects() const noexcept { return current_objects_; }

    [[nodiscard]] bool can_undo() const noexcept { return history_.can_undo(); }
    [[nodiscard]] bool can_redo() const noexcept { return history_.can_redo(); }
    [[nodiscard]] bool is_dirty() const noexcept {
        return epoch_ != saved_epoch_ || revision_ != saved_revision_;
    }

    void mark_saved(uint64_t epoch, uint64_t revision) noexcept {
        saved_epoch_ = epoch;
        saved_revision_ = revision;
    }

    CommandResult execute_command(CommandPtr cmd);
    bool undo();
    bool redo();

    TransactionWriter& writer() noexcept { return writer_; }

    /// Validates background worker proposal against strict base revision and commits atomically
    ProposalStatus validate_and_commit_proposal(AsyncProposal proposal);

    /// Adds a listener invoked whenever a new immutable snapshot is published
    void add_snapshot_listener(SnapshotListener listener);

    /// Internal method for commands to apply state mutations
    void apply_mutation(Page page, PersistentObjectMap objects);

    /// Resets document state from a loaded snapshot, clears undo history, and publishes snapshot
    void reset_from_snapshot(SnapshotPtr snapshot);

private:
    void publish_snapshot();

    std::string document_id_;
    uint64_t epoch_{1};
    uint64_t revision_{0};
    uint64_t saved_epoch_{1};
    uint64_t saved_revision_{0};

    Page current_page_;
    PersistentObjectMap current_objects_;
    SnapshotPtr current_snapshot_;

    UndoHistory history_;
    TransactionWriter writer_;
    std::vector<SnapshotListener> listeners_;
};

} // namespace pikdraw::document
