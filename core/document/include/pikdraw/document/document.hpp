#pragma once

#include <thread>
#include <deque>
#include <vector>
#include <unordered_set>
#include <memory>
#include <stdexcept>
#include <atomic>
#include <functional>
#include <limits>
#include "pikdraw/document/types.hpp"
#include "pikdraw/document/snapshot.hpp"
#include "pikdraw/document/command.hpp"

namespace pikdraw::document {

enum class ProposalRejectionReason {
    None = 0,
    EpochMismatch,
    StaleBaseRevision,
    GenerationMismatch,
    JobCancelled,
    InvalidPayload
    , DocumentMismatch
};

struct ProposalValidationResult {
    bool accepted{false};
    ProposalRejectionReason reason{ProposalRejectionReason::None};
    std::string message;
};

struct AsyncJobProposal {
    DocumentId doc_id;
    Epoch epoch{0};
    Revision base_revision{0};
    JobGeneration generation{0};
    uint64_t job_id{0};
    std::vector<std::shared_ptr<DocumentObject>> updated_objects;
};

struct TransactionMetrics {
    double prepare_us{},operation_us{},source_validation_us{},snapshot_us{},history_cleanup_us{},publish_us{},total_us{};
    bool committed{false};
};

class Document {
public:
    explicit Document(DocumentId id);

    void set_writer_thread(std::thread::id tid) {
        assert_writer_thread();
        if (tid != writer_thread_id_) throw std::logic_error("Writer migration is unsupported");
    }
    void assert_writer_thread() const {
        if (std::this_thread::get_id() != writer_thread_id_) {
            throw std::runtime_error("Document mutation attempted from non-writer thread");
        }
    }

    [[nodiscard]] const DocumentId& id() const { return doc_id_; }
    [[nodiscard]] Epoch epoch() const { return epoch_; }
    [[nodiscard]] Revision revision() const { return revision_; }
    [[nodiscard]] JobGeneration generation() const { return generation_; }
    [[nodiscard]] bool is_dirty() const { return is_dirty_; }

    ObjectId allocate_id() {
        assert_writer_thread();
        if (next_object_id_ == std::numeric_limits<ObjectId>::max()) throw std::overflow_error("Object IDs exhausted");
        return next_object_id_++;
    }

    void execute_command(std::unique_ptr<DocumentCommand> cmd);
    bool undo();
    bool redo();

    [[nodiscard]] std::shared_ptr<const DocumentSnapshot> create_snapshot() const;
    [[nodiscard]] const TransactionMetrics& last_transaction_metrics() const {
        assert_writer_thread(); return last_metrics_;
    }

    JobGeneration advance_generation() {
        assert_writer_thread();
        if (generation_ == std::numeric_limits<JobGeneration>::max()) throw std::overflow_error("Generation exhausted");
        return ++generation_;
    }

    void cancel_job(uint64_t job_id) {
        assert_writer_thread();
        cancelled_jobs_.insert(job_id);
    }

    void close_and_reopen();

    ProposalValidationResult validate_and_apply_proposal(const AsyncJobProposal& proposal);

    void notify_save_completed(Epoch saved_epoch, Revision saved_revision) {
        assert_writer_thread();
        if (epoch_ == saved_epoch && revision_ == saved_revision) {
            is_dirty_ = false;
            clean_epoch_ = saved_epoch;
            clean_revision_ = saved_revision;
        }
        // If newer edits occurred (revision_ > saved_revision), is_dirty_ remains true!
    }

    // Internal chunk/object access for commands
    void internal_put_object(std::shared_ptr<DocumentObject> obj);
    void internal_remove_object(ObjectId id);
    [[nodiscard]] std::shared_ptr<DocumentObject> internal_get_mutable_object(ObjectId id);

private:
    friend class PersistenceManager;
    void restore_snapshot(const DocumentSnapshot& snapshot);
    void transact(const std::function<void()>& operation);
    using Snapshot = std::shared_ptr<const DocumentSnapshot>;
    Snapshot published_;
    bool transaction_active_{false};
    TransactionMetrics last_metrics_;
    DocumentId doc_id_;
    Epoch epoch_{1};
    Revision revision_{0};
    JobGeneration generation_{1};
    ObjectId next_object_id_{1};
    bool is_dirty_{false};
    Epoch clean_epoch_{1};
    Revision clean_revision_{0};

    std::thread::id writer_thread_id_{std::this_thread::get_id()};

    // COW chunks
    ChunkMap chunks_;
    SpatialIndex spatial_index_;

    // Bounded history
    size_t max_history_{500};
    std::deque<Snapshot> undo_stack_;
    std::vector<Snapshot> redo_stack_;

    std::unordered_set<uint64_t> cancelled_jobs_;
};

} // namespace pikdraw::document
