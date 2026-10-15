#include "pikdraw/document/document.hpp"
#include <cassert>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::document;

void test_snapshot_immutability() {
    Document doc("doc-snap-1");

    PathObject p1;
    p1.id = generate_object_id();
    p1.path.move_to({10, 10});
    doc.execute_command(std::make_unique<AddObjectCommand>(p1));

    SnapshotPtr snap1 = doc.current_snapshot();
    assert(snap1->revision() == 1);
    assert(snap1->object_count() == 1);

    // Document mutates
    PathObject p2;
    p2.id = generate_object_id();
    p2.path.move_to({20, 20});
    doc.execute_command(std::make_unique<AddObjectCommand>(p2));

    SnapshotPtr snap2 = doc.current_snapshot();
    assert(snap2->revision() == 2);
    assert(snap2->object_count() == 2);

    // Old snapshot held by reader remains unchanged!
    assert(snap1->revision() == 1);
    assert(snap1->object_count() == 1);
    assert(snap1->contains_object(p1.id));
    assert(!snap1->contains_object(p2.id));
}

void test_listener_notification() {
    Document doc("doc-snap-2");
    uint64_t last_notified_rev = 0;
    size_t notification_count = 0;

    doc.add_snapshot_listener([&](SnapshotPtr snap) {
        last_notified_rev = snap->revision();
        notification_count++;
    });

    PathObject p;
    p.id = generate_object_id();
    doc.execute_command(std::make_unique<AddObjectCommand>(p));

    assert(notification_count == 1);
    assert(last_notified_rev == 1);

    doc.undo();
    assert(notification_count == 2);
    assert(last_notified_rev == 2);
}

int main() {
    std::cout << "[test_snapshots] Running snapshot tests...\n";
    test_snapshot_immutability();
    test_listener_notification();
    std::cout << "[test_snapshots] All tests passed!\n";
    return 0;
}
