#include "test_harness.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/document/persistence.hpp"
#include "pikdraw/geometry/boolean_offset.hpp"
#include <atomic>
#include <limits>
#include <chrono>
#include <map>
#include <random>

using namespace pikdraw::document;
using namespace pikdraw::geometry;

PIK_TEST_CASE(paged_iterator_owns_root_after_map_replacement) {
    auto iterator=[] { PersistentMap<uint64_t,int> map; map.set(7,91); return map.begin(); }();
    PIK_CHECK(iterator->first==7 && iterator->second==91);
    auto held=iterator; ++iterator;
    PersistentMap<uint64_t,int> empty;
    PIK_CHECK(iterator==empty.end());
    PIK_CHECK(held->second==91);
    PersistentMap<uint64_t,int> current; current.set(2,4);
    auto old=current.begin(); current.set(2,5); current.erase(2);
    PIK_CHECK(old->second==4 && current.empty());
}

PIK_TEST_CASE(empty_chunk_is_pruned_but_retained_root_survives) {
    Document doc("prune"); auto object=std::make_shared<PathObject>(); object->id=doc.allocate_id();
    doc.execute_command(std::make_unique<InsertObjectCommand>(object));
    auto retained=doc.create_snapshot();
    doc.execute_command(std::make_unique<RemoveObjectCommand>(object->id));
    PIK_CHECK(doc.create_snapshot()->chunks().empty());
    PIK_CHECK(retained->get_object(object->id));
    PIK_CHECK(doc.undo()); PIK_CHECK(doc.create_snapshot()->get_object(object->id));
}

PIK_TEST_CASE(history_total_remains_bounded_across_undo_redo) {
    Document doc("bounded-history"); auto object=std::make_shared<PathObject>(); object->id=doc.allocate_id();
    doc.execute_command(std::make_unique<InsertObjectCommand>(object));
    for (int i=0;i<600;++i) doc.execute_command(std::make_unique<TransformObjectCommand>(object->id,Transform2D::translation(1,0)));
    int undone=0; while (doc.undo()) ++undone; PIK_CHECK_EQUAL(undone,500);
    int redone=0; while (doc.redo()) ++redone; PIK_CHECK_EQUAL(redone,500);
}

PIK_TEST_CASE(source_curve_storage_is_shared_but_edits_detach) {
    SourcePath original; original.move_to({0,0}); original.line_to({10,20});
    SourcePath copy=original;
    PIK_CHECK(original.segments().data()==copy.segments().data());
    original.set_segment(1,PathSegment::make_line_to({99,100}));
    PIK_CHECK(original.segments().data()!=copy.segments().data());
    PIK_CHECK(copy.segments()[1].p1.x==10 && copy.compute_bounds().max_x==10);
    PIK_CHECK(original.compute_bounds().max_x==99);
    copy.append_segment(copy.segments()[1]); // Alias remains safe if push reallocates.
    PIK_CHECK(copy.segments().back().p1.y==20);
    PIK_CHECK_THROWS(original.line_to({std::numeric_limits<double>::quiet_NaN(),0}));
    PIK_CHECK(original.segments().size()==2);
    Document doc("shared-path"); auto object=std::make_shared<PathObject>(); object->id=doc.allocate_id(); object->path=copy;
    doc.execute_command(std::make_unique<InsertObjectCommand>(object));
    auto before=doc.create_snapshot();
    doc.execute_command(std::make_unique<TransformObjectCommand>(1,Transform2D::translation(5,0)));
    auto after=doc.create_snapshot();
    const auto& a=static_cast<const PathObject&>(*before->get_object(1));
    const auto& b=static_cast<const PathObject&>(*after->get_object(1));
    PIK_CHECK(a.path.segments().data()==b.path.segments().data());
    object->path.line_to({500,500});
    PIK_CHECK(a.path.segments().size()==3 && a.path.compute_bounds().max_x==10);
}

PIK_TEST_CASE(paged_roots_match_independent_map_and_survive_old_copy) {
    PersistentMap<uint64_t,int> map;
    std::map<uint64_t,int> oracle;
    std::mt19937_64 random(34819);
    for (int step=0;step<20000;++step) {
        const uint64_t key=random()%1024;
        auto old=map; const auto old_oracle=oracle;
        if (random()%3==0) PIK_CHECK_EQUAL(map.erase(key),oracle.erase(key));
        else { const int value=static_cast<int>(random()%10000); map.set(key,value); oracle[key]=value; }
        PIK_CHECK_EQUAL(map.size(),oracle.size());
        std::map<uint64_t,int> seen,old_seen;
        for (const auto& pair:map) seen.insert(pair);
        for (const auto& pair:old) old_seen.insert(pair);
        PIK_CHECK(seen==oracle && old_seen==old_oracle);
        if (map.contains(key)) { auto found=map.find(key); PIK_CHECK(found->second==oracle.at(key)); ++found; }
    }
}

PIK_TEST_CASE(rectangle_subset_rejects_nonfinite_and_preserves_failed_output) {
    Rect2D valid(0,0,10,10), invalid=valid; invalid.max_x=std::numeric_limits<double>::infinity();
    PIK_CHECK_THROWS(RectangleBooleanEngine::intersect(valid,invalid));
    PIK_CHECK_THROWS(RectangleBooleanEngine::subtract(invalid,valid));
    PIK_CHECK_THROWS(RectangleBooleanEngine::offset(valid,std::numeric_limits<double>::quiet_NaN()));
    PIK_CHECK_THROWS(RectangleBooleanEngine::offset(valid,-6));
    auto expanded=RectangleBooleanEngine::offset(valid,1);
    PIK_CHECK_NEAR(expanded.width(),12.0,0.0); PIK_CHECK_NEAR(expanded.min_x,-1.0,0.0);
    SourcePath result; result.move_to({99,99});
    PIK_CHECK(!evaluate_general_path_boolean({}, {}, result).ok());
    PIK_CHECK(result.segments().size()==1 && result.segments()[0].p0.x==99);
}

PIK_TEST_CASE(recreated_document_identity_rejects_previous_lifetime) {
    Document old("same-id");
    AsyncJobProposal proposal{old.id(),old.epoch(),old.revision(),old.generation(),1,{}};
    Document replacement("same-id");
    PIK_CHECK(old.epoch()!=replacement.epoch());
    PIK_CHECK(replacement.validate_and_apply_proposal(proposal).reason==ProposalRejectionReason::EpochMismatch);
}

PIK_TEST_CASE(native_reopen_preserves_revision_ids_and_rejects_truncated_data) {
    Document doc("reopen");
    auto object=std::make_shared<PathObject>(); object->id=5000;
    object->path.move_to({20000.005,10}); object->path.line_to({20000.015,10});
    doc.execute_command(std::make_unique<InsertObjectCommand>(object));
    for (int i=0;i<5;++i) doc.execute_command(std::make_unique<TransformObjectCommand>(5000,Transform2D::translation(1,0)));
    const auto snapshot=doc.create_snapshot();
    const auto bytes=PersistenceManager::serialize_to_buffer(*snapshot);
    for (size_t size=0;size<bytes.size();++size)
        PIK_CHECK_THROWS(PersistenceManager::deserialize_from_buffer(std::vector<uint8_t>(bytes.begin(),bytes.begin()+size)));
    const auto file=std::filesystem::current_path()/std::filesystem::path(
        L"native-review-ti\u1ebfng-vi\u1ec7t-"+std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count())+L".pikdraw");
    PIK_CHECK(!std::filesystem::exists(file));
    auto saved=PersistenceManager::save_snapshot(snapshot,file); PIK_CHECK(saved.success);
    auto loaded=PersistenceManager::load_document(file); PIK_CHECK(loaded.success);
    PIK_CHECK_EQUAL(loaded.document->revision(),snapshot->revision());
    PIK_CHECK(loaded.document->epoch()!=snapshot->epoch());
    PIK_CHECK(!loaded.document->is_dirty());
    PIK_CHECK(loaded.document->allocate_id()>5000);
    auto corrupt=bytes; corrupt.back()^=1;
    PIK_CHECK_THROWS(PersistenceManager::deserialize_from_buffer(corrupt));
    // Retain this small synthetic artifact for independent CRC/native format inspection.
}

PIK_TEST_CASE(external_alias_cannot_mutate_committed_snapshot) {
    Document doc("alias");
    auto obj = std::make_shared<PathObject>(); obj->id = doc.allocate_id(); obj->name = "original";
    auto command = std::make_unique<InsertObjectCommand>(obj);
    obj->name = "changed before execute";
    doc.execute_command(std::move(command));
    auto snapshot = doc.create_snapshot();
    obj->name = "changed after execute"; obj->transform.tx = 500;
    PIK_CHECK(snapshot->get_object(obj->id)->name == "original");
    PIK_CHECK_NEAR(snapshot->get_object(obj->id)->transform.tx, 0.0, 0.0);
    PIK_CHECK_THROWS(doc.internal_put_object(obj));
}

PIK_TEST_CASE(proposal_identity_atomicity_and_undo) {
    Document doc("proposal");
    for (int i=0; i<2; ++i) { auto p=std::make_shared<PathObject>(); p->id=doc.allocate_id();
        doc.execute_command(std::make_unique<InsertObjectCommand>(p)); }
    auto before=doc.create_snapshot();
    AsyncJobProposal p{doc.id(),doc.epoch(),doc.revision(),doc.generation(),42,{}};
    auto first=before->get_object(1)->clone(); first->transform.tx=10;
    auto second=before->get_object(2)->clone(); second->transform.tx=std::numeric_limits<double>::quiet_NaN();
    p.updated_objects={first,second};
    PIK_CHECK(!doc.validate_and_apply_proposal(p).accepted);
    PIK_CHECK(doc.create_snapshot() == before); // Failed second update publishes neither object.
    p.updated_objects={first}; p.doc_id="other";
    PIK_CHECK(doc.validate_and_apply_proposal(p).reason == ProposalRejectionReason::DocumentMismatch);
    p.doc_id=doc.id(); p.updated_objects={first,nullptr};
    PIK_CHECK(!doc.validate_and_apply_proposal(p).accepted);
    p.updated_objects={first,first}; PIK_CHECK(!doc.validate_and_apply_proposal(p).accepted);
    p.updated_objects={first}; doc.cancel_job(42); PIK_CHECK(!doc.validate_and_apply_proposal(p).accepted);
    p.job_id=43; PIK_CHECK(doc.validate_and_apply_proposal(p).accepted);
    first->transform.tx=999;
    PIK_CHECK_NEAR(doc.create_snapshot()->get_object(1)->transform.tx,10.0,0.0);
    PIK_CHECK(doc.undo()); PIK_CHECK_NEAR(doc.create_snapshot()->get_object(1)->transform.tx,0.0,0.0);
    PIK_CHECK(doc.redo()); PIK_CHECK_NEAR(doc.create_snapshot()->get_object(1)->transform.tx,10.0,0.0);
    PIK_CHECK(!doc.validate_and_apply_proposal(p).accepted);
}

PIK_TEST_CASE(readers_see_only_complete_root_and_index) {
    Document doc("readers");
    auto obj=std::make_shared<PathObject>(); obj->id=doc.allocate_id();
    doc.execute_command(std::make_unique<InsertObjectCommand>(obj));
    std::atomic<bool> done{false}, bad{false};
    std::thread reader([&] {
        Revision last=0;
        while (!done.load()) {
            auto snapshot=doc.create_snapshot();
            auto object=snapshot->get_object(1);
            const auto& bounds=snapshot->spatial_index().at(1);
            if (snapshot->revision()<last || !object || object->transform.tx!=bounds.min_x) bad=true;
            last=snapshot->revision();
        }
    });
    for (int i=0;i<1000;++i) doc.execute_command(std::make_unique<TransformObjectCommand>(1,Transform2D::translation(1,0)));
    done=true; reader.join(); PIK_CHECK(!bad.load());
}

PIK_TEST_CASE(manufacturing_zero_chord_and_overshoot_are_not_flattened_away) {
    SourcePath loop; loop.move_to({0,0}); loop.cubic_to({10,10},{-10,10},{0,0}); loop.close();
    auto result=discretize_for_manufacturing(loop,0.01);
    PIK_CHECK(result.polyline.size()>4);
    double max_y=0; for (auto pt:result.polyline) max_y=std::max(max_y,pt.y);
    PIK_CHECK_NEAR(max_y,7.5,0.01); // Independent analytic B_y(0.5).
    SourcePath overshoot; overshoot.move_to({0,0}); overshoot.cubic_to({10,0},{10,0},{1,0});
    auto line=discretize_for_manufacturing(overshoot,0.01);
    double max_x=0; for (auto pt:line.polyline) max_x=std::max(max_x,pt.x);
    PIK_CHECK(max_x>7.0);
    PIK_CHECK_THROWS(discretize_for_manufacturing(loop, std::numeric_limits<double>::quiet_NaN()));
    PIK_CHECK_THROWS(discretize_for_manufacturing(loop,-1));
    loop.move_to({100,100}); PIK_CHECK_THROWS(discretize_for_manufacturing(loop,0.01));
}
