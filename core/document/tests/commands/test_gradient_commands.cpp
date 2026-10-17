#include "pikdraw/document/document.hpp"
#include "pikdraw/document/commands/gradient_commands.hpp"
#include <cassert>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::document;

namespace {

PathObject make_simple_path(ObjectId id, const std::string& name, const color::ColorValue& fill_color) {
    PathObject po;
    po.id = id;
    po.name = name;
    po.fill = fill_color;
    po.has_fill = true;
    po.fill_gradient = nullptr;
    po.path.move_to({0.0, 0.0});
    po.path.line_to({100.0, 0.0});
    po.path.line_to({100.0, 50.0});
    po.path.close();
    return po;
}

PathObject get_path(const Document& doc, ObjectId id) {
    const auto opt = doc.objects().get(id);
    assert(opt.has_value() && std::holds_alternative<PathObject>(*opt));
    return std::get<PathObject>(*opt);
}

} // namespace

void test_batch_apply_and_undo_redo() {
    reset_gradient_id_allocators_for_tests();

    Document doc("doc-gradient-test-1");
    const auto id1 = generate_object_id();
    const auto id2 = generate_object_id();

    auto p1 = make_simple_path(id1, "Path 1", color::RGBColor{1.0, 0.0, 0.0, 1.0});
    auto p2 = make_simple_path(id2, "Path 2", color::RGBColor{0.0, 1.0, 0.0, 1.0});

    auto r1 = doc.execute_command(std::make_unique<AddObjectCommand>(p1));
    if (!r1.success) {
        std::cerr << "r1 failed: " << r1.error_message << "\n";
    }
    assert(r1.success);

    auto r2 = doc.execute_command(std::make_unique<AddObjectCommand>(p2));
    if (!r2.success) {
        std::cerr << "r2 failed: " << r2.error_message << "\n";
    }
    assert(r2.success);
    const uint64_t r_base = doc.revision();
    assert(r_base == 2);

    // Create Linear gradient for p1 and Radial gradient for p2
    auto grad1 = std::make_shared<GradientPaint>(
        GradientPaint::make_default_linear(allocate_gradient_paint_id(), geometry::Rect2D{0, 0, 100, 50}));
    auto grad2 = std::make_shared<GradientPaint>(
        GradientPaint::make_default_radial(allocate_gradient_paint_id(), geometry::Rect2D{0, 0, 100, 50}));

    std::vector<FillPaintReplacement> batch = {
        FillPaintReplacement{id1, true, p1.fill, grad1},
        FillPaintReplacement{id2, true, p2.fill, grad2}
    };

    // Execute batch
    auto res = doc.execute_command(std::make_unique<SetFillPaintCommand>(batch));
    if (!res.success) {
        std::cerr << "SetFillPaintCommand failed: " << res.error_message << "\n";
    }
    assert(res.success);
    assert(!res.is_no_change);
    assert(doc.revision() == r_base + 1);

    // Verify p1 and p2 have gradients and preserved names / geometry
    const auto& p1_after = get_path(doc, id1);
    assert(p1_after.name == "Path 1");
    assert(p1_after.fill_gradient != nullptr);
    assert(p1_after.fill_gradient->kind == GradientKind::Linear);
    assert(p1_after.effective_fill_paint().kind == FillPaintKind::Gradient);

    const auto& p2_after = get_path(doc, id2);
    assert(p2_after.name == "Path 2");
    assert(p2_after.fill_gradient != nullptr);
    assert(p2_after.fill_gradient->kind == GradientKind::Radial);
    assert(p2_after.effective_fill_paint().kind == FillPaintKind::Gradient);

    // Undo: single-step undo for the whole batch
    assert(doc.can_undo());
    assert(doc.undo());
    assert(doc.revision() == r_base + 2); // monotonic revision increase

    const auto& p1_undone = get_path(doc, id1);
    assert(p1_undone.fill_gradient == nullptr);
    assert(p1_undone.effective_fill_paint().kind == FillPaintKind::Solid);

    const auto& p2_undone = get_path(doc, id2);
    assert(p2_undone.fill_gradient == nullptr);
    assert(p2_undone.effective_fill_paint().kind == FillPaintKind::Solid);

    // Redo
    assert(doc.can_redo());
    assert(doc.redo());
    assert(doc.revision() == r_base + 3);

    const auto& p1_redone = get_path(doc, id1);
    assert(p1_redone.fill_gradient != nullptr);
    assert(p1_redone.fill_gradient->kind == GradientKind::Linear);

    const auto& p2_redone = get_path(doc, id2);
    assert(p2_redone.fill_gradient != nullptr);
    assert(p2_redone.fill_gradient->kind == GradientKind::Radial);

    std::cout << "[PASS] test_batch_apply_and_undo_redo\n";
}

void test_noop_does_not_dirty_or_add_undo() {
    Document doc("doc-gradient-test-2");
    const auto id1 = generate_object_id();
    auto p1 = make_simple_path(id1, "Path 1", color::RGBColor{0.2, 0.4, 0.6, 1.0});
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(p1)).success);

    doc.mark_saved(doc.epoch(), doc.revision());
    assert(!doc.is_dirty());
    const uint64_t r_saved = doc.revision();

    // No-op replacement: identical values
    std::vector<FillPaintReplacement> noop_batch = {
        FillPaintReplacement{id1, true, p1.fill, nullptr}
    };

    auto res = doc.execute_command(std::make_unique<SetFillPaintCommand>(noop_batch));
    assert(res.success);
    assert(res.is_no_change);
    assert(doc.revision() == r_saved);
    assert(!doc.is_dirty());

    std::cout << "[PASS] test_noop_does_not_dirty_or_add_undo\n";
}

void test_error_handling_atomic_preservation() {
    Document doc("doc-gradient-test-3");
    const auto id1 = generate_object_id();
    const auto id2 = generate_object_id();

    auto p1 = make_simple_path(id1, "Path 1", color::RGBColor{1, 0, 0, 1});
    auto p2 = make_simple_path(id2, "Path 2", color::RGBColor{0, 1, 0, 1});
    p2.locked = true; // p2 is locked!

    assert(doc.execute_command(std::make_unique<AddObjectCommand>(p1)).success);
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(p2)).success);
    const uint64_t r_before = doc.revision();

    auto grad = std::make_shared<GradientPaint>(
        GradientPaint::make_default_linear(1, geometry::Rect2D{0, 0, 100, 50}));

    // Batch contains p1 (unlocked) and p2 (locked)
    std::vector<FillPaintReplacement> locked_batch = {
        FillPaintReplacement{id1, true, p1.fill, grad},
        FillPaintReplacement{id2, true, p2.fill, grad}
    };

    auto res = doc.execute_command(std::make_unique<SetFillPaintCommand>(locked_batch));
    assert(!res.success);
    assert(doc.revision() == r_before);

    // Verify neither object was modified!
    const auto& p1_check = get_path(doc, id1);
    assert(p1_check.fill_gradient == nullptr);

    // Non-existent ID batch
    std::vector<FillPaintReplacement> missing_batch = {
        FillPaintReplacement{999999, true, p1.fill, grad}
    };
    auto res2 = doc.execute_command(std::make_unique<SetFillPaintCommand>(missing_batch));
    assert(!res2.success);
    assert(doc.revision() == r_before);

    // Invalid gradient in batch (singular matrix)
    auto bad_grad = std::make_shared<GradientPaint>(*grad);
    const_cast<GradientPaint*>(bad_grad.get())->gradient_to_object = geometry::Transform2D{0, 0, 0, 0, 0, 0};
    assert(!bad_grad->is_valid());

    std::vector<FillPaintReplacement> invalid_grad_batch = {
        FillPaintReplacement{id1, true, p1.fill, bad_grad}
    };
    auto res3 = doc.execute_command(std::make_unique<SetFillPaintCommand>(invalid_grad_batch));
    assert(!res3.success);
    assert(doc.revision() == r_before);

    std::cout << "[PASS] test_error_handling_atomic_preservation\n";
}

int main() {
    std::cout << "Starting test_gradient_commands...\n";
    std::cout.flush();
    try {
        test_batch_apply_and_undo_redo();
        test_noop_does_not_dirty_or_add_undo();
        test_error_handling_atomic_preservation();
        std::cout << "All test_gradient_commands checks PASSED.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Exception in test_gradient_commands: " << e.what() << "\n";
        return 1;
    }
}
