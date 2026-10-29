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

    // 1. Drag the source circle from center (10, 10) to radius 20mm.
    bool handled = controller.handle_pointer_press({10.0, 10.0});
    assert(handled);
    assert(controller.state() == AnnotationSessionState::DetailSourceDragging);
    assert(!controller.has_preview()); // The down event only arms a drag; it creates no default region.

    // 2. Drag to (30, 10) -> radius 20mm
    controller.handle_pointer_move({30.0, 10.0});
    assert(controller.has_preview());

    // 3. Mouse release locks the source region.
    handled = controller.handle_pointer_release({30.0, 10.0});
    assert(handled);
    assert(controller.state() == AnnotationSessionState::DetailFramePlacing);

    // 4. Move to top-left placement point (100, 100).
    controller.handle_pointer_move({100.0, 100.0});

    // 5. A click sequence commits on release and uses the top-left point.
    handled = controller.handle_pointer_press({100.0, 100.0});
    assert(handled);
    assert(controller.state() == AnnotationSessionState::DetailPlacementPressed);
    handled = controller.handle_pointer_release({100.0, 100.0});
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
    const auto dv = document::get_detail_view(doc, obj_id);
    assert(dv.has_value());
    assert(dv->label == "DETAIL X");
    assert(std::abs(dv->scale_ratio - 3.0) < 1e-4);

    std::cout << "[PASS] Detail View gesture and transaction commit\n";
}

void test_rectangle_detail_view_uses_final_pointer_and_uniform_scale() {
    document::Document doc;
    AnnotationToolController controller(doc);
    controller.activate(AnnotationToolMode::DetailView);
    controller.set_detail_shape(document::DetailViewShape::Rectangle);
    controller.set_scale_ratio(2.0);
    assert(controller.handle_pointer_press({10.0, 10.0}));
    assert(controller.handle_pointer_release({40.0, 30.0}));
    assert(controller.state() == AnnotationSessionState::DetailFramePlacing);

    // No move event is sent for the final click. Its release position is the
    // top-left corner of the frame.
    assert(controller.handle_pointer_press({130.0, 120.0}));
    assert(controller.handle_pointer_release({130.0, 120.0}));
    const auto layer_index = doc.page().find_layer_by_role(
        document::LayerRole::AnnotationDetailView);
    assert(layer_index.has_value());
    const auto& ids = doc.page().layers[*layer_index].object_ids;
    assert(ids.size() == 1);
    const auto detail = document::get_detail_view(doc, ids.front());
    assert(detail.has_value());
    assert(std::abs(detail->source_region.x - 10.0) < 1e-9);
    assert(std::abs(detail->source_region.y - 10.0) < 1e-9);
    assert(std::abs(detail->source_region.width - 30.0) < 1e-9);
    assert(std::abs(detail->source_region.height - 20.0) < 1e-9);
    assert(std::abs(detail->frame_rect.width - 60.0) < 1e-9);
    assert(std::abs(detail->frame_rect.height - 40.0) < 1e-9);
    assert(std::abs(detail->frame_rect.x - 130.0) < 1e-9);
    assert(std::abs(detail->frame_rect.y - 80.0) < 1e-9);
}

void test_detail_click_does_not_create_default_region() {
    document::Document doc;
    AnnotationToolController controller(doc);
    controller.activate(AnnotationToolMode::DetailView);
    assert(controller.handle_pointer_press({10.0, 10.0}, std::nullopt, std::nullopt, {100.0, 100.0}));
    assert(!controller.handle_pointer_release({10.0, 10.0}, {100.5, 100.5}));
    assert(controller.state() == AnnotationSessionState::Idle);
    const auto layer_index = doc.page().find_layer_by_role(document::LayerRole::AnnotationDetailView);
    assert(!layer_index.has_value());
}

void test_detail_frame_anchor_remains_fixed_when_scale_changes() {
    document::Document doc;
    AnnotationToolController controller(doc);
    controller.activate(AnnotationToolMode::DetailView);
    controller.set_detail_shape(document::DetailViewShape::Rectangle);
    assert(controller.handle_pointer_press({10.0, 10.0}));
    assert(controller.handle_pointer_release({40.0, 30.0}));
    controller.handle_pointer_move({130.0, 120.0});
    controller.set_scale_ratio(2.5);
    const auto& frame = controller.draft_detail_view().frame_rect;
    assert(std::abs(frame.x - 130.0) < 1e-9);
    assert(std::abs(frame.y - 70.0) < 1e-9);
    assert(std::abs(frame.width - 75.0) < 1e-9);
    assert(std::abs(frame.height - 50.0) < 1e-9);
}

void test_numeric_detail_geometry_updates_keep_the_other_anchor() {
    document::Document doc;
    AnnotationToolController controller(doc);
    controller.activate(AnnotationToolMode::DetailView);
    controller.set_detail_shape(document::DetailViewShape::Rectangle);
    assert(controller.handle_pointer_press({10.0, 10.0}));
    assert(controller.handle_pointer_release({40.0, 30.0}));
    controller.handle_pointer_move({130.0, 120.0});

    assert(controller.set_source_region({20.0, 15.0, 24.0, 12.0}));
    auto detail = controller.draft_detail_view();
    assert(std::abs(detail.source_region.x - 20.0) < 1e-9);
    assert(std::abs(detail.source_region.y - 15.0) < 1e-9);
    assert(std::abs(detail.frame_rect.x - 130.0) < 1e-9);
    assert(std::abs(detail.frame_rect.y + detail.frame_rect.height - 120.0) < 1e-9);
    assert(std::abs(detail.frame_rect.width - 48.0) < 1e-9);
    assert(std::abs(detail.frame_rect.height - 24.0) < 1e-9);

    assert(controller.set_frame_top_left({150.0, 175.0}));
    detail = controller.draft_detail_view();
    assert(std::abs(detail.source_region.x - 20.0) < 1e-9);
    assert(std::abs(detail.frame_rect.x - 150.0) < 1e-9);
    assert(std::abs(detail.frame_rect.y + detail.frame_rect.height - 175.0) < 1e-9);
}

void test_leader_note_gesture_and_commit() {
    document::Document doc;
    AnnotationToolController controller(doc);
    controller.activate(AnnotationToolMode::LeaderNote);

    // 1. Click anchor point (15, 20)
    bool handled = controller.handle_pointer_press({15.0, 20.0});
    assert(handled);
    assert(controller.state() == AnnotationSessionState::LeaderAnchorPicked);

    // 2. Click elbow point (35, 40)
    controller.handle_pointer_move({35.0, 40.0});
    handled = controller.handle_pointer_press({35.0, 40.0});
    assert(handled);
    assert(controller.state() == AnnotationSessionState::LeaderElbowPicked);

    // 3. Click landing/text point to place the note and enter actual text.
    controller.handle_pointer_move({50.0, 40.0});
    handled = controller.handle_pointer_press({50.0, 40.0});
    assert(handled);
    assert(controller.state() == AnnotationSessionState::NoteTextEditing);
    for (const char c : std::string("Bo R2, khong cat qua lo"))
        assert(controller.append_note_character(static_cast<uint32_t>(c)));
    assert(controller.commit());
    assert(controller.state() == AnnotationSessionState::Idle);

    // Verify document state
    const auto page = doc.page();
    const auto note_layer_idx = page.find_layer_by_role(document::LayerRole::AnnotationNotes);
    assert(note_layer_idx.has_value());
    const auto& note_layer = page.layers[*note_layer_idx];
    assert(!note_layer.object_ids.empty());

    const auto obj_id = note_layer.object_ids.back();
    assert(doc.objects().contains(obj_id));
    const auto ann = document::get_annotation(doc, obj_id);
    assert(ann.has_value());
    assert(ann->text == "Bo R2, khong cat qua lo");
    assert(std::abs(ann->anchor.anchor_pt.x - 15.0) < 1e-4);

    std::cout << "[PASS] Leader Note gesture and transaction commit\n";
}

void test_plain_note_requires_text_and_accepts_unicode() {
    document::Document doc;
    AnnotationToolController controller(doc);
    controller.activate(AnnotationToolMode::PlainNote);
    assert(controller.handle_pointer_press({25.0, 35.0}));
    assert(controller.state() == AnnotationSessionState::NoteTextEditing);
    assert(!controller.commit()); // An empty annotation must not be committed.
    assert(controller.append_note_character('A'));
    assert(controller.append_note_character(0xd83d));
    assert(controller.append_note_character(0xde80));
    assert(controller.commit());
    const auto layer_index = doc.page().find_layer_by_role(
        document::LayerRole::AnnotationNotes);
    assert(layer_index);
    const auto& ids = doc.page().layers[*layer_index].object_ids;
    assert(ids.size() == 1);
    const auto note = document::get_annotation(doc, ids.front());
    assert(note && note->text == "A\xF0\x9F\x9A\x80");
}

void test_note_template_and_straight_leader() {
    document::Document doc;
    AnnotationToolController controller(doc);
    controller.set_note_text("Template note");
    controller.set_note_font_family("Arial");
    controller.set_note_text_size_mm(3.0);
    controller.activate(AnnotationToolMode::PlainNote);
    assert(controller.handle_pointer_press({25.0, 35.0}));
    assert(controller.note_text() == "Template note");
    assert(controller.commit());
    auto ids = doc.page().layers[*doc.page().find_layer_by_role(
        document::LayerRole::AnnotationNotes)].object_ids;
    const auto plain = document::get_annotation(doc, ids.back());
    assert(plain && plain->text == "Template note");
    assert(plain->style.font_family == "Arial");
    assert(std::abs(plain->style.text_size_mm - 3.0) < 1e-9);

    controller.set_note_text("Straight leader");
    controller.set_leader_line_mode(LeaderLineMode::Straight);
    controller.activate(AnnotationToolMode::LeaderNote);
    assert(controller.handle_pointer_press({5.0, 5.0}));
    assert(controller.handle_pointer_press({30.0, 5.0}));
    assert(controller.note_text_editing());
    assert(controller.commit());
    ids = doc.page().layers[*doc.page().find_layer_by_role(
        document::LayerRole::AnnotationNotes)].object_ids;
    const auto leader = document::get_annotation(doc, ids.back());
    assert(leader && leader->kind == document::AnnotationKind::LeaderNote);
    assert(leader->text == "Straight leader");
    assert(leader->leader_points.size() == 1);
    assert(std::abs(leader->leader_points.front().x - 30.0) < 1e-9);
}

void test_existing_note_text_edit_updates_same_owner_and_undoes() {
    document::Document doc;
    AnnotationToolController controller(doc);
    controller.activate(AnnotationToolMode::PlainNote);
    assert(controller.handle_pointer_press({10.0, 15.0}));
    assert(controller.append_note_character('O'));
    assert(controller.append_note_character('l'));
    assert(controller.append_note_character('d'));
    assert(controller.commit());
    const auto id = *controller.last_committed_id();
    const auto original = document::get_annotation(doc, id);
    assert(original && original->text == "Old");

    controller.begin_edit_annotation(*original);
    assert(controller.note_text_editing());
    controller.set_note_text("Updated");
    assert(controller.commit());
    assert(controller.last_committed_id() == id);
    assert(document::get_annotation(doc, id)->text == "Updated");
    assert(doc.undo());
    assert(document::get_annotation(doc, id)->text == "Old");
}

void test_rebinding_document_cancels_pending_draft() {
    document::Document first;
    document::Document second;
    AnnotationToolController controller(first);
    controller.activate(AnnotationToolMode::PlainNote);
    assert(controller.handle_pointer_press({4.0, 7.0}));
    assert(controller.append_note_character('x'));
    controller.bind_document(second);
    assert(controller.bound_to(second));
    assert(controller.state() == AnnotationSessionState::Idle);
    assert(!controller.has_preview());
    assert(!controller.commit());
    assert(first.page().find_layer_by_role(document::LayerRole::AnnotationNotes) == std::nullopt);
    assert(second.page().find_layer_by_role(document::LayerRole::AnnotationNotes) == std::nullopt);
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
    test_rectangle_detail_view_uses_final_pointer_and_uniform_scale();
    test_detail_click_does_not_create_default_region();
    test_detail_frame_anchor_remains_fixed_when_scale_changes();
    test_numeric_detail_geometry_updates_keep_the_other_anchor();
    test_leader_note_gesture_and_commit();
    test_plain_note_requires_text_and_accepts_unicode();
    test_note_template_and_straight_leader();
    test_existing_note_text_edit_updates_same_owner_and_undoes();
    test_rebinding_document_cancels_pending_draft();
    test_escape_cancel();
    std::cout << "All AnnotationToolController tests PASSED!\n";
    return 0;
}
