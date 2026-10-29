#include "pikdraw/mcp_server.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
pikdraw::automation::JsonValue parse(const char* text) {
    pikdraw::automation::JsonValue value; std::string error;
    check(pikdraw::automation::JsonValue::parse(text, value, error), "test request parses");
    return value;
}
}

int main() {
    using pikdraw::automation::JsonValue;
    pikdraw::mcp::McpServer server;
    auto discovered = server.handle(parse(R"({"jsonrpc":"2.0","id":1,"method":"server/discover","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":{}}}})"));
    const auto* result = discovered.get("result");
    check(result && result->get("resultType") && *result->get("resultType")->string() == "complete",
          "modern discovery returns a complete result");
    check(result->get("supportedVersions") && result->get("capabilities"),
          "modern discovery advertises versions and tools capability");

    auto listed = server.handle(parse(R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":{}}}})"));
    const auto* tool_list = listed.get("result")->get("tools");
    check(tool_list && tool_list->array() && tool_list->array()->size() >= 20,
          "MCP tool list advertises the implemented semantic operations");
    bool batch_schema_covers_authoring_and_edits = false;
    bool batch_schema_supports_typed_physical_units = false;
    bool rectangle_schema_supports_typed_physical_units = false;
    bool file_open_schema_is_scoped = false;
    bool document_create_schema_is_scoped = false;
    bool document_create_schema_supports_typed_units = false;
    bool object_query_schema_supports_filters = false;
    bool object_changes_schema_is_guarded = false;
    if (tool_list && tool_list->array()) {
        for (const auto& tool : *tool_list->array()) {
            check(tool.get("inputSchema") && tool.get("inputSchema")->object(),
                  "every advertised MCP tool has a valid JSON input schema");
            const auto* name = tool.get("name");
            if (name && name->string() && *name->string() == "pikdraw.objects.query") {
                const auto encoded = tool.get("inputSchema")->serialize();
                object_query_schema_supports_filters =
                    encoded.find("object_id") != std::string::npos &&
                    encoded.find("layer_index") != std::string::npos &&
                    encoded.find("region_mm") != std::string::npos &&
                    encoded.find("path") != std::string::npos &&
                    encoded.find("v2:") != std::string::npos;
            }
            if (name && name->string() && *name->string() == "pikdraw.objects.changes") {
                const auto* required = tool.get("inputSchema")->get("required");
                const auto encoded = tool.get("inputSchema")->serialize();
                object_changes_schema_is_guarded = required && required->array() &&
                    required->array()->size() == 3 &&
                    encoded.find("expected_epoch") != std::string::npos &&
                    encoded.find("from_revision") != std::string::npos;
            }
            if (name && name->string() && *name->string() == "pikdraw.batch.apply") {
                const auto encoded = tool.get("inputSchema")->serialize();
                batch_schema_covers_authoring_and_edits =
                    encoded.find("object.create_polyline") != std::string::npos &&
                    encoded.find("object.transform") != std::string::npos &&
                    encoded.find("object.duplicate") != std::string::npos &&
                    encoded.find("object.delete") != std::string::npos &&
                    encoded.find("object.style") != std::string::npos &&
                    encoded.find("object.group") != std::string::npos &&
                    encoded.find("object.ungroup") != std::string::npos &&
                    encoded.find("object.align") != std::string::npos &&
                    encoded.find("object.distribute") != std::string::npos;
                batch_schema_supports_typed_physical_units =
                    encoded.find("length_unit") != std::string::npos &&
                    encoded.find("points") != std::string::npos &&
                    encoded.find("translate_x") != std::string::npos &&
                    encoded.find("offset_x") != std::string::npos &&
                    encoded.find("stroke_width") != std::string::npos &&
                    encoded.find("yd") != std::string::npos;
            }
            if (name && name->string() && *name->string() == "pikdraw.object.create_rectangle") {
                const auto encoded = tool.get("inputSchema")->serialize();
                rectangle_schema_supports_typed_physical_units =
                    encoded.find("length_unit") != std::string::npos &&
                    encoded.find("oneOf") != std::string::npos &&
                    encoded.find("pc") != std::string::npos;
            }
            if (name && name->string() && (*name->string() == "pikdraw.objects.get" ||
                *name->string() == "pikdraw.history.undo" || *name->string() == "pikdraw.history.redo"))
                check(tool.get("inputSchema") && tool.get("inputSchema")->get("required"),
                      "object lookup and history tools advertise a required guarded input schema");
            if (name && name->string() && *name->string() == "pikdraw.file.open") {
                const auto* required = tool.get("inputSchema")->get("required");
                const auto* session = required && required->array() && required->array()->size() == 2
                    ? required->array()->at(0).string() : nullptr;
                const auto* file_name = required && required->array() && required->array()->size() == 2
                    ? required->array()->at(1).string() : nullptr;
                file_open_schema_is_scoped = session && file_name &&
                    *session == "session_handle" && *file_name == "file_name";
            }
            if (name && name->string() && *name->string() == "pikdraw.document.create") {
                const auto* required = tool.get("inputSchema")->get("required");
                document_create_schema_is_scoped = required && required->array() &&
                    !required->array()->empty() && required->array()->front().string() &&
                    *required->array()->front().string() == "session_handle";
                const auto encoded = tool.get("inputSchema")->serialize();
                document_create_schema_supports_typed_units =
                    encoded.find("page_width_mm") != std::string::npos &&
                    encoded.find("page_height_mm") != std::string::npos &&
                    encoded.find("page_width") != std::string::npos &&
                    encoded.find("page_height") != std::string::npos &&
                    encoded.find("length_unit") != std::string::npos &&
                    encoded.find("oneOf") != std::string::npos &&
                    encoded.find("pc") != std::string::npos;
            }
        }
    }
    check(batch_schema_covers_authoring_and_edits,
          "MCP batch schema advertises only implemented line/polyline and stable-ID edits");
    check(batch_schema_supports_typed_physical_units && rectangle_schema_supports_typed_physical_units,
          "MCP create schemas expose explicit physical-unit inputs alongside legacy millimeter fields");
    check(file_open_schema_is_scoped,
          "native file.open requires a connected session and a granted-folder basename");
    check(document_create_schema_is_scoped,
          "document.create requires a session handle so the app can enforce its requested scope");
    check(document_create_schema_supports_typed_units,
          "document.create retains canonical mm fields and adds an exclusive typed physical-unit form");
    check(object_query_schema_supports_filters,
          "objects.query schema exposes stable-ID, layer, type and region filters with versioned cursors");
    check(object_changes_schema_is_guarded,
          "objects.changes schema requires session, epoch and source revision");

    auto create_without_session = server.handle(parse(R"({"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"pikdraw.document.create","arguments":{"instance_id":"123","page_width_mm":600,"page_height_mm":300},"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":{}}}})"));
    const auto* create_result = create_without_session.get("result");
    const auto* create_content = create_result && create_result->get("content") &&
        create_result->get("content")->array() && !create_result->get("content")->array()->empty()
        ? &create_result->get("content")->array()->front() : nullptr;
    check(create_result && create_result->get("isError") && *create_result->get("isError")->boolean() &&
              create_content && create_content->get("text") && create_content->get("text")->string() &&
              create_content->get("text")->string()->find("SESSION_REQUIRED") != std::string::npos,
          "document.create without a live session is denied before contacting the desktop");

    auto unsupported = server.handle(parse(R"({"jsonrpc":"2.0","id":3,"method":"tools/list","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2099-01-01","io.modelcontextprotocol/clientCapabilities":{}}}})"));
    check(unsupported.get("error") && *unsupported.get("error")->get("code")->number() == -32022,
          "unsupported modern protocol versions are rejected");

    auto call = server.handle(parse(R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"pikdraw.instances","arguments":{},"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":{}}}})"));
    const auto* tool_result = call.get("result");
    if (!(tool_result && tool_result->get("isError") && !*tool_result->get("isError")->boolean()))
        std::cerr << "Tool response: " << call.serialize() << '\n';
    check(tool_result && tool_result->get("isError") && !*tool_result->get("isError")->boolean(),
          "local instance enumeration is a valid MCP tool result");

    auto notification = server.handle(parse(R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":5,"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":{}}}})"));
    check(notification.is_null(), "notifications do not produce JSON-RPC responses");

    pikdraw::mcp::McpServer legacy;
    auto initialized = legacy.handle(parse(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})"));
    check(initialized.get("result") && initialized.get("result")->get("protocolVersion") &&
              *initialized.get("result")->get("protocolVersion")->string() == "2025-11-25",
          "legacy clients negotiate the latest supported handshake version");
    auto legacy_list = legacy.handle(parse(R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})"));
    check(legacy_list.get("result") && !legacy_list.get("result")->get("resultType") &&
              legacy_list.get("result")->get("tools"),
          "legacy tool listing omits modern result envelope fields");
    return 0;
}
