#include "pikdraw/automation/automation_rpc.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

namespace {
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

pikdraw::automation::JsonValue parse(const char* text) {
    pikdraw::automation::JsonValue value;
    std::string error;
    check(pikdraw::automation::JsonValue::parse(text, value, error), "test JSON parses");
    return value;
}
}

int main() {
    using namespace pikdraw;
    automation::JsonValue parsed;
    std::string error;
    check(automation::JsonValue::parse("{\"text\":\"\\u0043\\u1ee7a\",\"n\":1.25}", parsed, error),
          "JSON parser decodes Unicode and finite numbers");
    check(parsed.get("text") && parsed.get("text")->string() &&
              *parsed.get("text")->string() == "Của" &&
              parsed.serialize().find("Của") != std::string::npos,
          "JSON UTF-8 round trip preserves Vietnamese text");
    check(!automation::JsonValue::parse("{\"x\":NaN}", parsed, error),
          "JSON parser rejects non-standard non-finite values");
    check(!automation::JsonValue::parse("{\"x\":1,\"x\":2}", parsed, error),
          "JSON parser rejects duplicate keys");
    check(!automation::JsonValue::parse("\"\\ud800\"", parsed, error),
          "JSON parser rejects unpaired UTF-16 surrogates");
    std::string invalid_utf8 = "{\"x\":\"";
    invalid_utf8.push_back(static_cast<char>(0xc0));
    invalid_utf8.push_back(static_cast<char>(0xaf));
    invalid_utf8 += "\"}";
    check(!automation::JsonValue::parse(invalid_utf8, parsed, error),
          "JSON parser rejects overlong invalid UTF-8 sequences");

    document::Document document("rpc-test", document::Page::make_default());
    automation::AutomationService service(document);
    automation::AutomationRpc rpc(service, {"document.read", "document.edit"});
    auto read_session = rpc.handle(parse("{\"operation\":\"session.create\",\"scopes\":[\"document.read\"]}"));
    check(read_session.get("status") && *read_session.get("status")->string() == "ok",
          "read-only session is issued when allowed by the host");
    const auto session = *read_session.get("session_handle")->string();
    automation::JsonValue inspect_request(automation::JsonValue::Object{
        {"operation", "document.inspect"}, {"session_handle", session}});
    auto state = rpc.handle(inspect_request);
    check(state.get("document_id") && *state.get("document_id")->string() == "rpc-test",
          "document inspection is scoped to an active session");
    const auto initial = service.inspect();
    automation::JsonValue::Object create_args{
        {"operation", "object.create_rectangle"}, {"request_id", "test-write-1"},
        {"idempotency_key", "test-key-1"}, {"expected_epoch", std::to_string(initial.epoch)},
        {"expected_revision", std::to_string(initial.revision)}, {"x_mm", 0.0}, {"y_mm", 0.0},
        {"width_mm", 10.0}, {"height_mm", 10.0}, {"session_handle", session}};
    automation::JsonValue create_request(std::move(create_args));
    auto denied = rpc.handle(create_request);
    check(denied.get("code") && *denied.get("code")->string() == "SCOPE_DENIED" &&
              document.revision() == 0,
          "read-only session cannot mutate the document");
    auto write_session = rpc.handle(parse("{\"operation\":\"session.create\",\"scopes\":[\"document.read\",\"document.edit\"]}"));
    const auto write_handle = *write_session.get("session_handle")->string();
    create_args = *create_request.object();
    create_args["session_handle"] = write_handle;
    create_request = automation::JsonValue(std::move(create_args));
    auto created = rpc.handle(create_request);
    check(created.get("status") && *created.get("status")->string() == "ok" && document.revision() == 1,
          "write session creates a rectangle using explicit revision and millimeter values");
    const auto created_id = *created.get("created_ids")->array()->front().string();
    auto retried = rpc.handle(create_request);
    check(document.revision() == 1 &&
              *retried.get("created_ids")->array()->front().string() == created_id,
          "retrying the same idempotency key returns the original receipt without a second edit");
    auto conflicting_args = *create_request.object();
    conflicting_args["width_mm"] = 20.0;
    auto conflicting = rpc.handle(automation::JsonValue(std::move(conflicting_args)));
    check(conflicting.get("code") && *conflicting.get("code")->string() == "IDEMPOTENCY_CONFLICT" &&
              document.revision() == 1,
          "reusing a write key for a different payload is rejected");
    automation::JsonValue request_lookup(automation::JsonValue::Object{
        {"operation", "request.get"}, {"session_handle", write_handle}, {"idempotency_key", "test-key-1"}});
    auto looked_up = rpc.handle(request_lookup);
    check(looked_up.get("status") && *looked_up.get("status")->string() == "ok" &&
              *looked_up.get("created_ids")->array()->front().string() == created_id,
          "request.get resolves a committed write receipt by idempotency key");

    const auto before_batch = service.inspect();
    automation::JsonValue::Array batch_operations{
        automation::JsonValue::Object{{"op", "object.create_rectangle"}, {"client_ref", "panel"},
            {"x_mm", 10.0}, {"y_mm", 10.0}, {"width_mm", 30.0}, {"height_mm", 12.0}},
        automation::JsonValue::Object{{"op", "object.create_rectangle"}, {"client_ref", "stripe"},
            {"x_mm", 12.0}, {"y_mm", 22.0}, {"width_mm", 26.0}, {"height_mm", 2.0}}};
    automation::JsonValue::Object validate_args{{"operation", "batch.validate"},
        {"session_handle", write_handle}, {"expected_epoch", std::to_string(before_batch.epoch)},
        {"expected_revision", std::to_string(before_batch.revision)}, {"operations", batch_operations}};
    auto validation = rpc.handle(automation::JsonValue(validate_args));
    check(validation.get("valid") && *validation.get("valid")->boolean() && document.revision() == 1,
          "batch.validate reports a valid design batch without mutating the document");
    automation::JsonValue::Object batch_args{{"operation", "batch.apply"},
        {"session_handle", write_handle}, {"request_id", "test-batch-1"},
        {"idempotency_key", "test-batch-key"}, {"expected_epoch", std::to_string(before_batch.epoch)},
        {"expected_revision", std::to_string(before_batch.revision)}, {"operations", batch_operations}};
    auto batch = rpc.handle(automation::JsonValue(batch_args));
    check(batch.get("status") && *batch.get("status")->string() == "ok" && document.revision() == 2 &&
              batch.get("created_ids")->array()->size() == 2 &&
              *batch.get("client_ref_to_id")->get("panel")->string() ==
                  *batch.get("created_ids")->array()->at(0).string(),
          "batch.apply commits multiple objects in one revision and maps client references");
    auto repeated_batch = rpc.handle(automation::JsonValue(batch_args));
    check(document.revision() == 2 && repeated_batch.get("created_ids")->array()->size() == 2,
          "retrying a batch returns the receipt without applying a second transaction");
    auto invalid_batch_ops = batch_operations;
    invalid_batch_ops[1] = automation::JsonValue::Object{{"op", "object.create_rectangle"},
        {"x_mm", 0.0}, {"y_mm", 0.0}, {"width_mm", -5.0}, {"height_mm", 10.0}};
    const auto snapshot_before_invalid_batch = document.current_snapshot();
    const bool dirty_before_invalid_batch = document.is_dirty();
    const bool undo_before_invalid_batch = document.can_undo();
    auto invalid_args = batch_args;
    invalid_args["request_id"] = "test-batch-invalid";
    invalid_args["idempotency_key"] = "test-batch-invalid-key";
    invalid_args["expected_revision"] = std::to_string(service.inspect().revision);
    invalid_args["operations"] = std::move(invalid_batch_ops);
    auto invalid_batch = rpc.handle(automation::JsonValue(std::move(invalid_args)));
    check(invalid_batch.get("code") && *invalid_batch.get("code")->string() == "INVALID_ARGUMENT" &&
              document.revision() == 2 && document.current_snapshot() == snapshot_before_invalid_batch &&
              document.is_dirty() == dirty_before_invalid_batch &&
              document.can_undo() == undo_before_invalid_batch,
          "AC02 invalid batch preserves root snapshot, dirty state and undo history without partial changes");
    automation::JsonValue objects(automation::JsonValue::Object{
        {"operation", "objects.query"}, {"session_handle", write_handle}});
    auto queried = rpc.handle(objects);
    const auto* items = queried.get("objects");
    check(items && items->array() && items->array()->size() == 3,
          "object query returns committed model data after the atomic batch");
    automation::JsonValue page_one(automation::JsonValue::Object{
        {"operation", "objects.query"}, {"session_handle", write_handle}, {"limit", 2.0}});
    auto first_page = rpc.handle(page_one);
    check(first_page.get("objects")->array()->size() == 2 &&
              *first_page.get("next_offset")->string() == "2",
          "objects.query returns bounded pages and a continuation offset");
    const auto summary_page = rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "objects.query"}, {"session_handle", write_handle}, {"limit", 2.0},
        {"fields", automation::JsonValue::Array{"id", "type", "name", "layer_index", "bounds"}}}));
    const auto* summary_field = summary_page.get("objects");
    const auto* summary_objects = summary_field ? summary_field->array() : nullptr;
    check(summary_objects && summary_objects->size() == 2 &&
              summary_objects->front().get("id") && summary_objects->front().get("bounds") &&
              !summary_objects->front().get("path_elements"),
          "objects.query fields projection returns metadata without path geometry");
    const auto invalid_projection = rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "objects.query"}, {"session_handle", write_handle},
        {"fields", automation::JsonValue::Array{"id", "pixels"}}}));
    check(invalid_projection.get("code") && *invalid_projection.get("code")->string() == "INVALID_ARGUMENT",
          "objects.query rejects unsupported projections instead of silently dropping requested data");
    automation::JsonValue::Object filtered_query_args{
        {"operation", "objects.query"}, {"session_handle", write_handle}, {"limit", 2.0}, {"type", "path"}};
    const auto filtered_page_one = rpc.handle(automation::JsonValue(filtered_query_args));
    const auto* filtered_page_one_objects = filtered_page_one.get("objects");
    check(filtered_page_one.get("status") && *filtered_page_one.get("status")->string() == "ok" &&
              filtered_page_one_objects && filtered_page_one_objects->array() &&
              filtered_page_one_objects->array()->size() == 2 &&
              filtered_page_one.get("next_cursor") &&
              filtered_page_one.get("next_cursor")->string()->starts_with("v2:"),
          "type-filtered object query returns a bounded page and a filter-bound cursor");
    auto filtered_page_two_args = filtered_query_args;
    filtered_page_two_args["cursor"] = *filtered_page_one.get("next_cursor")->string();
    filtered_page_two_args["expected_epoch"] = *filtered_page_one.get("epoch")->string();
    filtered_page_two_args["expected_revision"] = *filtered_page_one.get("revision")->string();
    const auto filtered_page_two = rpc.handle(automation::JsonValue(filtered_page_two_args));
    check(filtered_page_two.get("objects") && filtered_page_two.get("objects")->array() &&
              filtered_page_two.get("objects")->array()->size() == 1 &&
              *filtered_page_two.get("offset")->string() == "2" &&
              filtered_page_two.get("next_cursor")->string()->empty(),
          "filtered cursor paging resumes at the next matching root and reports the logical offset");
    const auto valid_filtered_cursor = *filtered_page_one.get("next_cursor")->string();
    const auto fingerprint_separator = valid_filtered_cursor.rfind(':');
    const auto offset_separator = valid_filtered_cursor.rfind(':', fingerprint_separator - 1);
    auto oversized_cursor = valid_filtered_cursor.substr(0, offset_separator + 1) +
        "18446744073709551615:" + valid_filtered_cursor.substr(fingerprint_separator + 1);
    auto oversized_cursor_args = filtered_query_args;
    oversized_cursor_args["cursor"] = oversized_cursor;
    oversized_cursor_args["expected_epoch"] = *filtered_page_one.get("epoch")->string();
    oversized_cursor_args["expected_revision"] = *filtered_page_one.get("revision")->string();
    const auto oversized_cursor_result = rpc.handle(automation::JsonValue(std::move(oversized_cursor_args)));
    check(oversized_cursor_result.get("code") &&
              *oversized_cursor_result.get("code")->string() == "INVALID_ARGUMENT",
          "filtered cursors reject offsets beyond the current page object count");
    auto changed_filter_args = filtered_page_two_args;
    changed_filter_args["type"] = "image";
    const auto changed_filter_page = rpc.handle(automation::JsonValue(std::move(changed_filter_args)));
    check(changed_filter_page.get("code") &&
              *changed_filter_page.get("code")->string() == "INVALID_ARGUMENT",
          "filtered cursor rejects reuse with a different query or active page");
    const auto id_filtered = rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "objects.query"}, {"session_handle", write_handle}, {"object_id", created_id}}));
    check(id_filtered.get("objects") && id_filtered.get("objects")->array() &&
              id_filtered.get("objects")->array()->size() == 1 &&
              *id_filtered.get("objects")->array()->front().get("id")->string() == created_id,
          "object_id filter returns the requested top-level object by stable ID");
    const auto region_filtered = rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "objects.query"}, {"session_handle", write_handle},
        {"region_mm", automation::JsonValue::Object{{"x_mm", 0.0}, {"y_mm", 0.0},
            {"width_mm", 5.0}, {"height_mm", 5.0}}}}));
    check(region_filtered.get("objects") && region_filtered.get("objects")->array() &&
              region_filtered.get("objects")->array()->size() == 1 &&
              *region_filtered.get("objects")->array()->front().get("id")->string() == created_id,
          "region_mm filter uses committed hierarchy bounds to select intersecting roots");
    const auto invalid_layer_filter = rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "objects.query"}, {"session_handle", write_handle}, {"layer_index", 99.0}}));
    check(invalid_layer_filter.get("code") &&
              *invalid_layer_filter.get("code")->string() == "INVALID_ARGUMENT",
          "object query rejects a layer filter outside the active page");
    automation::JsonValue page_two(automation::JsonValue::Object{
        {"operation", "objects.query"}, {"session_handle", write_handle}, {"limit", 2.0},
        {"offset", "2"}, {"expected_epoch", *first_page.get("epoch")->string()},
        {"expected_revision", *first_page.get("revision")->string()}});
    auto second_page = rpc.handle(page_two);
    check(second_page.get("objects")->array()->size() == 1,
          "stable query paging returns the remaining object under the same revision");
    automation::JsonValue cursor_page_two(automation::JsonValue::Object{
        {"operation", "objects.query"}, {"session_handle", write_handle}, {"limit", 2.0},
        {"cursor", *first_page.get("next_cursor")->string()},
        {"expected_epoch", *first_page.get("epoch")->string()},
        {"expected_revision", *first_page.get("revision")->string()}});
    const auto cursor_second_page = rpc.handle(cursor_page_two);
    check(cursor_second_page.get("objects")->array()->size() == 1 &&
              *cursor_second_page.get("objects")->array()->front().get("id")->string() ==
                  *second_page.get("objects")->array()->front().get("id")->string(),
          "cursor paging preserves stable object order while retaining offset compatibility");
    const auto malformed_cursor = rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "objects.query"}, {"session_handle", write_handle},
        {"cursor", "v2:0:2"}, {"expected_epoch", *first_page.get("epoch")->string()},
        {"expected_revision", *first_page.get("revision")->string()}}));
    check(malformed_cursor.get("code") && *malformed_cursor.get("code")->string() == "INVALID_ARGUMENT",
          "object query rejects unknown cursor versions instead of guessing the position");
    auto stale_page = *page_two.object();
    stale_page["expected_revision"] = std::to_string(before_batch.revision);
    auto stale_page_result = rpc.handle(automation::JsonValue(std::move(stale_page)));
    check(stale_page_result.get("code") && *stale_page_result.get("code")->string() == "REVISION_CONFLICT",
          "continuation pages reject a revision from before a committed edit");

    auto font_query = rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "font.query"}, {"session_handle", write_handle},
        {"font_family", "Segoe UI"}, {"font_size_pt", 24.0}}));
    check(font_query.get("status") && *font_query.get("status")->string() == "ok" &&
              font_query.get("actual_postscript_name") &&
              !*font_query.get("fallback")->boolean(),
          "font.query reports a resolved installed face without a fallback");
    auto text_measure = rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "text.measure"}, {"session_handle", write_handle},
        {"text", "CỬA HÀNG ÁNH DƯƠNG"}, {"font_family", "Segoe UI"},
        {"font_size_pt", 24.0}, {"alignment", "center"}}));
    check(text_measure.get("status") && *text_measure.get("status")->string() == "ok" &&
              *text_measure.get("missing_glyph_count")->number() == 0.0 &&
              *text_measure.get("glyph_count")->number() > 0.0,
          "text.measure shapes Vietnamese Unicode and returns actual glyph metrics");

    const auto current = service.inspect();
    automation::JsonValue::Array typed_operations{
        automation::JsonValue::Object{{"op", "object.create_ellipse"}, {"client_ref", "sun"},
            {"x_mm", 40.0}, {"y_mm", 40.0}, {"width_mm", 32.0}, {"height_mm", 32.0}},
        automation::JsonValue::Object{{"op", "object.create_text"}, {"client_ref", "title"},
            {"text", "CỬA HÀNG ÁNH DƯƠNG"}, {"font_family", "Segoe UI"},
            {"font_size_pt", 24.0}, {"x_mm", 90.0}, {"y_mm", 80.0}}};
    automation::JsonValue::Object typed_batch_args{{"operation", "batch.apply"},
        {"session_handle", write_handle}, {"request_id", "typed-batch-1"},
        {"idempotency_key", "typed-batch-key"}, {"expected_epoch", std::to_string(current.epoch)},
        {"expected_revision", std::to_string(current.revision)}, {"operations", typed_operations}};
    const auto typed_batch = rpc.handle(automation::JsonValue(typed_batch_args));
    check(typed_batch.get("status") && *typed_batch.get("status")->string() == "ok" &&
              typed_batch.get("created_ids")->array()->size() == 2 && document.revision() == current.revision + 1,
          "ellipse and editable text batch commit atomically with stable client references");
    const auto document_validation = rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "document.validate"}, {"session_handle", write_handle}}));
    check(document_validation.get("valid") && *document_validation.get("valid")->boolean(),
          "document.validate reports a passing page and text metric assessment for supported content");
    check(!rpc.has_scope(write_handle, "file.write"),
          "document edit does not silently grant file.write within the application session");
    const auto unknown = rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "document.inspect"}, {"session_handle", write_handle}, {"ignore_me", true}}));
    check(unknown.get("code") && *unknown.get("code")->string() == "INVALID_ARGUMENT",
          "RPC rejects unknown fields instead of silently dropping unsupported input");

    const auto before_design_ops = service.inspect();
    const auto panel_id = *batch.get("created_ids")->array()->at(0).string();
    const auto stripe_id = *batch.get("created_ids")->array()->at(1).string();
    const auto ellipse_id = *typed_batch.get("created_ids")->array()->at(0).string();
    automation::JsonValue::Array points{
        automation::JsonValue::Object{{"x_mm", 180.0}, {"y_mm", 40.0}},
        automation::JsonValue::Object{{"x_mm", 210.0}, {"y_mm", 40.0}},
        automation::JsonValue::Object{{"x_mm", 195.0}, {"y_mm", 65.0}}};
    automation::JsonValue::Array panel_target{automation::JsonValue(panel_id)};
    automation::JsonValue::Array ellipse_target{automation::JsonValue(ellipse_id)};
    automation::JsonValue::Array stripe_target{automation::JsonValue(stripe_id)};
    automation::JsonValue::Array design_operations{
        automation::JsonValue::Object{{"op", "object.create_polyline"}, {"client_ref", "triangle"},
            {"points_mm", points}, {"closed", true}, {"name", "AI Triangle"}},
        automation::JsonValue::Object{{"op", "object.transform"}, {"target_ids", panel_target},
            {"translate_x_mm", 1.0}, {"pivot_x_mm", 0.0}, {"pivot_y_mm", 0.0}},
        automation::JsonValue::Object{{"op", "object.duplicate"}, {"target_ids", ellipse_target},
            {"offset_x_mm", 5.0}, {"offset_y_mm", 3.0}},
        automation::JsonValue::Object{{"op", "object.delete"}, {"target_ids", stripe_target}}};
    automation::JsonValue::Object design_validate_args{{"operation", "batch.validate"},
        {"session_handle", write_handle}, {"expected_epoch", std::to_string(before_design_ops.epoch)},
        {"expected_revision", std::to_string(before_design_ops.revision)},
        {"operations", design_operations}};
    const auto design_validation = rpc.handle(automation::JsonValue(design_validate_args));
    check(design_validation.get("valid") && *design_validation.get("valid")->boolean() &&
              document.revision() == before_design_ops.revision,
          "batch.validate accepts editable path and explicit-ID design edits without mutation");
    automation::JsonValue::Object design_apply_args{{"operation", "batch.apply"},
        {"session_handle", write_handle}, {"request_id", "design-ops-1"},
        {"idempotency_key", "design-ops-key"},
        {"expected_epoch", std::to_string(before_design_ops.epoch)},
        {"expected_revision", std::to_string(before_design_ops.revision)},
        {"operations", design_operations}};
    const auto design_receipt = rpc.handle(automation::JsonValue(design_apply_args));
    check(design_receipt.get("status") && *design_receipt.get("status")->string() == "ok" &&
              document.revision() == before_design_ops.revision + 1 &&
              design_receipt.get("created_ids")->array()->size() == 2 &&
              design_receipt.get("affected_ids")->array()->size() == 4 &&
              *design_receipt.get("client_ref_to_id")->get("triangle")->string() ==
                  *design_receipt.get("created_ids")->array()->at(0).string(),
          "one idempotent batch creates a path, transforms, duplicates, and deletes by stable IDs");
    const auto object_page = rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "objects.query"}, {"session_handle", write_handle}, {"limit", 100.0}}));
    bool found_editable_polyline = false;
    bool found_transformed_panel = false;
    for (const auto& item : *object_page.get("objects")->array()) {
        const auto* id = item.get("id");
        if (!id || !id->string()) continue;
        if (*id->string() == *design_receipt.get("created_ids")->array()->at(0).string()) {
            const auto* editable = item.get("editable_path");
            const auto* subpaths = editable ? editable->get("subpaths") : nullptr;
            const auto* nodes = subpaths && subpaths->array() && !subpaths->array()->empty()
                ? subpaths->array()->front().get("nodes") : nullptr;
            const auto* segments = subpaths && subpaths->array() && !subpaths->array()->empty()
                ? subpaths->array()->front().get("segments") : nullptr;
            found_editable_polyline = editable && nodes && nodes->array() &&
                !nodes->array()->empty() && nodes->array()->front().get("id") &&
                segments && segments->array() && segments->array()->size() == 3;
        }
        if (*id->string() == panel_id) {
            const auto* transform = item.get("transform");
            const auto* tx = transform ? transform->get("tx_mm") : nullptr;
            found_transformed_panel = tx && tx->number() && std::abs(*tx->number() - 1.0) < 1e-9;
        }
    }
    check(found_editable_polyline && found_transformed_panel,
          "objects.query exposes editable path IDs and the resulting object transform matrix");
    const auto repeated_design = rpc.handle(automation::JsonValue(design_apply_args));
    check(document.revision() == before_design_ops.revision + 1 &&
              repeated_design.get("created_ids")->array()->size() == 2,
          "retrying a mixed edit batch returns the original receipt without duplicating geometry");
    check(document.undo() && document.revision() == before_design_ops.revision + 2 &&
              document.current_snapshot()->object_count() == 5,
          "AC02 one document undo reverses the entire mixed MCP design transaction");
    const auto triangle_id = *design_receipt.get("created_ids")->array()->at(0).string();
    const auto duplicate_id = *design_receipt.get("created_ids")->array()->at(1).string();
    check(!document.current_snapshot()->get_object(std::stoull(triangle_id)).has_value() &&
              !document.current_snapshot()->get_object(std::stoull(duplicate_id)).has_value() &&
              document.current_snapshot()->get_object(std::stoull(stripe_id)).has_value(),
          "AC02 undo restores deleted source IDs and removes every object created by the atomic batch");
    check(document.redo() && document.revision() == before_design_ops.revision + 3 &&
              document.current_snapshot()->object_count() == 6 &&
              document.current_snapshot()->get_object(std::stoull(triangle_id)).has_value() &&
              document.current_snapshot()->get_object(std::stoull(duplicate_id)).has_value() &&
              !document.current_snapshot()->get_object(std::stoull(stripe_id)).has_value(),
          "AC02 redo reapplies the complete atomic batch with its original stable IDs");
    check(document.undo() && document.revision() == before_design_ops.revision + 4 &&
              document.current_snapshot()->object_count() == 5 &&
              document.current_snapshot()->get_object(std::stoull(stripe_id)).has_value(),
          "AC02 second undo restores the prior source document for the remaining RPC checks");

    const auto after_undo = service.inspect();
    std::vector<automation::CreateObject> layout_seed(3);
    layout_seed[0].bounds_mm = {10.0, 20.0, 20.0, 20.0};
    layout_seed[1].bounds_mm = {80.0, 20.0, 20.0, 20.0};
    layout_seed[2].bounds_mm = {160.0, 20.0, 20.0, 20.0};
    const auto seeded = service.create_objects(after_undo.epoch, after_undo.revision, layout_seed);
    check(seeded.ok() && seeded.created_ids.size() == 3,
          "automation setup creates three stable layout targets");
    const auto first_id = std::to_string(seeded.created_ids[0]);
    const auto second_id = std::to_string(seeded.created_ids[1]);
    const auto third_id = std::to_string(seeded.created_ids[2]);
    auto current_state = service.inspect();
    const auto get_object = [&](const std::string& object_id, const automation::DocumentState& state) {
        return rpc.handle(automation::JsonValue(automation::JsonValue::Object{
            {"operation", "objects.get"}, {"session_handle", write_handle}, {"object_id", object_id},
            {"expected_epoch", std::to_string(state.epoch)},
            {"expected_revision", std::to_string(state.revision)}}));
    };
    auto first_object = get_object(first_id, current_state);
    check(first_object.get("status") && *first_object.get("status")->string() == "ok" &&
              first_object.get("top_level") && *first_object.get("top_level")->boolean(),
          "objects.get returns one committed object under an explicit epoch and revision");

    const auto write_batch = [&](std::string request_id, std::string key,
                                 automation::JsonValue::Array operations,
                                 const automation::DocumentState& state) {
        return rpc.handle(automation::JsonValue(automation::JsonValue::Object{
            {"operation", "batch.apply"}, {"session_handle", write_handle},
            {"request_id", std::move(request_id)}, {"idempotency_key", std::move(key)},
            {"expected_epoch", std::to_string(state.epoch)},
            {"expected_revision", std::to_string(state.revision)}, {"operations", std::move(operations)}}));
    };
    automation::JsonValue::Array style_ops{automation::JsonValue::Object{
        {"op", "object.style"}, {"target_ids", automation::JsonValue::Array{first_id}},
        {"fill", automation::JsonValue::Object{{"model", "rgb"}, {"channels", automation::JsonValue::Object{
            {"r", 0.1}, {"g", 0.7}, {"b", 0.25}}}}}, {"stroke_width_mm", 1.5}}};
    const auto style_receipt = write_batch("style-mvp-1", "style-mvp-key", style_ops, current_state);
    check(style_receipt.get("status") && *style_receipt.get("status")->string() == "ok",
          "MCP batch parser applies supported existing-object style fields");

    current_state = service.inspect();
    automation::JsonValue::Array align_ops{automation::JsonValue::Object{
        {"op", "object.align"}, {"reference_id", first_id},
        {"target_ids", automation::JsonValue::Array{second_id}}, {"mode", "left"}}};
    const auto align_validation = rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "batch.validate"}, {"session_handle", write_handle},
        {"expected_epoch", std::to_string(current_state.epoch)},
        {"expected_revision", std::to_string(current_state.revision)}, {"operations", align_ops}}));
    const auto align_receipt = write_batch("align-mvp-1", "align-mvp-key", align_ops, current_state);
    check(align_validation.get("valid") && *align_validation.get("valid")->boolean() &&
              align_receipt.get("status") && *align_receipt.get("status")->string() == "ok",
          "MCP validates and applies explicit-reference alignment");

    current_state = service.inspect();
    automation::JsonValue::Array distribution_ops{automation::JsonValue::Object{
        {"op", "object.distribute"}, {"target_ids", automation::JsonValue::Array{first_id, second_id, third_id}},
        {"axis", "horizontal"}}};
    const auto distribution_validation = rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "batch.validate"}, {"session_handle", write_handle},
        {"expected_epoch", std::to_string(current_state.epoch)},
        {"expected_revision", std::to_string(current_state.revision)}, {"operations", distribution_ops}}));
    const auto distribution_receipt = write_batch("distribute-mvp-1", "distribute-mvp-key",
                                                   distribution_ops, current_state);
    check(distribution_validation.get("valid") && *distribution_validation.get("valid")->boolean() &&
              distribution_receipt.get("status") && *distribution_receipt.get("status")->string() == "ok",
          "MCP validates and applies stable-ID horizontal distribution");

    current_state = service.inspect();
    automation::JsonValue::Array group_ops{automation::JsonValue::Object{
        {"op", "object.group"}, {"client_ref", "layout_group"}, {"name", "Layout group"},
        {"target_ids", automation::JsonValue::Array{first_id, second_id}}}};
    const auto group_receipt = write_batch("group-mvp-1", "group-mvp-key", group_ops, current_state);
    check(group_receipt.get("status") && *group_receipt.get("status")->string() == "ok" &&
              group_receipt.get("client_ref_to_id") && group_receipt.get("client_ref_to_id")->get("layout_group"),
          "MCP grouping returns a stable group ID through client_ref");
    const auto group_id = *group_receipt.get("client_ref_to_id")->get("layout_group")->string();
    current_state = service.inspect();
    const auto nested_object = get_object(first_id, current_state);
    check(nested_object.get("top_level") && !*nested_object.get("top_level")->boolean() &&
              nested_object.get("parent_group_id") && *nested_object.get("parent_group_id")->string() == group_id,
          "objects.get identifies nested group membership while preserving the child ID");

    automation::JsonValue::Object undo_args{{"operation", "history.undo"}, {"session_handle", write_handle},
        {"request_id", "undo-mvp-1"}, {"idempotency_key", "undo-mvp-key"},
        {"expected_epoch", std::to_string(current_state.epoch)},
        {"expected_revision", std::to_string(current_state.revision)}};
    const auto undo_receipt = rpc.handle(automation::JsonValue(undo_args));
    const auto revision_after_undo = service.inspect().revision;
    const auto repeated_undo = rpc.handle(automation::JsonValue(undo_args));
    check(undo_receipt.get("status") && *undo_receipt.get("status")->string() == "ok" &&
              revision_after_undo == current_state.revision + 1 &&
              repeated_undo.get("revision_after") &&
              *repeated_undo.get("revision_after")->string() == std::to_string(revision_after_undo),
          "history.undo is revision guarded and idempotent after a lost response");
    current_state = service.inspect();
    automation::JsonValue::Object redo_args{{"operation", "history.redo"}, {"session_handle", write_handle},
        {"request_id", "redo-mvp-1"}, {"idempotency_key", "redo-mvp-key"},
        {"expected_epoch", std::to_string(current_state.epoch)},
        {"expected_revision", std::to_string(current_state.revision)}};
    const auto redo_receipt = rpc.handle(automation::JsonValue(redo_args));
    check(redo_receipt.get("status") && *redo_receipt.get("status")->string() == "ok" &&
              service.inspect().revision == current_state.revision + 1,
          "history.redo reapplies the original group with a guarded revision");

    current_state = service.inspect();
    automation::JsonValue::Array ungroup_ops{automation::JsonValue::Object{
        {"op", "object.ungroup"}, {"target_ids", automation::JsonValue::Array{group_id}}}};
    const auto ungroup_receipt = write_batch("ungroup-mvp-1", "ungroup-mvp-key", ungroup_ops, current_state);
    current_state = service.inspect();
    const auto restored_object = get_object(first_id, current_state);
    check(ungroup_receipt.get("status") && *ungroup_receipt.get("status")->string() == "ok" &&
              restored_object.get("top_level") && *restored_object.get("top_level")->boolean() &&
              !restored_object.get("parent_group_id"),
          "MCP ungroup restores children as top-level objects and objects.get reports their current location");

    document::Document scale_document("rpc-scale-test", document::Page::make_default());
    automation::AutomationService scale_service(scale_document);
    automation::AutomationRpc scale_rpc(scale_service, {"document.read", "document.edit"});
    const auto scale_session = scale_rpc.handle(parse(
        "{\"operation\":\"session.create\",\"scopes\":[\"document.read\",\"document.edit\"]}"));
    const auto scale_handle = *scale_session.get("session_handle")->string();
    auto scale_state = scale_service.inspect();
    for (size_t batch_index = 0; batch_index < 4; ++batch_index) {
        std::vector<automation::CreateObject> batch_requests;
        batch_requests.reserve(250);
        for (size_t item = 0; item < 250; ++item) {
            const auto index = batch_index * 250 + item;
            automation::CreateObject object;
            object.bounds_mm = {static_cast<double>(index % 100) * 2.0,
                                static_cast<double>(index / 100) * 2.0, 1.0, 1.0};
            batch_requests.push_back(std::move(object));
        }
        const auto receipt = scale_service.create_objects(scale_state.epoch, scale_state.revision,
                                                           batch_requests);
        check(receipt.ok() && receipt.created_ids.size() == 250,
              "AC07 scale fixture commits each bounded batch of 250 objects");
        scale_state = scale_service.inspect();
    }
    std::set<std::string> paged_ids;
    std::string scale_epoch;
    std::string scale_revision;
    std::string cursor;
    size_t page_count = 0;
    do {
        automation::JsonValue::Object query_args{{"operation", "objects.query"},
            {"session_handle", scale_handle}, {"limit", 100.0}};
        if (!cursor.empty()) {
            query_args["cursor"] = cursor;
            query_args["expected_epoch"] = scale_epoch;
            query_args["expected_revision"] = scale_revision;
        }
        const auto page = scale_rpc.handle(automation::JsonValue(std::move(query_args)));
        check(page.get("status") && *page.get("status")->string() == "ok" &&
                  page.get("objects") && page.get("objects")->array() &&
                  page.get("objects")->array()->size() == 100,
              "AC07 scale paging returns each full bounded page");
        if (page_count == 0) {
            scale_epoch = *page.get("epoch")->string();
            scale_revision = *page.get("revision")->string();
        } else {
            check(*page.get("epoch")->string() == scale_epoch &&
                      *page.get("revision")->string() == scale_revision,
                  "AC07 all continuation pages retain one epoch and revision");
        }
        for (const auto& object : *page.get("objects")->array())
            paged_ids.insert(*object.get("id")->string());
        cursor = *page.get("next_cursor")->string();
        ++page_count;
    } while (!cursor.empty());
    check(page_count == 10 && paged_ids.size() == 1000 &&
              scale_service.inspect().object_count == 1000,
          "AC07 1,000-object query pages cover every stable ID exactly once");
    automation::CreateObject later_object;
    later_object.bounds_mm = {10.0, 30.0, 1.0, 1.0};
    const auto later_edit = scale_service.create_objects(scale_state.epoch, scale_state.revision,
        std::vector<automation::CreateObject>{later_object});
    check(later_edit.ok(), "AC07 scale fixture can advance after a completed read snapshot");
    const auto stale_continuation = scale_rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "objects.query"}, {"session_handle", scale_handle}, {"limit", 100.0},
        {"offset", "100"}, {"expected_epoch", scale_epoch}, {"expected_revision", scale_revision}}));
    check(stale_continuation.get("code") && *stale_continuation.get("code")->string() == "REVISION_CONFLICT",
          "AC07 continuation rejects a page when the document changes after the first snapshot");

    document::Document delta_document("rpc-delta-test", document::Page::make_default());
    automation::AutomationService delta_service(delta_document);
    automation::AutomationRpc delta_rpc(delta_service, {"document.read", "document.edit"});
    const auto delta_session = delta_rpc.handle(parse(
        "{\"operation\":\"session.create\",\"scopes\":[\"document.read\",\"document.edit\"]}"));
    const auto delta_handle = *delta_session.get("session_handle")->string();
    auto delta_state = delta_service.inspect();
    const auto delta_create = delta_rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "object.create_rectangle"}, {"session_handle", delta_handle},
        {"request_id", "delta-create-1"}, {"idempotency_key", "delta-create-key"},
        {"expected_epoch", std::to_string(delta_state.epoch)},
        {"expected_revision", std::to_string(delta_state.revision)},
        {"x_mm", 0.0}, {"y_mm", 0.0}, {"width_mm", 20.0}, {"height_mm", 10.0}}));
    const auto delta_object_id = *delta_create.get("created_ids")->array()->front().string();
    const auto structural_delta = delta_rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "objects.changes"}, {"session_handle", delta_handle},
        {"expected_epoch", std::to_string(delta_state.epoch)}, {"from_revision", "0"}}));
    check(structural_delta.get("requires_full_query") &&
              *structural_delta.get("requires_full_query")->boolean() &&
              structural_delta.get("changed_ids")->array()->empty(),
          "objects.changes requires a full query after object creation and returns no partial IDs");

    delta_state = delta_service.inspect();
    const auto delta_transform = delta_rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "batch.apply"}, {"session_handle", delta_handle},
        {"request_id", "delta-transform-1"}, {"idempotency_key", "delta-transform-key"},
        {"expected_epoch", std::to_string(delta_state.epoch)},
        {"expected_revision", std::to_string(delta_state.revision)},
        {"operations", automation::JsonValue::Array{automation::JsonValue::Object{
            {"op", "object.transform"},
            {"target_ids", automation::JsonValue::Array{automation::JsonValue(delta_object_id)}},
            {"translate_x_mm", 5.0}, {"translate_y_mm", 2.0}}
        }}}));
    check(delta_transform.get("status") && *delta_transform.get("status")->string() == "ok",
          "objects.changes fixture commits a stable-ID transform");
    const auto object_delta = delta_rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "objects.changes"}, {"session_handle", delta_handle},
        {"expected_epoch", std::to_string(delta_state.epoch)},
        {"from_revision", std::to_string(delta_state.revision)}}));
    const auto* changed_ids = object_delta.get("changed_ids");
    check(object_delta.get("contiguous") && *object_delta.get("contiguous")->boolean() &&
              object_delta.get("requires_full_query") &&
              !*object_delta.get("requires_full_query")->boolean() && changed_ids &&
              changed_ids->array() && changed_ids->array()->size() == 1 &&
              *changed_ids->array()->front().string() == delta_object_id,
          "objects.changes returns the changed stable ID for a compatible object edit");

    delta_state = delta_service.inspect();
    const auto delta_undo = delta_rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "history.undo"}, {"session_handle", delta_handle},
        {"request_id", "delta-undo-1"}, {"idempotency_key", "delta-undo-key"},
        {"expected_epoch", std::to_string(delta_state.epoch)},
        {"expected_revision", std::to_string(delta_state.revision)}}));
    check(delta_undo.get("status") && *delta_undo.get("status")->string() == "ok",
          "objects.changes fixture can undo its compatible edit");
    const auto undo_delta = delta_rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "objects.changes"}, {"session_handle", delta_handle},
        {"expected_epoch", std::to_string(delta_state.epoch)},
        {"from_revision", std::to_string(delta_state.revision)}}));
    check(undo_delta.get("requires_full_query") &&
              *undo_delta.get("requires_full_query")->boolean() &&
              undo_delta.get("changed_ids")->array()->empty(),
          "objects.changes requires a full query after Undo and does not return a partial delta");
    auto wrong_epoch_args = automation::JsonValue::Object{
        {"operation", "objects.changes"}, {"session_handle", delta_handle},
        {"expected_epoch", std::to_string(delta_state.epoch + 1)},
        {"from_revision", std::to_string(delta_state.revision)}};
    const auto wrong_epoch_delta = delta_rpc.handle(automation::JsonValue(std::move(wrong_epoch_args)));
    check(wrong_epoch_delta.get("code") &&
              *wrong_epoch_delta.get("code")->string() == "EPOCH_MISMATCH",
          "objects.changes rejects a stale document epoch");

    document::Document unit_document("rpc-unit-test", document::Page::make_default());
    automation::AutomationService unit_service(unit_document);
    automation::AutomationRpc unit_rpc(unit_service, {"document.read", "document.edit"});
    const auto unit_session = unit_rpc.handle(parse(
        "{\"operation\":\"session.create\",\"scopes\":[\"document.read\",\"document.edit\"]}"));
    const auto unit_handle = *unit_session.get("session_handle")->string();
    auto unit_state = unit_service.inspect();
    auto inch_rectangle = unit_rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "object.create_rectangle"}, {"session_handle", unit_handle},
        {"request_id", "units-rectangle-1"}, {"idempotency_key", "units-rectangle-key"},
        {"expected_epoch", std::to_string(unit_state.epoch)},
        {"expected_revision", std::to_string(unit_state.revision)}, {"length_unit", "in"},
        {"x", 0.25}, {"y", 0.5}, {"width", 1.0}, {"height", 2.0}, {"stroke_width", 0.125}}));
    check(inch_rectangle.get("status") && *inch_rectangle.get("status")->string() == "ok",
          "AC-UNIT11 rectangle RPC accepts typed inch values without changing the legacy millimeter contract");
    const auto inch_id = *inch_rectangle.get("created_ids")->array()->front().string();
    unit_state = unit_service.inspect();
    const auto inch_object = unit_rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "objects.get"}, {"session_handle", unit_handle}, {"object_id", inch_id},
        {"expected_epoch", std::to_string(unit_state.epoch)},
        {"expected_revision", std::to_string(unit_state.revision)}}));
    const auto* inch_bounds = inch_object.get("bounds");
    check(inch_bounds && inch_bounds->get("x_mm") && inch_bounds->get("y_mm") &&
              inch_bounds->get("width_mm") && inch_bounds->get("height_mm") &&
              std::abs(*inch_bounds->get("x_mm")->number() - 6.35) < 1e-9 &&
              std::abs(*inch_bounds->get("y_mm")->number() - 12.7) < 1e-9 &&
              std::abs(*inch_bounds->get("width_mm")->number() - 25.4) < 1e-9 &&
              std::abs(*inch_bounds->get("height_mm")->number() - 50.8) < 1e-9 &&
              inch_object.get("stroke_width_mm") &&
              std::abs(*inch_object.get("stroke_width_mm")->number() - 3.175) < 1e-9,
          "AC-UNIT11 typed inches normalize to canonical millimeters before the document writer");

    unit_state = unit_service.inspect();
    automation::JsonValue::Array typed_path_operations{automation::JsonValue::Object{
        {"op", "object.create_polyline"}, {"client_ref", "typed_path"}, {"length_unit", "ft"},
        {"points", automation::JsonValue::Array{
            automation::JsonValue::Object{{"x", 1.0}, {"y", 2.0}},
            automation::JsonValue::Object{{"x", 3.0}, {"y", 4.0}}}}, {"stroke_width", 0.25}},
        automation::JsonValue::Object{{"op", "object.create_ellipse"}, {"client_ref", "typed_ellipse"},
            {"length_unit", "cm"}, {"x", 1.0}, {"y", 2.0}, {"width", 3.0}, {"height", 4.0}},
        automation::JsonValue::Object{{"op", "object.create_text"}, {"client_ref", "typed_text"},
            {"length_unit", "ft"}, {"x", 1.0}, {"y", 2.0},
            {"text", "Units"}, {"font_family", "Arial"}}};
    auto typed_path_receipt = unit_rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "batch.apply"}, {"session_handle", unit_handle},
        {"request_id", "units-path-1"}, {"idempotency_key", "units-path-key"},
        {"expected_epoch", std::to_string(unit_state.epoch)},
        {"expected_revision", std::to_string(unit_state.revision)}, {"operations", typed_path_operations}}));
    check(typed_path_receipt.get("status") && *typed_path_receipt.get("status")->string() == "ok",
          "AC-UNIT11 batch authoring accepts typed foot coordinates and stroke width");
    check(typed_path_receipt.get("created_ids") && typed_path_receipt.get("created_ids")->array() &&
              typed_path_receipt.get("created_ids")->array()->size() == 3,
          "AC-UNIT11 one batch creates typed path, ellipse, and text objects atomically");
    const auto& typed_created_ids = *typed_path_receipt.get("created_ids")->array();
    const auto typed_path_id = *typed_created_ids[0].string();
    const auto typed_ellipse_id = *typed_created_ids[1].string();
    const auto typed_text_id = *typed_created_ids[2].string();
    unit_state = unit_service.inspect();
    const auto typed_path = unit_rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "objects.get"}, {"session_handle", unit_handle}, {"object_id", typed_path_id},
        {"expected_epoch", std::to_string(unit_state.epoch)},
        {"expected_revision", std::to_string(unit_state.revision)}}));
    const auto* path_elements = typed_path.get("path_elements");
    const auto* first_path_element = path_elements && path_elements->array() && !path_elements->array()->empty()
        ? &path_elements->array()->front() : nullptr;
    check(first_path_element && first_path_element->get("x_mm") && first_path_element->get("y_mm") &&
              std::abs(*first_path_element->get("x_mm")->number() - 304.8) < 1e-8 &&
              std::abs(*first_path_element->get("y_mm")->number() - 609.6) < 1e-8 &&
              typed_path.get("stroke_width_mm") &&
              std::abs(*typed_path.get("stroke_width_mm")->number() - 76.2) < 1e-8,
          "AC-UNIT11 typed feet normalize every path point and stroke width to canonical millimeters");

    const auto typed_ellipse = unit_rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "objects.get"}, {"session_handle", unit_handle}, {"object_id", typed_ellipse_id},
        {"expected_epoch", std::to_string(unit_state.epoch)},
        {"expected_revision", std::to_string(unit_state.revision)}}));
    const auto* ellipse_bounds = typed_ellipse.get("bounds");
    const auto typed_text = unit_rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "objects.get"}, {"session_handle", unit_handle}, {"object_id", typed_text_id},
        {"expected_epoch", std::to_string(unit_state.epoch)},
        {"expected_revision", std::to_string(unit_state.revision)}}));
    check(ellipse_bounds && ellipse_bounds->get("x_mm") && ellipse_bounds->get("width_mm") &&
              std::abs(*ellipse_bounds->get("x_mm")->number() - 10.0) < 1e-9 &&
              std::abs(*ellipse_bounds->get("width_mm")->number() - 30.0) < 1e-9 &&
              typed_text.get("position_x_mm") && typed_text.get("position_y_mm") &&
              std::abs(*typed_text.get("position_x_mm")->number() - 304.8) < 1e-8 &&
              std::abs(*typed_text.get("position_y_mm")->number() - 609.6) < 1e-8,
          "AC-UNIT11 typed centimeters normalize ellipse geometry and typed feet normalize text position");

    const auto apply_unit_batch = [&](std::string request_id, std::string idempotency_key,
                                      automation::JsonValue::Array operations) {
        const auto state = unit_service.inspect();
        return unit_rpc.handle(automation::JsonValue(automation::JsonValue::Object{
            {"operation", "batch.apply"}, {"session_handle", unit_handle},
            {"request_id", std::move(request_id)}, {"idempotency_key", std::move(idempotency_key)},
            {"expected_epoch", std::to_string(state.epoch)},
            {"expected_revision", std::to_string(state.revision)}, {"operations", std::move(operations)}}));
    };
    const auto typed_transform = apply_unit_batch("units-transform-1", "units-transform-key",
        automation::JsonValue::Array{automation::JsonValue::Object{
            {"op", "object.transform"}, {"target_ids", automation::JsonValue::Array{automation::JsonValue(typed_path_id)}},
            {"length_unit", "in"}, {"translate_x", 0.5}, {"translate_y", -0.25}}});
    check(typed_transform.get("status") && *typed_transform.get("status")->string() == "ok",
          "AC-UNIT11 batch transform accepts typed translation values");
    unit_state = unit_service.inspect();
    const auto transformed_path = unit_rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "objects.get"}, {"session_handle", unit_handle}, {"object_id", typed_path_id},
        {"expected_epoch", std::to_string(unit_state.epoch)},
        {"expected_revision", std::to_string(unit_state.revision)}}));
    const auto* transform_value = transformed_path.get("transform");
    check(transform_value && transform_value->get("tx_mm") && transform_value->get("ty_mm") &&
              std::abs(*transform_value->get("tx_mm")->number() - 12.7) < 1e-8 &&
              std::abs(*transform_value->get("ty_mm")->number() + 6.35) < 1e-8,
          "AC-UNIT11 typed translation is normalized to millimeters before TransformObjectCommand");

    const auto typed_style = apply_unit_batch("units-style-1", "units-style-key",
        automation::JsonValue::Array{automation::JsonValue::Object{
            {"op", "object.style"}, {"target_ids", automation::JsonValue::Array{automation::JsonValue(typed_path_id)}},
            {"length_unit", "in"}, {"stroke_width", 0.125}}});
    check(typed_style.get("status") && *typed_style.get("status")->string() == "ok",
          "AC-UNIT11 style stroke width accepts typed length values");
    unit_state = unit_service.inspect();
    const auto styled_path = unit_rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "objects.get"}, {"session_handle", unit_handle}, {"object_id", typed_path_id},
        {"expected_epoch", std::to_string(unit_state.epoch)},
        {"expected_revision", std::to_string(unit_state.revision)}}));
    check(styled_path.get("stroke_width_mm") &&
              std::abs(*styled_path.get("stroke_width_mm")->number() - 3.175) < 1e-8,
          "AC-UNIT11 typed style width reaches the document in canonical millimeters");

    const auto before_duplicate = unit_service.inspect();
    const auto original_before_duplicate = unit_rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "objects.get"}, {"session_handle", unit_handle}, {"object_id", typed_path_id},
        {"expected_epoch", std::to_string(before_duplicate.epoch)},
        {"expected_revision", std::to_string(before_duplicate.revision)}}));
    const auto duplicate_receipt = apply_unit_batch("units-duplicate-1", "units-duplicate-key",
        automation::JsonValue::Array{automation::JsonValue::Object{
            {"op", "object.duplicate"}, {"target_ids", automation::JsonValue::Array{automation::JsonValue(typed_path_id)}},
            {"length_unit", "in"}, {"offset_x", 1.0}, {"offset_y", 0.5}}});
    check(duplicate_receipt.get("status") && *duplicate_receipt.get("status")->string() == "ok",
          "AC-UNIT11 batch duplicate accepts typed offsets");
    const auto typed_duplicate_id = *duplicate_receipt.get("created_ids")->array()->front().string();
    unit_state = unit_service.inspect();
    const auto duplicate_object = unit_rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "objects.get"}, {"session_handle", unit_handle}, {"object_id", typed_duplicate_id},
        {"expected_epoch", std::to_string(unit_state.epoch)},
        {"expected_revision", std::to_string(unit_state.revision)}}));
    const auto* original_bounds = original_before_duplicate.get("bounds");
    const auto* duplicate_bounds = duplicate_object.get("bounds");
    check(original_bounds && duplicate_bounds && original_bounds->get("x_mm") &&
              original_bounds->get("y_mm") && duplicate_bounds->get("x_mm") &&
              duplicate_bounds->get("y_mm") &&
              std::abs((*duplicate_bounds->get("x_mm")->number() - *original_bounds->get("x_mm")->number()) - 25.4) < 1e-8 &&
              std::abs((*duplicate_bounds->get("y_mm")->number() - *original_bounds->get("y_mm")->number()) - 12.7) < 1e-8,
          "AC-UNIT11 typed duplicate offsets preserve the requested physical separation");

    const auto before_rejected_pixel = unit_service.inspect();
    const auto rejected_pixel = unit_rpc.handle(automation::JsonValue(automation::JsonValue::Object{
        {"operation", "object.create_rectangle"}, {"session_handle", unit_handle},
        {"request_id", "units-pixel-1"}, {"idempotency_key", "units-pixel-key"},
        {"expected_epoch", std::to_string(before_rejected_pixel.epoch)},
        {"expected_revision", std::to_string(before_rejected_pixel.revision)}, {"length_unit", "px"},
        {"x", 1.0}, {"y", 1.0}, {"width", 10.0}, {"height", 10.0}}));
    check(rejected_pixel.get("code") && *rejected_pixel.get("code")->string() == "INVALID_ARGUMENT" &&
              unit_service.inspect().revision == before_rejected_pixel.revision,
          "AC-UNIT11 pixel values are rejected without a DPI basis or document mutation");
    return 0;
}
