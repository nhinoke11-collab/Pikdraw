#include "pikdraw/document/document.hpp"
#include <cassert>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::document;

void test_add_and_undo_path() {
    Document doc("doc-test-1");
    assert(doc.revision() == 0);
    assert(!doc.can_undo());

    PathObject po;
    po.id = generate_object_id();
    po.name = "Test Path";
    po.path.move_to({10.0, 20.0});
    po.path.line_to({100.0, 20.0});
    po.path.close();

    auto res = doc.execute_command(std::make_unique<AddObjectCommand>(po));
    assert(res.success);
    assert(doc.revision() == 1);
    assert(doc.can_undo());
    assert(doc.current_snapshot()->object_count() == 1);

    // Undo
    bool undo_res = doc.undo();
    assert(undo_res);
    assert(doc.revision() == 2); // Monotonic revision bump
    assert(doc.current_snapshot()->object_count() == 0);
    assert(doc.can_redo());

    // Redo
    bool redo_res = doc.redo();
    assert(redo_res);
    assert(doc.revision() == 3);
    assert(doc.current_snapshot()->object_count() == 1);
}

void test_remove_and_transform() {
    Document doc("doc-test-2");
    PathObject po;
    po.id = generate_object_id();
    po.name = "Box";
    po.path.move_to({0, 0});
    po.path.line_to({50, 50});

    doc.execute_command(std::make_unique<AddObjectCommand>(po));
    ObjectId id = po.id;

    // Transform
    auto t = geometry::Transform2D::translation(25.0, 30.0);
    doc.execute_command(std::make_unique<TransformObjectCommand>(id, t));
    auto snap = doc.current_snapshot();
    auto obj = snap->get_object(id);
    assert(obj.has_value());
    const auto& po_trans = std::get<PathObject>(*obj);
    assert(po_trans.transform.tx == 25.0);
    assert(po_trans.transform.ty == 30.0);

    // Remove
    doc.execute_command(std::make_unique<RemoveObjectCommand>(id));
    assert(doc.current_snapshot()->object_count() == 0);

    // Undo Remove
    doc.undo();
    assert(doc.current_snapshot()->object_count() == 1);
}

void test_transaction_writer() {
    Document doc("doc-test-3");
    auto& writer = doc.writer();

    writer.begin_transaction("Add Two Objects");

    PathObject p1;
    p1.id = generate_object_id();
    p1.path.move_to({0, 0});
    writer.stage_command(std::make_unique<AddObjectCommand>(p1));

    PathObject p2;
    p2.id = generate_object_id();
    p2.path.move_to({10, 10});
    writer.stage_command(std::make_unique<AddObjectCommand>(p2));

    auto res = writer.commit_transaction();
    assert(res.success);
    assert(doc.current_snapshot()->object_count() == 2);
    assert(doc.revision() == 1); // Only 1 revision bump for entire transaction

    // Single undo undos both objects
    doc.undo();
    assert(doc.current_snapshot()->object_count() == 0);
    assert(doc.revision() == 2);
}

int main() {
    std::cout << "[test_commands] Running command tests...\n";
    test_add_and_undo_path();
    test_remove_and_transform();
    test_transaction_writer();
    std::cout << "[test_commands] All tests passed!\n";
    return 0;
}
