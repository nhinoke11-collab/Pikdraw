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

int main() {
    test_add_remove_layer();
    test_rename_visibility_lock();
    test_reorder_and_move_objects();
    test_object_zorder_and_properties();
    std::cout << "All layer commands tests passed successfully!\n";
    return 0;
}
