#include "pikdraw/document/document.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/model/object.hpp"
#include "../src/tools/gradient_tool_controller.hpp"
#include "pikdraw/desktop/presenters/gradient_properties.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::desktop;
using namespace pikdraw::desktop::tools;

namespace {

document::ObjectId add_box(document::Document& doc, double x, double y, double w, double h,
                           std::shared_ptr<const document::GradientPaint> gradient = nullptr) {
    document::PathObject obj;
    obj.id = document::generate_object_id();
    obj.name = "Test Box";
    obj.path = geometry::Path2D::make_rect(x, y, w, h);
    obj.has_fill = true;
    obj.fill = color::RGBColor{0.2, 0.4, 0.8, 1.0};
    obj.fill_gradient = std::move(gradient);
    auto res = doc.execute_command(std::make_unique<document::AddObjectCommand>(obj, 0));
    assert(res.success);
    return obj.id;
}

void test_activation_and_lifecycle() {
    std::cout << "--- Test 1: Activation and Lifecycle ---" << std::endl;
    document::Document doc("test-doc-lifecycle");
    const auto id1 = add_box(doc, 0.0, 0.0, 100.0, 80.0);

    GradientToolController controller(doc);
    assert(!controller.is_active());
    assert(!controller.active());
    assert(controller.target_objects().empty());

    bool ok = controller.activate({id1});
    assert(ok);
    assert(controller.is_active());
    assert(controller.target_objects().size() == 1);
    assert(controller.target_objects()[0] == id1);

    const auto& summary = controller.presenter().summary();
    assert(summary.selected_count == 1);
    assert(summary.supported_count == 1);
    assert(!summary.all_gradients); // initially solid

    controller.deactivate();
    assert(!controller.is_active());
    assert(controller.target_objects().empty());
}

void test_canvas_drag_commit_and_threshold() {
    std::cout << "--- Test 2: Canvas Drag Commit and Threshold (AC-G08) ---" << std::endl;
    document::Document doc("test-doc-drag");
    const auto id1 = add_box(doc, 0.0, 0.0, 100.0, 80.0);
    const uint64_t initial_rev = doc.revision();

    GradientToolController controller(doc);
    controller.activate({id1});

    // 2a. Click / drag below 0.5 mm threshold should NOT commit gradient implicitly
    controller.on_pointer_down({10.0, 10.0}, false, false);
    controller.on_pointer_move({10.2, 10.1}, false, false);
    bool committed_sub_thresh = controller.on_pointer_up({10.2, 10.1});
    assert(!committed_sub_thresh);
    assert(doc.revision() == initial_rev);

    // 2b. Drag above threshold commits atomically with single undo
    controller.on_pointer_down({10.0, 10.0}, false, false);
    controller.on_pointer_move({90.0, 70.0}, false, false);
    bool committed = controller.on_pointer_up({90.0, 70.0});
    assert(committed);
    assert(doc.revision() > initial_rev);
    assert(doc.can_undo());

    // Verify object has gradient
    const auto* obj_var = doc.objects().lookup_ref(id1);
    assert(obj_var);
    const auto* path_obj = std::get_if<document::PathObject>(obj_var);
    assert(path_obj);
    assert(path_obj->fill_gradient != nullptr);
    assert(path_obj->fill_gradient->kind == document::GradientKind::Linear);

    // 2c. Drag cancel should restore committed state without extra commit
    const uint64_t post_commit_rev = doc.revision();
    controller.on_pointer_down({10.0, 10.0}, false, false);
    controller.on_pointer_move({50.0, 50.0}, false, false);
    controller.on_cancel();
    assert(doc.revision() == post_commit_rev);
    assert(!controller.has_unapplied_changes());
}

void test_session_token_and_stale_rejection() {
    std::cout << "--- Test 3: Session Token and Stale Result Rejection (AC-G08) ---" << std::endl;
    document::Document doc("test-doc-token");
    const auto id1 = add_box(doc, 0.0, 0.0, 100.0, 80.0);
    const auto id2 = add_box(doc, 110.0, 0.0, 100.0, 80.0);

    GradientToolController controller(doc);
    controller.activate({id1});

    const auto token1 = controller.current_token();
    assert(controller.validate_token(token1));
    assert(controller.publish_preview(token1));

    // Simulate external document change (e.g. undo or external edit)
    const auto id3 = add_box(doc, 220.0, 0.0, 50.0, 50.0);
    (void)id3;
    controller.on_document_changed();

    // Old token must be rejected
    assert(!controller.validate_token(token1));
    assert(!controller.publish_preview(token1));
    assert(!controller.apply_with_token(token1));

    // Change selection: old token must also be rejected
    const auto token2 = controller.current_token();
    assert(controller.validate_token(token2));
    controller.on_selection_changed({id2});
    assert(!controller.validate_token(token2));
    assert(!controller.publish_preview(token2));
    assert(!controller.apply_with_token(token2));
}

void test_properties_editing_enter_and_apply() {
    std::cout << "--- Test 4: Properties Presenter Editing, Enter, and Apply (AC-G09) ---" << std::endl;
    document::Document doc("test-doc-properties");
    const auto id1 = add_box(doc, 0.0, 0.0, 100.0, 80.0);
    const uint64_t initial_rev = doc.revision();

    GradientToolController controller(doc);
    controller.activate({id1});

    // Modify spread and angle in draft
    controller.presenter().set_spread(document::GradientSpread::Repeat);
    controller.presenter().set_angle_degrees(45.0);
    assert(controller.has_unapplied_changes());
    assert(doc.revision() == initial_rev); // Not committed yet

    // Enter confirms field value without committing document
    controller.presenter().confirm_field();
    assert(controller.has_unapplied_changes());
    assert(doc.revision() == initial_rev);

    // Apply commits transaction atomically
    bool applied = controller.apply();
    assert(applied);
    assert(!controller.has_unapplied_changes());
    assert(doc.revision() > initial_rev);

    // Check applied gradient properties
    const auto* obj_var = doc.objects().lookup_ref(id1);
    assert(obj_var);
    const auto* path_obj = std::get_if<document::PathObject>(obj_var);
    assert(path_obj && path_obj->fill_gradient);
    assert(path_obj->fill_gradient->spread == document::GradientSpread::Repeat);
    const auto proj = project_gradient_geometry(path_obj->fill_gradient->kind,
                                                path_obj->fill_gradient->gradient_to_object);
    assert(std::abs(proj.angle_degrees - 45.0) < 0.1);
}

void test_stop_operations_and_constraints() {
    std::cout << "--- Test 5: Stop Operations and Constraints (AC-G09, AC-G10) ---" << std::endl;
    document::Document doc("test-doc-stops");
    const auto id1 = add_box(doc, 0.0, 0.0, 100.0, 80.0);

    GradientToolController controller(doc);
    controller.activate({id1});

    auto& pres = controller.presenter();
    assert(pres.draft_paint()->stops.size() == 2);

    // Add stop at 0.5
    bool added = pres.add_stop(0.5, color::RGBColor{1.0, 0.0, 0.0, 1.0});
    assert(added);
    assert(pres.draft_paint()->stops.size() == 3);
    assert(pres.draft_paint()->stops[1].offset == 0.5);
    assert(pres.draft_paint()->stops[0].midpoint_to_next == 0.5);
    assert(pres.draft_paint()->stops[1].midpoint_to_next == 0.5);

    // Delete stop when count > 2 succeeds
    std::string reason;
    assert(pres.can_delete_stop(1, &reason));
    bool deleted = pres.delete_stop(1, &reason);
    assert(deleted);
    assert(pres.draft_paint()->stops.size() == 2);

    // Delete stop when count == 2 must be rejected with explicit reason
    assert(!pres.can_delete_stop(0, &reason));
    assert(reason == "Gradient phải có tối thiểu 2 stop.");
    bool deleted_min = pres.delete_stop(0, &reason);
    assert(!deleted_min);
    assert(pres.draft_paint()->stops.size() == 2);

    // Reverse stops
    pres.set_stop_offset(0, 0.2);
    pres.set_stop_offset(1, 0.8);
    pres.set_segment_midpoint(0, 0.7);
    pres.reverse_stops();
    assert(std::abs(pres.draft_paint()->stops[0].offset - 0.2) < 1e-4);
    assert(std::abs(pres.draft_paint()->stops[1].offset - 0.8) < 1e-4);
    assert(std::abs(pres.draft_paint()->stops[0].midpoint_to_next - 0.3) < 1e-4);
}

void test_multi_selection_mixed_summary_and_update() {
    std::cout << "--- Test 6: Multi-Selection Mixed Summary and Update (AC-G09) ---" << std::endl;
    document::Document doc("test-doc-mixed");

    // Create 2 objects with different color gradients
    auto grad1 = document::GradientPaint::make_default_linear(
        1001, geometry::Rect2D{0.0, 0.0, 50.0, 50.0},
        color::RGBColor{1.0, 0.0, 0.0, 1.0},
        color::RGBColor{0.0, 1.0, 0.0, 1.0}
    );
    auto grad2 = document::GradientPaint::make_default_linear(
        1002, geometry::Rect2D{60.0, 0.0, 50.0, 50.0},
        color::RGBColor{0.0, 0.0, 1.0, 1.0},
        color::RGBColor{1.0, 1.0, 0.0, 1.0}
    );

    const auto id1 = add_box(doc, 0.0, 0.0, 50.0, 50.0,
                             std::make_shared<const document::GradientPaint>(grad1));
    const auto id2 = add_box(doc, 60.0, 0.0, 50.0, 50.0,
                             std::make_shared<const document::GradientPaint>(grad2));

    GradientToolController controller(doc);
    controller.activate({id1, id2});

    const auto& summary = controller.presenter().summary();
    assert(summary.selected_count == 2);
    assert(summary.supported_count == 2);
    assert(summary.all_gradients);
    assert(!summary.mixed_kind); // both Linear
    assert(!summary.mixed_spread); // both Pad
    assert(summary.mixed_stops); // different colors!

    // Update ONLY spread to Reflect
    controller.presenter().set_spread(document::GradientSpread::Reflect);
    assert(controller.has_unapplied_changes());

    // Apply should update spread on BOTH, but preserve their distinct stops!
    bool applied = controller.apply();
    assert(applied);

    const auto* obj1 = std::get_if<document::PathObject>(doc.objects().lookup_ref(id1));
    const auto* obj2 = std::get_if<document::PathObject>(doc.objects().lookup_ref(id2));
    assert(obj1 && obj1->fill_gradient);
    assert(obj2 && obj2->fill_gradient);

    // Both have Reflect
    assert(obj1->fill_gradient->spread == document::GradientSpread::Reflect);
    assert(obj2->fill_gradient->spread == document::GradientSpread::Reflect);

    // Distinct colors are preserved!
    const auto* rgb1 = std::get_if<color::RGBColor>(&obj1->fill_gradient->stops[0].color.payload());
    const auto* rgb2 = std::get_if<color::RGBColor>(&obj2->fill_gradient->stops[0].color.payload());
    assert(rgb1 && rgb1->r > 0.9); // Red
    assert(rgb2 && rgb2->b > 0.9); // Blue
}

void test_unapplied_changes_discarded_on_selection_change() {
    std::cout << "--- Test 7: Unapplied Changes Discarded on Selection Change (AC-G08) ---" << std::endl;
    document::Document doc("test-doc-discard");
    const auto id1 = add_box(doc, 0.0, 0.0, 50.0, 50.0);
    const auto id2 = add_box(doc, 60.0, 0.0, 50.0, 50.0);
    const uint64_t initial_rev = doc.revision();

    GradientToolController controller(doc);
    controller.activate({id1});

    // Make an unapplied edit in properties
    controller.presenter().set_kind(document::GradientKind::Radial);
    assert(controller.has_unapplied_changes());

    // Switch selection to id2
    controller.on_selection_changed({id2});

    // Draft should be discarded, status message set, document revision unchanged
    assert(!controller.has_unapplied_changes());
    assert(doc.revision() == initial_rev);
    assert(controller.status_message() == "Đã hủy thay đổi Gradient chưa áp dụng");

    // Target 1 must NOT have any gradient applied
    const auto* obj1 = std::get_if<document::PathObject>(doc.objects().lookup_ref(id1));
    assert(obj1 && obj1->fill_gradient == nullptr);
}

} // namespace

int main() {
    std::cout << "=== Running PikDraw Desktop Gradient Tool Controller Tests ===" << std::endl;

    test_activation_and_lifecycle();
    test_canvas_drag_commit_and_threshold();
    test_session_token_and_stale_rejection();
    test_properties_editing_enter_and_apply();
    test_stop_operations_and_constraints();
    test_multi_selection_mixed_summary_and_update();
    test_unapplied_changes_discarded_on_selection_change();

    std::cout << "=== All Desktop Gradient Tool Controller Tests PASSED! ===" << std::endl;
    return 0;
}
