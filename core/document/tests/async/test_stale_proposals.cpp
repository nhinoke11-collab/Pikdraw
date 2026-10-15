#include "pikdraw/document/document.hpp"
#include <cassert>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::document;

void test_valid_proposal() {
    Document doc("doc-async-1");

    AsyncProposal proposal;
    proposal.metadata.document_id = "doc-async-1";
    proposal.metadata.epoch = doc.epoch();
    proposal.metadata.base_revision = doc.revision(); // rev 0
    proposal.metadata.job_id = 101;
    proposal.description = "Async worker contour simplification";

    PathObject p;
    p.id = generate_object_id();
    p.name = "Worker Result";
    p.path.move_to({0, 0});
    proposal.commands.push_back(std::make_unique<AddObjectCommand>(p));

    auto status = doc.validate_and_commit_proposal(std::move(proposal));
    assert(status == ProposalStatus::Accepted);
    assert(doc.revision() == 1);
    assert(doc.current_snapshot()->object_count() == 1);
}

void test_stale_proposal_rejected() {
    Document doc("doc-async-2");

    AsyncProposal proposal;
    proposal.metadata.document_id = "doc-async-2";
    proposal.metadata.epoch = doc.epoch();
    proposal.metadata.base_revision = doc.revision(); // rev 0

    // Main thread edits document while worker was working!
    PathObject user_obj;
    user_obj.id = generate_object_id();
    doc.execute_command(std::make_unique<AddObjectCommand>(user_obj));
    assert(doc.revision() == 1); // Document is now at revision 1!

    PathObject worker_obj;
    worker_obj.id = generate_object_id();
    proposal.commands.push_back(std::make_unique<AddObjectCommand>(worker_obj));

    // Proposal submitted with base_revision 0 must be REJECTED!
    auto status = doc.validate_and_commit_proposal(std::move(proposal));
    assert(status == ProposalStatus::StaleRevision);
    assert(doc.revision() == 1);
    assert(doc.current_snapshot()->object_count() == 1);
}

void test_epoch_mismatch_rejected() {
    Document doc("doc-async-3");

    AsyncProposal proposal;
    proposal.metadata.document_id = "doc-async-3";
    proposal.metadata.epoch = 999; // Mismatched epoch
    proposal.metadata.base_revision = 0;

    auto status = doc.validate_and_commit_proposal(std::move(proposal));
    assert(status == ProposalStatus::EpochMismatch);
}

int main() {
    std::cout << "[test_stale_proposals] Running async proposal tests...\n";
    test_valid_proposal();
    test_stale_proposal_rejected();
    test_epoch_mismatch_rejected();
    std::cout << "[test_stale_proposals] All tests passed!\n";
    return 0;
}
