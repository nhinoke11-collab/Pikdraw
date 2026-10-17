#include "pikdraw/mcp_server.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pikdraw::mcp {

McpServer::McpServer(InstanceProvider instances, AppCaller app_caller,
                     const std::atomic<bool>* cancellation)
    : instance_provider_(std::move(instances)), app_caller_(std::move(app_caller)),
      cancellation_(cancellation) {}
namespace {
using automation::JsonValue;
constexpr std::string_view kModernVersion = "2026-07-28";
constexpr std::string_view kLegacyVersion = "2025-11-25";
constexpr size_t kMaxMessageBytes = 1024 * 1024;

const JsonValue* field(const JsonValue& value, std::string_view name) { return value.get(name); }
const std::string* string_field(const JsonValue& value, std::string_view name) {
    const auto* item = field(value, name); return item ? item->string() : nullptr;
}

JsonValue parsed_literal(std::string_view source) {
    JsonValue value; std::string error;
    if (!JsonValue::parse(source, value, error)) return {};
    return value;
}

JsonValue object_or_empty(const JsonValue* value) {
    return value && value->object() ? *value : JsonValue(JsonValue::Object{});
}

JsonValue make_error(int code, std::string message, JsonValue data = {}) {
    JsonValue::Object error{{"code", code}, {"message", std::move(message)}};
    if (!data.is_null()) error.emplace("data", std::move(data));
    return error;
}

JsonValue protocol_error(int code, std::string message, const JsonValue& id,
                         JsonValue data = {}) {
    return JsonValue::Object{{"jsonrpc", "2.0"}, {"id", id},
                             {"error", make_error(code, std::move(message), std::move(data))}};
}

JsonValue add_modern_result_fields(JsonValue result) {
    auto object = result.object() ? *result.object() : JsonValue::Object{};
    object["resultType"] = "complete";
    object["_meta"] = JsonValue::Object{{"io.modelcontextprotocol/serverInfo",
        JsonValue::Object{{"name", "pikdraw-mcp-server"}, {"version", "0.1.0"}}}};
    return object;
}

JsonValue schema(std::string_view source) { return parsed_literal(source); }

JsonValue tool_definition(std::string_view name, std::string_view title,
                          std::string_view description, std::string_view input_schema) {
    return JsonValue::Object{{"name", std::string(name)}, {"title", std::string(title)},
        {"description", std::string(description)}, {"inputSchema", schema(input_schema)}};
}

JsonValue tool_definition_with_schema(std::string_view name, std::string_view title,
                                      std::string_view description, JsonValue input_schema) {
    return JsonValue::Object{{"name", std::string(name)}, {"title", std::string(title)},
        {"description", std::string(description)}, {"inputSchema", std::move(input_schema)}};
}

JsonValue batch_input_schema(bool write) {
    const auto rgb_color = schema(R"json({"type":"object","properties":{"model":{"const":"rgb"},"channels":{"type":"object","properties":{"r":{"type":"number","minimum":0,"maximum":1},"g":{"type":"number","minimum":0,"maximum":1},"b":{"type":"number","minimum":0,"maximum":1},"alpha":{"type":"number","minimum":0,"maximum":1}},"required":["r","g","b"],"additionalProperties":false}},"required":["model","channels"],"additionalProperties":false})json");
    const auto point = JsonValue::Object{{"type", "object"},
        {"properties", JsonValue::Object{{"x_mm", JsonValue::Object{{"type", "number"}}},
                                         {"y_mm", JsonValue::Object{{"type", "number"}}}}},
        {"required", JsonValue::Array{JsonValue("x_mm"), JsonValue("y_mm")}},
        {"additionalProperties", false}};
    const auto make_operation = [](JsonValue::Object properties, JsonValue::Array required) {
        return JsonValue::Object{{"type", "object"}, {"properties", std::move(properties)},
            {"required", std::move(required)}, {"additionalProperties", false}};
    };
    const auto string_id = JsonValue::Object{{"type", "string"}, {"pattern", "^[0-9]+$"}};
    JsonValue::Array operation_variants;
    operation_variants.emplace_back(make_operation({
        {"op", JsonValue::Object{{"enum", JsonValue::Array{JsonValue("object.create_rectangle"), JsonValue("object.create_ellipse")}}}},
        {"client_ref", JsonValue::Object{{"type", "string"}, {"minLength", 1}, {"maxLength", 64}}},
        {"name", JsonValue::Object{{"type", "string"}, {"maxLength", 256}}},
        {"x_mm", JsonValue::Object{{"type", "number"}}}, {"y_mm", JsonValue::Object{{"type", "number"}}},
        {"width_mm", JsonValue::Object{{"type", "number"}, {"exclusiveMinimum", 0}}},
        {"height_mm", JsonValue::Object{{"type", "number"}, {"exclusiveMinimum", 0}}},
        {"layer_index", JsonValue::Object{{"type", "integer"}, {"minimum", 0}, {"maximum", 4095}}},
        {"fill", rgb_color}, {"stroke", rgb_color},
        {"stroke_width_mm", JsonValue::Object{{"type", "number"}, {"minimum", 0}}}},
        {"op", "x_mm", "y_mm", "width_mm", "height_mm"}));
    operation_variants.emplace_back(make_operation({
        {"op", JsonValue::Object{{"const", "object.create_text"}}},
        {"client_ref", JsonValue::Object{{"type", "string"}, {"minLength", 1}, {"maxLength", 64}}},
        {"name", JsonValue::Object{{"type", "string"}, {"maxLength", 256}}},
        {"text", JsonValue::Object{{"type", "string"}, {"minLength", 1}, {"maxLength", 8192}}},
        {"font_family", JsonValue::Object{{"type", "string"}, {"minLength", 1}, {"maxLength", 256}}},
        {"font_postscript_name", JsonValue::Object{{"type", "string"}, {"maxLength", 256}}},
        {"font_weight", JsonValue::Object{{"type", "integer"}, {"minimum", 100}, {"maximum", 900}}},
        {"font_italic", JsonValue::Object{{"type", "boolean"}}},
        {"font_size_pt", JsonValue::Object{{"type", "number"}, {"exclusiveMinimum", 0}, {"maximum", 10000}}},
        {"alignment", JsonValue::Object{{"enum", JsonValue::Array{JsonValue("left"), JsonValue("center"), JsonValue("right")}}}},
        {"underline", JsonValue::Object{{"type", "boolean"}}},
        {"x_mm", JsonValue::Object{{"type", "number"}}}, {"y_mm", JsonValue::Object{{"type", "number"}}},
        {"layer_index", JsonValue::Object{{"type", "integer"}, {"minimum", 0}, {"maximum", 4095}}},
        {"fill", rgb_color}}, {"op", "text", "font_family", "x_mm", "y_mm"}));
    operation_variants.emplace_back(make_operation({
        {"op", JsonValue::Object{{"const", "object.create_line"}}},
        {"client_ref", JsonValue::Object{{"type", "string"}, {"minLength", 1}, {"maxLength", 64}}},
        {"name", JsonValue::Object{{"type", "string"}, {"maxLength", 256}}},
        {"points_mm", JsonValue::Object{{"type", "array"}, {"minItems", 2}, {"maxItems", 2}, {"items", point}}},
        {"layer_index", JsonValue::Object{{"type", "integer"}, {"minimum", 0}, {"maximum", 4095}}},
        {"fill", rgb_color}, {"stroke", rgb_color},
        {"stroke_width_mm", JsonValue::Object{{"type", "number"}, {"minimum", 0}}}},
        {"op", "points_mm"}));
    operation_variants.emplace_back(make_operation({
        {"op", JsonValue::Object{{"const", "object.create_polyline"}}},
        {"client_ref", JsonValue::Object{{"type", "string"}, {"minLength", 1}, {"maxLength", 64}}},
        {"name", JsonValue::Object{{"type", "string"}, {"maxLength", 256}}},
        {"points_mm", JsonValue::Object{{"type", "array"}, {"minItems", 2}, {"maxItems", 4096}, {"items", point}}},
        {"closed", JsonValue::Object{{"type", "boolean"}}},
        {"layer_index", JsonValue::Object{{"type", "integer"}, {"minimum", 0}, {"maximum", 4095}}},
        {"fill", rgb_color}, {"stroke", rgb_color},
        {"stroke_width_mm", JsonValue::Object{{"type", "number"}, {"minimum", 0}}}},
        {"op", "points_mm"}));
    operation_variants.emplace_back(make_operation({
        {"op", JsonValue::Object{{"const", "object.transform"}}},
        {"target_ids", JsonValue::Object{{"type", "array"}, {"minItems", 1}, {"maxItems", 256},
            {"uniqueItems", true}, {"items", string_id}}},
        {"translate_x_mm", JsonValue::Object{{"type", "number"}}},
        {"translate_y_mm", JsonValue::Object{{"type", "number"}}},
        {"scale_x", JsonValue::Object{{"type", "number"}, {"minimum", 0.0001}, {"maximum", 10000}}},
        {"scale_y", JsonValue::Object{{"type", "number"}, {"minimum", 0.0001}, {"maximum", 10000}}},
        {"rotation_degrees", JsonValue::Object{{"type", "number"}, {"minimum", -360000}, {"maximum", 360000}}},
        {"pivot_x_mm", JsonValue::Object{{"type", "number"}}}, {"pivot_y_mm", JsonValue::Object{{"type", "number"}}}},
        {"op", "target_ids"}));
    operation_variants.emplace_back(make_operation({
        {"op", JsonValue::Object{{"const", "object.duplicate"}}},
        {"target_ids", JsonValue::Object{{"type", "array"}, {"minItems", 1}, {"maxItems", 64},
            {"uniqueItems", true}, {"items", string_id}}},
        {"offset_x_mm", JsonValue::Object{{"type", "number"}}},
        {"offset_y_mm", JsonValue::Object{{"type", "number"}}}}, {"op", "target_ids"}));
    operation_variants.emplace_back(make_operation({
        {"op", JsonValue::Object{{"const", "object.delete"}}},
        {"target_ids", JsonValue::Object{{"type", "array"}, {"minItems", 1}, {"maxItems", 256},
            {"uniqueItems", true}, {"items", string_id}}}}, {"op", "target_ids"}));
    operation_variants.emplace_back(make_operation({
        {"op", JsonValue::Object{{"const", "object.style"}}},
        {"target_ids", JsonValue::Object{{"type", "array"}, {"minItems", 1}, {"maxItems", 256},
            {"uniqueItems", true}, {"items", string_id}}},
        {"fill", rgb_color}, {"stroke", rgb_color},
        {"stroke_width_mm", JsonValue::Object{{"type", "number"}, {"minimum", 0}}}},
        {"op", "target_ids"}));
    operation_variants.emplace_back(make_operation({
        {"op", JsonValue::Object{{"const", "object.group"}}},
        {"target_ids", JsonValue::Object{{"type", "array"}, {"minItems", 2}, {"maxItems", 256},
            {"uniqueItems", true}, {"items", string_id}}},
        {"client_ref", JsonValue::Object{{"type", "string"}, {"minLength", 1}, {"maxLength", 64}}},
        {"name", JsonValue::Object{{"type", "string"}, {"maxLength", 256}}}},
        {"op", "target_ids"}));
    operation_variants.emplace_back(make_operation({
        {"op", JsonValue::Object{{"const", "object.ungroup"}}},
        {"target_ids", JsonValue::Object{{"type", "array"}, {"minItems", 1}, {"maxItems", 256},
            {"uniqueItems", true}, {"items", string_id}}}}, {"op", "target_ids"}));
    operation_variants.emplace_back(make_operation({
        {"op", JsonValue::Object{{"const", "object.align"}}},
        {"reference_id", string_id},
        {"target_ids", JsonValue::Object{{"type", "array"}, {"minItems", 1}, {"maxItems", 255},
            {"uniqueItems", true}, {"items", string_id}}},
        {"mode", JsonValue::Object{{"enum", JsonValue::Array{JsonValue("left"), JsonValue("center_x"),
            JsonValue("right"), JsonValue("top"), JsonValue("center_y"), JsonValue("bottom")}}}}},
        {"op", "reference_id", "target_ids", "mode"}));
    operation_variants.emplace_back(make_operation({
        {"op", JsonValue::Object{{"const", "object.distribute"}}},
        {"target_ids", JsonValue::Object{{"type", "array"}, {"minItems", 3}, {"maxItems", 256},
            {"uniqueItems", true}, {"items", string_id}}},
        {"axis", JsonValue::Object{{"enum", JsonValue::Array{JsonValue("horizontal"), JsonValue("vertical")}}}}},
        {"op", "target_ids", "axis"}));
    const auto item = JsonValue::Object{{"oneOf", std::move(operation_variants)}};
    JsonValue::Object properties{{"session_handle", JsonValue::Object{{"type", "string"}}},
        {"expected_epoch", JsonValue::Object{{"type", "string"}, {"pattern", "^[0-9]+$"}}},
        {"expected_revision", JsonValue::Object{{"type", "string"}, {"pattern", "^[0-9]+$"}}},
        {"operations", JsonValue::Object{{"type", "array"}, {"minItems", 1}, {"maxItems", 256},
            {"items", item}}}};
    JsonValue::Array required{JsonValue("session_handle"), JsonValue("expected_epoch"),
        JsonValue("expected_revision"), JsonValue("operations")};
    if (write) {
        properties.emplace("request_id", JsonValue::Object{{"type", "string"}, {"minLength", 1}, {"maxLength", 128}});
        properties.emplace("idempotency_key", JsonValue::Object{{"type", "string"}, {"minLength", 1}, {"maxLength", 128}});
        required.emplace_back("request_id"); required.emplace_back("idempotency_key");
    }
    return JsonValue::Object{{"type", "object"}, {"properties", std::move(properties)},
        {"required", std::move(required)}, {"additionalProperties", false}};
}

JsonValue tools() {
    JsonValue::Array result;
    result.emplace_back(tool_definition("pikdraw.instances", "PikDraw instances",
        "List running PikDraw desktop instances that explicitly enabled the local MCP gateway.",
        R"({"type":"object","additionalProperties":false})"));
    result.emplace_back(tool_definition("pikdraw.connect", "Connect to PikDraw",
        "Create an application session for one listed instance. Editing requires PikDraw to have been started with --enable-mcp-edit.",
        R"({"type":"object","properties":{"instance_id":{"type":"string"},"scopes":{"type":"array","items":{"type":"string","enum":["document.read","document.create","document.edit","file.read","file.write"]},"uniqueItems":true}},"required":["instance_id"],"additionalProperties":false})"));
    result.emplace_back(tool_definition("pikdraw.capabilities", "PikDraw capabilities",
        "Return only automation operations implemented and enabled by this PikDraw build and session.",
        R"({"type":"object","properties":{"session_handle":{"type":"string"}},"required":["session_handle"],"additionalProperties":false})"));
    result.emplace_back(tool_definition("pikdraw.document.inspect", "Inspect document",
        "Read the connected document identity, epoch, revision, page size and dirty state without using the screen.",
        R"({"type":"object","properties":{"session_handle":{"type":"string"}},"required":["session_handle"],"additionalProperties":false})"));
    result.emplace_back(tool_definition("pikdraw.document.create", "Create an automation document",
        "Create a new document tab only through a connected session granted document.create. Connect again to the returned document before editing it.",
        R"({"type":"object","properties":{"session_handle":{"type":"string"},"title":{"type":"string","maxLength":128},"page_width_mm":{"type":"number","minimum":10,"maximum":20000},"page_height_mm":{"type":"number","minimum":10,"maximum":20000}},"required":["session_handle","page_width_mm","page_height_mm"],"additionalProperties":false})"));
    result.emplace_back(tool_definition("pikdraw.font.query", "Resolve a font",
        "Resolve a requested installed font face and report exact family, style, and fallback status.",
        R"({"type":"object","properties":{"session_handle":{"type":"string"},"font_family":{"type":"string","minLength":1,"maxLength":256},"font_postscript_name":{"type":"string","maxLength":256},"font_weight":{"type":"integer","minimum":100,"maximum":900},"font_italic":{"type":"boolean"},"font_size_pt":{"type":"number","exclusiveMinimum":0,"maximum":10000}},"required":["session_handle","font_family"],"additionalProperties":false})"));
    result.emplace_back(tool_definition("pikdraw.text.measure", "Measure text layout",
        "Shape Unicode text with the installed font backend and report advances, line bounds, fallback, and missing glyphs.",
        R"({"type":"object","properties":{"session_handle":{"type":"string"},"text":{"type":"string","maxLength":8192},"font_family":{"type":"string","minLength":1,"maxLength":256},"font_postscript_name":{"type":"string","maxLength":256},"font_weight":{"type":"integer","minimum":100,"maximum":900},"font_italic":{"type":"boolean"},"font_size_pt":{"type":"number","exclusiveMinimum":0,"maximum":10000},"max_width_mm":{"type":"number","minimum":0},"line_spacing_ratio":{"type":"number","exclusiveMinimum":0},"alignment":{"enum":["left","center","right"]}},"required":["session_handle","text","font_family"],"additionalProperties":false})"));
    result.emplace_back(tool_definition("pikdraw.document.validate", "Validate document geometry and text",
        "Run implemented page-bound, text-layout, glyph and optional safe-margin checks. Unsupported group bounds are reported as not assessed.",
        R"({"type":"object","properties":{"session_handle":{"type":"string"},"safe_margin_mm":{"type":"number","minimum":0},"safe_margin_exempt_ids":{"type":"array","maxItems":1024,"items":{"type":"string","pattern":"^[0-9]+$"}}},"required":["session_handle"],"additionalProperties":false})"));
    result.emplace_back(tool_definition("pikdraw.file.save", "Save native document",
        "Save the committed snapshot as an editable PikDraw native file under the app's local automation files folder. Filenames are basenames and existing files are not overwritten.",
        R"({"type":"object","properties":{"session_handle":{"type":"string"},"file_name":{"type":"string","minLength":1,"maxLength":100}},"required":["session_handle","file_name"],"additionalProperties":false})"));
    result.emplace_back(tool_definition("pikdraw.file.open", "Open native document in a new tab",
        "Open a granted native PikDraw file into a new document tab without replacing the connected document. Reconnect to the returned document before editing.",
        R"({"type":"object","properties":{"session_handle":{"type":"string"},"file_name":{"type":"string","minLength":1,"maxLength":100}},"required":["session_handle","file_name"],"additionalProperties":false})"));
    result.emplace_back(tool_definition("pikdraw.file.verify_reopen", "Verify native save by reopening",
        "Load a saved native file from the app's local automation files folder and report its parsed page, revision, object IDs and object count without replacing the active document.",
        R"({"type":"object","properties":{"session_handle":{"type":"string"},"file_name":{"type":"string","minLength":1,"maxLength":100}},"required":["session_handle","file_name"],"additionalProperties":false})"));
    result.emplace_back(tool_definition("pikdraw.objects.query", "Query document objects",
        "Read a page of committed document objects. Keep epoch and revision from the first page for stable paging; restart if the document changes.",
        R"({"type":"object","properties":{"session_handle":{"type":"string"},"offset":{"type":"string","pattern":"^[0-9]+$"},"limit":{"type":"integer","minimum":1,"maximum":100},"expected_epoch":{"type":"string","pattern":"^[0-9]+$"},"expected_revision":{"type":"string","pattern":"^[0-9]+$"}},"required":["session_handle"],"additionalProperties":false})"));
    result.emplace_back(tool_definition("pikdraw.objects.get", "Get one document object",
        "Read one object by stable ID, including its style, editable path nodes, children and source geometry, under an explicit document epoch and revision.",
        R"({"type":"object","properties":{"session_handle":{"type":"string"},"object_id":{"type":"string","pattern":"^[0-9]+$"},"expected_epoch":{"type":"string","pattern":"^[0-9]+$"},"expected_revision":{"type":"string","pattern":"^[0-9]+$"}},"required":["session_handle","object_id","expected_epoch","expected_revision"],"additionalProperties":false})"));
    result.emplace_back(tool_definition("pikdraw.request.get", "Get write receipt",
        "Resolve a possibly timed out write by its idempotency key. A retry with the same key and payload returns the original receipt without applying the edit twice.",
        R"({"type":"object","properties":{"session_handle":{"type":"string"},"idempotency_key":{"type":"string","minLength":1,"maxLength":128}},"required":["session_handle","idempotency_key"],"additionalProperties":false})"));
    result.emplace_back(tool_definition_with_schema("pikdraw.batch.validate", "Validate object batch",
        "Dry-run 1 to 256 typed design operations: rectangle, ellipse, Unicode text, editable line/polyline path, stable-ID transform, style, group/ungroup, alignment, distribution, duplicate, or delete. Returns diagnostics and the base revision without changing the document.",
        batch_input_schema(false)));
    result.emplace_back(tool_definition_with_schema("pikdraw.batch.apply", "Apply object batch",
        "Apply typed create and stable-ID edit operations as one document transaction and one undo unit. Supports line/polyline authoring, move/scale/rotate, style, grouping, align/distribute, path/text duplication and deletion. Include request_id/idempotency_key; a stale base revision is rejected.",
        batch_input_schema(true)));
    result.emplace_back(tool_definition("pikdraw.history.undo", "Undo one document transaction",
        "Undo one committed document transaction only if the supplied epoch and revision still match; writes require request_id and idempotency_key.",
        R"({"type":"object","properties":{"session_handle":{"type":"string"},"request_id":{"type":"string","minLength":1,"maxLength":128},"idempotency_key":{"type":"string","minLength":1,"maxLength":128},"expected_epoch":{"type":"string","pattern":"^[0-9]+$"},"expected_revision":{"type":"string","pattern":"^[0-9]+$"}},"required":["session_handle","request_id","idempotency_key","expected_epoch","expected_revision"],"additionalProperties":false})"));
    result.emplace_back(tool_definition("pikdraw.history.redo", "Redo one document transaction",
        "Redo one committed document transaction only if the supplied epoch and revision still match; writes require request_id and idempotency_key.",
        R"({"type":"object","properties":{"session_handle":{"type":"string"},"request_id":{"type":"string","minLength":1,"maxLength":128},"idempotency_key":{"type":"string","minLength":1,"maxLength":128},"expected_epoch":{"type":"string","pattern":"^[0-9]+$"},"expected_revision":{"type":"string","pattern":"^[0-9]+$"}},"required":["session_handle","request_id","idempotency_key","expected_epoch","expected_revision"],"additionalProperties":false})"));
    result.emplace_back(tool_definition("pikdraw.object.create_rectangle", "Create rectangle",
        "Create one editable vector rectangle in millimeters through PikDraw's document command writer. Uses explicit epoch and revision; stale writes are rejected.",
        R"({"type":"object","properties":{"session_handle":{"type":"string"},"request_id":{"type":"string","minLength":1,"maxLength":128},"idempotency_key":{"type":"string","minLength":1,"maxLength":128},"expected_epoch":{"type":"string","pattern":"^[0-9]+$"},"expected_revision":{"type":"string","pattern":"^[0-9]+$"},"x_mm":{"type":"number"},"y_mm":{"type":"number"},"width_mm":{"type":"number","exclusiveMinimum":0},"height_mm":{"type":"number","exclusiveMinimum":0},"layer_index":{"type":"integer","minimum":0,"maximum":4095},"stroke_width_mm":{"type":"number","minimum":0}},"required":["session_handle","request_id","idempotency_key","expected_epoch","expected_revision","x_mm","y_mm","width_mm","height_mm"],"additionalProperties":false})"));
    result.emplace_back(tool_definition("pikdraw.disconnect", "Disconnect from PikDraw",
        "Disconnect an application session and discard its local handle.",
        R"({"type":"object","properties":{"session_handle":{"type":"string"}},"required":["session_handle"],"additionalProperties":false})"));
    return result;
}

#ifdef _WIN32
std::filesystem::path manifest_directory() {
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (size == 0) return {};
    std::wstring value(size, L'\0');
    const DWORD copied = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), size);
    if (copied == 0 || copied >= size) return {};
    value.resize(copied);
    return std::filesystem::path(value) / L"PikDraw" / L"automation";
}

bool parse_pid(std::string_view value, DWORD& pid) {
    uint32_t parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed == 0) return false;
    pid = parsed;
    return true;
}

std::wstring expected_pipe(DWORD pid) {
    return LR"(\\.\pipe\PikDraw.Automation.v1.)" + std::to_wstring(pid);
}

std::string wide_to_utf8(const std::wstring& input) {
    if (input.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
        static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string result(static_cast<size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
            static_cast<int>(input.size()), result.data(), bytes, nullptr, nullptr) != bytes) return {};
    return result;
}

bool process_is_running(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    DWORD code = 0;
    const bool running = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    CloseHandle(process);
    return running;
}

bool overlapped_transfer(HANDLE pipe, void* buffer, DWORD size, bool writing, DWORD timeout_ms) {
    auto* bytes = static_cast<std::byte*>(buffer);
    DWORD completed = 0;
    while (completed < size) {
        HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!event) return false;
        OVERLAPPED overlapped{};
        overlapped.hEvent = event;
        DWORD immediate = 0;
        const BOOL started = writing
            ? WriteFile(pipe, bytes + completed, size - completed, &immediate, &overlapped)
            : ReadFile(pipe, bytes + completed, size - completed, &immediate, &overlapped);
        if (!started && GetLastError() != ERROR_IO_PENDING) {
            CloseHandle(event); return false;
        }
        DWORD count = immediate;
        if (!started) {
            const DWORD wait = WaitForSingleObject(event, timeout_ms);
            if (wait != WAIT_OBJECT_0) {
                (void)CancelIoEx(pipe, &overlapped);
                (void)WaitForSingleObject(event, INFINITE);
                CloseHandle(event); return false;
            }
            if (!GetOverlappedResult(pipe, &overlapped, &count, FALSE)) {
                CloseHandle(event); return false;
            }
        }
        CloseHandle(event);
        if (count == 0) return false;
        completed += count;
    }
    return true;
}

bool pipe_request(const std::wstring& name, const JsonValue& request, JsonValue& response,
                  std::string& error) {
    if (!WaitNamedPipeW(name.c_str(), 1500)) {
        error = "PikDraw instance is unavailable or its gateway is busy."; return false;
    }
    HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IMPERSONATION, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) { error = "Could not open the PikDraw local pipe."; return false; }
    const auto body = request.serialize();
    if (body.empty() || body.size() > kMaxMessageBytes) {
        CloseHandle(pipe); error = "PikDraw request exceeds the 1 MiB message limit."; return false;
    }
    const auto size = static_cast<uint32_t>(body.size());
    std::array<uint8_t, 4> header{static_cast<uint8_t>(size), static_cast<uint8_t>(size >> 8),
        static_cast<uint8_t>(size >> 16), static_cast<uint8_t>(size >> 24)};
    bool ok = overlapped_transfer(pipe, header.data(), 4, true, 30000) &&
              overlapped_transfer(pipe, const_cast<char*>(body.data()), size, true, 30000);
    if (ok) ok = overlapped_transfer(pipe, header.data(), 4, false, 30000);
    const uint32_t reply_size = static_cast<uint32_t>(header[0]) |
        (static_cast<uint32_t>(header[1]) << 8) | (static_cast<uint32_t>(header[2]) << 16) |
        (static_cast<uint32_t>(header[3]) << 24);
    std::string reply;
    if (ok && reply_size > 0 && reply_size <= kMaxMessageBytes) {
        reply.resize(reply_size);
        ok = overlapped_transfer(pipe, reply.data(), reply_size, false, 30000);
    } else ok = false;
    CloseHandle(pipe);
    if (!ok) { error = "PikDraw pipe request failed or timed out; inspect the document before retrying a write."; return false; }
    if (!JsonValue::parse(reply, response, error)) { error = "PikDraw returned malformed JSON."; return false; }
    return true;
}
#endif

} // namespace

JsonValue McpServer::list_instances() const {
    if (instance_provider_) return instance_provider_();
    JsonValue::Array instances;
#ifdef _WIN32
    const auto directory = manifest_directory();
    std::error_code ec;
    if (directory.empty() || !std::filesystem::exists(directory, ec))
        return JsonValue::Object{{"status", "ok"}, {"instances", std::move(instances)}};
    size_t visited = 0;
    for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end && visited < 128;
         it.increment(ec), ++visited) {
        if (!it->is_regular_file(ec) || it->path().extension() != L".json") continue;
        const auto file_size = it->file_size(ec);
        if (ec || file_size == 0 || file_size > 16 * 1024) { ec.clear(); continue; }
        std::ifstream input(it->path(), std::ios::binary);
        std::string body((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (body.empty() || body.size() > 16 * 1024) continue;
        JsonValue manifest; std::string parse_error;
        if (!JsonValue::parse(body, manifest, parse_error)) continue;
        const auto* id = string_field(manifest, "instance_id");
        const auto* pipe_text = string_field(manifest, "pipe_name");
        const auto* scopes = field(manifest, "granted_scopes");
        DWORD pid = 0;
        if (!id || !pipe_text || !scopes || !scopes->array() || !parse_pid(*id, pid) ||
            it->path().stem().string() != *id || !process_is_running(pid)) continue;
        const auto expected = expected_pipe(pid);
        if (wide_to_utf8(expected) != *pipe_text) continue;
        instances.emplace_back(JsonValue::Object{{"instance_id", *id}, {"pipe_name", *pipe_text},
                                                  {"granted_scopes", *scopes}});
    }
#endif
    return JsonValue::Object{{"status", "ok"}, {"instances", std::move(instances)}};
}

bool McpServer::find_instance(std::string_view instance_id, Endpoint& endpoint) const {
    const auto all = list_instances();
    const auto* entries = all.get("instances");
    if (!entries || !entries->array()) return false;
    for (const auto& entry : *entries->array()) {
        const auto* id = string_field(entry, "instance_id");
        const auto* pipe_text = string_field(entry, "pipe_name");
        const auto* scopes = field(entry, "granted_scopes");
        if (!id || *id != instance_id || !scopes) continue;
        endpoint.instance_id = *id;
        endpoint.granted_scopes = *scopes;
        if (app_caller_) return true;
        if (!pipe_text) continue;
#ifdef _WIN32
        DWORD pid = 0;
        if (!parse_pid(*id, pid)) return false;
        endpoint.pipe_name = expected_pipe(pid);
        return true;
#else
        (void)endpoint;
        return false;
#endif
    }
    return false;
}

bool McpServer::call_app(const Endpoint& endpoint, const JsonValue& request, JsonValue& response,
                         std::string& error) const {
    if (app_caller_)
        return app_caller_(endpoint.instance_id, request, response, error, cancellation_);
#ifdef _WIN32
    return pipe_request(endpoint.pipe_name, request, response, error);
#else
    (void)endpoint; (void)request; (void)response;
    error = "The PikDraw MCP sidecar currently requires Windows named pipes.";
    return false;
#endif
}

void McpServer::disconnect_all() {
    for (const auto& [handle, endpoint] : sessions_) {
        JsonValue response;
        std::string error;
        const JsonValue request(JsonValue::Object{
            {"operation", "session.disconnect"}, {"session_handle", handle}});
        (void)call_app(endpoint, request, response, error);
    }
    sessions_.clear();
}

JsonValue McpServer::dispatch_tool(std::string_view name, const JsonValue& arguments) {
    if (!arguments.object()) return JsonValue::Object{{"status", "error"}, {"code", "INVALID_ARGUMENT"},
        {"message", "Tool arguments must be a JSON object."}};
    if (name == "pikdraw.instances") return list_instances();
    if (name == "pikdraw.connect") {
        if (sessions_.size() >= 64)
            return JsonValue::Object{{"status", "error"}, {"code", "RESOURCE_LIMIT"},
                {"message", "This MCP sidecar allows at most 64 active application sessions."}};
        const auto* instance_id = string_field(arguments, "instance_id");
        if (!instance_id) return JsonValue::Object{{"status", "error"}, {"code", "INVALID_ARGUMENT"},
            {"message", "instance_id is required."}};
        Endpoint endpoint;
        if (!find_instance(*instance_id, endpoint))
            return JsonValue::Object{{"status", "error"}, {"code", "INSTANCE_NOT_FOUND"},
                {"message", "The instance is not running or did not enable the PikDraw gateway."}};
        const JsonValue default_scopes(JsonValue::Array{JsonValue("document.read")});
        const auto* scopes = field(arguments, "scopes");
        if (!scopes) scopes = &default_scopes;
        JsonValue::Object request{{"operation", "session.create"}, {"scopes", *scopes}};
        JsonValue response; std::string pipe_error;
        if (!call_app(endpoint, request, response, pipe_error))
            return JsonValue::Object{{"status", "error"}, {"code", "PIPE_FAILURE"}, {"message", pipe_error}};
        const auto* status = string_field(response, "status");
        const auto* handle = string_field(response, "session_handle");
        if (!status || *status != "ok" || !handle) return response;
        sessions_[*handle] = endpoint;
        return response;
    }

    const auto* session = string_field(arguments, "session_handle");
    if (!session) return JsonValue::Object{{"status", "error"}, {"code", "SESSION_REQUIRED"},
        {"message", "session_handle is required. Call pikdraw.connect first."}};
    const auto endpoint_it = sessions_.find(*session);
    if (endpoint_it == sessions_.end()) return JsonValue::Object{{"status", "error"},
        {"code", "SESSION_EXPIRED"}, {"message", "The session handle is unknown to this MCP process."}};
    std::string operation;
    if (name == "pikdraw.capabilities") operation = "capabilities";
    else if (name == "pikdraw.document.inspect") operation = "document.inspect";
    else if (name == "pikdraw.document.create") operation = "document.create";
    else if (name == "pikdraw.font.query") operation = "font.query";
    else if (name == "pikdraw.text.measure") operation = "text.measure";
    else if (name == "pikdraw.document.validate") operation = "document.validate";
    else if (name == "pikdraw.objects.query") operation = "objects.query";
    else if (name == "pikdraw.objects.get") operation = "objects.get";
    else if (name == "pikdraw.request.get") operation = "request.get";
    else if (name == "pikdraw.history.undo") operation = "history.undo";
    else if (name == "pikdraw.history.redo") operation = "history.redo";
    else if (name == "pikdraw.batch.validate") operation = "batch.validate";
    else if (name == "pikdraw.batch.apply") operation = "batch.apply";
    else if (name == "pikdraw.object.create_rectangle") operation = "object.create_rectangle";
    else if (name == "pikdraw.file.save") operation = "file.save";
    else if (name == "pikdraw.file.open") operation = "file.open";
    else if (name == "pikdraw.file.verify_reopen") operation = "file.verify_reopen";
    else if (name == "pikdraw.disconnect") operation = "session.disconnect";
    else return JsonValue::Object{{"status", "error"}, {"code", "UNSUPPORTED_CAPABILITY"},
        {"message", "No handler is registered for this tool."}};

    JsonValue::Object request = *arguments.object();
    request["operation"] = std::move(operation);
    JsonValue response; std::string pipe_error;
    if (!call_app(endpoint_it->second, JsonValue(std::move(request)), response, pipe_error))
        return JsonValue::Object{{"status", "error"}, {"code", "PIPE_FAILURE"}, {"message", pipe_error}};
    const auto* status = string_field(response, "status");
    if (name == "pikdraw.disconnect" && status && *status == "ok") sessions_.erase(endpoint_it);
    return response;
}

JsonValue McpServer::tool_result(const JsonValue& result, bool modern) const {
    const auto encoded = result.serialize();
    const auto* status = string_field(result, "status");
    JsonValue::Object content{{"type", "text"}, {"text", encoded}};
    JsonValue::Object wrapped{{"content", JsonValue::Array{JsonValue(std::move(content))}},
        {"structuredContent", result}, {"isError", status && *status == "error"}};
    if (modern) return add_modern_result_fields(JsonValue(std::move(wrapped)));
    return wrapped;
}

JsonValue McpServer::handle(const JsonValue& request) {
    const auto* method = string_field(request, "method");
    if (!method || !request.object()) return protocol_error(-32600, "Invalid Request", JsonValue());
    const auto* id = field(request, "id");
    const bool notification = id == nullptr;
    const JsonValue response_id = notification ? JsonValue() : *id;
    const auto params = object_or_empty(field(request, "params"));

    if (*method == "notifications/initialized" || method->starts_with("notifications/"))
        return {};
    if (*method == "initialize") {
        if (mode_ == ProtocolMode::Modern)
            return protocol_error(-32600, "Cannot initialize after using the modern MCP protocol.", response_id);
        mode_ = ProtocolMode::Legacy;
        if (notification) return {};
        return JsonValue::Object{{"jsonrpc", "2.0"}, {"id", response_id},
            {"result", JsonValue::Object{{"protocolVersion", std::string(kLegacyVersion)},
                {"capabilities", JsonValue::Object{{"tools", JsonValue::Object{}}}},
                {"serverInfo", JsonValue::Object{{"name", "pikdraw-mcp-server"}, {"version", "0.1.0"}}},
                {"instructions", "Connect to a listed local PikDraw instance, inspect the document, and use only advertised automation capabilities."}}}};
    }

    bool modern = mode_ == ProtocolMode::Modern;
    if (mode_ == ProtocolMode::Undecided) {
        const auto* meta = field(params, "_meta");
        const auto* version = meta ? string_field(*meta, "io.modelcontextprotocol/protocolVersion") : nullptr;
        const auto* client_caps = meta ? field(*meta, "io.modelcontextprotocol/clientCapabilities") : nullptr;
        if (version && *version == kModernVersion && client_caps && client_caps->object()) {
            mode_ = ProtocolMode::Modern; modern = true;
        } else {
            return protocol_error(-32022, "Unsupported protocol version. Use server/discover with 2026-07-28 metadata.",
                response_id, JsonValue::Object{{"supportedVersions", JsonValue::Array{JsonValue(std::string(kModernVersion))}}});
        }
    } else if (modern) {
        const auto* meta = field(params, "_meta");
        const auto* version = meta ? string_field(*meta, "io.modelcontextprotocol/protocolVersion") : nullptr;
        const auto* client_caps = meta ? field(*meta, "io.modelcontextprotocol/clientCapabilities") : nullptr;
        if (!version || !client_caps || !client_caps->object())
            return protocol_error(-32602, "Modern MCP requests require supported protocolVersion and clientCapabilities metadata.", response_id);
        if (*version != kModernVersion)
            return protocol_error(-32022, "Unsupported protocol version.", response_id,
                JsonValue::Object{{"supportedVersions", JsonValue::Array{JsonValue(std::string(kModernVersion))}}});
    }
    if (notification) return {};

    if (*method == "server/discover") {
        if (!modern) return protocol_error(-32601, "Method not found", response_id);
        JsonValue result = JsonValue::Object{{"supportedVersions", JsonValue::Array{JsonValue(std::string(kModernVersion))}},
            {"capabilities", JsonValue::Object{{"tools", JsonValue::Object{{"listChanged", false}}}}},
            {"instructions", "Use pikdraw.instances, connect to the intended desktop, read its capabilities, then inspect and edit through the document API. Sessions are explicit handles and expire when the document epoch changes."},
            {"ttlMs", 1000}, {"cacheScope", "private"}};
        return JsonValue::Object{{"jsonrpc", "2.0"}, {"id", response_id}, {"result", add_modern_result_fields(std::move(result))}};
    }
    if (*method == "ping") {
        JsonValue result(JsonValue::Object{});
        if (modern) result = add_modern_result_fields(std::move(result));
        return JsonValue::Object{{"jsonrpc", "2.0"}, {"id", response_id}, {"result", std::move(result)}};
    }
    if (*method == "tools/list") {
        JsonValue result = JsonValue::Object{{"tools", tools()}};
        if (modern) {
            auto* object = const_cast<JsonValue::Object*>(result.object());
            (*object)["ttlMs"] = 1000; (*object)["cacheScope"] = "private";
            result = add_modern_result_fields(std::move(result));
        }
        return JsonValue::Object{{"jsonrpc", "2.0"}, {"id", response_id}, {"result", std::move(result)}};
    }
    if (*method == "tools/call") {
        const auto* name = string_field(params, "name");
        const auto* arguments = field(params, "arguments");
        if (!name || !arguments || !arguments->object())
            return protocol_error(-32602, "tools/call requires name and object arguments.", response_id);
        bool known = false;
        const auto advertised_tools = tools();
        for (const auto& tool : *advertised_tools.array()) {
            const auto* tool_name = string_field(tool, "name");
            if (tool_name && *tool_name == *name) { known = true; break; }
        }
        if (!known) return protocol_error(-32602, "Unknown tool: " + *name, response_id);
        const auto result = tool_result(dispatch_tool(*name, *arguments), modern);
        return JsonValue::Object{{"jsonrpc", "2.0"}, {"id", response_id}, {"result", result}};
    }
    return protocol_error(-32601, "Method not found", response_id);
}

} // namespace pikdraw::mcp
