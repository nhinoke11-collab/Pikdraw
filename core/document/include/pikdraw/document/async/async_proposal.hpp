#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/model/document_page.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace pikdraw::document {

/// Metadata attached to asynchronous background worker jobs and proposals
struct AsyncProposalMetadata {
    std::string document_id;
    uint64_t epoch{0};
    uint64_t base_revision{0};
    uint64_t job_id{0};
    uint64_t job_generation{0};
    std::vector<ObjectId> target_object_ids;
    PageId target_page_id{kInvalidPageId};
    uint64_t page_navigation_generation{0};
};

/// Result code for asynchronous proposal validation and commitment
enum class ProposalStatus {
    Accepted,
    StaleRevision,      // Document revision advanced while worker was computing
    EpochMismatch,      // Document was closed, re-opened, or reset
    JobCancelled,       // Job was superseded or cancelled
    ExecutionFailed,    // Command failed to apply during atomic commit
    ReadOnlyDocument    // Document content is locked to a source representation
};

/// Asynchronous proposal containing proposed mutations from background workers
struct AsyncProposal {
    AsyncProposalMetadata metadata;
    std::string description;
    std::vector<CommandPtr> commands;
};

} // namespace pikdraw::document
