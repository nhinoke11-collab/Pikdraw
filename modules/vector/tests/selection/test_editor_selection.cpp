#include "../test_support.hpp"
#include "pikdraw/vector/gestures/selection_resize.hpp"
#include "pikdraw/vector/vector_controller.hpp"
#include <algorithm>
#include <utility>
#include <vector>
using namespace pikdraw;

namespace {
document::ObjectId add(document::Document &doc, document::PathObject object) {
    const auto id = object.id;
    const auto result = doc.execute_command(std::make_unique<document::AddObjectCommand>(object, 0));
    if (!result.success)
        std::cerr << "AddObject fixture failed in " << doc.id() << ": " << result.error_message << '\n';
    CHECK(result.success);
    return id;
}
void directional_regions() {
    document::Document doc("region-fixture");
    auto first = vector::ShapeFactory::create_rectangle({10, 10, 20, 20});
    first.has_stroke = false;
    auto second = vector::ShapeFactory::create_rectangle({40, 10, 20, 20});
    second.has_stroke = false;
    const auto a = add(doc, first), b = add(doc, second);
    const auto snapshot = doc.current_snapshot();
    const geometry::Rect2D region{0, 0, 50, 40};
    CHECK(vector::SelectionModel::query_region(region, vector::RegionMode::Window, *snapshot) ==
          std::vector<document::ObjectId>{a});
    CHECK((vector::SelectionModel::query_region(region, vector::RegionMode::Crossing, *snapshot) ==
           std::vector<document::ObjectId>{a, b}));
    vector::VectorController controller;
    controller.select_region(region, vector::RegionMode::Window, *snapshot);
    controller.set_selection({b}, *snapshot, vector::SelectionMode::Add);
    CHECK(controller.selection().count() == 2);
    controller.set_selection({a}, *snapshot, vector::SelectionMode::Toggle);
    CHECK(controller.selection().selected_ids() == std::vector<document::ObjectId>{b});
    CHECK(doc.revision() == 2); // selecting never edits source
}
void crossing_uses_geometry() {
    document::Document doc("geometry-fixture");
    auto triangle = vector::ShapeFactory::create_rectangle({0, 0, 100, 100});
    triangle.path = {};
    triangle.authoring_source.reset();
    triangle.path.move_to(0, 0);
    triangle.path.line_to(100, 0);
    triangle.path.line_to(0, 100);
    triangle.path.close();
    triangle.has_stroke = false;
    add(doc, triangle);
    CHECK(vector::SelectionModel::query_region({90, 90, 5, 5}, vector::RegionMode::Crossing,
                                               *doc.current_snapshot())
              .empty());
    CHECK(vector::SelectionModel::query_region({5, 5, 2, 2}, vector::RegionMode::Crossing,
                                               *doc.current_snapshot())
              .size() == 1);

    document::Document hole_doc("hole-fixture");
    auto hole = vector::ShapeFactory::create_rectangle({0, 0, 100, 100});
    hole.authoring_source.reset();
    hole.path.move_to(20, 20);
    hole.path.line_to(80, 20);
    hole.path.line_to(80, 80);
    hole.path.line_to(20, 80);
    hole.path.close();
    hole.path.set_winding_rule(geometry::WindingRule::EvenOdd);
    hole.has_stroke = false;
    add(hole_doc, hole);
    CHECK(vector::SelectionModel::query_region({30, 30, 10, 10}, vector::RegionMode::Crossing,
                                               *hole_doc.current_snapshot())
              .empty());
    CHECK(vector::SelectionModel::query_region({5, 5, 2, 2}, vector::RegionMode::Crossing,
                                               *hole_doc.current_snapshot())
              .size() == 1);
    CHECK(vector::SelectionModel::query_region({95, 30, 10, 10}, vector::RegionMode::Crossing,
                                               *hole_doc.current_snapshot())
              .size() == 1);
}
void stroke_and_locked_targets() {
    document::Document doc("stroke-fixture");
    auto line = vector::ShapeFactory::create_rectangle({0, 0, 1, 1});
    line.path = {};
    line.authoring_source.reset();
    line.path.move_to(0, 0);
    line.path.line_to(100, 100);
    line.has_fill = false;
    line.stroke_width_mm = 2;
    line.transform = geometry::Transform2D::scale(2, 1);
    const auto id = add(doc, line);
    const auto snapshot = doc.current_snapshot();
    CHECK(
        vector::SelectionModel::query_region({80, 8, 5, 5}, vector::RegionMode::Crossing, *snapshot).empty());
    CHECK(vector::SelectionModel::query_region({100, 49, 2, 3}, vector::RegionMode::Crossing, *snapshot)
              .size() == 1);
    CHECK(vector::SelectionModel::query_region({0, 0, 200, 100}, vector::RegionMode::Window, *snapshot)
              .empty());
    CHECK(vector::SelectionModel::query_region({-3, -2, 206, 104}, vector::RegionMode::Window, *snapshot)
              .size() == 1);
    auto locked = line;
    locked.locked = true;
    auto map = snapshot->objects().set(id, locked);
    document::DocumentSnapshot locked_snapshot(snapshot->epoch(), snapshot->revision(), snapshot->page(), map,
                                               doc.id());
    CHECK(vector::SelectionModel::query_region({-3, -2, 206, 104}, vector::RegionMode::Crossing,
                                               locked_snapshot)
              .empty());
}
void hairline_pick_and_marquee() {
    document::Document doc("hairline-selection");
    document::PathObject horizontal;
    horizontal.id = document::generate_object_id();
    horizontal.has_fill = false;
    horizontal.has_stroke = true;
    horizontal.stroke_width_mm = 0.0;
    horizontal.path.move_to(10, 20);
    horizontal.path.line_to(110, 20);
    const auto first = add(doc, horizontal);
    auto vertical = horizontal;
    vertical.id = document::generate_object_id();
    vertical.path.clear();
    vertical.path.move_to(150, 10);
    vertical.path.line_to(150, 100);
    const auto second = add(doc, vertical);
    const auto snapshot = doc.current_snapshot();
    vector::VectorController controller;
    const auto picked = controller.pick({50, 22}, *snapshot, 3.0);
    CHECK(picked && picked->id == first);
    CHECK(!controller.pick({50, 24}, *snapshot, 3.0));
    const auto crossing = vector::SelectionModel::query_region(
        {40, 15, 10, 10}, vector::RegionMode::Crossing, *snapshot);
    CHECK(crossing.size() == 1 && crossing.front() == first);
    const auto window = vector::SelectionModel::query_region(
        {0, 0, 160, 110}, vector::RegionMode::Window, *snapshot);
    CHECK(window.size() == 2);
    controller.on_pointer_down({50, 22}, *snapshot, vector::SelectionMode::Replace, 3.0);
    controller.on_pointer_down({149, 65}, *snapshot, vector::SelectionMode::Add, 3.0);
    CHECK(controller.selection().is_selected(first) && controller.selection().is_selected(second));
    const auto bounds = controller.selection().bounds(*snapshot);
    CHECK(bounds && bounds->x == 10 && bounds->y == 10 &&
          bounds->right() == 150 && bounds->bottom() == 100);
}
void nonuniform_stroke_pick() {
    document::Document doc("nonuniform-stroke-pick");
    auto outline = vector::ShapeFactory::create_rectangle({0, 0, 100, 100});
    outline.path.clear();
    outline.authoring_source.reset();
    outline.path.move_to(0, 0);
    outline.path.line_to(0, 100);
    outline.has_fill = false;
    outline.stroke_width_mm = 1;
    outline.transform = geometry::Transform2D::scale(100, 1);
    add(doc, outline);
    CHECK(!vector::SelectionModel::hit_test({500, 50}, *doc.current_snapshot(), 6));
    CHECK(vector::SelectionModel::hit_test({54, 50}, *doc.current_snapshot(), 6));
    CHECK(!vector::SelectionModel::hit_test({58, 50}, *doc.current_snapshot(), 6));
}
void indexed_queries_match_exhaustive_and_invalidate() {
    auto page = document::Page::make_default();
    document::PersistentObjectMap objects;
    std::vector<document::ObjectId> ids;
    ids.reserve(700);
    for (uint32_t i = 0; i < 700; ++i) {
        const double x = static_cast<double>(i % 35) * 30.0;
        const double y = static_cast<double>(i / 35) * 30.0;
        auto object = vector::ShapeFactory::create_rectangle({x, y, 18, 18});
        object.has_stroke = (i % 3) != 0;
        object.stroke_width_mm = 1.25;
        if (i % 7 == 0)
            object.transform = geometry::Transform2D::translation(0.25, -0.5) *
                               geometry::Transform2D::scale(1.5, 0.75);
        const auto id = object.id;
        objects = objects.set(id, object);
        page.layers.front().object_ids.push_back(id);
        ids.push_back(id);
    }
    auto overlap = vector::ShapeFactory::create_rectangle({60, 60, 18, 18});
    overlap.has_stroke = false;
    const auto overlap_id = overlap.id;
    objects = objects.set(overlap_id, overlap);
    page.layers.front().object_ids.push_back(overlap_id);
    ids.push_back(overlap_id);

    auto hidden = vector::ShapeFactory::create_rectangle({60, 60, 18, 18});
    hidden.visible = false;
    const auto hidden_id = hidden.id;
    objects = objects.set(hidden_id, hidden);
    page.layers.front().object_ids.push_back(hidden_id);

    document::TextObject text;
    text.id = document::generate_object_id();
    text.text = "indexed text";
    text.position = {1200, 800};
    text.transform = geometry::Transform2D::scale(4, 3);
    const auto text_id = text.id;
    objects = objects.set(text_id, text);
    page.layers.front().object_ids.push_back(text_id);
    document::GroupObject unsupported_group;
    unsupported_group.id = document::generate_object_id();
    objects = objects.set(unsupported_group.id, unsupported_group);
    page.layers.front().object_ids.push_back(unsupported_group.id);

    document::DocumentSnapshot snapshot(11, 4, page, objects, "selection-index-fixture");
    vector::VectorController controller;
    for (const auto &[region, mode] : std::vector<std::pair<geometry::Rect2D, vector::RegionMode>>{
             {{50, 50, 35, 35}, vector::RegionMode::Crossing},
             {{0, 0, 75, 75}, vector::RegionMode::Window},
             {{400, 300, 120, 95}, vector::RegionMode::Crossing}}) {
        const auto expected = vector::SelectionModel::query_region(region, mode, snapshot);
        controller.select_region(region, mode, snapshot);
        CHECK(controller.selection().selected_ids() == expected);
    }

    const auto topmost = vector::SelectionModel::hit_test_detail({65, 65}, snapshot, 1.0);
    CHECK(topmost && topmost->id == overlap_id);
    controller.on_pointer_down({65, 65}, snapshot);
    CHECK(controller.selection().selected_ids() == std::vector<document::ObjectId>{overlap_id});
    const geometry::Point2D empty_pick{1500, 900};
    CHECK(!vector::SelectionModel::hit_test_detail(empty_pick, snapshot, 2.0));
    controller.on_pointer_down(empty_pick, snapshot);
    CHECK(controller.selection().empty());
    const auto text_point = text.bounds().center();
    const auto exhaustive_text_hit =
        vector::SelectionModel::hit_test_detail(text_point, snapshot, 2.0);
    CHECK(exhaustive_text_hit && exhaustive_text_hit->id == text_id);
    controller.on_pointer_down(text_point, snapshot);
    CHECK(controller.selection().selected_ids() == std::vector<document::ObjectId>{text_id});

    auto moved = overlap;
    moved.transform = geometry::Transform2D::translation(5000, 5000);
    auto moved_objects = objects.set(overlap_id, moved);
    document::DocumentSnapshot changed(11, 5, page, moved_objects,
                                       "selection-index-fixture");
    const geometry::Rect2D old_region{50, 50, 35, 35};
    controller.select_region(old_region, vector::RegionMode::Crossing, changed);
    CHECK(controller.selection().selected_ids() ==
          vector::SelectionModel::query_region(old_region, vector::RegionMode::Crossing, changed));
    CHECK(std::find(controller.selection().selected_ids().begin(),
                    controller.selection().selected_ids().end(), overlap_id) ==
          controller.selection().selected_ids().end());

    auto first_page = document::make_document_page("First", page, objects);
    auto second_layout = document::Page::make_default();
    document::PersistentObjectMap second_objects;
    auto second_object = vector::ShapeFactory::create_rectangle({9000, 9000, 20, 20});
    const auto second_id = second_object.id;
    second_objects = second_objects.set(second_id, second_object);
    second_layout.layers.front().object_ids.push_back(second_id);
    auto second_page = document::make_document_page("Second", second_layout, second_objects);
    const std::vector<document::DocumentPage> pages{first_page, second_page};
    document::DocumentSnapshot first_snapshot(12, 1, pages, first_page.id, "page-index-fixture");
    document::DocumentSnapshot second_snapshot(12, 1, pages, second_page.id, "page-index-fixture");
    const geometry::Rect2D first_page_region{50, 50, 35, 35};
    controller.select_region(first_page_region, vector::RegionMode::Crossing, first_snapshot);
    CHECK(controller.selection().selected_ids() ==
          vector::SelectionModel::query_region(first_page_region, vector::RegionMode::Crossing,
                                               first_snapshot));
    controller.select_region({8999, 8999, 22, 22}, vector::RegionMode::Crossing, second_snapshot);
    CHECK(controller.selection().selected_ids() == std::vector<document::ObjectId>{second_id});
}
void resize_and_atomic_style() {
    const geometry::Rect2D bounds{10, 20, 40, 20};
    const auto resize = vector::selection_resize_transform(bounds, 2, {20, 10});
    CHECK(resize && resize->map_rect(bounds).approx_equal({10, 20, 60, 30}));
    const auto ratio = vector::selection_resize_transform(bounds, 2, {20, 2}, true);
    CHECK(ratio && ratio->map_rect(bounds).approx_equal({10, 20, 60, 30}));
    CHECK(!vector::selection_resize_transform(bounds, 2, {-40, -20}));

    document::Document doc("style-fixture");
    auto object = vector::ShapeFactory::create_rectangle(bounds);
    object.fill.set_overprint({true, false});
    const auto a = add(doc, object);
    object.id = document::generate_object_id();
    object.authoring_source.reset();
    object.fill.set_overprint({false, true});
    const auto b = add(doc, object);
    const auto before = doc.current_snapshot();
    vector::StylePatch patch;
    patch.fill = color::ColorValue::make_cmyk(.125, .5, .75, .2, .8);
    patch.width_mm = 1.75;
    patch.has_stroke = false;
    CHECK(vector::apply_object_styles(doc, {a, b}, patch).success);
    CHECK(doc.revision() == before->revision() + 1);
    for (const auto id : {a, b}) {
        const auto changed = std::get<document::PathObject>(*doc.objects().get(id));
        CHECK(changed.fill.color_space() == color::ColorSpace::Cmyk);
        CHECK(near(changed.stroke_width_mm, 1.75) && !changed.has_stroke);
        const auto original = std::get<document::PathObject>(*before->get_object(id));
        CHECK(changed.fill.overprint().fill == original.fill.overprint().fill);
        CHECK(changed.fill.overprint().stroke == original.fill.overprint().stroke);
    }
    CHECK(doc.undo());
    CHECK(std::get<document::PathObject>(*doc.objects().get(a)).fill.color_space() == color::ColorSpace::Rgb);
    CHECK(doc.redo());
    vector::TransformGesture gesture;
    CHECK(gesture.begin({0, 0}, {a, b}, *doc.current_snapshot()));
    CHECK(gesture.update_transform(*resize));
    const auto revision = doc.revision();
    CHECK(gesture.commit(doc).success && doc.revision() == revision + 1);
    CHECK(doc.undo());
    CHECK(std::get<document::PathObject>(*doc.objects().get(a)).bounds().approx_equal(bounds));
    CHECK(gesture.begin({0, 0}, {a}, *doc.current_snapshot()));
    CHECK(vector::apply_object_styles(doc, {a}, patch).success);
    CHECK(!gesture.commit(doc).success); // style edits invalidate an in-flight transform
    const auto stable = doc.current_snapshot();
    CHECK(!vector::apply_object_styles(doc, {a, document::kInvalidObjectId}, patch).success);
    CHECK(doc.current_snapshot() == stable);
}
} // namespace
int main() {
    return run([] {
        directional_regions();
        crossing_uses_geometry();
        stroke_and_locked_targets();
        nonuniform_stroke_pick();
        hairline_pick_and_marquee();
        indexed_queries_match_exhaustive_and_invalidate();
        resize_and_atomic_style();
    });
}
