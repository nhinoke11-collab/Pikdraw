#include "pikdraw/document/document.hpp"
#include <cmath>

namespace pikdraw::document {
namespace {
Epoch new_epoch() {
    static std::atomic<Epoch> next{1};
    auto value=next.load();
    for (;;) {
        if (value==std::numeric_limits<Epoch>::max()) throw std::overflow_error("Epoch exhausted");
        if (next.compare_exchange_weak(value,value+1)) return value;
    }
}
}

Document::Document(DocumentId id)
    : doc_id_(std::move(id)),
      writer_thread_id_(std::this_thread::get_id()) {
    epoch_=new_epoch(); clean_epoch_=epoch_;
    if (doc_id_.empty()) throw std::invalid_argument("Document ID is empty");
    std::atomic_store_explicit(&published_, std::make_shared<const DocumentSnapshot>(doc_id_, epoch_, revision_, chunks_, spatial_index_), std::memory_order_release);
}

void Document::transact(const std::function<void()>& operation) {
    assert_writer_thread();
    if (transaction_active_) throw std::logic_error("Nested transaction");
    if (revision_ == std::numeric_limits<Revision>::max()) throw std::overflow_error("Revision exhausted");
    const auto before = create_snapshot();
    auto rollback_chunks = chunks_;
    auto rollback_index = spatial_index_;
    const auto rollback_id = next_object_id_;
    transaction_active_ = true;
    try {
        operation();
        auto after = std::make_shared<const DocumentSnapshot>(doc_id_, epoch_, revision_ + 1, chunks_, spatial_index_);
        undo_stack_.push_back(before); // Allocate before publication.
        redo_stack_.clear();
        if (undo_stack_.size() > max_history_) undo_stack_.pop_front();
        ++revision_;
        is_dirty_ = true;
        std::atomic_store_explicit(&published_, std::move(after), std::memory_order_release);
        transaction_active_ = false;
    } catch (...) {
        chunks_.swap(rollback_chunks);
        spatial_index_.swap(rollback_index);
        next_object_id_ = rollback_id;
        transaction_active_ = false;
        throw;
    }
}

void Document::execute_command(std::unique_ptr<DocumentCommand> cmd) {
    assert_writer_thread();
    if (!cmd) throw std::invalid_argument("Null command");
    transact([&] { cmd->execute(*this); });
}

bool Document::undo() {
    assert_writer_thread();
    if (transaction_active_) throw std::logic_error("Undo inside transaction");
    if (undo_stack_.empty()) {
        return false;
    }
    if (revision_ == std::numeric_limits<Revision>::max()) throw std::overflow_error("Revision exhausted");
    const auto target = undo_stack_.back();
    auto chunks = target->chunks();
    auto index = target->spatial_index();
    auto after = std::make_shared<const DocumentSnapshot>(doc_id_, epoch_, revision_ + 1, chunks, index);
    redo_stack_.push_back(create_snapshot());
    undo_stack_.pop_back();
    chunks_.swap(chunks); spatial_index_.swap(index);
    ++revision_;
    is_dirty_ = (revision_ != clean_revision_ || epoch_ != clean_epoch_);
    std::atomic_store_explicit(&published_, std::move(after), std::memory_order_release);
    return true;
}

bool Document::redo() {
    assert_writer_thread();
    if (transaction_active_) throw std::logic_error("Redo inside transaction");
    if (redo_stack_.empty()) {
        return false;
    }
    if (revision_ == std::numeric_limits<Revision>::max()) throw std::overflow_error("Revision exhausted");
    const auto target = redo_stack_.back();
    auto chunks = target->chunks();
    auto index = target->spatial_index();
    auto after = std::make_shared<const DocumentSnapshot>(doc_id_, epoch_, revision_ + 1, chunks, index);
    undo_stack_.push_back(create_snapshot());
    redo_stack_.pop_back();
    chunks_.swap(chunks); spatial_index_.swap(index);
    ++revision_;
    is_dirty_ = (revision_ != clean_revision_ || epoch_ != clean_epoch_);
    std::atomic_store_explicit(&published_, std::move(after), std::memory_order_release);
    return true;
}

std::shared_ptr<const DocumentSnapshot> Document::create_snapshot() const {
    return std::atomic_load_explicit(&published_, std::memory_order_acquire);
}

void Document::close_and_reopen() {
    assert_writer_thread();
    if (transaction_active_) throw std::logic_error("Lifecycle change inside transaction");
    const auto epoch=new_epoch();
    auto after = std::make_shared<const DocumentSnapshot>(doc_id_, epoch, revision_, chunks_, spatial_index_);
    epoch_=epoch; generation_ = 1; cancelled_jobs_.clear();
    undo_stack_.clear(); redo_stack_.clear();
    std::atomic_store_explicit(&published_, std::move(after), std::memory_order_release);
}

void Document::restore_snapshot(const DocumentSnapshot& snapshot) {
    assert_writer_thread();
    if (revision_ || !chunks_.empty()) throw std::logic_error("Restore requires empty new document");
    transact([&] {
        for (const auto& [cid, chunk] : snapshot.chunks()) {
            (void)cid;
            if (chunk) for (const auto& obj : chunk->objects) internal_put_object(obj ? obj->clone() : nullptr);
        }
    });
    auto restored = std::make_shared<const DocumentSnapshot>(doc_id_, epoch_, snapshot.revision(), chunks_, spatial_index_);
    revision_ = snapshot.revision(); clean_revision_ = revision_; clean_epoch_ = epoch_; is_dirty_ = false;
    undo_stack_.clear(); redo_stack_.clear();
    std::atomic_store_explicit(&published_, std::move(restored), std::memory_order_release);
}

void Document::internal_put_object(std::shared_ptr<DocumentObject> obj) {
    assert_writer_thread();
    if (!transaction_active_) throw std::logic_error("Mutation outside transaction");
    if (!obj || obj->id == INVALID_OBJECT_ID || obj->id >= std::numeric_limits<ObjectId>::max() ||
        obj->id / OBJECTS_PER_CHUNK > std::numeric_limits<ChunkId>::max() ||
        !obj->transform.is_finite() || !std::isfinite(obj->stroke_width_mm) || obj->stroke_width_mm < 0)
        throw std::invalid_argument("Invalid object");
    // Own a deep clone; callers must not retain a writable alias to a published object.
    validate_source_object(*obj);
    obj = obj->clone();
    if (!obj || !obj->bounds().is_finite()) throw std::invalid_argument("Invalid clone or bounds");
    validate_source_object(*obj);
    const ChunkId cid = static_cast<ChunkId>(obj->id / OBJECTS_PER_CHUNK);
    auto& chunk_ptr = chunks_[cid];
    // Copy-on-write chunk
    auto new_chunk = std::make_shared<ObjectChunk>();
    if (chunk_ptr) {
        new_chunk->objects = chunk_ptr->objects;
    }
    bool replaced = false;
    for (auto& item : new_chunk->objects) {
        if (item && item->id == obj->id) {
            item = obj;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        new_chunk->objects.push_back(obj);
    }
    chunk_ptr = new_chunk;
    spatial_index_[obj->id] = obj->bounds();
    next_object_id_ = std::max(next_object_id_, obj->id + 1);
}

void Document::internal_remove_object(ObjectId id) {
    assert_writer_thread();
    if (!transaction_active_) throw std::logic_error("Mutation outside transaction");
    const ChunkId cid = static_cast<ChunkId>(id / OBJECTS_PER_CHUNK);
    auto it = chunks_.find(cid);
    if (it != chunks_.end() && it->second) {
        auto new_chunk = std::make_shared<ObjectChunk>();
        for (const auto& item : it->second->objects) {
            if (item && item->id != id) {
                new_chunk->objects.push_back(item);
            }
        }
        it->second = new_chunk;
    }
    spatial_index_.erase(id);
}

std::shared_ptr<DocumentObject> Document::internal_get_mutable_object(ObjectId id) {
    assert_writer_thread();
    const ChunkId cid = static_cast<ChunkId>(id / OBJECTS_PER_CHUNK);
    auto it = chunks_.find(cid);
    if (it == chunks_.end() || !it->second) {
        return nullptr;
    }
    for (const auto& item : it->second->objects) {
        if (item && item->id == id) {
            // Return clone for mutation
            return item->clone();
        }
    }
    return nullptr;
}

ProposalValidationResult Document::validate_and_apply_proposal(const AsyncJobProposal& proposal) {
    assert_writer_thread();
    ProposalValidationResult res;
    if (proposal.doc_id != doc_id_) return {false, ProposalRejectionReason::DocumentMismatch, "Document ID mismatch"};
    if (proposal.epoch != epoch_) {
        res.accepted = false;
        res.reason = ProposalRejectionReason::EpochMismatch;
        res.message = "Epoch mismatch (document closed/reopened)";
        return res;
    }
    if (proposal.base_revision != revision_) {
        res.accepted = false;
        res.reason = ProposalRejectionReason::StaleBaseRevision;
        res.message = "Stale base revision (document was modified or undone)";
        return res;
    }
    if (proposal.generation != generation_) {
        res.accepted = false;
        res.reason = ProposalRejectionReason::GenerationMismatch;
        res.message = "Job generation mismatch";
        return res;
    }
    if (cancelled_jobs_.contains(proposal.job_id)) {
        res.accepted = false;
        res.reason = ProposalRejectionReason::JobCancelled;
        res.message = "Job was cancelled";
        return res;
    }

    std::unordered_set<ObjectId> ids;
    if (proposal.updated_objects.empty() || proposal.updated_objects.size() > 100000)
        return {false, ProposalRejectionReason::InvalidPayload, "Invalid object count"};
    for (const auto& obj : proposal.updated_objects)
        if (!obj || !ids.insert(obj->id).second || !spatial_index_.contains(obj->id))
            return {false, ProposalRejectionReason::InvalidPayload, "Null, duplicate or unknown update ID"};
    try {
        transact([&] { for (const auto& obj : proposal.updated_objects) internal_put_object(obj); });
    } catch (const std::exception& e) {
        return {false, ProposalRejectionReason::InvalidPayload, e.what()};
    }
    res.accepted = true;
    res.reason = ProposalRejectionReason::None;
    res.message = "Success";
    return res;
}

// Command implementations
InsertObjectCommand::InsertObjectCommand(std::shared_ptr<DocumentObject> obj)
    : object_(obj ? obj->clone() : nullptr) {}

void InsertObjectCommand::execute(Document& doc) {
    if (!object_ || doc.internal_get_mutable_object(object_->id)) throw std::invalid_argument("Duplicate or null insert");
    doc.internal_put_object(object_);
}

void InsertObjectCommand::undo(Document& doc) {
    doc.internal_remove_object(object_->id);
}

RemoveObjectCommand::RemoveObjectCommand(ObjectId id)
    : id_(id) {}

void RemoveObjectCommand::execute(Document& doc) {
    saved_object_ = doc.internal_get_mutable_object(id_);
    doc.internal_remove_object(id_);
}

void RemoveObjectCommand::undo(Document& doc) {
    if (saved_object_) {
        doc.internal_put_object(saved_object_);
    }
}

TransformObjectCommand::TransformObjectCommand(
    ObjectId id, pikdraw::geometry::Transform2D delta_transform
) : id_(id), delta_transform_(delta_transform) {}

void TransformObjectCommand::execute(Document& doc) {
    auto obj = doc.internal_get_mutable_object(id_);
    if (obj) {
        old_transform_ = obj->transform;
        obj->transform = pikdraw::geometry::Transform2D::compose(delta_transform_, old_transform_);
        doc.internal_put_object(obj);
    }
}

void TransformObjectCommand::undo(Document& doc) {
    auto obj = doc.internal_get_mutable_object(id_);
    if (obj) {
        obj->transform = old_transform_;
        doc.internal_put_object(obj);
    }
}

} // namespace pikdraw::document
