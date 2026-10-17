#include "pikdraw/automation/automation_rpc.hpp"
#include "automation_rpc_internal.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <random>
#include <sstream>

namespace pikdraw::automation {
namespace {

std::string random_handle() {
    std::array<uint32_t, 4> words{};
    std::random_device random;
    for (auto& word : words) word = random();
    std::ostringstream output;
    output << "ses_" << std::hex << std::setfill('0');
    for (const auto word : words) output << std::setw(8) << word;
    return output.str();
}

} // namespace

AutomationRpc::AutomationRpc(AutomationService& service, std::set<std::string> granted_scopes)
    : service_(service), granted_scopes_(std::move(granted_scopes)) {}

bool AutomationRpc::owns_session(std::string_view session_handle) const noexcept {
    return sessions_.find(std::string(session_handle)) != sessions_.end();
}

bool AutomationRpc::has_scope(std::string_view session_handle, std::string_view scope) const {
    const auto it = sessions_.find(std::string(session_handle));
    if (it == sessions_.end()) return false;
    const auto state = service_.inspect();
    return it->second.document_id == state.document_id && it->second.epoch == state.epoch &&
           it->second.scopes.contains(std::string(scope));
}

JsonValue AutomationRpc::error(std::string code, std::string message) const {
    return JsonValue::Object{{"status", "error"}, {"code", std::move(code)}, {"message", std::move(message)}};
}

bool AutomationRpc::authorize(const JsonValue& request, std::string_view required_scope,
                              JsonValue& failure) const {
    const auto* session = string_field(request, "session_handle");
    if (!session) { failure = error("SESSION_REQUIRED", "Connect to this PikDraw instance first."); return false; }
    const auto it = sessions_.find(*session);
    if (it == sessions_.end()) { failure = error("SESSION_EXPIRED", "The session handle is unknown or expired."); return false; }
    const auto state = service_.inspect();
    if (it->second.document_id != state.document_id || it->second.epoch != state.epoch) {
        failure = error("SESSION_EXPIRED", "The document identity or epoch changed after this session was created.");
        return false;
    }
    if (!it->second.scopes.contains(std::string(required_scope))) {
        failure = error("SCOPE_DENIED", "This session does not have the required permission."); return false;
    }
    return true;
}

JsonValue AutomationRpc::handle_connect(const JsonValue& request) {
    if (!only_fields(request, {"operation", "scopes"}))
        return error("INVALID_ARGUMENT", "session.create contains an unknown field.");
    std::set<std::string> requested{"document.read"};
    if (const auto* scopes = field(request, "scopes")) {
        if (!scopes->array()) return error("INVALID_ARGUMENT", "scopes must be an array of strings.");
        requested.clear();
        for (const auto& value : *scopes->array()) {
            if (!value.string()) return error("INVALID_ARGUMENT", "Every scope must be a string.");
            requested.insert(*value.string());
        }
    }
    for (const auto& scope : requested) {
        if (scope != "document.read" && scope != "document.create" &&
            scope != "document.edit" && scope != "file.read" && scope != "file.write")
            return error("INVALID_ARGUMENT", "Unknown scope: " + scope);
        if (!granted_scopes_.contains(scope))
            return error("SCOPE_DENIED", "The PikDraw instance did not grant scope: " + scope);
    }
    if (!requested.contains("document.read") &&
        (requested.contains("document.create") || requested.contains("document.edit") ||
         requested.contains("file.read") || requested.contains("file.write")))
        return error("INVALID_ARGUMENT", "Write and file scopes require document.read.");
    if (requested.contains("file.write") && !requested.contains("file.read"))
        return error("INVALID_ARGUMENT", "file.write requires file.read.");
    if (sessions_.size() >= 8) return error("RESOURCE_LIMIT", "Too many automation sessions are active.");
    const auto state = service_.inspect();
    const auto handle = random_handle();
    sessions_.emplace(handle, Session{requested, state.document_id, state.epoch, {}});
    return JsonValue::Object{{"status", "ok"}, {"session_handle", handle},
        {"granted_scopes", [&] { JsonValue::Array a; for (const auto& s : requested) a.emplace_back(s); return JsonValue(std::move(a)); }()},
        {"document_id", state.document_id}, {"epoch", id_string(state.epoch)}};
}

JsonValue AutomationRpc::handle(const JsonValue& request) {
    const auto* operation = string_field(request, "operation");
    if (!operation) return error("INVALID_ARGUMENT", "operation is required.");
    if (*operation == "session.create") return handle_connect(request);
    if (*operation == "session.disconnect") {
        if (!only_fields(request, {"operation", "session_handle"}))
            return error("INVALID_ARGUMENT", "session.disconnect contains an unknown field.");
        const auto* session = string_field(request, "session_handle");
        if (!session || sessions_.erase(*session) == 0) return error("SESSION_EXPIRED", "The session is unknown.");
        return JsonValue::Object{{"status", "ok"}, {"disconnected", true}};
    }
    if (*operation == "capabilities") {
        if (!only_fields(request, {"operation", "session_handle"}))
            return error("INVALID_ARGUMENT", "capabilities contains an unknown field.");
        JsonValue failure;
        if (!authorize(request, "document.read", failure)) return failure;
        JsonValue::Array capabilities;
        for (const auto& capability : service_.capabilities()) {
            bool allowed = true;
        for (const auto& scope : capability.required_scopes) {
                JsonValue ignored;
                if (!authorize(request, scope, ignored)) { allowed = false; break; }
            }
            if (!allowed) continue;
            capabilities.emplace_back(JsonValue::Object{{"id", capability.id},
                {"input_schema", capability.input_schema}, {"writes_document", capability.writes_document}});
        }
        capabilities.emplace_back(JsonValue::Object{{"id", "request.get"},
            {"input_schema", "{idempotency_key}"}, {"writes_document", false}});
        if (authorize(request, "document.create", failure))
            capabilities.emplace_back(JsonValue::Object{{"id", "document.create"},
                {"input_schema", "{page_width_mm,page_height_mm,title}"}, {"writes_document", true}});
        failure = {};
        if (authorize(request, "file.read", failure))
            capabilities.emplace_back(JsonValue::Object{{"id", "file.verify_reopen"},
                {"input_schema", "{file_name}"}, {"writes_document", false}});
        failure = {};
        if (authorize(request, "file.write", failure))
            capabilities.emplace_back(JsonValue::Object{{"id", "file.save"},
                {"input_schema", "{file_name}"}, {"writes_document", false}});
        return JsonValue::Object{{"status", "ok"}, {"api_version", std::string(kApiVersion)},
            {"capabilities", std::move(capabilities)}};
    }
    if (*operation == "document.inspect") {
        if (!only_fields(request, {"operation", "session_handle"}))
            return error("INVALID_ARGUMENT", "document.inspect contains an unknown field.");
        JsonValue failure;
        if (!authorize(request, "document.read", failure)) return failure;
        const auto state = service_.inspect();
        return JsonValue::Object{{"status", "ok"}, {"api_version", std::string(kApiVersion)},
            {"document_id", state.document_id}, {"epoch", id_string(state.epoch)},
            {"revision", id_string(state.revision)}, {"page_width_mm", state.page_width_mm},
            {"page_height_mm", state.page_height_mm}, {"object_count", static_cast<double>(state.object_count)},
            {"layer_count", static_cast<double>(state.layer_count)}, {"dirty", state.dirty}};
    }
    if (*operation == "font.query") {
        if (!only_fields(request, {"operation", "session_handle", "font_family", "font_postscript_name",
                                  "font_weight", "font_italic", "font_size_pt"}))
            return error("INVALID_ARGUMENT", "font.query contains an unknown field.");
        JsonValue failure;
        if (!authorize(request, "document.read", failure)) return failure;
        typography::FontIdentity requested;
        std::string diagnostic;
        if (!parse_font(request, requested, diagnostic)) return error("INVALID_ARGUMENT", diagnostic);
        try {
            const auto resolved = service_.query_font(requested);
            return JsonValue::Object{{"status", "ok"}, {"api_version", std::string(kApiVersion)},
                {"requested_family", resolved.requested.family_name},
                {"requested_postscript_name", resolved.requested.postscript_name},
                {"actual_family", resolved.actual.family_name},
                {"actual_postscript_name", resolved.actual.postscript_name},
                {"weight", static_cast<double>(resolved.actual.weight)},
                {"italic", resolved.actual.italic}, {"size_pt", resolved.actual.size_pt},
                {"fallback", resolved.is_fallback}, {"warning", resolved.warning_message}};
        } catch (const std::exception& exception) {
            return error("UNSUPPORTED_CAPABILITY", std::string("Font resolution failed: ") + exception.what());
        }
    }
    if (*operation == "text.measure") {
        if (!only_fields(request, {"operation", "session_handle", "text", "font_family",
            "font_postscript_name", "font_weight", "font_italic", "font_size_pt",
            "max_width_mm", "line_spacing_ratio", "alignment"}))
            return error("INVALID_ARGUMENT", "text.measure contains an unknown field.");
        JsonValue failure;
        if (!authorize(request, "document.read", failure)) return failure;
        const auto* text = string_field(request, "text");
        typography::FontIdentity font;
        std::string diagnostic;
        if (!text || !parse_font(request, font, diagnostic))
            return error("INVALID_ARGUMENT", text ? diagnostic : "text is required.");
        typography::LayoutOptions options;
        if (const auto* width = number_field(request, "max_width_mm")) options.max_width_mm = *width;
        if (const auto* spacing = number_field(request, "line_spacing_ratio")) options.line_spacing_ratio = *spacing;
        if (const auto* alignment = string_field(request, "alignment")) {
            if (*alignment == "left") options.alignment = typography::TextAlignment::Left;
            else if (*alignment == "center") options.alignment = typography::TextAlignment::Center;
            else if (*alignment == "right") options.alignment = typography::TextAlignment::Right;
            else return error("INVALID_ARGUMENT", "alignment must be left, center or right.");
        }
        try {
            const auto measured = service_.measure_text(*text, font, options);
            return JsonValue::Object{{"status", "ok"}, {"api_version", std::string(kApiVersion)},
                {"requested_family", measured.requested_font.family_name},
                {"requested_postscript_name", measured.requested_font.postscript_name},
                {"actual_family", measured.actual_font.family_name},
                {"actual_postscript_name", measured.actual_font.postscript_name},
                {"font_size_pt", font.size_pt}, {"fallback", measured.is_fallback},
                {"warning", measured.warning}, {"line_count", static_cast<double>(measured.line_count)},
                {"glyph_count", static_cast<double>(measured.glyph_count)},
                {"missing_glyph_count", static_cast<double>(measured.missing_glyph_count)},
                {"bounds_mm", rect_json(measured.bounds_mm)}};
        } catch (const std::exception& exception) {
            return error("UNSUPPORTED_CAPABILITY", std::string("Text layout failed: ") + exception.what());
        }
    }
    if (*operation == "document.validate") {
        if (!only_fields(request, {"operation", "session_handle", "safe_margin_mm", "safe_margin_exempt_ids"}))
            return error("INVALID_ARGUMENT", "document.validate contains an unknown field.");
        JsonValue failure;
        if (!authorize(request, "document.read", failure)) return failure;
        double margin = 0.0;
        if (const auto* value = number_field(request, "safe_margin_mm")) margin = *value;
        std::set<uint64_t> exempt_ids;
        if (const auto* values = field(request, "safe_margin_exempt_ids")) {
            if (!values->array() || values->array()->size() > 1024)
                return error("INVALID_ARGUMENT", "safe_margin_exempt_ids must be an array with at most 1024 IDs.");
            for (const auto& value : *values->array()) {
                const auto* text_id = value.string();
                uint64_t id = 0;
                if (!text_id) return error("INVALID_ARGUMENT", "Every exempt object ID must be a decimal string.");
                const auto parsed = std::from_chars(text_id->data(), text_id->data() + text_id->size(), id);
                if (parsed.ec != std::errc{} || parsed.ptr != text_id->data() + text_id->size())
                    return error("INVALID_ARGUMENT", "Exempt object IDs must be unsigned decimal strings.");
                exempt_ids.insert(id);
            }
        }
        const auto state = service_.inspect();
        const auto validation = service_.validate_document(margin, exempt_ids);
        JsonValue::Array diagnostics;
        for (const auto& item : validation.diagnostics)
            diagnostics.emplace_back(JsonValue::Object{{"code", item.code}, {"severity", item.severity},
                {"object_id", id_string(item.object_id)}, {"message", item.message}});
        return JsonValue::Object{{"status", "ok"}, {"api_version", std::string(kApiVersion)},
            {"document_id", state.document_id}, {"epoch", id_string(state.epoch)},
            {"revision", id_string(state.revision)}, {"valid", validation.valid},
            {"diagnostics", std::move(diagnostics)}};
    }
    if (*operation == "objects.query") {
        if (!only_fields(request, {"operation", "session_handle", "offset", "limit",
                                   "expected_epoch", "expected_revision"}))
            return error("INVALID_ARGUMENT", "objects.query contains an unknown field.");
        JsonValue failure;
        if (!authorize(request, "document.read", failure)) return failure;
        const auto state = service_.inspect();
        const auto read_unsigned = [&](std::string_view name, uint64_t fallback, uint64_t& out) {
            const auto* text = string_field(request, name);
            if (!text) { out = fallback; return true; }
            const auto parsed = std::from_chars(text->data(), text->data() + text->size(), out);
            return parsed.ec == std::errc{} && parsed.ptr == text->data() + text->size();
        };
        uint64_t offset = 0;
        uint64_t expected_epoch = state.epoch;
        uint64_t expected_revision = state.revision;
        const bool has_epoch = string_field(request, "expected_epoch") != nullptr;
        const bool has_revision = string_field(request, "expected_revision") != nullptr;
        if (!read_unsigned("offset", 0, offset) ||
            !read_unsigned("expected_epoch", state.epoch, expected_epoch) ||
            !read_unsigned("expected_revision", state.revision, expected_revision))
            return error("INVALID_ARGUMENT", "offset, expected_epoch and expected_revision must be unsigned decimal strings.");
        if (offset > 0 && (!has_epoch || !has_revision))
            return error("INVALID_ARGUMENT", "Paged queries after the first page require expected_epoch and expected_revision.");
        if (expected_epoch != state.epoch || expected_revision != state.revision)
            return error("REVISION_CONFLICT", "The document changed between query pages; inspect and restart paging.");
        uint64_t limit = 100;
        if (const auto* value = number_field(request, "limit")) {
            if (*value < 1.0 || *value > 100.0 || std::floor(*value) != *value)
                return error("INVALID_ARGUMENT", "limit must be an integer in [1, 100].");
            limit = static_cast<uint64_t>(*value);
        }
        const auto snapshot = service_.snapshot();
        if (!snapshot) return error("DOCUMENT_CLOSED", "No committed document snapshot is available.");
        JsonValue::Array objects;
        const auto summaries = service_.query_objects();
        if (offset > summaries.size()) return error("INVALID_ARGUMENT", "offset is outside the current object list.");
        const auto end = std::min<uint64_t>(static_cast<uint64_t>(summaries.size()), offset + limit);
        for (uint64_t index = offset; index < end; ++index) {
            const auto& summary = summaries[static_cast<size_t>(index)];
            const auto object = snapshot->get_object(summary.id);
            if (object) objects.emplace_back(object_json(*object, summary.layer_index));
        }
        return JsonValue::Object{{"status", "ok"}, {"api_version", std::string(kApiVersion)},
            {"document_id", snapshot->document_id()}, {"epoch", id_string(snapshot->epoch())},
            {"revision", id_string(snapshot->revision())}, {"offset", id_string(offset)},
            {"limit", static_cast<double>(limit)}, {"next_offset", end < summaries.size() ? id_string(end) : ""},
            {"objects", std::move(objects)}};
    }
    if (*operation == "objects.get") {
        if (!only_fields(request, {"operation", "session_handle", "object_id",
                                   "expected_epoch", "expected_revision"}))
            return error("INVALID_ARGUMENT", "objects.get contains an unknown field.");
        JsonValue failure;
        if (!authorize(request, "document.read", failure)) return failure;
        uint64_t epoch = 0, revision = 0, object_id = 0;
        std::string diagnostic;
        if (!read_revision_pair(request, epoch, revision) ||
            !parse_object_id(request, "object_id", object_id, diagnostic))
            return error("INVALID_ARGUMENT", diagnostic.empty()
                ? "expected_epoch and expected_revision must be unsigned decimal strings."
                : std::move(diagnostic));
        const auto state = service_.inspect();
        if (state.epoch != epoch) return error("EPOCH_MISMATCH", "The document lifecycle changed.");
        if (state.revision != revision) return error("REVISION_CONFLICT", "The document revision changed.");
        const auto snapshot = service_.snapshot();
        if (!snapshot) return error("DOCUMENT_CLOSED", "No committed document snapshot is available.");
        const auto object = snapshot->get_object(object_id);
        const auto location = find_object_location(*snapshot, object_id);
        if (!object || !location) return error("OBJECT_NOT_FOUND", "No committed object matches object_id.");
        auto result = *object_json(*object, location->layer_index).object();
        result["status"] = "ok";
        result["api_version"] = std::string(kApiVersion);
        result["document_id"] = snapshot->document_id();
        result["epoch"] = id_string(snapshot->epoch());
        result["revision"] = id_string(snapshot->revision());
        result["top_level"] = location->top_level;
        if (location->parent_group_id)
            result["parent_group_id"] = id_string(*location->parent_group_id);
        return result;
    }
    if (*operation == "request.get") {
        if (!only_fields(request, {"operation", "session_handle", "idempotency_key"}))
            return error("INVALID_ARGUMENT", "request.get contains an unknown field.");
        JsonValue failure;
        if (!authorize(request, "document.read", failure)) return failure;
        const auto* session_handle = string_field(request, "session_handle");
        const auto* key = string_field(request, "idempotency_key");
        if (!session_handle || !key || key->empty())
            return error("INVALID_ARGUMENT", "session_handle and idempotency_key are required.");
        const auto session = sessions_.find(*session_handle);
        if (session == sessions_.end()) return error("SESSION_EXPIRED", "The session handle is unknown or expired.");
        const auto receipt = session->second.receipts.find(*key);
        if (receipt == session->second.receipts.end())
            return error("REQUEST_NOT_FOUND", "No committed receipt matches this idempotency key in the current session.");
        return receipt->second.response;
    }
    if (*operation == "history.undo" || *operation == "history.redo") {
        const auto& action = *operation == "history.undo" ? std::string("undo") : std::string("redo");
        if (!only_fields(request, {"operation", "session_handle", "request_id", "idempotency_key",
                                   "expected_epoch", "expected_revision"}))
            return error("INVALID_ARGUMENT", "history." + action + " contains an unknown field.");
        JsonValue failure;
        if (!authorize(request, "document.edit", failure)) return failure;
        const auto* session_handle = string_field(request, "session_handle");
        const auto* request_id = string_field(request, "request_id");
        const auto* idempotency_key = string_field(request, "idempotency_key");
        if (!session_handle || !request_id || request_id->empty() || request_id->size() > 128 ||
            !idempotency_key || idempotency_key->empty() || idempotency_key->size() > 128)
            return error("INVALID_ARGUMENT", "request_id and idempotency_key (1 to 128 characters) are required for every write.");
        auto session = sessions_.find(*session_handle);
        if (session == sessions_.end()) return error("SESSION_EXPIRED", "The session handle is unknown or expired.");
        JsonValue::Object payload = *request.object();
        payload.erase("session_handle"); payload.erase("request_id"); payload.erase("idempotency_key");
        const auto canonical_payload = JsonValue(std::move(payload)).serialize();
        const auto prior = session->second.receipts.find(*idempotency_key);
        if (prior != session->second.receipts.end()) {
            if (prior->second.canonical_payload != canonical_payload)
                return error("IDEMPOTENCY_CONFLICT", "This idempotency_key was already used with a different write payload.");
            return prior->second.response;
        }
        if (session->second.receipts.size() >= 128)
            return error("RESOURCE_LIMIT", "This session has reached its 128 committed write receipt limit.");
        uint64_t epoch = 0, revision = 0;
        if (!read_revision_pair(request, epoch, revision))
            return error("INVALID_ARGUMENT", "expected_epoch and expected_revision must be unsigned decimal strings.");
        const auto result = action == "undo" ? service_.undo(epoch, revision) : service_.redo(epoch, revision);
        if (!result.ok()) {
            const char* code = result.status == Status::RevisionConflict ? "REVISION_CONFLICT" :
                result.status == Status::EpochMismatch ? "EPOCH_MISMATCH" :
                result.status == Status::BusyInteraction ? "BUSY_INTERACTION" :
                result.status == Status::InternalError ? "INTERNAL_ERROR" : "INVALID_ARGUMENT";
            return error(code, result.diagnostic);
        }
        JsonValue receipt = JsonValue::Object{{"status", "ok"}, {"api_version", std::string(kApiVersion)},
            {"request_id", *request_id}, {"idempotency_key", *idempotency_key},
            {"document_id", service_.inspect().document_id}, {"epoch", id_string(epoch)},
            {"action", action}, {"revision_before", id_string(result.revision_before)},
            {"revision_after", id_string(result.revision_after)}, {"dirty", service_.inspect().dirty}};
        session->second.receipts.emplace(*idempotency_key, Receipt{canonical_payload, receipt});
        return receipt;
    }
    if (*operation == "batch.validate") {
        JsonValue failure;
        if (!authorize(request, "document.read", failure)) return failure;
        for (const auto& [key, ignored] : *request.object()) {
            (void)ignored;
            if (key != "operation" && key != "session_handle" && key != "expected_epoch" &&
                key != "expected_revision" && key != "operations")
                return error("INVALID_ARGUMENT", "Unknown batch.validate field: " + key);
        }
        uint64_t epoch = 0, revision = 0;
        if (!read_revision_pair(request, epoch, revision))
            return error("INVALID_ARGUMENT", "expected_epoch and expected_revision must be unsigned decimal strings.");
        const auto state = service_.inspect();
        if (state.epoch != epoch) return error("EPOCH_MISMATCH", "The document lifecycle changed.");
        if (state.revision != revision) return error("REVISION_CONFLICT", "The document revision changed.");
        std::vector<BatchOperation> operations;
        std::vector<std::string> client_refs;
        std::string diagnostic;
        if (!parse_object_operations(field(request, "operations"), epoch, revision,
                                     operations, client_refs, diagnostic))
            return error("INVALID_ARGUMENT", std::move(diagnostic));
        const auto validation = service_.validate_batch(operations);
        JsonValue::Array diagnostics;
        for (const auto& item : validation.diagnostics) diagnostics.emplace_back(item);
        return JsonValue::Object{{"status", "ok"}, {"api_version", std::string(kApiVersion)},
            {"document_id", state.document_id}, {"epoch", id_string(state.epoch)},
            {"revision", id_string(state.revision)}, {"valid", validation.valid},
            {"operation_count", static_cast<double>(operations.size())}, {"diagnostics", std::move(diagnostics)}};
    }
    if (*operation == "batch.apply") {
        JsonValue failure;
        if (!authorize(request, "document.edit", failure)) return failure;
        for (const auto& [key, ignored] : *request.object()) {
            (void)ignored;
            if (key != "operation" && key != "session_handle" && key != "request_id" &&
                key != "idempotency_key" && key != "expected_epoch" &&
                key != "expected_revision" && key != "operations")
                return error("INVALID_ARGUMENT", "Unknown batch.apply field: " + key);
        }
        const auto* session_handle = string_field(request, "session_handle");
        const auto* request_id = string_field(request, "request_id");
        const auto* idempotency_key = string_field(request, "idempotency_key");
        if (!session_handle || !request_id || request_id->empty() || request_id->size() > 128 ||
            !idempotency_key || idempotency_key->empty() || idempotency_key->size() > 128)
            return error("INVALID_ARGUMENT", "request_id and idempotency_key (1 to 128 characters) are required for every write.");
        auto session = sessions_.find(*session_handle);
        if (session == sessions_.end()) return error("SESSION_EXPIRED", "The session handle is unknown or expired.");
        JsonValue::Object payload = request.object() ? *request.object() : JsonValue::Object{};
        payload.erase("session_handle"); payload.erase("request_id"); payload.erase("idempotency_key");
        const auto canonical_payload = JsonValue(std::move(payload)).serialize();
        if (canonical_payload.size() > 64 * 1024)
            return error("RESOURCE_LIMIT", "A batch payload is limited to 64 KiB.");
        const auto prior = session->second.receipts.find(*idempotency_key);
        if (prior != session->second.receipts.end()) {
            if (prior->second.canonical_payload != canonical_payload)
                return error("IDEMPOTENCY_CONFLICT", "This idempotency_key was already used with a different write payload.");
            return prior->second.response;
        }
        if (session->second.receipts.size() >= 128)
            return error("RESOURCE_LIMIT", "This session has reached its 128 committed write receipt limit.");
        uint64_t epoch = 0, revision = 0;
        if (!read_revision_pair(request, epoch, revision))
            return error("INVALID_ARGUMENT", "expected_epoch and expected_revision must be unsigned decimal strings.");
        std::vector<BatchOperation> operations;
        std::vector<std::string> client_refs;
        std::string diagnostic;
        if (!parse_object_operations(field(request, "operations"), epoch, revision,
                                     operations, client_refs, diagnostic))
            return error("INVALID_ARGUMENT", std::move(diagnostic));
        const auto result = service_.apply_batch(epoch, revision, operations);
        if (!result.ok()) {
            const char* code = result.status == Status::RevisionConflict ? "REVISION_CONFLICT" :
                result.status == Status::EpochMismatch ? "EPOCH_MISMATCH" :
                result.status == Status::TargetNotFound ? "TARGET_NOT_FOUND" :
                result.status == Status::UnsupportedCapability ? "UNSUPPORTED_CAPABILITY" :
                result.status == Status::BusyInteraction ? "BUSY_INTERACTION" :
                result.status == Status::InternalError ? "INTERNAL_ERROR" : "INVALID_ARGUMENT";
            return error(code, result.diagnostic);
        }
        JsonValue::Array ids;
        JsonValue::Array affected_ids;
        JsonValue::Object client_ref_map;
        for (size_t index = 0; index < result.created_ids.size(); ++index) {
            const auto id = id_string(result.created_ids[index]);
            ids.emplace_back(id);
            if (index < result.created_operation_indices.size()) {
                const auto operation_index = result.created_operation_indices[index];
                if (operation_index < client_refs.size() && !client_refs[operation_index].empty())
                    client_ref_map[client_refs[operation_index]] = id;
            }
        }
        for (const auto id : result.affected_ids) affected_ids.emplace_back(id_string(id));
        JsonValue receipt = JsonValue::Object{{"status", "ok"}, {"api_version", std::string(kApiVersion)},
            {"request_id", *request_id}, {"idempotency_key", *idempotency_key},
            {"document_id", service_.inspect().document_id}, {"epoch", id_string(epoch)},
            {"revision_before", id_string(result.revision_before)},
            {"revision_after", id_string(result.revision_after)}, {"created_ids", std::move(ids)},
            {"affected_ids", std::move(affected_ids)},
            {"client_ref_to_id", std::move(client_ref_map)}};
        session->second.receipts.emplace(*idempotency_key, Receipt{canonical_payload, receipt});
        return receipt;
    }
    if (*operation == "object.create_rectangle") {
        if (!only_fields(request, {"operation", "session_handle", "request_id", "idempotency_key",
            "expected_epoch", "expected_revision", "x_mm", "y_mm", "width_mm", "height_mm",
            "layer_index", "fill", "stroke", "stroke_width_mm"}))
            return error("INVALID_ARGUMENT", "object.create_rectangle contains an unknown field.");
        JsonValue failure;
        if (!authorize(request, "document.edit", failure)) return failure;
        const auto* session_handle = string_field(request, "session_handle");
        const auto* request_id = string_field(request, "request_id");
        const auto* idempotency_key = string_field(request, "idempotency_key");
        if (!session_handle || !request_id || request_id->empty() || request_id->size() > 128 ||
            !idempotency_key || idempotency_key->empty() || idempotency_key->size() > 128)
            return error("INVALID_ARGUMENT", "request_id and idempotency_key (1 to 128 characters) are required for every write.");
        auto session = sessions_.find(*session_handle);
        if (session == sessions_.end()) return error("SESSION_EXPIRED", "The session handle is unknown or expired.");
        JsonValue::Object payload = request.object() ? *request.object() : JsonValue::Object{};
        payload.erase("session_handle"); payload.erase("request_id"); payload.erase("idempotency_key");
        const auto canonical_payload = JsonValue(std::move(payload)).serialize();
        if (canonical_payload.size() > 16 * 1024)
            return error("RESOURCE_LIMIT", "A write payload is limited to 16 KiB.");
        const auto prior = session->second.receipts.find(*idempotency_key);
        if (prior != session->second.receipts.end()) {
            if (prior->second.canonical_payload != canonical_payload)
                return error("IDEMPOTENCY_CONFLICT", "This idempotency_key was already used with a different write payload.");
            return prior->second.response;
        }
        if (session->second.receipts.size() >= 128)
            return error("RESOURCE_LIMIT", "This session has reached its 128 committed write receipt limit; disconnect and create a new session.");
        const auto* epoch_text = string_field(request, "expected_epoch");
        const auto* revision_text = string_field(request, "expected_revision");
        const auto* x = number_field(request, "x_mm");
        const auto* y = number_field(request, "y_mm");
        const auto* width = number_field(request, "width_mm");
        const auto* height = number_field(request, "height_mm");
        if (!epoch_text || !revision_text || !x || !y || !width || !height)
            return error("INVALID_ARGUMENT", "expected_epoch, expected_revision and finite mm dimensions are required.");
        uint64_t epoch = 0, revision = 0;
        const auto ep = std::from_chars(epoch_text->data(), epoch_text->data() + epoch_text->size(), epoch);
        const auto rev = std::from_chars(revision_text->data(), revision_text->data() + revision_text->size(), revision);
        if (ep.ec != std::errc{} || ep.ptr != epoch_text->data() + epoch_text->size() ||
            rev.ec != std::errc{} || rev.ptr != revision_text->data() + revision_text->size())
            return error("INVALID_ARGUMENT", "Epoch and revision must be unsigned decimal strings.");
        CreateRectangle create;
        create.expected_epoch = epoch; create.expected_revision = revision;
        create.bounds_mm = {*x, *y, *width, *height};
        if (const auto* layer = number_field(request, "layer_index")) {
            if (*layer < 0.0 || *layer > 4095.0 || std::floor(*layer) != *layer)
                return error("INVALID_ARGUMENT", "layer_index must be an integer in [0, 4095].");
            create.layer_index = static_cast<size_t>(*layer);
        }
        if (!parse_color(field(request, "fill"), create.fill) ||
            !parse_color(field(request, "stroke"), create.stroke))
            return error("INVALID_ARGUMENT", "The current create_rectangle capability accepts valid RGB source colors.");
        if (const auto* stroke_width = number_field(request, "stroke_width_mm")) create.stroke_width_mm = *stroke_width;
        const auto result = service_.create_rectangle(create);
        if (!result.ok()) {
            const char* code = result.status == Status::RevisionConflict ? "REVISION_CONFLICT" :
                result.status == Status::EpochMismatch ? "EPOCH_MISMATCH" :
                result.status == Status::TargetNotFound ? "TARGET_NOT_FOUND" : "INVALID_ARGUMENT";
            return error(code, result.diagnostic);
        }
        JsonValue::Array ids; for (const auto id : result.created_ids) ids.emplace_back(id_string(id));
        JsonValue receipt = JsonValue::Object{{"status", "ok"}, {"api_version", std::string(kApiVersion)},
            {"request_id", *request_id}, {"idempotency_key", *idempotency_key},
            {"revision_before", id_string(result.revision_before)}, {"revision_after", id_string(result.revision_after)},
            {"created_ids", std::move(ids)}};
        session->second.receipts.emplace(*idempotency_key, Receipt{canonical_payload, receipt});
        return receipt;
    }
    return error("UNSUPPORTED_CAPABILITY", "No handler is registered for this operation.");
}

} // namespace pikdraw::automation
