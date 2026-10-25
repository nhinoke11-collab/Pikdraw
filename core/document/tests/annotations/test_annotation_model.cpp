#include "pikdraw/document/annotations/annotation_object.hpp"
#include "pikdraw/document/annotations/annotation_layout.hpp"
#include "pikdraw/document/commands/annotation_commands.hpp"
#include "pikdraw/document/document.hpp"

#include <cassert>
#include <iostream>
#include <cmath>

using namespace pikdraw;
using namespace pikdraw::document;

void test_annotation_spec_roundtrip() {
    AnnotationObject ann;
    ann.id = 101;
    ann.name = "Note 1";
    ann.kind = AnnotationKind::LeaderNote;
    ann.anchor.source_object_id = 42;
    ann.anchor.anchor_pt = {15.0, 25.0};
    ann.anchor.state = AssociationState::Valid;
    ann.anchor.element_id = 7;
    ann.anchor.parameter = 0.5;
    ann.leader_points = {{30.0, 45.0}, {50.0, 45.0}};
    ann.landing_point = {50.0, 45.0};
    ann.text_point = {55.0, 45.0};
    ann.text = "Bo R2, khong cat qua lo";
    ann.style.text_size_mm = 2.5;
    ann.style.line_width_mm = 0.25;
    ann.style.arrow_size_mm = 2.0;
    ann.style.has_landing = true;
    ann.style.landing_length_mm = 4.0;
    ann.style.gap_mm = 1.0;

    std::string spec = serialize_annotation_spec(ann);
    assert(spec.starts_with("annotation:v1;"));

    std::string error;
    auto parsed = deserialize_annotation_spec(spec, &error);
    assert(parsed.has_value());
    assert(parsed->kind == AnnotationKind::LeaderNote);
    assert(parsed->anchor.source_object_id == 42);
    assert(std::abs(parsed->anchor.anchor_pt.x - 15.0) < 1e-4);
    assert(std::abs(parsed->anchor.anchor_pt.y - 25.0) < 1e-4);
    assert(parsed->anchor.state == AssociationState::Valid);
    assert(parsed->anchor.element_id.has_value() && *parsed->anchor.element_id == 7);
    assert(parsed->leader_points.size() == 2);
    assert(parsed->text == "Bo R2, khong cat qua lo");
    assert(std::abs(parsed->style.text_size_mm - 2.5) < 1e-4);
    assert(std::abs(parsed->style.arrow_size_mm - 2.0) < 1e-4);
    std::cout << "[PASS] Annotation spec serialization roundtrip\n";
}

void test_detail_view_spec_roundtrip() {
    DetailViewObject dv;
    dv.id = 202;
    dv.marker_id = 203;
    dv.label_id = 204;
    dv.source_page_id = 1;
    dv.source_region = {10.0, 20.0, 20.0, 20.0};
    dv.frame_rect = {80.0, 40.0, 60.0, 60.0};
    dv.scale_ratio = 3.0;
    dv.shape = DetailViewShape::Circle;
    dv.show_connector = true;
    dv.label = "DETAIL A";
    dv.line_width_mm = 0.25;
    dv.text_size_mm = 2.5;

    std::string spec = serialize_detail_view_spec(dv);
    assert(spec.starts_with("detail_view:v1;"));

    std::string error;
    auto parsed = deserialize_detail_view_spec(spec, &error);
    assert(parsed.has_value());
    assert(parsed->marker_id == 203);
    assert(parsed->label_id == 204);
    assert(std::abs(parsed->source_region.x - 10.0) < 1e-4);
    assert(std::abs(parsed->source_region.width - 20.0) < 1e-4);
    assert(std::abs(parsed->frame_rect.width - 60.0) < 1e-4);
    assert(std::abs(parsed->scale_ratio - 3.0) < 1e-4);
    assert(parsed->shape == DetailViewShape::Circle);
    assert(parsed->show_connector == true);
    assert(parsed->label == "DETAIL A");
    std::cout << "[PASS] Detail view spec serialization roundtrip\n";
}

void test_annotation_layout_and_bounds() {
    AnnotationObject ann;
    ann.anchor.anchor_pt = {10.0, 10.0};
    ann.leader_points = {{30.0, 30.0}};
    ann.text_point = {40.0, 30.0};
    ann.text = "Weld 5mm";
    ann.style.arrow_type = geometry::ArrowType::ClosedFilled;
    ann.style.arrow_size_mm = 2.0;
    ann.style.has_landing = true;
    ann.style.landing_length_mm = 5.0;

    const auto layout = evaluate_annotation_layout(ann);
    assert(!layout.leader_path.empty());
    assert(!layout.arrow_path.empty());
    assert(layout.bounds.width > 20.0);
    assert(layout.bounds.height > 10.0);
    assert(layout.text_origin.x >= 35.0); // landing ends at 35.0

    const auto path_obj = annotation_to_path_object(ann, layout);
    assert(path_obj.contour_spec.starts_with("annotation:v1;"));
    assert(path_obj.has_stroke == true);
    assert(path_obj.has_fill == false);
    std::cout << "[PASS] Annotation layout & path conversion\n";
}

void test_detail_view_layout_and_transform() {
    DetailViewObject dv;
    dv.source_region = {10.0, 10.0, 20.0, 20.0}; // center: 20, 20
    dv.frame_rect = {70.0, 70.0, 60.0, 60.0};    // center: 100, 100
    dv.scale_ratio = 3.0; // 3:1
    dv.shape = DetailViewShape::Circle;
    dv.show_connector = true;
    dv.label = "DETAIL B";

    const auto layout = evaluate_detail_view_layout(dv);
    assert(!layout.marker_path.empty());
    assert(!layout.frame_path.empty());
    assert(!layout.connector_path.empty());
    assert(layout.formatted_label == "DETAIL B · 3:1");

    // Center of source maps to center of frame under source_to_frame_transform
    const auto mapped_center = layout.source_to_frame_transform.map_point({20.0, 20.0});
    assert(std::abs(mapped_center.x - 100.0) < 1e-4);
    assert(std::abs(mapped_center.y - 100.0) < 1e-4);

    // Vector of length 5 maps to length 15 (3x scale)
    const auto mapped_pt = layout.source_to_frame_transform.map_point({25.0, 20.0});
    assert(std::abs(mapped_pt.x - 115.0) < 1e-4);

    const auto path_obj = detail_view_to_path_object(dv, layout);
    assert(path_obj.contour_spec.starts_with("detail_view:v1;"));
    std::cout << "[PASS] Detail view layout & transform mapping\n";
}

void test_annotation_group_and_commands() {
    Document doc;
    Page page = doc.page();
    assert(page.layers.size() == 1);
    assert(page.layers[0].name == "Layer 1");

    // Test ensure_annotation_layer creates Chú thích group header and Ghi chú child
    const size_t note_idx = ensure_annotation_layer(page, LayerRole::AnnotationNotes);
    assert(page.layers.size() == 3); // Layer 1, Chú thích, Ghi chú
    assert(page.layers[1].role == LayerRole::AnnotationGroupHeader);
    assert(page.layers[1].name == "Chú thích");
    assert(page.layers[2].role == LayerRole::AnnotationNotes);
    assert(page.layers[2].name == "Ghi chú");
    assert(page.layers[2].group_id == page.layers[1].id);
    assert(note_idx == 2);

    // Ensure Ô chi tiết adds child under same group without duplicating header
    const size_t dv_idx = ensure_annotation_layer(page, LayerRole::AnnotationDetailView);
    assert(page.layers.size() == 4);
    assert(page.layers[3].role == LayerRole::AnnotationDetailView);
    assert(page.layers[3].name == "Ô chi tiết");
    assert(page.layers[3].group_id == page.layers[1].id);
    assert(dv_idx == 3);

    // Effective visibility test
    assert(page.effective_layer_visible(2) == true);
    assert(page.effective_layer_locked(2) == false);
    assert(page.effective_layer_printable(2) == true);

    // If group header is hidden, child effective visibility is false, but child's own flag is still true!
    page.layers[1].visible = false;
    assert(page.layers[2].visible == true);
    assert(page.effective_layer_visible(2) == false);
    page.layers[1].visible = true;
    assert(page.effective_layer_visible(2) == true);

    // If group header is locked, child effective locked is true
    page.layers[1].locked = true;
    assert(page.effective_layer_locked(2) == true);
    page.layers[1].locked = false;

    std::cout << "[PASS] Annotation group & effective inheritance flags\n";

    // Test AddAnnotationCommand execution and undo
    AnnotationObject note_ann;
    note_ann.id = generate_object_id();
    note_ann.text = "Test note";
    note_ann.anchor.anchor_pt = {5.0, 5.0};
    note_ann.leader_points = {{10.0, 10.0}};
    note_ann.text_point = {15.0, 10.0};
    const ObjectId note_id = note_ann.id;

    auto add_cmd = std::make_unique<AddAnnotationCommand>(note_ann);
    auto res = doc.execute_command(std::move(add_cmd));
    assert(res.success);
    assert(doc.objects().contains(note_id));
    assert(get_annotation(doc, note_id).has_value());
    assert(get_annotation(doc, note_id)->text == "Test note");

    // Update command
    auto updated = *get_annotation(doc, note_id);
    updated.text = "Updated note text";
    auto upd_cmd = std::make_unique<UpdateAnnotationCommand>(note_id, updated);
    res = doc.execute_command(std::move(upd_cmd));
    assert(res.success);
    assert(get_annotation(doc, note_id)->text == "Updated note text");

    // Undo update
    bool undone = doc.undo();
    assert(undone);
    assert(get_annotation(doc, note_id)->text == "Test note");

    // Undo add
    undone = doc.undo();
    assert(undone);
    assert(!doc.objects().contains(note_id));
    assert(!get_annotation(doc, note_id).has_value());

    // Detail view add command
    DetailViewObject dv_obj;
    dv_obj.id = generate_object_id();
    dv_obj.label = "DETAIL C";
    dv_obj.source_region = {0, 0, 10, 10};
    dv_obj.frame_rect = {50, 50, 30, 30};
    dv_obj.scale_ratio = 2.0;
    const ObjectId dv_id = dv_obj.id;

    auto add_dv_cmd = std::make_unique<AddDetailViewCommand>(dv_obj);
    res = doc.execute_command(std::move(add_dv_cmd));
    assert(res.success);
    assert(doc.objects().contains(dv_id));
    assert(get_detail_view(doc, dv_id).has_value());
    assert(get_detail_view(doc, dv_id)->label == "DETAIL C");

    // Undo add detail view
    undone = doc.undo();
    assert(undone);
    assert(!doc.objects().contains(dv_id));
    assert(!get_detail_view(doc, dv_id).has_value());

    std::cout << "[PASS] Add/Update/Undo commands for Annotation & DetailView\n";
}

int main() {
    test_annotation_spec_roundtrip();
    test_detail_view_spec_roundtrip();
    test_annotation_layout_and_bounds();
    test_detail_view_layout_and_transform();
    test_annotation_group_and_commands();
    std::cout << "All annotation model tests PASSED!\n";
    return 0;
}
