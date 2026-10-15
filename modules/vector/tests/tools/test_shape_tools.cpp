#include "../test_support.hpp"
#include "pikdraw/vector/vector_controller.hpp"
using namespace pikdraw;
using namespace vector;
using namespace document;
void shape_lifecycle() {
    Document doc("shapes");
    VectorController c(ToolMode::Rectangle);
    CHECK(c.on_pointer_up({1, 2}, doc).success);
    CHECK(doc.revision() == 0);
    c.on_pointer_down({100, 200}, *doc.current_snapshot());
    (void)c.on_pointer_move({80, 170});
    CHECK(doc.revision() == 0 && c.shape_preview());
    CHECK(near(c.shape_preview()->width, 20));
    CHECK(c.on_pointer_up({60, 140}, doc).success);
    CHECK(doc.revision() == 1 && doc.current_snapshot()->object_count() == 1);
    const auto id = c.selection().selected_ids()[0];
    const auto p = std::get<PathObject>(*doc.current_snapshot()->get_object(id));
    CHECK(near(p.bounds().x, 60) && near(p.bounds().y, 140));
    CHECK(near(p.bounds().width, 40) && near(p.bounds().height, 60));
    c.on_pointer_down({1, 2}, *doc.current_snapshot());
    c.on_escape();
    CHECK(c.on_pointer_up({500, 500}, doc).success && doc.revision() == 1);
    c.set_mode(ToolMode::Ellipse);
    c.on_pointer_down({10, 10}, *doc.current_snapshot());
    CHECK(c.on_pointer_up({30, 50}, doc).success);
    const auto ellipse =
        std::get<PathObject>(*doc.current_snapshot()->get_object(c.selection().selected_ids()[0]));
    CHECK(near(ellipse.bounds().width, 20) && near(ellipse.bounds().height, 40));
    CHECK(c.delete_selected(doc).success && c.selection().empty());
    CHECK(doc.undo() && doc.current_snapshot()->object_count() == 2);
    c.on_pointer_down({1, 1}, *doc.current_snapshot());
    CHECK(c.on_pointer_up({1, 5}, doc).success); // zero width no-op
    CHECK(doc.current_snapshot()->object_count() == 2);
}
void stale_cancel_failure_selection() {
    Document doc("one");
    VectorController c(ToolMode::Rectangle);
    c.on_pointer_down({0, 0}, *doc.current_snapshot());
    CHECK(doc.execute_command(
                 std::make_unique<AddObjectCommand>(ShapeFactory::create_rectangle({50, 50, 10, 10})))
              .success);
    CHECK(!c.on_pointer_up({10, 10}, doc).success && doc.revision() == 1);
    c.on_pointer_down({0, 0}, *doc.current_snapshot());
    Document other("one");
    CHECK(!c.on_pointer_up({20, 20}, other).success && other.revision() == 0);
    c.set_mode(ToolMode::Select);
    c.on_pointer_down({55, 55}, *doc.current_snapshot());
    CHECK(c.selection().count() == 1);
    const auto oldid = c.selection().selected_ids()[0];
    c.selection().select(999999, SelectionMode::Add);
    CHECK(!c.delete_selected(doc).success && c.selection().count() == 2);
    CHECK(doc.current_snapshot()->contains_object(oldid));
    CHECK(!c.delete_selected(other).success && other.revision() == 0);
    c.set_active_layer(77);
    CHECK(!c.insert_text(doc, {0, 0}, "bad layer").success);
    c.set_active_layer(0);
    CHECK(c.insert_text(doc, {5, 5}, "Hà Nội").success);
    CHECK(c.selection().count() == 1);
    CHECK(std::get<TextObject>(*doc.current_snapshot()->get_object(c.selection().selected_ids()[0])).text ==
          "Hà Nội");
}
int main() {
    return run([] {
        shape_lifecycle();
        stale_cancel_failure_selection();
    });
}
