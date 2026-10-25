#include "tools/annotation/annotation_tool_controller.hpp"
#include "pikdraw/document/document.hpp"

#include <cassert>
#include <iostream>
#include <cmath>

using namespace pikdraw;
using namespace pikdraw::desktop::tools;

void test_detail_view_gesture_and_commit() {
    document::Document doc;
    AnnotationToolController controller(doc);
    controller.activate(AnnotationToolMode::DetailView);
    assert(controller.active());
    assert(controller.state() == AnnotationSessionState::Idle);

    controller.set_scale_ratio(3.0);
    assert(std::abs(controller.scale_ratio() - 3.0) < 1e-4);
    controller.set_detail_label("DETAIL X");
    assert(controller.detail_label() == "DETAIL X");

    // 1. Mouse down at (10, 10)
    bool handled = controller.handle_pointer_press({10.0, 10.0});
    assert(handled);
    assert(controller.state() == AnnotationSessionState::DetailSourceDragging);
    assert(controller.has_preview());

    // 2. Drag to (30, 10) -> radius 20mm
    controller.handle_pointer_move({30.0, 10.0});
    assert(controller.has_preview());

    // 3. Mouse release -> transitions to FramePlacing
    handled = controller.handle_pointer_release({30.0, 10.0});
    assert(handled);
    assert(controller.state() == AnnotationSessionState::DetailFramePlacing);

    // 4. Move to (100, 100)
    controller.handle_pointer_move({100.0, 100.0});

    // 5. Click to commit
    handled = controller.handle_pointer_press({100.0, 100.0});
    assert(handled);
    assert(controller.state() == AnnotationSessionState::Idle);
    assert(!controller.has_preview());

    // Verify document state
    const auto page = doc.page();
    assert(page.layers.size() >= 3);
    const auto dv_layer_idx = page.find_layer_by_role(document::LayerRole::AnnotationDetailView);
    assert(dv_layer_idx.has_value());
    const auto& dv_layer = page.layers[*dv_layer_idx];
    assert(!dv_layer.object_ids.empty());

    const auto obj_id = dv_layer.object_ids.back();
    assert(doc.objects().contains(obj_id));
    assert(document::DetailViewRegistry::instance().contains(obj_id));
    const auto dv = *document::DetailViewRegistry::instance().get(obj_id);
    assert(dv.label == "DETAIL X");
    assert(std::abs(dv.scale_ratio - 3.0) < 1e-4);

    std::cout << "[PASS] Detail View gesture and transaction commit\n";
}

void test_leader_note_gesture_and_commit() {
    document::Document doc;
    AnnotationToolController controller(doc);
    controller.activate(AnnotationToolMode::LeaderNote);
    controller.set_note_text("Bo R2, khong cat qua lo");

    // 1. Click anchor point (15, 20)
    bool handled = controller.handle_pointer_press({15.0, 20.0});
    assert(handled);
    assert(controller.state() == AnnotationSessionState::LeaderAnchorPicked);

    // 2. Click elbow point (35, 40)
    controller.handle_pointer_move({35.0, 40.0});
    handled = controller.handle_pointer_press({35.0, 40.0});
    assert(handled);
    assert(controller.state() == AnnotationSessionState::LeaderElbowPicked);

    // 3. Click landing/text point (50, 40) -> commit
    controller.handle_pointer_move({50.0, 40.0});
    handled = controller.handle_pointer_press({50.0, 40.0});
    assert(handled);
    assert(controller.state() == AnnotationSessionState::Idle);

    // Verify document state
    const auto page = doc.page();
    const auto note_layer_idx = page.find_layer_by_role(document::LayerRole::AnnotationNotes);
    assert(note_layer_idx.has_value());
    const auto& note_layer = page.layers[*note_layer_idx];
    assert(!note_layer.object_ids.empty());

    const auto obj_id = note_layer.object_ids.back();
    assert(doc.objects().contains(obj_id));
    assert(document::AnnotationRegistry::instance().contains(obj_id));
    const auto ann = *document::AnnotationRegistry::instance().get(obj_id);
    assert(ann.text == "Bo R2, khong cat qua lo");
    assert(std::abs(ann.anchor.anchor_pt.x - 15.0) < 1e-4);

    std::cout << "[PASS] Leader Note gesture and transaction commit\n";
}

void test_escape_cancel() {
    document::Document doc;
    AnnotationToolController controller(doc);
    controller.activate(AnnotationToolMode::LeaderNote);
    controller.handle_pointer_press({10.0, 10.0});
    assert(controller.state() == AnnotationSessionState::LeaderAnchorPicked);

    const bool cancelled = controller.handle_escape();
    assert(cancelled);
    assert(controller.state() == AnnotationSessionState::Idle);
    assert(!controller.has_preview());

    std::cout << "[PASS] AnnotationToolController escape cancellation\n";
}

int main() {
    test_detail_view_gesture_and_commit();
    test_leader_note_gesture_and_commit();
    test_escape_cancel();
    std::cout << "All AnnotationToolController tests PASSED!\n";
    return 0;
}
