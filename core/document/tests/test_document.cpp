#include "test_harness.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/document/object.hpp"

using namespace pikdraw::document;
using namespace pikdraw::geometry;

PIK_TEST_CASE(single_writer_thread_guard) {
    Document doc("doc-writer-test");
    auto obj = std::make_shared<PathObject>();
    obj->id = doc.allocate_id();

    std::string error_msg;
    std::thread other_thread([&]() {
        try {
            doc.execute_command(std::make_unique<InsertObjectCommand>(obj));
        } catch (const std::exception& e) {
            error_msg = e.what();
        }
    });
    other_thread.join();
    PIK_CHECK(!error_msg.empty());
}

PIK_TEST_CASE(monotonic_revision_and_undo_redo) {
    Document doc("doc-undo-test");
    PIK_CHECK_EQUAL(doc.revision(), 0);

    auto obj = std::make_shared<PathObject>();
    obj->id = doc.allocate_id();

    doc.execute_command(std::make_unique<InsertObjectCommand>(obj));
    PIK_CHECK_EQUAL(doc.revision(), 1);
    PIK_CHECK(doc.is_dirty());

    doc.execute_command(std::make_unique<TransformObjectCommand>(obj->id, Transform2D::translation(10, 0)));
    PIK_CHECK_EQUAL(doc.revision(), 2);

    // Undo must strictly increment revision
    bool undone = doc.undo();
    PIK_CHECK(undone);
    PIK_CHECK_EQUAL(doc.revision(), 3);

    // Redo must strictly increment revision
    bool redone = doc.redo();
    PIK_CHECK(redone);
    PIK_CHECK_EQUAL(doc.revision(), 4);
}

PIK_TEST_CASE(vector_undo_stale_proposal) {
    // Contract vector: undo-stale-proposal
    // snapshot r=10 epoch=1 gen=1 -> edit -> 11 -> undo -> 12 -> worker returns base=10 -> expected: reject
    Document doc("doc-stale-test");
    auto obj = std::make_shared<PathObject>();
    obj->id = doc.allocate_id();
    doc.execute_command(std::make_unique<InsertObjectCommand>(obj));

    // Advance to r=10
    for (int i = 0; i < 9; ++i) {
        doc.execute_command(std::make_unique<TransformObjectCommand>(obj->id, Transform2D::translation(1, 0)));
    }
    PIK_CHECK_EQUAL(doc.revision(), 10);
    const auto snap10 = doc.create_snapshot();
    PIK_CHECK_EQUAL(snap10->revision(), 10);

    // Edit -> 11
    doc.execute_command(std::make_unique<TransformObjectCommand>(obj->id, Transform2D::translation(1, 0)));
    PIK_CHECK_EQUAL(doc.revision(), 11);

    // Undo -> 12
    doc.undo();
    PIK_CHECK_EQUAL(doc.revision(), 12);

    // Worker proposal with base_revision=10
    AsyncJobProposal prop;
    prop.doc_id = doc.id();
    prop.epoch = doc.epoch();
    prop.base_revision = 10;
    prop.generation = doc.generation();
    prop.job_id = 101;

    auto res = doc.validate_and_apply_proposal(prop);
    PIK_CHECK(!res.accepted);
    PIK_CHECK(res.reason == ProposalRejectionReason::StaleBaseRevision);
}

PIK_TEST_CASE(vector_close_reopen_stale_proposal) {
    // Contract vector: close-reopen-stale-proposal
    // snapshot r=10 epoch=1 gen=1 -> close/reopen epoch=2 -> worker returns epoch=1 -> expected: reject
    Document doc("doc-epoch-test");
    const auto old_epoch=doc.epoch();

    AsyncJobProposal prop;
    prop.doc_id = doc.id();
    prop.epoch = old_epoch;
    prop.base_revision = doc.revision();
    prop.generation = doc.generation();
    prop.job_id = 202;

    doc.close_and_reopen();
    PIK_CHECK(doc.epoch() != old_epoch);

    auto res = doc.validate_and_apply_proposal(prop);
    PIK_CHECK(!res.accepted);
    PIK_CHECK(res.reason == ProposalRejectionReason::EpochMismatch);
}

PIK_TEST_CASE(chunk_cow_and_snapshot_immutability) {
    Document doc("doc-cow-test");
    auto o1 = std::make_shared<PathObject>(); o1->id = doc.allocate_id();
    auto o2 = std::make_shared<PathObject>(); o2->id = doc.allocate_id();
    doc.execute_command(std::make_unique<InsertObjectCommand>(o1));
    doc.execute_command(std::make_unique<InsertObjectCommand>(o2));

    auto snap1 = doc.create_snapshot();
    PIK_CHECK_EQUAL(snap1->object_count(), 2);

    // Modify o1
    doc.execute_command(std::make_unique<TransformObjectCommand>(o1->id, Transform2D::translation(50, 50)));
    auto snap2 = doc.create_snapshot();

    // Snapshot 1 remains unchanged
    auto s1_obj = snap1->get_object(o1->id);
    PIK_CHECK_NEAR(s1_obj->transform.tx, 0.0, 1e-9);
    auto s2_obj = snap2->get_object(o1->id);
    PIK_CHECK_NEAR(s2_obj->transform.tx, 50.0, 1e-9);
}
