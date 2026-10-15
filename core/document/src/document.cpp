#include "pikdraw/document/document.hpp"

namespace pikdraw::document {

Document::Document(std::string document_id, Page page)
    : document_id_(std::move(document_id)),
      current_page_(std::move(page)),
      writer_(*this) {
    publish_snapshot();
}

CommandResult Document::execute_command(CommandPtr cmd) {
    if (!cmd) {
        return CommandResult::fail("Null command provided to Document::execute_command");
    }

    auto res = cmd->execute(*this);
    if (res.success) {
        ++revision_;
        history_.push_undo(std::move(cmd));
        publish_snapshot();
    }
    return res;
}

bool Document::undo() {
    if (!can_undo()) return false;
    CommandPtr cmd = history_.pop_undo();
    if (!cmd) return false;

    auto res = cmd->undo(*this);
    if (res.success) {
        ++revision_; // Monotonic revision bump on undo
        history_.push_redo(std::move(cmd));
        publish_snapshot();
        return true;
    }
    return false;
}

bool Document::redo() {
    if (!can_redo()) return false;
    CommandPtr cmd = history_.pop_redo();
    if (!cmd) return false;

    auto res = cmd->execute(*this);
    if (res.success) {
        ++revision_; // Monotonic revision bump on redo
        history_.push_undo(std::move(cmd));
        publish_snapshot();
        return true;
    }
    return false;
}

ProposalStatus Document::validate_and_commit_proposal(AsyncProposal proposal) {
    // 1. Strict epoch validation
    if (proposal.metadata.epoch != epoch_) {
        return ProposalStatus::EpochMismatch;
    }

    // 2. Strict base revision validation
    if (proposal.metadata.base_revision != revision_) {
        return ProposalStatus::StaleRevision;
    }

    if (proposal.commands.empty()) {
        return ProposalStatus::Accepted;
    }

    // 3. Atomic execution of proposed commands
    auto composite = std::make_unique<CompositeCommand>(std::move(proposal.description));
    for (auto& c : proposal.commands) {
        composite->add_command(std::move(c));
    }

    auto res = execute_command(std::move(composite));
    if (!res.success) {
        return ProposalStatus::ExecutionFailed;
    }

    return ProposalStatus::Accepted;
}

void Document::apply_mutation(Page page, PersistentObjectMap objects) {
    current_page_ = std::move(page);
    current_objects_ = std::move(objects);
}

void Document::reset_from_snapshot(SnapshotPtr snapshot) {
    if (!snapshot) return;
    epoch_ = snapshot->epoch();
    revision_ = snapshot->revision();
    saved_epoch_ = epoch_;
    saved_revision_ = revision_;
    current_page_ = snapshot->page();
    current_objects_ = snapshot->objects();
    history_.clear();
    publish_snapshot();
}

void Document::publish_snapshot() {
    current_snapshot_ = std::make_shared<const DocumentSnapshot>(
        epoch_, revision_, current_page_, current_objects_);

    for (const auto& listener : listeners_) {
        if (listener) {
            listener(current_snapshot_);
        }
    }
}

void Document::add_snapshot_listener(SnapshotListener listener) {
    if (listener) {
        listeners_.push_back(std::move(listener));
    }
}

} // namespace pikdraw::document
