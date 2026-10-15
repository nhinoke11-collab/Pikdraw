#include "test_harness.hpp"
#include "pikdraw/document/document.hpp"
#include <random>
#include <map>
#include <deque>
#include <chrono>
using namespace pikdraw::document;
using namespace pikdraw::geometry;

PIK_TEST_CASE(trace_10k_seeded_commands) {
    Document doc("doc-10k-trace");
    std::mt19937_64 random(42);
    using Oracle = std::map<ObjectId, std::pair<double,double>>;
    Oracle expected;
    std::deque<Oracle> undo, redo;
    struct Retained { std::shared_ptr<const DocumentSnapshot> snapshot; Oracle expected; };
    std::vector<Retained> retained;
    auto record_edit = [&] { undo.push_back(expected); if (undo.size()>500) undo.pop_front(); redo.clear(); };
    const auto start=std::chrono::steady_clock::now();
    for (int step=0;step<10000;++step) {
        unsigned op=static_cast<unsigned>(random()%100);
        if ((op<15 && !undo.empty())) {
            PIK_CHECK(doc.undo()); redo.push_back(expected); expected=undo.back(); undo.pop_back();
        } else if (op<25 && !redo.empty()) {
            PIK_CHECK(doc.redo()); undo.push_back(expected); expected=redo.back(); redo.pop_back();
        } else if (expected.empty() || (op<50 && expected.size()<64)) {
            auto object=std::make_shared<PathObject>(); object->id=doc.allocate_id();
            record_edit(); expected[object->id]={0,0};
            doc.execute_command(std::make_unique<InsertObjectCommand>(object));
        } else {
            auto target=expected.begin(); std::advance(target,random()%expected.size());
            const auto id=target->first;
            record_edit();
            if (op>=95) {
                expected.erase(id); doc.execute_command(std::make_unique<RemoveObjectCommand>(id));
            } else {
                expected[id].first+=1.5; expected[id].second-=0.5;
                doc.execute_command(std::make_unique<TransformObjectCommand>(id,Transform2D::translation(1.5,-0.5)));
            }
        }
        auto snapshot=doc.create_snapshot();
        PIK_CHECK_EQUAL(snapshot->revision(),static_cast<Revision>(step+1));
        PIK_CHECK_EQUAL(snapshot->object_count(),expected.size());
        for (const auto& [id,xy]:expected) {
            auto object=snapshot->get_object(id); PIK_CHECK(object);
            PIK_CHECK_NEAR(object->transform.tx,xy.first,0.0);
            PIK_CHECK_NEAR(object->transform.ty,xy.second,0.0);
            const auto& bounds=snapshot->spatial_index().at(id);
            PIK_CHECK_NEAR(bounds.min_x,xy.first,0.0); PIK_CHECK_NEAR(bounds.min_y,xy.second,0.0);
        }
        if (step%1000==0) retained.push_back({snapshot,expected});
    }
    for (const auto& history:retained) {
        PIK_CHECK_EQUAL(history.snapshot->object_count(),history.expected.size());
        for (const auto& [id,xy]:history.expected) {
            auto object=history.snapshot->get_object(id); PIK_CHECK(object);
            PIK_CHECK_NEAR(object->transform.tx,xy.first,0.0);
            PIK_CHECK_NEAR(object->transform.ty,xy.second,0.0);
        }
    }
    std::cout << "TRACE seed=42 commits=10000 max_live_objects=64 retained_snapshots=" << retained.size()
              << " elapsed_ms=" << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()
              << " (correctness workload; not a document latency benchmark)\n";
}
