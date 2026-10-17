#include "pikdraw/document/document.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <utility>

using namespace pikdraw;
using namespace pikdraw::document;

namespace {

PathObject make_path_with_effect(ObjectId object_id, EffectInstanceId effect_id) {
    PathObject path;
    path.id = object_id;
    EffectNode effect;
    effect.id = effect_id;
    path.appearance.effects.push_back(effect);
    return path;
}

PathObject make_editable_path_with_effect(ObjectId object_id, uint64_t subpath_id,
                                          uint64_t first_node_id, uint64_t second_node_id,
                                          uint64_t segment_id, EffectInstanceId effect_id) {
    PathObject path;
    path.id = object_id;
    EditablePath source;
    EditableSubpath subpath;
    subpath.id = SubpathId{subpath_id};
    subpath.nodes = {
        PathNode{NodeId{first_node_id}, {0.0, 0.0}},
        PathNode{NodeId{second_node_id}, {10.0, 0.0}},
    };
    subpath.segments = {EditableSegment{SegmentId{segment_id}, PathSegmentKind::Line}};
    source.subpaths.push_back(std::move(subpath));
    path.path = editable_path_to_path2d(source);
    path.set_authoring_source(std::move(source));
    EffectNode effect;
    effect.id = effect_id;
    path.appearance.effects.push_back(effect);
    return path;
}

void expect_snapshot_rejected(Page page, PersistentObjectMap objects) {
    bool rejected = false;
    try {
        DocumentSnapshot snapshot{1, 0, std::move(page), std::move(objects)};
        (void)snapshot;
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}

} // namespace

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

void test_effect_ids_share_document_stable_id_namespace() {
    // An effect must not reuse another object's stable ID, regardless of map order.
    {
        const ObjectId effect_owner_id = 70001;
        const ObjectId colliding_object_id = 70002;
        auto owner = make_path_with_effect(effect_owner_id, colliding_object_id);
        PathObject other;
        other.id = colliding_object_id;
        PersistentObjectMap objects;
        objects = objects.set(owner.id, owner);
        objects = objects.set(other.id, other);
        auto page = Page::make_default();
        page.layers.front().object_ids = {owner.id, other.id};
        expect_snapshot_rejected(std::move(page), std::move(objects));
    }

    // All editable path element kinds use the same namespace as effect instances.
    const struct {
        uint64_t subpath_id;
        uint64_t first_node_id;
        uint64_t second_node_id;
        uint64_t segment_id;
        EffectInstanceId effect_id;
    } path_collisions[] = {
        {71002, 71003, 71004, 71005, 71002},
        {72002, 72003, 72004, 72005, 72003},
        {73002, 73003, 73004, 73005, 73005},
    };
    for (size_t i = 0; i < std::size(path_collisions); ++i) {
        const ObjectId object_id = 71001 + static_cast<ObjectId>(i) * 1000;
        auto path = make_editable_path_with_effect(
            object_id, path_collisions[i].subpath_id, path_collisions[i].first_node_id,
            path_collisions[i].second_node_id, path_collisions[i].segment_id,
            path_collisions[i].effect_id);
        PersistentObjectMap objects;
        objects = objects.set(path.id, path);
        auto page = Page::make_default();
        page.layers.front().object_ids.push_back(path.id);
        expect_snapshot_rejected(std::move(page), std::move(objects));
    }

    // Duplicate effect IDs across different objects are also rejected.
    {
        const EffectInstanceId shared_effect_id = 74003;
        auto first = make_path_with_effect(74001, shared_effect_id);
        auto second = make_path_with_effect(74002, shared_effect_id);
        PersistentObjectMap objects;
        objects = objects.set(first.id, first);
        objects = objects.set(second.id, second);
        auto page = Page::make_default();
        page.layers.front().object_ids = {first.id, second.id};
        expect_snapshot_rejected(std::move(page), std::move(objects));
    }

    // Text and path objects participate in the same effect ID namespace.
    {
        constexpr EffectInstanceId shared_effect_id = 76003;
        auto path = make_path_with_effect(76001, shared_effect_id);
        TextObject text;
        text.id = 76002;
        EffectNode effect;
        effect.id = shared_effect_id;
        text.appearance.effects.push_back(effect);
        PersistentObjectMap objects;
        objects = objects.set(path.id, path);
        objects = objects.set(text.id, text);
        auto page = Page::make_default();
        page.layers.front().object_ids = {path.id, text.id};
        expect_snapshot_rejected(std::move(page), std::move(objects));
    }

    // A distinct ID remains valid through full snapshot validation.
    {
        auto path = make_path_with_effect(75001, 75002);
        PersistentObjectMap objects;
        objects = objects.set(path.id, path);
        auto page = Page::make_default();
        page.layers.front().object_ids.push_back(path.id);
        DocumentSnapshot snapshot{1, 0, std::move(page), std::move(objects)};
        assert(snapshot.object_count() == 1);
    }
}

void test_document_effect_node_limit() {
    constexpr size_t total_effects = kMaxEffectsPerDocument;
    size_t effects_added = 0;
    ObjectId next_stable_id = 800'000;
    PersistentObjectMap objects;
    auto page = Page::make_default();
    while (effects_added < total_effects) {
        PathObject path;
        path.id = next_stable_id++;
        const size_t batch_size = std::min(kMaxEffectsPerObject, total_effects - effects_added);
        path.appearance.effects.reserve(batch_size);
        for (size_t i = 0; i < batch_size; ++i) {
            EffectNode effect;
            effect.id = next_stable_id++;
            path.appearance.effects.push_back(effect);
        }
        page.layers.front().object_ids.push_back(path.id);
        objects = objects.set(path.id, std::move(path));
        effects_added += batch_size;
    }
    DocumentSnapshot at_limit{1, 0, page, objects};
    assert(at_limit.object_count() == page.layers.front().object_ids.size());

    PathObject over_limit;
    over_limit.id = next_stable_id++;
    EffectNode effect;
    effect.id = next_stable_id++;
    over_limit.appearance.effects.push_back(effect);
    page.layers.front().object_ids.push_back(over_limit.id);
    objects = objects.set(over_limit.id, std::move(over_limit));
    expect_snapshot_rejected(std::move(page), std::move(objects));
}

int main() {
    std::cout << "[test_snapshots] Running snapshot tests...\n";
    test_snapshot_immutability();
    test_listener_notification();
    test_effect_ids_share_document_stable_id_namespace();
    test_document_effect_node_limit();
    std::cout << "[test_snapshots] All tests passed!\n";
    return 0;
}
