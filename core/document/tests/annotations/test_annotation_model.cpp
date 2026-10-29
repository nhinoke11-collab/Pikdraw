#include "pikdraw/document/annotations/annotation_object.hpp"
#include "pikdraw/document/annotations/annotation_layout.hpp"
#include "pikdraw/document/commands/annotation_commands.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/document/storage/native_format.hpp"

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
    ann.anchor.source_object_id = 0xF00000010000002AULL;
    ann.anchor.anchor_pt = {15.0, 25.0};
    ann.anchor.state = AssociationState::Valid;
    ann.anchor.element_id = 0xE000000000000007ULL;
    ann.anchor.parameter = 0.5;
    ann.leader_points = {{30.0, 45.0}, {50.0, 45.0}};
    ann.landing_point = {50.0, 45.0};
    ann.text_point = {55.0, 45.0};
    ann.text = "Chú thích; 100%\nBo R2, không cắt qua lỗ";
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
    assert(parsed->anchor.source_object_id == 0xF00000010000002AULL);
    assert(std::abs(parsed->anchor.anchor_pt.x - 15.0) < 1e-4);
    assert(std::abs(parsed->anchor.anchor_pt.y - 25.0) < 1e-4);
    assert(parsed->anchor.state == AssociationState::Valid);
    assert(parsed->anchor.element_id.has_value() &&
           *parsed->anchor.element_id == 0xE000000000000007ULL);
    assert(parsed->leader_points.size() == 2);
    assert(parsed->text == ann.text);
    assert(std::abs(parsed->style.text_size_mm - 2.5) < 1e-4);
    assert(std::abs(parsed->style.arrow_size_mm - 2.0) < 1e-4);

    auto invalid_number = spec;
    const auto number_pos = invalid_number.find(";anc_x=15");
    assert(number_pos != std::string::npos);
    invalid_number.replace(number_pos, std::string(";anc_x=15").size(), ";anc_x=nan");
    assert(!deserialize_annotation_spec(invalid_number));
    auto too_many_points = spec;
    const auto count_pos = too_many_points.find(";pts_cnt=2");
    assert(count_pos != std::string::npos);
    too_many_points.replace(count_pos, std::string(";pts_cnt=2").size(), ";pts_cnt=4097");
    assert(!deserialize_annotation_spec(too_many_points));
    assert(!deserialize_annotation_spec(spec + ";kind=0"));
    auto invalid_utf8 = ann;
    invalid_utf8.text.assign("\xC0\xAF", 2);
    assert(!validate_annotation_object(invalid_utf8));
    assert(!deserialize_annotation_spec(serialize_annotation_spec(invalid_utf8)));
    auto embedded_nul = ann;
    embedded_nul.text.assign("note\0tail", 9);
    assert(!validate_annotation_object(embedded_nul));
    std::cout << "[PASS] Annotation spec serialization roundtrip\n";
}

void test_detail_view_spec_roundtrip() {
    DetailViewObject dv;
    dv.id = 202;
    dv.marker_id = 0xF0000000000000CBULL;
    dv.label_id = 0xE0000000000000CCULL;
    dv.source_page_id = 0x100000001ULL;
    dv.source_region = {10.0, 20.0, 20.0, 20.0};
    dv.frame_rect = {80.0, 40.0, 60.0, 60.0};
    dv.scale_ratio = 3.0;
    dv.shape = DetailViewShape::Circle;
    dv.show_connector = true;
    dv.label = "DETAIL; 100%\nA · 構造";
    dv.line_width_mm = 0.25;
    dv.text_size_mm = 2.5;

    std::string spec = serialize_detail_view_spec(dv);
    assert(spec.starts_with("detail_view:v1;"));

    std::string error;
    auto parsed = deserialize_detail_view_spec(spec, &error);
    assert(parsed.has_value());
    assert(parsed->marker_id == 0xF0000000000000CBULL);
    assert(parsed->label_id == 0xE0000000000000CCULL);
    assert(parsed->source_page_id == 0x100000001ULL);
    assert(std::abs(parsed->source_region.x - 10.0) < 1e-4);
    assert(std::abs(parsed->source_region.width - 20.0) < 1e-4);
    assert(std::abs(parsed->frame_rect.width - 60.0) < 1e-4);
    assert(std::abs(parsed->scale_ratio - 3.0) < 1e-4);
    assert(parsed->shape == DetailViewShape::Circle);
    assert(parsed->show_connector == true);
    assert(parsed->label == dv.label);
    auto invalid_scale = dv;
    invalid_scale.frame_rect.width -= 1.0;
    assert(!validate_detail_view_object(invalid_scale));
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

    dv.shape = DetailViewShape::Rectangle;
    dv.source_region = {0.0, 0.0, 20.0, 10.0};
    dv.frame_rect = {100.0, -5.0, 40.0, 20.0};
    dv.scale_ratio = 2.0;
    dv.label = "RECT";
    dv.show_connector = true;
    const auto horizontal = evaluate_detail_view_layout(dv);
    const auto& connector_elements = horizontal.connector_path.elements();
    assert(connector_elements.size() == 2);
    assert(std::abs(connector_elements[0].pt.x - 20.0) < 1e-9);
    assert(std::abs(connector_elements[0].pt.y - 5.0) < 1e-9);
    assert(std::abs(connector_elements[1].pt.x - 100.0) < 1e-9);
    assert(std::abs(connector_elements[1].pt.y - 5.0) < 1e-9);

    dv.scale_ratio = 1.25;
    dv.frame_rect = {100.0, 37.5, 25.0, 12.5};
    assert(evaluate_detail_view_layout(dv).formatted_label == "RECT · 1.25:1");
    dv.scale_ratio = 0.5;
    dv.frame_rect = {100.0, 45.0, 10.0, 5.0};
    assert(evaluate_detail_view_layout(dv).formatted_label == "RECT · 1:2");
    assert(format_detail_scale_ratio(1.25) == "1.25:1");
    assert(format_detail_scale_ratio(0.5) == "1:2");
    assert(std::abs(*parse_detail_scale_ratio("2:1") - 2.0) < 1e-9);
    assert(std::abs(*parse_detail_scale_ratio("1:2") - 0.5) < 1e-9);
    assert(std::abs(*parse_detail_scale_ratio("2.5") - 2.5) < 1e-9);
    assert(!parse_detail_scale_ratio("1:0"));
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

    AnnotationObject orphaned_link;
    orphaned_link.anchor.state = AssociationState::Valid;
    orphaned_link.anchor.source_object_id = 0xF0000000000000FEULL;
    const auto rejected_link = doc.execute_command(
        std::make_unique<AddAnnotationCommand>(orphaned_link));
    assert(!rejected_link.success);
    assert(doc.page().layers.size() == 1);

    // Test AddAnnotationCommand execution and undo
    AnnotationObject note_ann;
    note_ann.id = generate_object_id();
    note_ann.text = "Test note";
    note_ann.transform = geometry::Transform2D::translation(13.0, 17.0);
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
    const auto updated_path_value = doc.objects().get(note_id);
    assert(updated_path_value.has_value());
    const auto& updated_path = std::get<PathObject>(*updated_path_value);
    assert(std::abs(updated_path.transform.tx - 13.0) < 1e-9);
    assert(std::abs(updated_path.transform.ty - 17.0) < 1e-9);

    // Undo update
    bool undone = doc.undo();
    assert(undone);
    assert(get_annotation(doc, note_id)->text == "Test note");

    // Undo add
    undone = doc.undo();
    assert(undone);
    assert(!doc.objects().contains(note_id));
    assert(!get_annotation(doc, note_id).has_value());
    assert(doc.page().layers.size() == 1);
    assert(doc.redo());
    assert(get_annotation(doc, note_id).has_value());
    assert(doc.undo());
    assert(doc.page().layers.size() == 1);

    // Detail view add command
    DetailViewObject dv_obj;
    dv_obj.id = generate_object_id();
    dv_obj.label = "DETAIL C";
    dv_obj.source_region = {0, 0, 10, 10};
    dv_obj.frame_rect = {50, 50, 20, 20};
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
    assert(doc.page().layers.size() == 1);

    std::cout << "[PASS] Add/Update/Undo commands for Annotation & DetailView\n";
}

void test_deleting_anchor_breaks_association_and_undo_restores_it() {
    Document doc;
    PathObject source;
    source.id = generate_object_id();
    source.path = geometry::Path2D::make_rect(0, 0, 20, 20);
    const ObjectId source_id = source.id;
    assert(doc.execute_command(std::make_unique<AddObjectCommand>(source)).success);

    AnnotationObject note;
    note.anchor.source_object_id = source_id;
    note.anchor.state = AssociationState::Valid;
    note.text = "Anchor lifecycle";
    const ObjectId note_id = generate_object_id();
    note.id = note_id;
    assert(doc.execute_command(std::make_unique<AddAnnotationCommand>(note)).success);

    assert(doc.execute_command(std::make_unique<RemoveObjectCommand>(source_id)).success);
    const auto broken = get_annotation(doc, note_id);
    assert(broken && broken->anchor.state == AssociationState::Broken);
    assert(doc.undo());
    const auto restored = get_annotation(doc, note_id);
    assert(restored && restored->anchor.state == AssociationState::Valid);
    assert(doc.objects().contains(source_id));
}

void test_annotation_snapshot_is_document_local_and_persistent() {
    constexpr ObjectId shared_id = 0xF100000000000001ULL;
    Document first("first-document");
    Document second("second-document");
    AnnotationObject first_annotation;
    first_annotation.id = shared_id;
    first_annotation.text = "first";
    AnnotationObject second_annotation;
    second_annotation.id = shared_id;
    second_annotation.text = "second";
    assert(first.execute_command(std::make_unique<AddAnnotationCommand>(first_annotation)).success);
    assert(second.execute_command(std::make_unique<AddAnnotationCommand>(second_annotation)).success);
    assert(get_annotation(first, shared_id)->text == "first");
    assert(get_annotation(second, shared_id)->text == "second");
    DetailViewObject first_detail_view;
    first_detail_view.id = 0xF100000000000002ULL;
    first_detail_view.source_page_id = first.active_page_id();
    first_detail_view.label = "DETAIL A · 2:1";
    first_detail_view.source_region = {3.0, 4.0, 12.0, 8.0};
    first_detail_view.frame_rect = {40.0, 50.0, 24.0, 16.0};
    first_detail_view.scale_ratio = 2.0;
    first_detail_view.shape = DetailViewShape::Rectangle;
    const ObjectId shared_detail_view_id = first_detail_view.id;
    assert(first.execute_command(
        std::make_unique<AddDetailViewCommand>(first_detail_view)).success);
    assert(first.execute_command(std::make_unique<ToggleAnnotationGroupCommand>(
        ToggleAnnotationGroupCommand::ToggleKind::Visible)).success);

    const auto bytes = NativeFormat::serialize(*first.current_snapshot());
    assert(bytes.size() > 5 && bytes[4] == kLayerHierarchyContainerVersion);
    std::string error;
    auto reopened_snapshot = NativeFormat::deserialize(bytes.data(), bytes.size(), &error);
    assert(reopened_snapshot && error.empty());
    Document reopened(*reopened_snapshot);
    const auto reopened_annotation = get_annotation(reopened, shared_id);
    assert(reopened_annotation && reopened_annotation->text == "first");
    const auto reopened_detail_view = get_detail_view(reopened, shared_detail_view_id);
    assert(reopened_detail_view && reopened_detail_view->label == "DETAIL A · 2:1");
    assert(reopened_detail_view->source_region.x == 3.0 &&
           reopened_detail_view->source_region.y == 4.0 &&
           reopened_detail_view->source_region.width == 12.0 &&
           reopened_detail_view->source_region.height == 8.0);
    assert(reopened_detail_view->frame_rect.x == 40.0 &&
           reopened_detail_view->frame_rect.y == 50.0 &&
           reopened_detail_view->frame_rect.width == 24.0 &&
           reopened_detail_view->frame_rect.height == 16.0);
    assert(reopened_detail_view->scale_ratio == 2.0);
    const auto group_index = reopened.page().find_layer_by_role(
        LayerRole::AnnotationGroupHeader);
    const auto note_index = reopened.page().find_layer_by_role(
        LayerRole::AnnotationNotes);
    const auto detail_view_index = reopened.page().find_layer_by_role(
        LayerRole::AnnotationDetailView);
    assert(group_index && note_index);
    assert(detail_view_index);
    assert(reopened.page().layers[*group_index].id != kInvalidLayerId);
    assert(reopened.page().layers[*note_index].id != kInvalidLayerId);
    assert(reopened.page().layers[*note_index].group_id ==
           reopened.page().layers[*group_index].id);
    assert(reopened.page().layers[*detail_view_index].group_id ==
           reopened.page().layers[*group_index].id);
    assert(!reopened.page().effective_layer_visible(*note_index));
}

int main() {
    test_annotation_spec_roundtrip();
    test_detail_view_spec_roundtrip();
    test_annotation_layout_and_bounds();
    test_detail_view_layout_and_transform();
    test_annotation_group_and_commands();
    test_deleting_anchor_breaks_association_and_undo_restores_it();
    test_annotation_snapshot_is_document_local_and_persistent();
    std::cout << "All annotation model tests PASSED!\n";
    return 0;
}
