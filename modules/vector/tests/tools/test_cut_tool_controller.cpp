#include "../test_support.hpp"
#include "pikdraw/vector/tools/cut_tool_controller.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/document/commands/command.hpp"

using namespace pikdraw;
using namespace pikdraw::vector::tools;
using namespace pikdraw::document;

void test_knife_cut_controller() {
    Document doc("cut_tool_doc");
    PathObject rect;
    rect.id = generate_object_id();
    rect.name = "Test Rect";
    rect.path = geometry::Path2D::make_rect(10.0, 10.0, 80.0, 60.0);
    rect.has_fill = true;
    rect.fill = color::ColorValue{color::RGBColor{0.2, 0.4, 0.8, 1.0}};
    const auto r_id = rect.id;

    CHECK(doc.execute_command(std::make_unique<AddObjectCommand>(rect, 0)).success);
    CHECK(doc.current_snapshot()->object_count() == 1);

    CutToolController controller(doc);
    controller.set_mode(CutToolMode::Knife);

    // Knife straight cut vertically at x = 50.0 (from (50, 0) to (50, 80))
    bool cut_ok = controller.execute_knife_cut({r_id}, {50.0, 0.0}, {50.0, 80.0}, 0.0);
    CHECK(cut_ok);
    CHECK(doc.current_snapshot()->object_count() == 2);

    // Verify Undo restores 1 object
    CHECK(doc.undo());
    CHECK(doc.current_snapshot()->object_count() == 1);
    CHECK(doc.current_snapshot()->objects().get(r_id).has_value());

    // Verify Redo
    CHECK(doc.redo());
    CHECK(doc.current_snapshot()->object_count() == 2);
}

void test_eraser_controller() {
    Document doc("eraser_doc");
    PathObject rect;
    rect.id = generate_object_id();
    rect.name = "Eraser Rect";
    rect.path = geometry::Path2D::make_rect(0.0, 0.0, 100.0, 100.0);
    rect.has_fill = true;
    const auto r_id = rect.id;

    CHECK(doc.execute_command(std::make_unique<AddObjectCommand>(rect, 0)).success);

    CutToolController controller(doc);
    controller.set_mode(CutToolMode::VectorEraser);
    controller.set_eraser_diameter_mm(5.0);

    // Erase horizontal line across
    std::vector<geometry::Point2D> stroke{{-10.0, 50.0}, {110.0, 50.0}};
    bool erase_ok = controller.execute_eraser_cut({r_id}, stroke, 5.0, true);
    CHECK(erase_ok);
    CHECK(doc.current_snapshot()->object_count() == 2);

    // Undo
    CHECK(doc.undo());
    CHECK(doc.current_snapshot()->object_count() == 1);
}

void test_trim_controller() {
    Document doc("trim_doc");
    PathObject target;
    target.id = generate_object_id();
    target.name = "Target";
    target.path = geometry::Path2D::make_rect(0.0, 0.0, 100.0, 100.0);
    target.has_fill = true;
    const auto tid = target.id;

    PathObject source;
    source.id = generate_object_id();
    source.name = "Source";
    source.path = geometry::Path2D::make_rect(50.0, 0.0, 100.0, 100.0);
    source.has_fill = true;
    const auto sid = source.id;

    CHECK(doc.execute_command(std::make_unique<AddObjectCommand>(target, 0)).success);
    CHECK(doc.execute_command(std::make_unique<AddObjectCommand>(source, 0)).success);
    CHECK(doc.current_snapshot()->object_count() == 2);

    CutToolController controller(doc);
    // Trim target with source, keep_sources = true
    bool trim_ok = controller.execute_trim_objects(tid, {sid}, true);
    CHECK(trim_ok);
    CHECK(doc.current_snapshot()->object_count() == 2); // target trimmed + source kept

    // Undo
    CHECK(doc.undo());
    CHECK(doc.current_snapshot()->object_count() == 2);
}

void test_knife_batches_multiple_targets_into_one_undo() {
    Document doc("cut_batch_doc");
    PathObject first;
    first.id = generate_object_id();
    first.name = "First";
    first.path = geometry::Path2D::make_rect(0.0, 0.0, 40.0, 40.0);
    first.has_fill = true;
    PathObject second = first;
    second.id = generate_object_id();
    second.name = "Second";
    second.path = geometry::Path2D::make_rect(0.0, 0.0, 40.0, 40.0);
    const auto first_id = first.id;
    const auto second_id = second.id;
    CHECK(doc.execute_command(std::make_unique<AddObjectCommand>(first, 0)).success);
    CHECK(doc.execute_command(std::make_unique<AddObjectCommand>(second, 0)).success);

    CutToolController controller(doc);
    CHECK(controller.execute_knife_cut({first_id, second_id}, {20.0, -5.0}, {20.0, 45.0}));
    CHECK(doc.current_snapshot()->object_count() == 4);
    CHECK(doc.undo());
    CHECK(doc.current_snapshot()->object_count() == 2);
    CHECK(doc.current_snapshot()->objects().get(first_id).has_value());
    CHECK(doc.current_snapshot()->objects().get(second_id).has_value());
}

void test_knife_uses_group_world_transform() {
    constexpr ObjectId path_id = 31'100'001;
    constexpr ObjectId group_id = 31'100'002;
    PathObject path;
    path.id = path_id;
    path.name = "Grouped";
    path.path = geometry::Path2D::make_rect(0.0, 0.0, 100.0, 100.0);
    path.has_fill = true;
    GroupObject group;
    group.id = group_id;
    group.children_ids = {path_id};
    group.transform = geometry::Transform2D::translation(150.0, 25.0);
    auto page = Page::make_default();
    page.layers.front().object_ids = {group_id};
    PersistentObjectMap objects;
    objects = objects.set(path_id, path);
    objects = objects.set(group_id, group);
    const DocumentSnapshot initial(1, 0, std::move(page), std::move(objects), "group_cut_doc");
    Document doc(initial);

    CutToolController controller(doc);
    CHECK(controller.execute_knife_cut({path_id}, {200.0, 0.0}, {200.0, 150.0}));
    const auto snapshot = doc.current_snapshot();
    const auto updated_group = snapshot->objects().get(group_id);
    CHECK(updated_group.has_value());
    CHECK(std::get<GroupObject>(*updated_group).children_ids.size() == 2);
    const auto first_piece_object = snapshot->objects().get(path_id);
    CHECK(first_piece_object.has_value());
    const auto* first_piece = std::get_if<PathObject>(&*first_piece_object);
    CHECK(first_piece != nullptr);
    CHECK(first_piece->transform.is_finite());
    const auto* first_entry = snapshot->hierarchy().find(path_id);
    CHECK(first_entry != nullptr && first_entry->bounds.has_value());
    CHECK(near(first_entry->bounds->x, 150.0));
    CHECK(near(first_entry->bounds->width, 50.0));
    CHECK(doc.undo());
    const auto restored_group = doc.current_snapshot()->objects().get(group_id);
    CHECK(restored_group.has_value());
    CHECK(std::get<GroupObject>(*restored_group).children_ids.size() == 1);
}

void test_eraser_non_overlap_is_no_change() {
    Document doc("eraser_noop_doc");
    PathObject rect;
    rect.id = generate_object_id();
    rect.path = geometry::Path2D::make_rect(0.0, 0.0, 20.0, 20.0);
    rect.has_fill = true;
    const auto id = rect.id;
    CHECK(doc.execute_command(std::make_unique<AddObjectCommand>(rect, 0)).success);
    const auto revision = doc.current_snapshot()->revision();

    CutToolController controller(doc);
    CHECK(!controller.execute_eraser_cut({id}, {{40.0, 40.0}, {50.0, 50.0}}, 2.0, true));
    CHECK(doc.current_snapshot()->revision() == revision);
    CHECK(doc.current_snapshot()->object_count() == 1);
}

void test_trim_non_overlap_is_no_change() {
    Document doc("trim_noop_doc");
    PathObject target;
    target.id = generate_object_id();
    target.path = geometry::Path2D::make_rect(0.0, 0.0, 20.0, 20.0);
    target.has_fill = true;
    PathObject source = target;
    source.id = generate_object_id();
    source.path = geometry::Path2D::make_rect(50.0, 50.0, 20.0, 20.0);
    const auto target_id = target.id;
    const auto source_id = source.id;
    CHECK(doc.execute_command(std::make_unique<AddObjectCommand>(target, 0)).success);
    CHECK(doc.execute_command(std::make_unique<AddObjectCommand>(source, 0)).success);
    const auto revision = doc.current_snapshot()->revision();

    CutToolController controller(doc);
    CHECK(!controller.execute_trim_objects(target_id, {source_id}, false));
    CHECK(doc.current_snapshot()->revision() == revision);
    CHECK(doc.current_snapshot()->objects().get(target_id).has_value());
    CHECK(doc.current_snapshot()->objects().get(source_id).has_value());
}

void test_trim_splits_target_into_components_and_undoes_atomically() {
    Document doc("trim_components_doc");
    PathObject target;
    target.id = generate_object_id();
    target.path = geometry::Path2D::make_rect(0.0, 0.0, 100.0, 100.0);
    target.has_fill = true;
    PathObject source;
    source.id = generate_object_id();
    source.path = geometry::Path2D::make_rect(45.0, 0.0, 10.0, 100.0);
    source.has_fill = true;
    const auto target_id = target.id;
    const auto source_id = source.id;
    CHECK(doc.execute_command(std::make_unique<AddObjectCommand>(target, 0)).success);
    CHECK(doc.execute_command(std::make_unique<AddObjectCommand>(source, 0)).success);

    CutToolController controller(doc);
    CHECK(controller.execute_trim_objects(target_id, {source_id}, true));
    CHECK(doc.current_snapshot()->object_count() == 3);
    CHECK(controller.last_created_ids().size() == 2);
    CHECK(doc.undo());
    CHECK(doc.current_snapshot()->object_count() == 2);
    CHECK(doc.current_snapshot()->objects().get(target_id).has_value());
    CHECK(doc.current_snapshot()->objects().get(source_id).has_value());
}

int main() {
    test_knife_cut_controller();
    test_eraser_controller();
    test_trim_controller();
    test_knife_batches_multiple_targets_into_one_undo();
    test_knife_uses_group_world_transform();
    test_eraser_non_overlap_is_no_change();
    test_trim_non_overlap_is_no_change();
    test_trim_splits_target_into_components_and_undoes_atomically();
    std::cout << "[PASS] All cut tool controller tests passed.\n";
    return 0;
}
