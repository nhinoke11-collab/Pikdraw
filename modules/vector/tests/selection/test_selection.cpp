#include "../test_support.hpp"
#include "pikdraw/vector/vector_controller.hpp"
using namespace pikdraw;
using namespace vector;
using namespace document;
DocumentSnapshot make_snapshot(Page page, const std::vector<ObjectVariant> &objects) {
    PersistentObjectMap map;
    for (const auto &object : objects) {
        const auto id = std::visit([](const auto &o) { return o.id; }, object);
        map = map.set(id, object);
    }
    return {1, 0, std::move(page), std::move(map), "selection"};
}
void geometry_and_painter_order() {
    auto bottom = ShapeFactory::create_rectangle({0, 0, 100, 100});
    auto top = ShapeFactory::create_rectangle({0, 0, 100, 100});
    bottom.id = 900;
    top.id = 100; // painter order must not be ID order
    auto page = Page::make_default();
    page.layers[0].object_ids = {900, 100};
    auto snapshot = make_snapshot(page, {bottom, top});
    CHECK(SelectionModel::hit_test({10, 10}, snapshot) == 100);
    top.locked = true;
    snapshot = make_snapshot(page, {bottom, top});
    CHECK(SelectionModel::hit_test({10, 10}, snapshot) == 900);
    top.locked = false;
    top.visible = false;
    snapshot = make_snapshot(page, {bottom, top});
    CHECK(SelectionModel::hit_test({10, 10}, snapshot) == 900);
    top.visible = true;
    page.layers[0].object_ids = {900};
    page.layers.push_back(Layer{"top", true, true, {100}});
    snapshot = make_snapshot(page, {bottom, top});
    CHECK(SelectionModel::hit_test({10, 10}, snapshot) == 900);
    page.layers[1].locked = false;
    page.layers[1].visible = false;
    snapshot = make_snapshot(page, {bottom, top});
    CHECK(SelectionModel::hit_test({10, 10}, snapshot) == 900);
    PathObject triangle;
    triangle.id = 1;
    triangle.path.move_to(0, 0);
    triangle.path.line_to(100, 0);
    triangle.path.line_to(0, 100);
    triangle.path.close();
    triangle.has_stroke = false;
    page = Page::make_default();
    page.layers[0].object_ids = {1};
    snapshot = make_snapshot(page, {triangle});
    CHECK(!SelectionModel::hit_test({80, 80}, snapshot, 0)); // in bounds but outside source fill
    CHECK(SelectionModel::hit_test({10, 10}, snapshot, 0) == 1);
    triangle.has_fill = false;
    triangle.has_stroke = true;
    triangle.stroke_width_mm = 2;
    snapshot = make_snapshot(page, {triangle});
    CHECK(!SelectionModel::hit_test({20, 20}, snapshot, 0));
    CHECK(SelectionModel::hit_test({50, .5}, snapshot, 0) == 1);
    triangle.transform =
        geometry::Transform2D::translation(1000, 2000) * geometry::Transform2D::rotation_degrees(30);
    snapshot = make_snapshot(page, {triangle});
    CHECK(SelectionModel::hit_test(triangle.transform.map_point({50, .5}), snapshot, 0) == 1);
}
void winding_curves_and_quality() {
    PathObject p;
    p.id = 1;
    p.has_stroke = false;
    p.path = geometry::Path2D::make_rect(0, 0, 100, 100);
    const auto inner = geometry::Path2D::make_rect(25, 25, 50, 50);
    for (const auto &e : inner.elements()) {
        if (e.verb == geometry::PathVerb::MoveTo)
            p.path.move_to(e.pt);
        else if (e.verb == geometry::PathVerb::LineTo)
            p.path.line_to(e.pt);
        else if (e.verb == geometry::PathVerb::Close)
            p.path.close();
    }
    auto page = Page::make_default();
    page.layers[0].object_ids = {1};
    p.path.set_winding_rule(geometry::WindingRule::EvenOdd);
    auto snapshot = make_snapshot(page, {p});
    CHECK(!SelectionModel::hit_test({50, 50}, snapshot, 0));
    CHECK(SelectionModel::hit_test({10, 10}, snapshot, 0) == 1);
    p.path.set_winding_rule(geometry::WindingRule::NonZero);
    snapshot = make_snapshot(page, {p});
    CHECK(SelectionModel::hit_test({50, 50}, snapshot, 0) == 1);
    p = ShapeFactory::create_ellipse({50, 50}, 50, 50);
    p.id = 1;
    p.has_stroke = false;
    snapshot = make_snapshot(page, {p});
    CHECK(!SelectionModel::hit_test({1, 1}, snapshot, .01));
    const auto hit = SelectionModel::hit_test_detail({50, 50}, snapshot, 0);
    CHECK(hit && hit->quality == HitQuality::CurveApproximation);
    TextObject text;
    text.id = 2;
    text.text = "A";
    page.layers[0].object_ids = {2};
    snapshot = make_snapshot(page, {text});
    const auto text_hit = SelectionModel::hit_test_detail({.1, .1}, snapshot, 0);
    CHECK(text_hit && text_hit->quality == HitQuality::TextApproximation);
    CHECK(!SelectionModel::hit_test_detail({.1, .1}, snapshot, 0, false));
    SelectionModel selection;
    selection.select(1);
    selection.select(2, SelectionMode::Add);
    selection.select(1, SelectionMode::Toggle);
    CHECK(selection.count() == 1 && selection.is_selected(2));
}
void large_source_and_diagnostics() {
    PathObject path;
    path.id = 1;
    path.has_stroke = false;
    path.path.move_to(0, 0);
    for (int i = 1; i <= 100000; ++i)
        path.path.line_to(i * .01, (i % 2) * .1);
    path.path.line_to(1000, 100);
    path.path.line_to(0, 100);
    path.path.close();
    auto page = Page::make_default();
    page.layers[0].object_ids = {1};
    auto snapshot = make_snapshot(page, {path});
    CHECK(SelectionModel::hit_test({500, 50}, snapshot, 0) == 1);
    path.path.clear();
    path.path.move_to(0, 0);
    path.path.arc_to(10, 10, 0, false, true, 20, 0);
    snapshot = make_snapshot(page, {path});
    HitDiagnostics diagnostics;
    CHECK(!SelectionModel::hit_test_detail({10, 10}, snapshot, 0, true, &diagnostics));
    CHECK(diagnostics.unsupported_objects == 1);
    path = ShapeFactory::create_ellipse({50, 50}, 50, 50);
    path.id = 1;
    snapshot = make_snapshot(page, {path});
    CHECK(!SelectionModel::hit_test_detail({50, 50}, snapshot, 1e-300, true, &diagnostics));
    CHECK(diagnostics.approximation_budget_exceeded == 1);
    path.path.clear();
    path.path.move_to(0, 0);
    path.path.cubic_to(100, 0, 100, 0, 1, 0); // collinear overshoot must not collapse to [0,1]
    path.has_fill = false;
    path.has_stroke = true;
    path.stroke_width_mm = 1;
    snapshot = make_snapshot(page, {path});
    CHECK(SelectionModel::hit_test({70, 0}, snapshot, 0) == 1);
}
int main() {
    return run([] {
        geometry_and_painter_order();
        winding_curves_and_quality();
        large_source_and_diagnostics();
    });
}
