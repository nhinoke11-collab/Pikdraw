#include "pikdraw/vector/selection/selection_model.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include "pikdraw/document/document.hpp"
#include <cassert>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::vector;
using namespace pikdraw::document;

void test_hit_test_and_selection() {
    Document doc("doc-sel-test");

    auto rect_obj = ShapeFactory::create_rectangle(geometry::Rect2D{100, 100, 200, 150});
    auto id1 = rect_obj.id;
    doc.execute_command(std::make_unique<AddObjectCommand>(rect_obj));

    auto ellipse_obj = ShapeFactory::create_ellipse(geometry::Point2D{500, 500}, 50, 50);
    auto id2 = ellipse_obj.id;
    doc.execute_command(std::make_unique<AddObjectCommand>(ellipse_obj));

    auto snap = doc.current_snapshot();

    // Hit test inside rectangle
    auto hit1 = SelectionModel::hit_test(geometry::Point2D{150, 150}, *snap);
    assert(hit1.has_value());
    assert(*hit1 == id1);

    // Hit test near boundary within tolerance
    auto hit_near = SelectionModel::hit_test(geometry::Point2D{99.0, 100.0}, *snap, 2.0);
    assert(hit_near.has_value());
    assert(*hit_near == id1);

    // Hit test outside
    auto hit_miss = SelectionModel::hit_test(geometry::Point2D{0, 0}, *snap);
    assert(!hit_miss.has_value());

    // SelectionModel operations
    SelectionModel sel;
    assert(sel.empty());

    sel.select(id1, SelectionMode::Replace);
    assert(sel.count() == 1);
    assert(sel.is_selected(id1));

    sel.select(id2, SelectionMode::Add);
    assert(sel.count() == 2);
    assert(sel.is_selected(id2));

    sel.select(id1, SelectionMode::Toggle);
    assert(sel.count() == 1);
    assert(!sel.is_selected(id1));
    assert(sel.is_selected(id2));

    sel.clear();
    assert(sel.empty());
}

int main() {
    std::cout << "[test_selection] Running vector selection tests...\n";
    test_hit_test_and_selection();
    std::cout << "[test_selection] All tests passed!\n";
    return 0;
}
