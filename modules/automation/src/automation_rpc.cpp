#include "pikdraw/automation/automation_rpc.hpp"
#include "automation_rpc_internal.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <type_traits>

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

JsonValue query_summary_json(const document::DocumentSnapshot& snapshot,
                             document::ObjectId id,
                             const document::ObjectVariant& object,
                             size_t layer_index,
                             const std::set<std::string, std::less<>>& fields) {
    const auto* entry = snapshot.hierarchy().find(id);
    JsonValue::Object result;
    for (const auto& name : fields) {
        if (name == "id") result.emplace(name, id_string(id));
        else if (name == "type") {
            const auto type = std::visit([](const auto& value) -> const char* {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, document::PathObject>) return "path";
                if constexpr (std::is_same_v<T, document::TextObject>) return "text";
                if constexpr (std::is_same_v<T, document::GroupObject>) return "group";
                return "image";
            }, object);
            result.emplace(name, type);
        } else if (name == "name") {
            result.emplace(name, std::visit([](const auto& value) { return value.name; }, object));
        } else if (name == "layer_index") {
            result.emplace(name, static_cast<double>(layer_index));
        } else if (name == "bounds" || name == "bounds_quality") {
            const auto quality = std::visit([](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, document::PathObject> ||
                              std::is_same_v<T, document::ImageObject>)
                    return ObjectSummary::BoundsQuality::SourceGeometry;
                if constexpr (std::is_same_v<T, document::TextObject>)
                    return ObjectSummary::BoundsQuality::TextApproximation;
                return ObjectSummary::BoundsQuality::Unsupported;
            }, object);
            if (name == "bounds_quality") result.emplace(name, bounds_quality(quality));
            else if (quality == ObjectSummary::BoundsQuality::Unsupported || !entry || !entry->bounds)
                result.emplace(name, nullptr);
            else result.emplace(name, rect_json(*entry->bounds));
        }
    }
    return result;
}

const char* query_object_type(const document::ObjectVariant& object) {
    return std::visit([](const auto& value) -> const char* {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, document::PathObject>) return "path";
        if constexpr (std::is_same_v<T, document::TextObject>) return "text";
        if constexpr (std::is_same_v<T, document::GroupObject>) return "group";
        return "image";
    }, object);
}

uint64_t query_filter_fingerprint(uint64_t active_page_id,
                                  const std::optional<uint64_t>& object_id,
                                  const std::optional<size_t>& layer_index,
                                  const std::optional<std::string>& object_type,
                                  const std::optional<geometry::Rect2D>& region) {
    std::string canonical = std::to_string(active_page_id) + ";";
    canonical += object_id ? std::to_string(*object_id) : "-";
    canonical += ";";
    canonical += layer_index ? std::to_string(*layer_index) : "-";
    canonical += ";";
    canonical += object_type ? *object_type : "-";
    canonical += ";";
    if (region) {
        canonical += std::to_string(std::bit_cast<uint64_t>(region->x)) + ";";
        canonical += std::to_string(std::bit_cast<uint64_t>(region->y)) + ";";
        canonical += std::to_string(std::bit_cast<uint64_t>(region->width)) + ";";
        canonical += std::to_string(std::bit_cast<uint64_t>(region->height));
    } else {
        canonical += "-";
    }
    uint64_t hash = 14695981039346656037ull;
    for (const unsigned char byte : canonical) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
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
                {"input_schema", "{page_width_mm,page_height_mm,title} or {page_width,page_height,length_unit,title}"},
                {"writes_document", true}});
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
    if (*operation == "objects.changes") {
        if (!only_fields(request, {"operation", "session_handle", "expected_epoch",
                                   "from_revision"}))
            return error("INVALID_ARGUMENT", "objects.changes contains an unknown field.");
        JsonValue failure;
        if (!authorize(request, "document.read", failure)) return failure;
        const auto* epoch_text = string_field(request, "expected_epoch");
        const auto* from_text = string_field(request, "from_revision");
        uint64_t expected_epoch = 0;
        uint64_t from_revision = 0;
        const auto parse_u64 = [](const std::string* text, uint64_t& value) {
            if (!text || text->empty()) return false;
            const auto parsed = std::from_chars(text->data(),
                text->data() + text->size(), value);
            return parsed.ec == std::errc{} &&
                   parsed.ptr == text->data() + text->size();
        };
        if (!parse_u64(epoch_text, expected_epoch) ||
            !parse_u64(from_text, from_revision))
            return error("INVALID_ARGUMENT",
                "expected_epoch and from_revision must be unsigned decimal strings.");
        const auto snapshot = service_.snapshot();
        if (!snapshot)
            return error("DOCUMENT_CLOSED", "No committed document snapshot is available.");
        if (expected_epoch != snapshot->epoch())
            return error("EPOCH_MISMATCH", "The document lifecycle changed.");
        const auto delta = snapshot->changes_since(from_revision);
        JsonValue::Array changed_ids;
        for (const auto id : delta.changed_ids)
            changed_ids.emplace_back(id_string(id));
        return JsonValue::Object{{"status", "ok"},
            {"api_version", std::string(kApiVersion)},
            {"document_id", snapshot->document_id()},
            {"epoch", id_string(snapshot->epoch())},
            {"from_revision", id_string(from_revision)},
            {"to_revision", id_string(delta.to_revision)},
            {"contiguous", delta.contiguous},
            {"requires_full_query", delta.requires_full_query},
            {"changed_ids", std::move(changed_ids)}};
    }
    if (*operation == "objects.query") {
        if (!only_fields(request, {"operation", "session_handle", "offset", "limit", "cursor", "fields",
                                   "object_id", "layer_index", "type", "region_mm",
                                   "expected_epoch", "expected_revision"}))
            return error("INVALID_ARGUMENT", "objects.query contains an unknown field.");
        JsonValue failure;
        if (!authorize(request, "document.read", failure)) return failure;
        const auto state = service_.inspect();
        const auto snapshot = service_.snapshot();
        if (!snapshot) return error("DOCUMENT_CLOSED", "No committed document snapshot is available.");
        std::optional<std::set<std::string, std::less<>>> projection;
        if (const auto* fields_value = field(request, "fields")) {
            const auto* fields = fields_value->array();
            if (!fields || fields->empty() || fields->size() > 6)
                return error("INVALID_ARGUMENT", "fields must be a non-empty array of at most six field names.");
            static const std::set<std::string, std::less<>> allowed_fields{
                "bounds", "bounds_quality", "id", "layer_index", "name", "type"};
            projection.emplace();
            for (const auto& value : *fields) {
                if (!value.string() || !allowed_fields.contains(*value.string()) ||
                    !projection->insert(*value.string()).second)
                    return error("INVALID_ARGUMENT", "fields contains an unknown, non-string, or duplicate field.");
            }
        }
        const auto read_unsigned = [&](std::string_view name, uint64_t fallback, uint64_t& out) {
            const auto* text = string_field(request, name);
            if (!text) { out = fallback; return true; }
            const auto parsed = std::from_chars(text->data(), text->data() + text->size(), out);
            return parsed.ec == std::errc{} && parsed.ptr == text->data() + text->size();
        };
        uint64_t offset = 0;
        uint64_t expected_epoch = state.epoch;
        uint64_t expected_revision = state.revision;
        const bool has_offset = field(request, "offset") != nullptr;
        const bool has_cursor = field(request, "cursor") != nullptr;
        const bool has_epoch = string_field(request, "expected_epoch") != nullptr;
        const bool has_revision = string_field(request, "expected_revision") != nullptr;
        if ((has_cursor && !string_field(request, "cursor")) ||
            (has_offset && !string_field(request, "offset")) ||
            (has_cursor && has_offset))
            return error("INVALID_ARGUMENT", "cursor and offset must be strings and cannot be combined.");
        if (!read_unsigned("offset", 0, offset) ||
            !read_unsigned("expected_epoch", state.epoch, expected_epoch) ||
            !read_unsigned("expected_revision", state.revision, expected_revision))
            return error("INVALID_ARGUMENT", "offset, expected_epoch and expected_revision must be unsigned decimal strings.");
        uint64_t limit = 100;
        if (field(request, "limit") && !number_field(request, "limit"))
            return error("INVALID_ARGUMENT", "limit must be an integer in [1, 100].");
        if (const auto* value = number_field(request, "limit")) {
            if (*value < 1.0 || *value > 100.0 || std::floor(*value) != *value)
                return error("INVALID_ARGUMENT", "limit must be an integer in [1, 100].");
            limit = static_cast<uint64_t>(*value);
        }

        const auto& layers = snapshot->page().layers;
        uint64_t root_count = 0;
        for (const auto& layer : layers) root_count += static_cast<uint64_t>(layer.object_ids.size());
        std::optional<uint64_t> filter_object_id;
        std::optional<size_t> filter_layer_index;
        std::optional<std::string> filter_type;
        std::optional<geometry::Rect2D> filter_region;
        if (field(request, "object_id")) {
            const auto* text = string_field(request, "object_id");
            uint64_t id = 0;
            if (!text || text->empty())
                return error("INVALID_ARGUMENT", "object_id must be a nonzero unsigned decimal string.");
            const auto parsed = std::from_chars(text->data(), text->data() + text->size(), id);
            if (parsed.ec != std::errc{} || parsed.ptr != text->data() + text->size() || id == 0)
                return error("INVALID_ARGUMENT", "object_id must be a nonzero unsigned decimal string.");
            filter_object_id = id;
        }
        if (field(request, "layer_index")) {
            const auto* value = number_field(request, "layer_index");
            if (!value || !std::isfinite(*value) || *value < 0.0 ||
                *value >= static_cast<double>(layers.size()) || std::floor(*value) != *value)
                return error("INVALID_ARGUMENT", "layer_index must be an existing nonnegative integer index.");
            filter_layer_index = static_cast<size_t>(*value);
        }
        if (field(request, "type")) {
            const auto* value = string_field(request, "type");
            if (!value || (*value != "path" && *value != "text" &&
                           *value != "group" && *value != "image"))
                return error("INVALID_ARGUMENT", "type must be path, text, group or image.");
            filter_type = *value;
        }
        if (field(request, "region_mm")) {
            const auto* area = field(request, "region_mm");
            if (!area || !area->object() ||
                !only_fields(*area, {"x_mm", "y_mm", "width_mm", "height_mm"}))
                return error("INVALID_ARGUMENT", "region_mm must contain only x_mm, y_mm, width_mm and height_mm.");
            const auto* x = number_field(*area, "x_mm");
            const auto* y = number_field(*area, "y_mm");
            const auto* width = number_field(*area, "width_mm");
            const auto* height = number_field(*area, "height_mm");
            if (!x || !y || !width || !height)
                return error("INVALID_ARGUMENT", "region_mm requires finite x_mm, y_mm, width_mm and height_mm.");
            geometry::Rect2D region{*x, *y, *width, *height};
            if (!region.is_finite() || region.width <= 0.0 || region.height <= 0.0 ||
                !std::isfinite(region.right()) || !std::isfinite(region.bottom()))
                return error("INVALID_ARGUMENT", "region_mm must have finite coordinates and positive dimensions.");
            filter_region = region;
        }
        const bool has_filters = filter_object_id || filter_layer_index || filter_type || filter_region;

        if (has_filters) {
            const auto filter_fingerprint = query_filter_fingerprint(
                snapshot->active_page_id(), filter_object_id, filter_layer_index, filter_type, filter_region);
            const auto matches_filter = [&](uint64_t id, const document::ObjectVariant& object,
                                            size_t layer_index) {
                if (filter_object_id && id != *filter_object_id) return false;
                if (filter_layer_index && layer_index != *filter_layer_index) return false;
                if (filter_type && *filter_type != query_object_type(object)) return false;
                if (filter_region) {
                    const auto* entry = snapshot->hierarchy().find(id);
                    if (!entry || !entry->bounds || !entry->bounds->intersects(*filter_region)) return false;
                }
                return true;
            };
            if ((has_cursor || offset > 0) && (!has_epoch || !has_revision))
                return error("INVALID_ARGUMENT", "Filtered continuation pages require expected_epoch and expected_revision.");
            if (expected_epoch != snapshot->epoch() || expected_revision != snapshot->revision())
                return error("REVISION_CONFLICT", "The document changed between query pages; inspect and restart paging.");

            size_t start_layer = 0;
            size_t start_object = 0;
            uint64_t logical_offset = offset;
            if (has_cursor) {
                const std::string_view cursor = *string_field(request, "cursor");
                std::array<std::string_view, 6> parts{};
                size_t part_begin = 0;
                bool well_formed = cursor.starts_with("v2:");
                for (size_t index = 0; well_formed && index + 1 < parts.size(); ++index) {
                    const auto separator = cursor.find(':', part_begin);
                    if (separator == std::string_view::npos) {
                        well_formed = false;
                        break;
                    }
                    parts[index] = cursor.substr(part_begin, separator - part_begin);
                    part_begin = separator + 1;
                }
                if (well_formed) parts.back() = cursor.substr(part_begin);
                const auto parse_decimal = [](std::string_view text, uint64_t& value) {
                    if (text.empty()) return false;
                    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
                    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
                };
                uint64_t page_id = 0, layer_index = 0, object_index = 0, cursor_offset = 0,
                         cursor_fingerprint = 0;
                if (!well_formed || parts.back().find(':') != std::string_view::npos ||
                    parts[0] != "v2" ||
                    !parse_decimal(parts[1], page_id) || !parse_decimal(parts[2], layer_index) ||
                    !parse_decimal(parts[3], object_index) || !parse_decimal(parts[4], cursor_offset) ||
                    !parse_decimal(parts[5], cursor_fingerprint))
                    return error("INVALID_ARGUMENT", "Filtered cursor is malformed or from an unsupported version.");
                if (page_id != snapshot->active_page_id() || cursor_fingerprint != filter_fingerprint ||
                    cursor_offset > root_count)
                    return error("INVALID_ARGUMENT", "Filtered cursor belongs to a different page or query.");
                if (layer_index >= layers.size() ||
                    object_index >= layers[static_cast<size_t>(layer_index)].object_ids.size())
                    return error("INVALID_ARGUMENT", "Filtered cursor position is outside the current object list.");
                start_layer = static_cast<size_t>(layer_index);
                start_object = static_cast<size_t>(object_index);
                logical_offset = cursor_offset;
            } else {
                if (offset > root_count)
                    return error("INVALID_ARGUMENT", "offset is outside the current object list.");
                uint64_t skipped_matches = 0;
                bool found_start = false;
                for (size_t layer_index = 0; layer_index < layers.size() && !found_start; ++layer_index) {
                    const auto& ids = layers[layer_index].object_ids;
                    for (size_t object_index = 0; object_index < ids.size(); ++object_index) {
                        const auto* object = snapshot->objects().lookup_ref(ids[object_index]);
                        if (!object || !matches_filter(ids[object_index], *object, layer_index)) continue;
                        if (skipped_matches == offset) {
                            start_layer = layer_index;
                            start_object = object_index;
                            found_start = true;
                            break;
                        }
                        ++skipped_matches;
                    }
                }
                if (!found_start) {
                    start_layer = layers.size();
                    start_object = 0;
                }
            }

            JsonValue::Array objects;
            objects.reserve(static_cast<size_t>(limit));
            std::optional<std::pair<size_t, size_t>> next_match;
            for (size_t layer_index = start_layer; layer_index < layers.size() && !next_match; ++layer_index) {
                const auto& ids = layers[layer_index].object_ids;
                const size_t first = layer_index == start_layer ? start_object : 0;
                for (size_t object_index = first; object_index < ids.size(); ++object_index) {
                    const auto id = ids[object_index];
                    const auto* object = snapshot->objects().lookup_ref(id);
                    if (!object || !matches_filter(id, *object, layer_index)) continue;
                    if (objects.size() >= limit) {
                        next_match = std::pair{layer_index, object_index};
                        break;
                    }
                    if (projection)
                        objects.emplace_back(query_summary_json(*snapshot, id, *object, layer_index, *projection));
                    else
                        objects.emplace_back(object_json(*object, layer_index));
                }
            }

            const auto next_offset = logical_offset + static_cast<uint64_t>(objects.size());
            std::string next_cursor;
            if (next_match) {
                next_cursor = "v2:" + std::to_string(snapshot->active_page_id()) + ":" +
                    std::to_string(next_match->first) + ":" + std::to_string(next_match->second) + ":" +
                    std::to_string(next_offset) + ":" + std::to_string(filter_fingerprint);
            }
            return JsonValue::Object{{"status", "ok"}, {"api_version", std::string(kApiVersion)},
                {"document_id", snapshot->document_id()}, {"epoch", id_string(snapshot->epoch())},
                {"revision", id_string(snapshot->revision())}, {"offset", id_string(logical_offset)},
                {"limit", static_cast<double>(limit)},
                {"next_offset", next_match ? id_string(next_offset) : ""},
                {"next_cursor", std::move(next_cursor)}, {"objects", std::move(objects)}};
        }

        const uint64_t total = root_count;
        const auto position_for_offset = [&](uint64_t absolute) {
            for (size_t layer_index = 0; layer_index < layers.size(); ++layer_index) {
                const auto layer_size = static_cast<uint64_t>(layers[layer_index].object_ids.size());
                if (absolute < layer_size) return std::pair{layer_index, static_cast<size_t>(absolute)};
                absolute -= layer_size;
            }
            return std::pair{layers.size(), size_t{0}};
        };

        size_t start_layer = 0;
        size_t start_object = 0;
        if (has_cursor) {
            const auto& cursor = *string_field(request, "cursor");
            constexpr std::string_view prefix = "v1:";
            const auto separator = cursor.find(':', prefix.size());
            uint64_t layer_index = 0;
            uint64_t object_index = 0;
            if (!cursor.starts_with(prefix) || separator == std::string::npos ||
                separator == prefix.size() || separator + 1 == cursor.size())
                return error("INVALID_ARGUMENT", "cursor is malformed or from an unsupported version.");
            const auto layer_parse = std::from_chars(cursor.data() + prefix.size(),
                cursor.data() + separator, layer_index);
            const auto object_parse = std::from_chars(cursor.data() + separator + 1,
                cursor.data() + cursor.size(), object_index);
            if (layer_parse.ec != std::errc{} || layer_parse.ptr != cursor.data() + separator ||
                object_parse.ec != std::errc{} || object_parse.ptr != cursor.data() + cursor.size() ||
                layer_index >= layers.size() ||
                object_index >= layers[static_cast<size_t>(layer_index)].object_ids.size())
                return error("INVALID_ARGUMENT", "cursor position is outside the current object list.");
            start_layer = static_cast<size_t>(layer_index);
            start_object = static_cast<size_t>(object_index);
            for (size_t index = 0; index < start_layer; ++index)
                offset += static_cast<uint64_t>(layers[index].object_ids.size());
            offset += object_index;
        } else {
            if (offset > total) return error("INVALID_ARGUMENT", "offset is outside the current object list.");
            const auto position = position_for_offset(offset);
            start_layer = position.first;
            start_object = position.second;
        }
        if ((has_cursor || offset > 0) && (!has_epoch || !has_revision))
            return error("INVALID_ARGUMENT", "Paged queries after the first page require expected_epoch and expected_revision.");
        if (expected_epoch != snapshot->epoch() || expected_revision != snapshot->revision())
            return error("REVISION_CONFLICT", "The document changed between query pages; inspect and restart paging.");

        const auto count = std::min<uint64_t>(limit, total - offset);
        const auto end = offset + count;
        JsonValue::Array objects;
        objects.reserve(static_cast<size_t>(count));
        uint64_t absolute = offset;
        for (size_t layer_index = start_layer; layer_index < layers.size() && absolute < end; ++layer_index) {
            const auto& ids = layers[layer_index].object_ids;
            const size_t first = layer_index == start_layer ? start_object : 0;
            for (size_t object_index = first; object_index < ids.size() && absolute < end;
                 ++object_index, ++absolute) {
                const auto* object = snapshot->objects().lookup_ref(ids[object_index]);
                if (object) {
                    if (projection)
                        objects.emplace_back(query_summary_json(*snapshot, ids[object_index], *object,
                                                               layer_index, *projection));
                    else
                        objects.emplace_back(object_json(*object, layer_index));
                }
            }
        }
        std::string next_cursor;
        if (end < total) {
            const auto next = position_for_offset(end);
            next_cursor = "v1:" + std::to_string(next.first) + ":" + std::to_string(next.second);
        }
        return JsonValue::Object{{"status", "ok"}, {"api_version", std::string(kApiVersion)},
            {"document_id", snapshot->document_id()}, {"epoch", id_string(snapshot->epoch())},
            {"revision", id_string(snapshot->revision())}, {"offset", id_string(offset)},
            {"limit", static_cast<double>(limit)}, {"next_offset", end < total ? id_string(end) : ""},
            {"next_cursor", std::move(next_cursor)},
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
            "x", "y", "width", "height", "length_unit", "layer_index", "fill", "stroke",
            "stroke_width_mm", "stroke_width"}))
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
        if (!epoch_text || !revision_text)
            return error("INVALID_ARGUMENT", "expected_epoch and expected_revision are required.");
        const bool has_mm_fields = field(request, "x_mm") || field(request, "y_mm") ||
            field(request, "width_mm") || field(request, "height_mm") || field(request, "stroke_width_mm");
        const bool has_typed_fields = field(request, "x") || field(request, "y") ||
            field(request, "width") || field(request, "height") || field(request, "stroke_width");
        const auto* length_unit_text = string_field(request, "length_unit");
        const bool has_length_unit = field(request, "length_unit") != nullptr;
        if ((has_mm_fields && (has_typed_fields || has_length_unit)) ||
            (has_typed_fields != has_length_unit))
            return error("INVALID_ARGUMENT", "Use either explicit *_mm fields or x/y/width/height with length_unit; do not mix them.");

        geometry::units::LengthUnit unit = geometry::units::LengthUnit::Millimeter;
        if (has_typed_fields) {
            const auto parsed_unit = length_unit_text
                ? parse_physical_length_unit(*length_unit_text) : std::nullopt;
            if (!parsed_unit)
                return error("INVALID_ARGUMENT", "length_unit must be one of mm, cm, m, in, ft, yd, pt or pc; px needs an explicit DPI basis.");
            unit = *parsed_unit;
        } else if (has_length_unit) {
            return error("INVALID_ARGUMENT", "length_unit is only valid with x/y/width/height typed fields.");
        }

        const auto read_required_length = [&](std::string_view key, double& value_mm) {
            const auto* value = number_field(request, key);
            return value && std::isfinite(*value) &&
                geometry::units::convert_length_to_mm(*value, unit, value_mm);
        };
        double x_mm = 0.0, y_mm = 0.0, width_mm = 0.0, height_mm = 0.0;
        const bool lengths_valid = has_typed_fields
            ? read_required_length("x", x_mm) && read_required_length("y", y_mm) &&
                read_required_length("width", width_mm) && read_required_length("height", height_mm)
            : read_required_length("x_mm", x_mm) && read_required_length("y_mm", y_mm) &&
                read_required_length("width_mm", width_mm) && read_required_length("height_mm", height_mm);
        if (!lengths_valid)
            return error("INVALID_ARGUMENT", "Rectangle coordinates and dimensions must be finite numbers in one explicit physical unit.");
        uint64_t epoch = 0, revision = 0;
        const auto ep = std::from_chars(epoch_text->data(), epoch_text->data() + epoch_text->size(), epoch);
        const auto rev = std::from_chars(revision_text->data(), revision_text->data() + revision_text->size(), revision);
        if (ep.ec != std::errc{} || ep.ptr != epoch_text->data() + epoch_text->size() ||
            rev.ec != std::errc{} || rev.ptr != revision_text->data() + revision_text->size())
            return error("INVALID_ARGUMENT", "Epoch and revision must be unsigned decimal strings.");
        CreateRectangle create;
        create.expected_epoch = epoch; create.expected_revision = revision;
        create.bounds_mm = {x_mm, y_mm, width_mm, height_mm};
        if (const auto* layer = number_field(request, "layer_index")) {
            if (*layer < 0.0 || *layer > 4095.0 || std::floor(*layer) != *layer)
                return error("INVALID_ARGUMENT", "layer_index must be an integer in [0, 4095].");
            create.layer_index = static_cast<size_t>(*layer);
        }
        if (!parse_color(field(request, "fill"), create.fill) ||
            !parse_color(field(request, "stroke"), create.stroke))
            return error("INVALID_ARGUMENT", "The current create_rectangle capability accepts valid RGB source colors.");
        const auto* stroke_width_value = number_field(request,
            has_typed_fields ? "stroke_width" : "stroke_width_mm");
        if (field(request, has_typed_fields ? "stroke_width" : "stroke_width_mm")) {
            double stroke_width_mm = 0.0;
            if (!stroke_width_value || !std::isfinite(*stroke_width_value) ||
                !geometry::units::convert_length_to_mm(*stroke_width_value, unit, stroke_width_mm))
                return error("INVALID_ARGUMENT", "stroke_width must be a finite length in the selected unit.");
            create.stroke_width_mm = stroke_width_mm;
        }
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
