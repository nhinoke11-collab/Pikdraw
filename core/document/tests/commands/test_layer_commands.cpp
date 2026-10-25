#include "pikdraw/document/document.hpp"
#include "pikdraw/document/commands/layer_commands.hpp"
#include "pikdraw/document/commands/command.hpp"
#include <cassert>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::document;

static PathObject make_test_path(ObjectId id, const std::string& name = "Path 1") {
    PathObject obj;
    obj.id = id;
    obj.name = name;
    obj.visible = true;
    obj.locked = false;
    return obj;
}

void test_add_remove_layer() {
    Document doc;
    assert(doc.page().layers.size() == 1);

    auto add_cmd = std::make_unique<AddLayerCommand>("Background");
    auto res = doc.execute_command(std::move(add_cmd));
    assert(res.success);
    assert(doc.page().layers.size() == 2);
    assert(doc.page().layers[1].name == "Background");

    assert(doc.undo());
    assert(doc.page().layers.size() == 1);

    assert(doc.redo());
    assert(doc.page().layers.size() == 2);
    assert(doc.page().layers[1].name == "Background");

    // Remove layer
    auto rem_cmd = std::make_unique<RemoveLayerCommand>(1);
    res = doc.execute_command(std::move(rem_cmd));
    assert(res.success);
    assert(doc.page().layers.size() == 1);

    assert(doc.undo());
    assert(doc.page().layers.size() == 2);
    assert(doc.page().layers[1].name == "Background");

    // Cannot remove last remaining layer
    auto rem_last1 = std::make_unique<RemoveLayerCommand>(0);
    assert(doc.execute_command(std::move(rem_last1)).success);
    assert(doc.page().layers.size() == 1);

    auto rem_last2 = std::make_unique<RemoveLayerCommand>(0);
    auto fail_res = doc.execute_command(std::move(rem_last2));
    assert(!fail_res.success);
}

void test_rename_visibility_lock() {
    Document doc;
    // Rename
    auto rename_cmd = std::make_unique<RenameLayerCommand>(0, "Artwork");
    assert(doc.execute_command(std::move(rename_cmd)).success);
    assert(doc.page().layers[0].name == "Artwork");
    assert(doc.undo());
    assert(doc.page().layers[0].name == "Layer 1");
    assert(doc.redo());
    assert(doc.page().layers[0].name == "Artwork");

    // Visibility
    auto vis_cmd = std::make_unique<SetLayerVisibilityCommand>(0, false);
    assert(doc.execute_command(std::move(vis_cmd)).success);
    assert(!doc.page().layers[0].visible);
    assert(doc.undo());
    assert(doc.page().layers[0].visible);

    // Lock
    auto lock_cmd = std::make_unique<SetLayerLockedCommand>(0, true);
    assert(doc.execute_command(std::move(lock_cmd)).success);
    assert(doc.page().layers[0].locked);
    assert(doc.undo());
    assert(!doc.page().layers[0].locked);
}

void test_reorder_and_move_objects() {
    Document doc;
    auto add_layer = std::make_unique<AddLayerCommand>("Layer 2");
    assert(doc.execute_command(std::move(add_layer)).success);

    ObjectId id1 = 1001;
    ObjectId id2 = 1002;
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(make_test_path(id1, "Shape 1"), 0)).success);
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(make_test_path(id2, "Shape 2"), 0)).success);

    assert(doc.page().layers[0].object_ids.size() == 2);
    assert(doc.page().layers[1].object_ids.empty());

    // Move id2 to Layer 2
    auto move_cmd = std::make_unique<MoveObjectToLayerCommand>(id2, 1);
    assert(doc.execute_command(std::move(move_cmd)).success);
    assert(doc.page().layers[0].object_ids.size() == 1);
    assert(doc.page().layers[1].object_ids.size() == 1);
    assert(doc.page().layers[1].object_ids[0] == id2);

    assert(doc.undo());
    assert(doc.page().layers[0].object_ids.size() == 2);
    assert(doc.page().layers[1].object_ids.empty());

    // Reorder layers
    auto reorder_l = std::make_unique<ReorderLayerCommand>(0, 1);
    assert(doc.execute_command(std::move(reorder_l)).success);
    assert(doc.page().layers[0].name == "Layer 2");
    assert(doc.page().layers[1].name == "Layer 1");
    assert(doc.undo());
    assert(doc.page().layers[0].name == "Layer 1");
}

void test_object_zorder_and_properties() {
    Document doc;
    ObjectId id1 = 2001;
    ObjectId id2 = 2002;
    ObjectId id3 = 2003;
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(make_test_path(id1, "A"), 0)).success);
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(make_test_path(id2, "B"), 0)).success);
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(make_test_path(id3, "C"), 0)).success);

    // Initial order: [id1, id2, id3]
    assert(doc.page().layers[0].object_ids[0] == id1);
    assert(doc.page().layers[0].object_ids[1] == id2);
    assert(doc.page().layers[0].object_ids[2] == id3);

    // Send id3 to back -> [id3, id1, id2]
    auto send_back = std::make_unique<ReorderObjectInLayerCommand>(id3, ReorderObjectInLayerCommand::Step::SendToBack);
    assert(doc.execute_command(std::move(send_back)).success);
    assert(doc.page().layers[0].object_ids[0] == id3);
    assert(doc.page().layers[0].object_ids[1] == id1);
    assert(doc.page().layers[0].object_ids[2] == id2);

    assert(doc.undo());
    assert(doc.page().layers[0].object_ids[2] == id3);

    // Object visibility and lock
    assert(doc.execute_command(std::make_unique<SetObjectVisibilityCommand>(id1, false)).success);
    auto opt1 = doc.objects().get(id1);
    assert(opt1 && !std::visit([](const auto& o) { return o.visible; }, *opt1));
    assert(doc.undo());
    opt1 = doc.objects().get(id1);
    assert(opt1 && std::visit([](const auto& o) { return o.visible; }, *opt1));

    // Object rename
    assert(doc.execute_command(std::make_unique<RenameObjectCommand>(id1, "Custom Shape")).success);
    opt1 = doc.objects().get(id1);
    assert(opt1 && std::visit([](const auto& o) { return o.name; }, *opt1) == "Custom Shape");
    assert(doc.undo());
    opt1 = doc.objects().get(id1);
    assert(opt1 && std::visit([](const auto& o) { return o.name; }, *opt1) == "A");
}

void test_printable_and_color() {
    Document doc;
    assert(doc.page().layers[0].printable == true);
    assert(doc.page().layers[0].color_rgba == 0x00F2C3FF);

    // Printable toggle
    auto print_cmd = std::make_unique<SetLayerPrintableCommand>(0, false);
    assert(doc.execute_command(std::move(print_cmd)).success);
    assert(!doc.page().layers[0].printable);
    assert(doc.undo());
    assert(doc.page().layers[0].printable);
    assert(doc.redo());
    assert(!doc.page().layers[0].printable);

    // Color change
    uint32_t coral_red = 0xEF4444FF;
    auto color_cmd = std::make_unique<SetLayerColorCommand>(0, coral_red);
    assert(doc.execute_command(std::move(color_cmd)).success);
    assert(doc.page().layers[0].color_rgba == coral_red);
    assert(doc.undo());
    assert(doc.page().layers[0].color_rgba == 0x00F2C3FF);
    assert(doc.redo());
    assert(doc.page().layers[0].color_rgba == coral_red);
}

void test_duplicate_and_move_multiple() {
    Document doc;
    ObjectId id1 = 3001;
    ObjectId id2 = 3002;
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(make_test_path(id1, "Path A"), 0)).success);
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(make_test_path(id2, "Path B"), 0)).success);
    assert(doc.page().layers[0].object_ids.size() == 2);

    // Duplicate Layer 0
    auto dup_cmd = std::make_unique<DuplicateLayerCommand>(0);
    assert(doc.execute_command(std::move(dup_cmd)).success);
    assert(doc.page().layers.size() == 2);
    assert(doc.page().layers[1].name == "Layer 1 Copy");
    assert(doc.page().layers[1].object_ids.size() == 2);
    ObjectId dup_id1 = doc.page().layers[1].object_ids[0];
    ObjectId dup_id2 = doc.page().layers[1].object_ids[1];
    assert(dup_id1 != id1 && dup_id1 != kInvalidObjectId);
    assert(dup_id2 != id2 && dup_id2 != kInvalidObjectId);
    assert(doc.objects().contains(dup_id1));
    assert(doc.objects().contains(dup_id2));

    // Test undo duplicate
    assert(doc.undo());
    assert(doc.page().layers.size() == 1);
    assert(!doc.objects().contains(dup_id1));
    assert(!doc.objects().contains(dup_id2));

    // Test redo duplicate
    assert(doc.redo());
    assert(doc.page().layers.size() == 2);
    assert(doc.objects().contains(dup_id1));
    assert(doc.objects().contains(dup_id2));

    // Move multiple objects from Layer 1 to Layer 0
    auto move_multi = std::make_unique<MoveObjectsToLayerCommand>(std::vector<ObjectId>{dup_id1, dup_id2}, 0);
    assert(doc.execute_command(std::move(move_multi)).success);
    assert(doc.page().layers[0].object_ids.size() == 4);
    assert(doc.page().layers[1].object_ids.empty());

    // Undo move multiple
    assert(doc.undo());
    assert(doc.page().layers[0].object_ids.size() == 2);
    assert(doc.page().layers[1].object_ids.size() == 2);
    assert(doc.page().layers[1].object_ids[0] == dup_id1);
    assert(doc.page().layers[1].object_ids[1] == dup_id2);

    // Same-layer insertion supports reorder and its Undo restores exact order.
    assert(doc.execute_command(std::make_unique<MoveObjectsToLayerCommand>(
        std::vector<ObjectId>{id1}, 0, 2)).success);
    assert(doc.page().layers[0].object_ids[0] == id2);
    assert(doc.page().layers[0].object_ids[1] == id1);
    assert(doc.undo());
    assert(doc.page().layers[0].object_ids[0] == id1);
    assert(doc.page().layers[0].object_ids[1] == id2);
}

void test_duplicate_layer_deep_clones_groups() {
    Page page = Page::make_default();
    PathObject child = make_test_path(4101, "Nested path");
    GroupObject group;
    group.id = 4102;
    group.name = "Nested group";
    group.children_ids = {child.id};
    page.layers[0].object_ids = {group.id};
    PersistentObjectMap objects;
    objects = objects.set(child.id, child);
    objects = objects.set(group.id, group);

    const DocumentSnapshot source{1, 0, page, objects, "deep-layer-copy"};
    Document doc(source);
    assert(doc.execute_command(std::make_unique<DuplicateLayerCommand>(0)).success);
    assert(doc.page().layers.size() == 2);
    const ObjectId cloned_group_id = doc.page().layers[1].object_ids.front();
    assert(cloned_group_id != group.id);
    const auto cloned_group_opt = doc.objects().get(cloned_group_id);
    assert(cloned_group_opt && std::holds_alternative<GroupObject>(*cloned_group_opt));
    const auto& cloned_group = std::get<GroupObject>(*cloned_group_opt);
    assert(cloned_group.children_ids.size() == 1);
    const ObjectId cloned_child_id = cloned_group.children_ids.front();
    assert(cloned_child_id != child.id && doc.objects().contains(cloned_child_id));

    assert(doc.undo());
    assert(doc.page().layers.size() == 1);
    assert(!doc.objects().contains(cloned_group_id));
    assert(!doc.objects().contains(cloned_child_id));
    assert(doc.redo());
    assert(doc.objects().contains(cloned_group_id));
    assert(doc.objects().contains(cloned_child_id));
}

int main() {
    test_add_remove_layer();
    test_rename_visibility_lock();
    test_reorder_and_move_objects();
    test_object_zorder_and_properties();
    test_printable_and_color();
    test_duplicate_and_move_multiple();
    test_duplicate_layer_deep_clones_groups();
    std::cout << "All layer commands tests passed successfully!\n";
    return 0;
}
