#include "../test_support.hpp"
#include "pikdraw/vector/gestures/selection_resize.hpp"
#include "pikdraw/vector/vector_controller.hpp"
using namespace pikdraw;

namespace {
document::ObjectId add(document::Document &doc, document::PathObject object) {
    const auto id = object.id;
    CHECK(doc.execute_command(std::make_unique<document::AddObjectCommand>(object, 0)).success);
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
void nonuniform_stroke_pick() {
    document::Document doc("nonuniform-stroke-pick");
    auto outline = vector::ShapeFactory::create_rectangle({0, 0, 100, 100});
    outline.has_fill = false;
    outline.stroke_width_mm = 1;
    outline.transform = geometry::Transform2D::scale(100, 1);
    add(doc, outline);
    CHECK(!vector::SelectionModel::hit_test({500, 50}, *doc.current_snapshot(), 6));
    CHECK(vector::SelectionModel::hit_test({54, 50}, *doc.current_snapshot(), 6));
    CHECK(!vector::SelectionModel::hit_test({58, 50}, *doc.current_snapshot(), 6));
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
        resize_and_atomic_style();
    });
}
