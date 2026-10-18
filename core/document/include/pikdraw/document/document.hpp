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
#include <atomic>
#include <thread>
#include <unordered_map>

namespace pikdraw::document {

using SnapshotListener = std::function<void(SnapshotPtr)>;

/// Primary Document facade coordinating transactions, snapshots, undo/redo, and dirty state
class Document {
public:
    explicit Document(std::string document_id = "doc-default", Page page = Page::make_default());
    explicit Document(const DocumentSnapshot& saved);
    ~Document() = default;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    [[nodiscard]] const std::string& id() const noexcept { return document_id_; }
    [[nodiscard]] uint64_t epoch() const { require_writer(); return epoch_; }
    [[nodiscard]] uint64_t revision() const { require_writer(); return revision_; }
    [[nodiscard]] SnapshotPtr current_snapshot() const noexcept {
        return std::atomic_load_explicit(&current_snapshot_, std::memory_order_acquire);
    }
    [[nodiscard]] const Page& page() const { require_writer(); return current_page_; }
    [[nodiscard]] const PersistentObjectMap& objects() const { require_writer(); return current_objects_; }
    [[nodiscard]] const std::vector<DocumentPage>& pages() const { require_writer(); return pages_; }
    [[nodiscard]] PageId active_page_id() const { require_writer(); return active_page_id_; }
    [[nodiscard]] color::DocumentColorMode color_mode() const { return page().color_mode; }
    [[nodiscard]] const std::string& color_profile_name() const { return page().color_profile_name; }
    [[nodiscard]] int32_t raster_dpi() const { return page().raster_dpi; }

    [[nodiscard]] bool can_undo() const { require_writer(); return history_.can_undo(); }
    [[nodiscard]] bool can_redo() const { require_writer(); return history_.can_redo(); }
    [[nodiscard]] bool is_dirty() const {
        require_writer();
        return recovery_pending_save_ || epoch_ != saved_epoch_ || revision_ != saved_revision_;
    }

    void mark_saved(uint64_t epoch, uint64_t revision);
    /// Recovered data has no durable user-selected file until the next save.
    void mark_recovered_unsaved();

    CommandResult execute_command(CommandPtr cmd);
    bool undo();
    bool redo();

    TransactionWriter& writer() { require_idle_writer(); return writer_; }

    // Register before dispatch. Restarting a job ID invalidates its old generation.
    AsyncProposalMetadata begin_job(uint64_t job_id);
    void cancel_job(uint64_t job_id);
    void close();
    [[nodiscard]] size_t listener_failure_count() const { require_writer(); return listener_failures_; }

    /// Validates background worker proposal against strict base revision and commits atomically
    ProposalStatus validate_and_commit_proposal(AsyncProposal proposal);

    /// Adds a listener invoked whenever a new immutable snapshot is published
    void add_snapshot_listener(SnapshotListener listener);

    /// Internal method for commands to apply state mutations
    void apply_mutation(Page page, PersistentObjectMap objects);
    /// Replaces page roots as part of an executing command.
    void apply_page_collection(std::vector<DocumentPage> pages, PageId active_page_id);
    /// Changes the visible page without creating an edit or undo record.
    [[nodiscard]] bool set_active_page(PageId page_id);

    /// Replaces document state as a new lifecycle, clearing history and invalidating old jobs.
    [[nodiscard]] bool reset_from_snapshot(SnapshotPtr snapshot);

private:
    friend class TransactionWriter;
    void require_writer() const;
    void require_idle_writer() const;
    CommandResult prepare_command(DocumentCommand& cmd, bool reverse, SnapshotPtr& next);
    void publish_snapshot(SnapshotPtr next) noexcept;
    bool move_history(bool reverse);

    std::string document_id_;
    uint64_t epoch_{1};
    uint64_t revision_{0};
    uint64_t saved_epoch_{1};
    uint64_t saved_revision_{0};
    bool recovery_pending_save_{false};

    Page current_page_;
    PersistentObjectMap current_objects_;
    std::vector<DocumentPage> pages_;
    PageId active_page_id_{kInvalidPageId};
    SnapshotPtr current_snapshot_;
    const std::thread::id writer_thread_{std::this_thread::get_id()};
    bool busy_{false};
    bool executing_{false};
    bool open_{true};
    uint64_t next_job_generation_{0};
    size_t listener_failures_{0};
    std::unordered_map<uint64_t, uint64_t> jobs_;

    UndoHistory history_;
    TransactionWriter writer_;
    std::vector<SnapshotListener> listeners_;
};

} // namespace pikdraw::document
