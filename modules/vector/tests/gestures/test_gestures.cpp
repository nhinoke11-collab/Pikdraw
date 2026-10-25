#include "../test_support.hpp"
#include "pikdraw/vector/vector_controller.hpp"
#include <limits>
using namespace pikdraw;
using namespace vector;
using namespace document;
void gestures() {
    Document doc("gestures");
    auto p = ShapeFactory::create_rectangle({0, 0, 100, 100});
    CHECK(doc.execute_command(std::make_unique<AddObjectCommand>(p)).success);
    TransformGesture gesture;
    CHECK(gesture.begin({0, 0}, {p.id}, *doc.current_snapshot()));
    const auto revision = doc.revision();
    (void)gesture.update({20, 30});
    CHECK(doc.revision() == revision);
    CHECK(gesture.commit(doc).success && doc.revision() == revision + 1);
    CHECK(doc.undo());
    CHECK(std::get<PathObject>(*doc.current_snapshot()->get_object(p.id)).transform.tx == 0);
    VectorController c;
    c.on_pointer_down({10, 10}, *doc.current_snapshot());
    CHECK(c.on_pointer_up({60, 70}, doc).success); // no move event
    const auto moved = std::get<PathObject>(*doc.current_snapshot()->get_object(p.id));
    CHECK(near(moved.transform.tx, 50) && near(moved.transform.ty, 60));
    CHECK(gesture.begin({0, 0}, {p.id}, *doc.current_snapshot()));
    CHECK(doc.undo());
    (void)gesture.update({1, 1});
    CHECK(!gesture.commit(doc).success);
    CHECK(gesture.begin({0, 0}, {p.id}, *doc.current_snapshot()));
    gesture.cancel();
    CHECK(!gesture.commit(doc).success);
    CHECK(gesture.begin({0, 0}, {p.id}, *doc.current_snapshot()));
    (void)gesture.update({std::numeric_limits<double>::infinity(), 0});
    CHECK(!gesture.is_active());
    CHECK(gesture.begin({0, 0}, {p.id}, *doc.current_snapshot()));
    (void)gesture.update({1, 1});
    Document reopened(*doc.current_snapshot());
    const auto reopened_revision = reopened.revision();
    CHECK(!gesture.commit(reopened).success && reopened.revision() == reopened_revision);
    CHECK(gesture.begin({0, 0}, {p.id}, *doc.current_snapshot()));
    (void)gesture.update({1, 1});
    doc.close();
    CHECK(!gesture.commit(doc).success);
}
void properties() {
    Document doc("styles");
    auto p = ShapeFactory::create_rectangle({0, 0, 10, 10});
    p.has_fill = false;
    p.has_stroke = false;
    CHECK(doc.execute_command(std::make_unique<AddObjectCommand>(p)).success);
    auto red = color::RGBColor{1, 0, 0, .4};
    CHECK(doc.execute_command(std::make_unique<SetObjectStyleCommand>(p.id, red, red, 2.5)).success);
    auto q = std::get<PathObject>(*doc.current_snapshot()->get_object(p.id));
    CHECK(q.has_fill && q.has_stroke && near(q.stroke_width_mm, 2.5));
    CHECK(doc.undo());
    q = std::get<PathObject>(*doc.current_snapshot()->get_object(p.id));
    CHECK(!q.has_fill && !q.has_stroke && near(q.stroke_width_mm, 0));
    CHECK(doc.redo());
    CHECK(doc.execute_command(std::make_unique<SetObjectStyleCommand>(p.id, std::nullopt, std::nullopt,
                                                                      std::nullopt, false, false))
              .success);
    q = std::get<PathObject>(*doc.current_snapshot()->get_object(p.id));
    CHECK(!q.has_fill && !q.has_stroke);
    const auto revision = doc.revision();
    CHECK(!doc.execute_command(std::make_unique<SetObjectStyleCommand>(p.id, std::nullopt, std::nullopt, -1))
               .success);
    CHECK(doc.revision() == revision);
    TextObject t;
    t.id = generate_object_id();
    t.text = "Cũ";
    CHECK(doc.execute_command(std::make_unique<AddObjectCommand>(t)).success);
    auto font = t.font;
    font.family_name = "Requested missing font";
    font.postscript_name = "RequestedPS";
    font.weight = 700;
    font.italic = true;
    font.size_pt = 24;
    CHECK(doc.execute_command(std::make_unique<UpdateTextCommand>(
                                  t.id, std::optional<std::string>{"Việt Nam a\u0301"}, font))
              .success);
    const auto updated = std::get<TextObject>(*doc.current_snapshot()->get_object(t.id));
    CHECK(updated.text == "Việt Nam a\u0301" && updated.font.family_name == font.family_name);
    CHECK(updated.font.postscript_name == "RequestedPS" && updated.font.italic &&
          near(updated.font.size_pt, 24));
    CHECK(doc.undo());
    const auto old = std::get<TextObject>(*doc.current_snapshot()->get_object(t.id));
    CHECK(old.text == "Cũ" && old.font.family_name == t.font.family_name);
    CHECK(doc.redo());
    const auto rev = doc.revision();
    CHECK(!doc.execute_command(std::make_unique<SetObjectStyleCommand>(t.id, std::nullopt, red, std::nullopt))
               .success);
    CHECK(doc.revision() == rev);
    font.size_pt = -1;
    CHECK(!doc.execute_command(std::make_unique<SetTextFontCommand>(t.id, font)).success);
    CHECK(doc.revision() == rev);
}
void atomic_property_composition() {
    Document doc("atomic-properties");
    auto p = ShapeFactory::create_rectangle({0, 0, 10, 10});
    const auto red = color::RGBColor{1, 0, 0, 1};
    const auto blue = color::RGBColor{0, 0, 1, 1};
    auto transaction = std::make_unique<CompositeCommand>("Add and style atomically");
    transaction->add_command(std::make_unique<AddObjectCommand>(p));
    transaction->add_command(std::make_unique<SetObjectStyleCommand>(p.id, red, std::nullopt, std::nullopt));
    transaction->add_command(std::make_unique<SetObjectStyleCommand>(p.id, std::nullopt, blue, 3));
    CHECK(doc.execute_command(std::move(transaction)).success);
    CHECK(doc.revision() == 1);
    const auto result = std::get<PathObject>(*doc.current_snapshot()->get_object(p.id));
    CHECK(std::get<color::RGBColor>(result.fill.payload()).r == 1);
    CHECK(std::get<color::RGBColor>(result.stroke.payload()).b == 1);
    CHECK(near(result.stroke_width_mm, 3));
    CHECK(doc.undo() && doc.current_snapshot()->object_count() == 0);
    CHECK(doc.redo() && doc.current_snapshot()->object_count() == 1);
    TextObject text;
    text.id = generate_object_id();
    text.text = "before";
    CHECK(doc.execute_command(std::make_unique<AddObjectCommand>(text)).success);
    auto font = text.font;
    font.size_pt = 36;
    transaction = std::make_unique<CompositeCommand>("Text and font atomically");
    transaction->add_command(std::make_unique<UpdateTextCommand>(text.id, "after"));
    transaction->add_command(std::make_unique<SetTextFontCommand>(text.id, font));
    CHECK(doc.execute_command(std::move(transaction)).success);
    const auto updated = std::get<TextObject>(*doc.current_snapshot()->get_object(text.id));
    CHECK(updated.text == "after" && near(updated.font.size_pt, 36));
}
int main() {
    return run([] {
        gestures();
        properties();
        atomic_property_composition();
    });
}
