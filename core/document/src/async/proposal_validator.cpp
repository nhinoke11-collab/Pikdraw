#include "pikdraw/document/document.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace pikdraw::document {

AsyncProposalMetadata Document::begin_job(uint64_t job_id) {
    require_idle_writer();
    if (job_id == 0) throw std::invalid_argument("Job ID must be nonzero");
    if (next_job_generation_ == std::numeric_limits<uint64_t>::max())
        throw std::overflow_error("Job generation exhausted");
    const auto generation = ++next_job_generation_;
    jobs_[job_id] = generation;
    return {document_id_, epoch_, revision_, job_id, generation, {},
            active_page_id_, page_navigation_generation_};
}

void Document::cancel_job(uint64_t job_id) {
    require_idle_writer();
    jobs_.erase(job_id);
}

void Document::close() {
    require_idle_writer();
    open_ = false;
    jobs_.clear();
    writer_.rollback_transaction();
}

ProposalStatus Document::validate_and_commit_proposal(AsyncProposal proposal) {
    try { require_idle_writer(); }
    catch (...) { return ProposalStatus::ExecutionFailed; }
    if (is_content_locked()) return ProposalStatus::ReadOnlyDocument;
    const auto& metadata = proposal.metadata;
    if (metadata.document_id != document_id_ || metadata.epoch != epoch_)
        return ProposalStatus::EpochMismatch;
    if (metadata.base_revision != revision_) return ProposalStatus::StaleRevision;
    const auto target_page = metadata.target_page_id == kInvalidPageId
        ? pages_.end()
        : std::find_if(pages_.begin(), pages_.end(), [&](const auto& page) {
              return page.id == metadata.target_page_id;
          });
    if (metadata.target_page_id != kInvalidPageId && target_page == pages_.end())
        return ProposalStatus::ExecutionFailed;
    const auto job = jobs_.find(metadata.job_id);
    if (job == jobs_.end() || job->second != metadata.job_generation)
        return ProposalStatus::JobCancelled;
    if (!metadata.target_object_ids.empty()) {
        if (target_page == pages_.end() ||
            metadata.target_page_id != active_page_id_ ||
            metadata.page_navigation_generation != page_navigation_generation_)
            return ProposalStatus::StalePageContext;
        for (const auto id : metadata.target_object_ids)
            if (!target_page->objects.contains(id))
                return ProposalStatus::ExecutionFailed;
    }
    for (const auto& command : proposal.commands)
        if (!command) return ProposalStatus::ExecutionFailed;
    if (proposal.commands.empty()) {
        jobs_.erase(job);
        return ProposalStatus::Accepted;
    }
    auto composite = std::make_unique<CompositeCommand>(std::move(proposal.description));
    for (auto& command : proposal.commands) composite->add_command(std::move(command));
    const auto result = execute_command(std::move(composite));
    if (!result.success) return ProposalStatus::ExecutionFailed;
    return ProposalStatus::Accepted;
}

ProposalStatus Document::replace_from_proposal(AsyncProposalMetadata metadata,
                                                 SnapshotPtr snapshot) {
    try { require_idle_writer(); }
    catch (...) { return ProposalStatus::ExecutionFailed; }
    if (!snapshot) return ProposalStatus::ExecutionFailed;
    if (metadata.document_id != document_id_ || metadata.epoch != epoch_)
        return ProposalStatus::EpochMismatch;
    if (metadata.base_revision != revision_) return ProposalStatus::StaleRevision;
    const auto job = jobs_.find(metadata.job_id);
    if (job == jobs_.end() || job->second != metadata.job_generation)
        return ProposalStatus::JobCancelled;
    if (!metadata.target_object_ids.empty()) return ProposalStatus::ExecutionFailed;
    if (!reset_from_snapshot(std::move(snapshot)))
        return ProposalStatus::ExecutionFailed;
    return ProposalStatus::Accepted;
}

} // namespace pikdraw::document
