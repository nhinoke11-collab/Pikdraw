#include "pikdraw/vector/vector_controller.hpp"
#include "pikdraw/color/preview/color_transform.hpp"
#include <cassert>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::vector;
using namespace pikdraw::document;

void test_transform_gesture_commit_and_undo() {
    Document doc("doc-gesture-test");
    auto rect = ShapeFactory::create_rectangle(geometry::Rect2D{0, 0, 100, 100});
    auto id = rect.id;
    doc.execute_command(std::make_unique<AddObjectCommand>(rect));
    assert(doc.revision() == 1);

    TransformGesture gesture;
    gesture.begin(geometry::Point2D{50, 50}, {id});
    assert(gesture.is_active());

    // Transient updates during drag
    auto delta1 = gesture.update(geometry::Point2D{80, 70});
    assert(delta1.tx == 30.0);
    assert(delta1.ty == 20.0);
    assert(doc.revision() == 1); // Dragging does NOT bump document revision!

    // Commit on pointer up
    auto res = gesture.commit(doc);
    assert(res.success);
    assert(doc.revision() == 2); // Exactly 1 revision bump on commit!

    auto snap = doc.current_snapshot();
    auto obj = snap->get_object(id);
    assert(obj.has_value());
    const auto& po = std::get<PathObject>(*obj);
    assert(po.transform.tx == 30.0);
    assert(po.transform.ty == 20.0);

    // Single undo undos the entire drag gesture
    doc.undo();
    assert(doc.revision() == 3);
    auto undone_obj = doc.current_snapshot()->get_object(id);
    const auto& undone_po = std::get<PathObject>(*undone_obj);
    assert(undone_po.transform.tx == 0.0);
    assert(undone_po.transform.ty == 0.0);
}

void test_gesture_cancel_on_escape() {
    Document doc("doc-cancel-test");
    auto rect = ShapeFactory::create_rectangle(geometry::Rect2D{0, 0, 50, 50});
    doc.execute_command(std::make_unique<AddObjectCommand>(rect));
    assert(doc.revision() == 1);

    TransformGesture gesture;
    gesture.begin(geometry::Point2D{25, 25}, {rect.id});
    (void)gesture.update(geometry::Point2D{100, 100});

    // Press Escape
    gesture.cancel();
    assert(!gesture.is_active());
    assert(doc.revision() == 1); // Document remains untouched!
}

void test_style_and_vietnamese_text_commands() {
    Document doc("doc-style-text-test");

    // Test text update with Vietnamese Unicode (NFC and combining marks)
    TextObject to;
    to.id = generate_object_id();
    to.text = "Ban đầu";
    to.position = {10, 10};
    doc.execute_command(std::make_unique<AddObjectCommand>(to));
    assert(doc.revision() == 1);

    std::string vi_text = "Cộng hòa Xã hội Chủ nghĩa Việt Nam";
    doc.execute_command(std::make_unique<UpdateTextCommand>(to.id, vi_text));
    assert(doc.revision() == 2);

    auto snap = doc.current_snapshot();
    auto text_obj = std::get<TextObject>(*snap->get_object(to.id));
    assert(text_obj.text == vi_text);

    // Undo text update
    doc.undo();
    assert(doc.revision() == 3);
    auto undone_text = std::get<TextObject>(*doc.current_snapshot()->get_object(to.id));
    assert(undone_text.text == "Ban đầu");

    // Test Style update
    auto new_fill = color::RGBColor{0.9, 0.1, 0.1, 1.0};
    doc.execute_command(std::make_unique<SetObjectStyleCommand>(
        to.id, new_fill, std::nullopt, std::nullopt));
    assert(doc.revision() == 4);
    auto styled_text = std::get<TextObject>(*doc.current_snapshot()->get_object(to.id));
    auto rgb = color::to_preview_rgba(styled_text.fill);
    assert(std::abs(rgb.r - 0.9f) < 1e-3);
}

int main() {
    std::cout << "[test_gestures] Running vector gestures tests...\n";
    test_transform_gesture_commit_and_undo();
    test_gesture_cancel_on_escape();
    test_style_and_vietnamese_text_commands();
    std::cout << "[test_gestures] All tests passed!\n";
    return 0;
}
