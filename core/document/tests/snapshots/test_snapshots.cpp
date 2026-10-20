#include "pikdraw/document/document.hpp"
#include "pikdraw/document/commands/page_commands.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <iterator>
#include <limits>
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

void test_page_roots_are_isolated_and_undoable() {
    Document doc("doc-pages-test");
    const auto first_page_id = doc.active_page_id();
    auto second_page = make_document_page("Page 2", Page::make_default());
    const auto second_page_id = second_page.id;
    assert(doc.execute_command(std::make_unique<AddPageCommand>(second_page)).success);
    assert(doc.current_snapshot()->page_count() == 2);
    assert(doc.active_page_id() == second_page_id);

    PathObject second_object;
    second_object.id = generate_object_id();
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(second_object)).success);
    const auto revision_after_edit = doc.revision();
    assert(doc.set_active_page(first_page_id));
    assert(doc.revision() == revision_after_edit);
    assert(doc.is_dirty());
    assert(doc.objects().empty());
    assert(doc.current_snapshot()->find_page(second_page_id)->objects.contains(second_object.id));

    assert(doc.execute_command(std::make_unique<RemovePageCommand>(second_page_id)).success);
    assert(doc.current_snapshot()->page_count() == 1);
    assert(doc.undo());
    assert(doc.current_snapshot()->page_count() == 2);
    assert(doc.current_snapshot()->find_page(second_page_id)->objects.contains(second_object.id));
    assert(doc.redo());
    assert(doc.current_snapshot()->page_count() == 1);
}

void test_stable_object_ids_are_unique_across_pages() {
    constexpr ObjectId duplicate_id = 900001;
    PathObject first_object;
    first_object.id = duplicate_id;
    PathObject second_object;
    second_object.id = duplicate_id;
    auto first = make_document_page("Page 1");
    auto second = make_document_page("Page 2");
    first.layout.layers.front().object_ids.push_back(duplicate_id);
    first.objects = first.objects.set(duplicate_id, first_object);
    second.layout.layers.front().object_ids.push_back(duplicate_id);
    second.objects = second.objects.set(duplicate_id, second_object);
    bool rejected = false;
    try {
        DocumentSnapshot snapshot{1, 0, std::vector<DocumentPage>{first, second},
                                  first.id};
        (void)snapshot;
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}

void test_page_commands_rename_reorder_and_duplicate_roots() {
    Document doc("doc-page-command-test");
    const auto first_id = doc.active_page_id();
    PathObject source;
    source.id = generate_object_id();
    source.name = "Gradient path";
    source.path.move_to({1.0, 2.0});
    source.path.line_to({12.0, 2.0});
    source.appearance.effects.push_back(EffectNode{generate_effect_instance_id()});
    auto gradient = std::make_shared<GradientPaint>();
    gradient->id = generate_object_id();
    gradient->stops = {
        GradientStop{generate_object_id(), 0.0, color::RGBColor{1, 0, 0, 1}, 1.0, 0.5},
        GradientStop{generate_object_id(), 1.0, color::RGBColor{0, 0, 1, 1}, 1.0, 0.5}};
    source.fill_gradient = gradient;
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(source)).success);

    const auto second = make_document_page("Page 2");
    assert(doc.execute_command(std::make_unique<AddPageCommand>(second, false)).success);
    assert(doc.execute_command(std::make_unique<RenamePageCommand>(second.id, "Artwork two")).success);
    assert(doc.current_snapshot()->find_page(second.id)->name == "Artwork two");
    assert(doc.undo());
    assert(doc.current_snapshot()->find_page(second.id)->name == "Page 2");

    assert(doc.execute_command(std::make_unique<ReorderPageCommand>(second.id, 0)).success);
    assert(doc.current_snapshot()->pages().front().id == second.id);
    assert(doc.undo());
    assert(doc.current_snapshot()->pages().front().id == first_id);

    assert(doc.execute_command(std::make_unique<DuplicatePageCommand>(first_id)).success);
    const auto snapshot = doc.current_snapshot();
    assert(snapshot->page_count() == 3);
    assert(snapshot->active_page_id() != first_id);
    const auto* duplicate = snapshot->find_page(snapshot->active_page_id());
    assert(duplicate && duplicate->name == "Page 1 copy");
    const auto duplicate_id = duplicate->layout.layers.front().object_ids.front();
    assert(duplicate_id != source.id);
    const auto original_object = snapshot->find_page(first_id)->objects.get(source.id);
    const auto duplicate_object = duplicate->objects.get(duplicate_id);
    assert(original_object && duplicate_object);
    const auto& original_path = std::get<PathObject>(*original_object);
    const auto& duplicate_path = std::get<PathObject>(*duplicate_object);
    assert(duplicate_path.appearance.effects.front().id != original_path.appearance.effects.front().id);
    assert(duplicate_path.fill_gradient->id != original_path.fill_gradient->id);
    assert(duplicate_path.fill_gradient->stops.front().id != original_path.fill_gradient->stops.front().id);
    assert(doc.undo());
    assert(doc.current_snapshot()->page_count() == 2);
}

void test_resize_page_preserves_artwork_and_rejects_invalid_dimensions() {
    Document doc("doc-page-resize-test");
    const auto page_id = doc.active_page_id();
    const auto original = doc.current_snapshot()->find_page(page_id)->layout;
    PathObject object;
    object.id = generate_object_id();
    object.path.move_to({10.0, 20.0});
    object.path.line_to({40.0, 20.0});
    object.transform.tx = 7.5;
    object.transform.ty = -3.25;
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(object)).success);

    const auto before = doc.current_snapshot();
    const auto original_revision = doc.revision();
    assert(doc.execute_command(
        std::make_unique<ResizePageCommand>(page_id, 420.0, 297.0)).success);
    const auto resized = doc.current_snapshot();
    const auto* resized_page = resized->find_page(page_id);
    assert(resized_page && resized_page->layout.width_mm == 420.0 &&
           resized_page->layout.height_mm == 297.0);
    assert(resized->active_page_id() == page_id);
    const auto resized_object = resized_page->objects.get(object.id);
    assert(resized_object && std::holds_alternative<PathObject>(*resized_object));
    const auto& resized_path = std::get<PathObject>(*resized_object);
    assert(resized_path.transform.tx == 7.5 && resized_path.transform.ty == -3.25);
    assert(doc.revision() == original_revision + 1);

    const auto no_change_revision = doc.revision();
    const auto no_change = doc.execute_command(
        std::make_unique<ResizePageCommand>(page_id, 420.0, 297.0));
    assert(no_change.success && no_change.is_no_change);
    assert(doc.revision() == no_change_revision);

    assert(doc.undo());
    const auto* undone = doc.current_snapshot()->find_page(page_id);
    assert(undone && undone->layout.width_mm == original.width_mm &&
           undone->layout.height_mm == original.height_mm);
    assert(undone->objects.get(object.id).has_value());
    assert(doc.redo());
    assert(doc.current_snapshot()->find_page(page_id)->layout.width_mm == 420.0);

    const auto stable_snapshot = doc.current_snapshot();
    const auto stable_revision = doc.revision();
    const auto invalid = doc.execute_command(std::make_unique<ResizePageCommand>(
        page_id, std::numeric_limits<double>::quiet_NaN(), 300.0));
    assert(!invalid.success);
    assert(doc.revision() == stable_revision);
    assert(doc.current_snapshot() == stable_snapshot);
    assert(doc.current_snapshot()->find_page(page_id)->layout.width_mm == 420.0);
    assert(before->find_page(page_id)->layout.width_mm == original.width_mm);
}

int main() {
    std::cout << "[test_snapshots] Running snapshot tests...\n";
    test_snapshot_immutability();
    test_listener_notification();
    test_effect_ids_share_document_stable_id_namespace();
    test_document_effect_node_limit();
    test_page_roots_are_isolated_and_undoable();
    test_stable_object_ids_are_unique_across_pages();
    test_page_commands_rename_reorder_and_duplicate_roots();
    test_resize_page_preserves_artwork_and_rejects_invalid_dimensions();
    std::cout << "[test_snapshots] All tests passed!\n";
    return 0;
}
