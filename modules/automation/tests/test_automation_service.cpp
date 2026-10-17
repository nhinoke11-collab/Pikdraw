#include "pikdraw/automation/automation_service.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    pikdraw::document::Document document("automation-test", pikdraw::document::Page::make_default());
    pikdraw::automation::AutomationService service(document);

    const auto initial = service.inspect();
    check(initial.document_id == "automation-test" && initial.revision == 0 && initial.object_count == 0,
          "inspect reads the committed document snapshot");
    const auto capabilities = service.capabilities();
    check(capabilities.size() == 11 && capabilities[2].id == "objects.get" &&
              capabilities[3].id == "font.query" && capabilities[4].id == "text.measure" &&
              capabilities[6].id == "batch.validate" && capabilities[7].id == "batch.apply" &&
              capabilities[8].id == "history.undo" && capabilities[9].id == "history.redo" &&
              capabilities[10].id == "document.validate",
          "capabilities include only implemented handlers and typed object operations");

    pikdraw::automation::CreateRectangle request;
    request.expected_epoch = initial.epoch;
    request.expected_revision = initial.revision;
    request.bounds_mm = {12.0, 18.0, 600.0, 300.0};
    const auto created = service.create_rectangle(request);
    check(created.ok() && created.revision_before == 0 && created.revision_after == 1 &&
              created.created_ids.size() == 1,
          "rectangle creation commits one document transaction");
    check(document.can_undo(), "rectangle creation is undoable");

    const auto objects = service.query_objects();
    check(objects.size() == 1 && objects.front().id == created.created_ids.front() &&
              objects.front().type == "path" && objects.front().bounds.x == 12.0 &&
              objects.front().bounds.width == 600.0 &&
              objects.front().bounds_quality == pikdraw::automation::ObjectSummary::BoundsQuality::SourceGeometry,
          "query returns stable ID and exact source bounds");

    request.expected_revision = 0;
    request.bounds_mm.x = 900.0;
    const auto conflict = service.create_rectangle(request);
    check(conflict.status == pikdraw::automation::Status::RevisionConflict &&
              document.revision() == 1 && service.query_objects().size() == 1,
          "stale write is rejected without changing source or history");

    request.expected_revision = document.revision();
    request.bounds_mm.width = 0.0;
    const auto invalid = service.create_rectangle(request);
    check(invalid.status == pikdraw::automation::Status::InvalidArgument &&
              document.revision() == 1 && service.query_objects().size() == 1,
          "invalid geometry is rejected before writer mutation");

    check(document.undo() && document.revision() == 2 && service.query_objects().empty(),
          "undo restores the initial model and advances the monotonic revision");

    auto sign_page = pikdraw::document::Page::make_default();
    sign_page.width_mm = 600.0;
    sign_page.height_mm = 300.0;
    pikdraw::document::Document sign("automation-sign-test", sign_page);
    pikdraw::automation::AutomationService sign_service(sign);
    const auto sign_initial = sign_service.inspect();
    pikdraw::typography::FontIdentity font;
    font.family_name = "Segoe UI";
    font.postscript_name.clear();
    font.size_pt = 30.0;
    const auto text_metrics = sign_service.measure_text("CỬA HÀNG ÁNH DƯƠNG", font);
    check(text_metrics.line_count == 1 && text_metrics.glyph_count > 0 &&
              text_metrics.missing_glyph_count == 0 && text_metrics.bounds_mm.width > 0.0,
          "Unicode text receives real font-shaping metrics and Vietnamese glyph coverage");
    std::vector<pikdraw::automation::CreateObject> sign_objects;
    pikdraw::automation::CreateObject background;
    background.kind = pikdraw::automation::CreateObjectKind::Rectangle;
    background.bounds_mm = {0.0, 0.0, 600.0, 300.0};
    background.stroke_width_mm = 0.0;
    sign_objects.push_back(background);
    pikdraw::automation::CreateObject emblem;
    emblem.kind = pikdraw::automation::CreateObjectKind::Ellipse;
    emblem.bounds_mm = {30.0, 30.0, 44.0, 44.0};
    sign_objects.push_back(emblem);
    pikdraw::automation::CreateObject heading;
    heading.kind = pikdraw::automation::CreateObjectKind::Text;
    heading.text = "CỬA HÀNG ÁNH DƯƠNG";
    heading.position_mm = {80.0, 220.0};
    heading.font = font;
    heading.font.size_pt = 30.0;
    sign_objects.push_back(heading);
    const auto sign_batch = sign_service.create_objects(sign_initial.epoch, sign_initial.revision, sign_objects);
    check(sign_batch.ok() && sign_batch.created_ids.size() == 3 && sign.revision() == 1 &&
              sign.current_snapshot()->object_count() == 3,
          "rectangles, ellipses and editable Unicode text commit in one transaction");
    const auto validated_sign = sign_service.validate_document(10.0, {sign_batch.created_ids.front()});
    check(validated_sign.valid,
          "document validator applies safe margin to content while allowing an explicit bleed background");
    auto invalid_text = heading;
    invalid_text.font.size_pt = 0.0;
    check(!sign_service.validate_objects({invalid_text}).valid && sign.revision() == 1,
          "invalid text style is rejected without changing the committed document");

    pikdraw::document::Document design("automation-design-ops-test", sign_page);
    pikdraw::automation::AutomationService design_service(design);
    const auto design_initial = design_service.inspect();
    pikdraw::automation::CreateObject line;
    line.kind = pikdraw::automation::CreateObjectKind::Line;
    line.layer_index = 0;
    line.path_points_mm = {{20.0, 20.0}, {40.0, 20.0}};
    line.name = "Baseline";
    pikdraw::automation::CreateObject closed_polyline;
    closed_polyline.kind = pikdraw::automation::CreateObjectKind::Polyline;
    closed_polyline.layer_index = 0;
    closed_polyline.path_points_mm = {{100.0, 40.0}, {130.0, 40.0}, {115.0, 70.0}};
    closed_polyline.path_closed = true;
    closed_polyline.name = "Triangle";
    auto disposable_line = line;
    disposable_line.path_points_mm = {{200.0, 20.0}, {210.0, 20.0}};
    disposable_line.name = "Disposable";
    std::vector<pikdraw::automation::BatchOperation> path_batch{line, closed_polyline, disposable_line};
    const auto path_validation = design_service.validate_batch(path_batch);
    check(path_validation.valid && design.revision() == 0,
          "line and closed polyline receive a non-mutating topology preflight");
    const auto path_result = design_service.apply_batch(design_initial.epoch,
        design_initial.revision, path_batch);
    check(path_result.ok() && path_result.created_ids.size() == 3 &&
              path_result.created_operation_indices == std::vector<size_t>{0, 1, 2} &&
              design.revision() == 1 && design.current_snapshot()->object_count() == 3,
          "line and closed polyline commit together as one undo transaction");
    const auto line_variant = design.current_snapshot()->get_object(path_result.created_ids[0]);
    const auto polygon_variant = design.current_snapshot()->get_object(path_result.created_ids[1]);
    const auto* line_path = line_variant ? std::get_if<pikdraw::document::PathObject>(&*line_variant) : nullptr;
    const auto* polygon_path = polygon_variant ? std::get_if<pikdraw::document::PathObject>(&*polygon_variant) : nullptr;
    check(line_path && line_path->authoring_source &&
              line_path->authoring_source->subpaths.front().nodes.size() == 2 &&
              line_path->authoring_source->subpaths.front().segments.size() == 1 &&
              polygon_path && polygon_path->authoring_source &&
              polygon_path->authoring_source->subpaths.front().closed &&
              polygon_path->authoring_source->subpaths.front().nodes.size() == 3 &&
              polygon_path->authoring_source->subpaths.front().segments.size() == 3,
          "MCP-authored paths retain editable source nodes and segments");

    pikdraw::automation::TransformObjects move_line;
    move_line.target_ids = {path_result.created_ids[0]};
    move_line.translate_x_mm = 10.0;
    move_line.scale_x = 2.0;
    move_line.scale_y = 2.0;
    pikdraw::automation::DuplicateObjects duplicate_polygon;
    duplicate_polygon.target_ids = {path_result.created_ids[1]};
    duplicate_polygon.offset_x_mm = 8.0;
    duplicate_polygon.offset_y_mm = 5.0;
    pikdraw::automation::DeleteObjects delete_line;
    delete_line.target_ids = {path_result.created_ids[2]};
    pikdraw::automation::DeleteObjects conflicting_delete_line;
    conflicting_delete_line.target_ids = {path_result.created_ids[0]};
    const auto overlap = design_service.validate_batch({move_line, conflicting_delete_line});
    check(!overlap.valid && overlap.diagnostics.front().find("only once") != std::string::npos &&
              design.revision() == 1,
          "a batch cannot target one object through conflicting operations");
    pikdraw::automation::TransformObjects no_op_transform;
    no_op_transform.target_ids = {path_result.created_ids[0]};
    const auto no_op_validation = design_service.validate_batch({no_op_transform});
    auto out_of_range_duplicate = duplicate_polygon;
    out_of_range_duplicate.offset_x_mm = 1000000.0;
    const auto duplicate_bounds_validation = design_service.validate_batch({out_of_range_duplicate});
    auto invalid_path_layer = line;
    invalid_path_layer.layer_index = 99;
    const auto path_layer_validation = design_service.validate_batch({invalid_path_layer});
    check(!no_op_validation.valid && !duplicate_bounds_validation.valid &&
              !path_layer_validation.valid && design.revision() == 1,
          "no-op transforms, out-of-range duplicate bounds, and invalid path layers fail preflight");
    const auto edit_result = design_service.apply_batch(design_initial.epoch, design.revision(),
        {move_line, duplicate_polygon, delete_line});
    check(edit_result.ok() && design.revision() == 2 &&
              design.current_snapshot()->object_count() == 3 &&
              edit_result.created_ids.size() == 1 &&
              edit_result.created_operation_indices == std::vector<size_t>{1} &&
              edit_result.affected_ids.size() == 3,
          "transform, duplicate, and delete use explicit IDs in one atomic transaction");
    const auto duplicate_variant = design.current_snapshot()->get_object(edit_result.created_ids.front());
    const auto* duplicate_path = duplicate_variant
        ? std::get_if<pikdraw::document::PathObject>(&*duplicate_variant) : nullptr;
    check(duplicate_path && duplicate_path->authoring_source &&
              duplicate_path->authoring_source->subpaths.front().id !=
                  polygon_path->authoring_source->subpaths.front().id &&
              duplicate_path->authoring_source->subpaths.front().nodes.front().id !=
                  polygon_path->authoring_source->subpaths.front().nodes.front().id &&
              duplicate_path->authoring_source->subpaths.front().segments.front().id !=
                  polygon_path->authoring_source->subpaths.front().segments.front().id,
          "duplicated editable geometry receives fresh subpath, node and segment IDs");
    const auto moved_variant = design.current_snapshot()->get_object(path_result.created_ids[0]);
    const auto* moved_path = moved_variant
        ? std::get_if<pikdraw::document::PathObject>(&*moved_variant) : nullptr;
    check(moved_path && std::abs(moved_path->bounds().x - 50.0) < 1e-9 &&
              std::abs(moved_path->bounds().y - 40.0) < 1e-9 &&
              std::abs(moved_path->bounds().width - 40.0) < 1e-9 &&
              !design.current_snapshot()->get_object(path_result.created_ids[2]).has_value(),
          "explicit-ID transform composes move and scale while delete removes its target");
    check(design.undo() && design.revision() == 3 &&
              design.current_snapshot()->object_count() == 3 &&
              design.current_snapshot()->get_object(path_result.created_ids[0]).has_value() &&
              !design.current_snapshot()->get_object(edit_result.created_ids.front()).has_value(),
          "one undo restores the source path and reverses the whole mixed edit batch");

    pikdraw::automation::SetObjectStyle style;
    style.target_ids = {path_result.created_ids[1]};
    style.fill = pikdraw::color::ColorValue::make_rgb(0.92, 0.18, 0.12);
    style.stroke_width_mm = 2.5;
    const auto style_result = design_service.apply_batch(design_initial.epoch, design.revision(), {style});
    const auto styled_variant = design.current_snapshot()->get_object(path_result.created_ids[1]);
    const auto* styled_path = styled_variant
        ? std::get_if<pikdraw::document::PathObject>(&*styled_variant) : nullptr;
    check(style_result.ok() && styled_path && styled_path->has_fill &&
              std::abs(styled_path->stroke_width_mm - 2.5) < 1e-9 && design.revision() == 4,
          "style edits preserve the stable object ID and commit through one transaction");

    pikdraw::automation::AlignObjects align;
    align.reference_id = path_result.created_ids[0];
    align.target_ids = {path_result.created_ids[1]};
    align.mode = pikdraw::automation::AlignmentMode::Left;
    const auto align_validation = design_service.validate_batch({align});
    const auto align_result = design_service.apply_batch(design_initial.epoch, design.revision(), {align});
    const auto aligned_variant = design.current_snapshot()->get_object(path_result.created_ids[1]);
    const auto* aligned_path = aligned_variant
        ? std::get_if<pikdraw::document::PathObject>(&*aligned_variant) : nullptr;
    check(align_validation.valid && align_result.ok() && aligned_path &&
              std::abs(aligned_path->bounds().x - 20.0) < 1e-9 && design.revision() == 5,
          "explicit-reference alignment moves the target to the selected source edge");

    pikdraw::automation::DistributeObjects distribute;
    distribute.target_ids = path_result.created_ids;
    distribute.axis = pikdraw::automation::DistributionAxis::Horizontal;
    const auto distribution_validation = design_service.validate_batch({distribute});
    const auto distribution_result = design_service.apply_batch(
        design_initial.epoch, design.revision(), {distribute});
    const auto distributed_variant = design.current_snapshot()->get_object(path_result.created_ids[1]);
    const auto* distributed_path = distributed_variant
        ? std::get_if<pikdraw::document::PathObject>(&*distributed_variant) : nullptr;
    check(distribution_validation.valid && distribution_result.ok() && distributed_path &&
              std::abs(distributed_path->bounds().x - 105.0) < 1e-9 && design.revision() == 6,
          "horizontal distribution spaces intermediate objects while preserving anchor objects");

    pikdraw::automation::GroupObjects group;
    group.target_ids = {path_result.created_ids[0], path_result.created_ids[1]};
    group.name = "Sign artwork";
    const auto group_result = design_service.apply_batch(design_initial.epoch, design.revision(), {group});
    check(group_result.ok() && group_result.created_ids.size() == 1 && design.revision() == 7 &&
              design_service.query_objects().size() == 2,
          "same-layer top-level objects group atomically without changing child IDs");
    auto undo_group = design_service.undo(design_initial.epoch, design.revision());
    check(undo_group.ok() && design.revision() == 8 && design_service.query_objects().size() == 3,
          "revision-guarded undo reverses the most recent automation group transaction");
    auto redo_group = design_service.redo(design_initial.epoch, design.revision());
    check(redo_group.ok() && design.revision() == 9 && design_service.query_objects().size() == 2,
          "revision-guarded redo restores the same group and child IDs");
    pikdraw::automation::UngroupObjects ungroup;
    ungroup.target_ids = group_result.created_ids;
    const auto ungroup_result = design_service.apply_batch(design_initial.epoch, design.revision(), {ungroup});
    check(ungroup_result.ok() && design.revision() == 10 && design_service.query_objects().size() == 3 &&
              design.current_snapshot()->get_object(path_result.created_ids[0]).has_value() &&
              design.current_snapshot()->get_object(path_result.created_ids[1]).has_value(),
          "ungroup restores all original top-level objects with stable IDs");
    return 0;
}
