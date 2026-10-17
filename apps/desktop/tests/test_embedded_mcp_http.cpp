#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include "embedded_mcp_http_host.hpp"
#include "automation_pipe_host.hpp"
#include "pikdraw/automation/json_value.hpp"

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <future>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <chrono>
#include <sstream>
#endif

namespace {

void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

using JsonValue = pikdraw::automation::JsonValue;

JsonValue parse(const std::string& text) {
    JsonValue value;
    std::string error;
    check(JsonValue::parse(text, value, error), "JSON response parses");
    return value;
}

#ifdef _WIN32

struct HttpResponse {
    int status{0};
    std::map<std::string, std::string, std::less<>> headers;
    std::string body;
};

std::string lower(std::string value) {
    for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool send_bytes(SOCKET socket, const std::string& value) {
    size_t offset = 0;
    while (offset < value.size()) {
        const int sent = send(socket, value.data() + offset,
            static_cast<int>(value.size() - offset), 0);
        if (sent <= 0) return false;
        offset += static_cast<size_t>(sent);
    }
    return true;
}

HttpResponse request(std::string_view endpoint, std::string_view token, std::string_view method,
                     std::string_view body = {}, std::string_view session = {},
                     std::string_view protocol = {}, std::string_view origin = {},
                     std::string_view accept = "application/json, text/event-stream",
                     std::string_view mcp_method = {}, std::string_view mcp_name = {},
                     std::string_view host_override = {}) {
    const auto scheme = endpoint.find("://");
    const auto path_at = endpoint.find('/', scheme == std::string_view::npos ? 0 : scheme + 3);
    check(scheme != std::string_view::npos && path_at != std::string_view::npos,
          "configured endpoint has a URL and path");
    const std::string authority(endpoint.substr(scheme + 3, path_at - scheme - 3));
    const auto colon = authority.rfind(':');
    check(colon != std::string::npos, "loopback endpoint has a port");
    const auto port = static_cast<unsigned short>(std::stoi(authority.substr(colon + 1)));

    SOCKET socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_handle == INVALID_SOCKET) return {};
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (connect(socket_handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        closesocket(socket_handle);
        return {};
    }

    std::string encoded(method);
    encoded += " /mcp HTTP/1.1\r\nHost: " +
        std::string(host_override.empty() ? std::string_view(authority) : host_override) + "\r\n";
    encoded += "Accept: " + std::string(accept) + "\r\n";
    encoded += "Content-Type: application/json\r\n";
    encoded += "Authorization: Bearer " + std::string(token) + "\r\n";
    if (!session.empty()) encoded += "MCP-Session-Id: " + std::string(session) + "\r\n";
    if (!protocol.empty()) encoded += "MCP-Protocol-Version: " + std::string(protocol) + "\r\n";
    if (!mcp_method.empty()) encoded += "Mcp-Method: " + std::string(mcp_method) + "\r\n";
    if (!mcp_name.empty()) encoded += "Mcp-Name: " + std::string(mcp_name) + "\r\n";
    if (!origin.empty()) encoded += "Origin: " + std::string(origin) + "\r\n";
    if (method == "POST") encoded += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    encoded += "Connection: close\r\n\r\n";
    encoded.append(body);
    if (!send_bytes(socket_handle, encoded)) { closesocket(socket_handle); return {}; }

    std::string received;
    char chunk[4096];
    for (;;) {
        const int count = recv(socket_handle, chunk, sizeof(chunk), 0);
        if (count <= 0) break;
        received.append(chunk, static_cast<size_t>(count));
    }
    closesocket(socket_handle);
    HttpResponse response;
    const auto header_end = received.find("\r\n\r\n");
    if (header_end == std::string::npos) return response;
    const auto first_end = received.find("\r\n");
    if (first_end == std::string::npos) return response;
    const auto first = received.substr(0, first_end);
    const auto space = first.find(' ');
    if (space == std::string::npos) return response;
    response.status = std::stoi(first.substr(space + 1, 3));
    size_t cursor = first_end + 2;
    while (cursor < header_end) {
        const auto line_end = received.find("\r\n", cursor);
        if (line_end == std::string::npos || line_end > header_end) break;
        const auto line = received.substr(cursor, line_end - cursor);
        const auto split = line.find(':');
        if (split != std::string::npos)
            response.headers[lower(line.substr(0, split))] = line.substr(split + 1 + (line[split + 1] == ' ' ? 1 : 0));
        cursor = line_end + 2;
    }
    response.body = received.substr(header_end + 4);
    return response;
}

template <typename Future>
auto pump_until_ready(Future& future) {
    while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return future.get();
}

JsonValue call_tool_body(int id, std::string_view name, JsonValue::Object arguments,
                         bool modern = false) {
    JsonValue::Object params{{"name", std::string(name)}, {"arguments", std::move(arguments)}};
    if (modern) params["_meta"] = JsonValue::Object{
        {"io.modelcontextprotocol/protocolVersion", "2026-07-28"},
        {"io.modelcontextprotocol/clientCapabilities", JsonValue::Object{}},
        {"io.modelcontextprotocol/clientInfo", JsonValue::Object{{"name", "pikdraw-http-test"}, {"version", "1"}}}};
    return JsonValue::Object{{"jsonrpc", "2.0"}, {"id", id}, {"method", "tools/call"},
        {"params", std::move(params)}};
}

#endif

} // namespace

int main() {
#ifndef _WIN32
    check(true, "embedded HTTP endpoint is a Windows desktop capability");
    return 0;
#else
    WSADATA client_winsock{};
    check(WSAStartup(MAKEWORD(2, 2), &client_winsock) == 0, "test client initializes Winsock");

    const wchar_t* class_name = L"PikDrawEmbeddedMcpHttpTestWindow";
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = class_name;
    check(RegisterClassW(&window_class) != 0, "test registers a hidden UI-owner window");
    HWND owner_window = CreateWindowExW(0, class_name, L"PikDraw MCP integration test", 0,
        0, 0, 32, 32, nullptr, nullptr, window_class.hInstance, nullptr);
    check(owner_window != nullptr, "test creates a hidden UI-owner window");
    const DWORD owner_thread = GetCurrentThreadId();

    std::atomic<DWORD> handler_thread{0};
    std::atomic<unsigned> handler_calls{0};
    std::atomic<unsigned> app_call_count{0};
    std::atomic<bool> disconnected{false};
    pikdraw::desktop::AutomationPipeHost pipe;
    std::string error;
    check(pipe.start(owner_window, [&](const JsonValue& input) {
        handler_thread.store(GetCurrentThreadId(), std::memory_order_release);
        handler_calls.fetch_add(1, std::memory_order_relaxed);
        const auto* operation_value = input.get("operation");
        const auto* operation = operation_value ? operation_value->string() : nullptr;
        if (!operation) return JsonValue::Object{{"status", "error"}, {"code", "INVALID_ARGUMENT"}};
        if (*operation == "session.create")
            return JsonValue::Object{{"status", "ok"}, {"session_handle", "ui-session-1"},
                                     {"document_id", "ui-document-1"}};
        if (*operation == "document.create") {
            const auto* width = input.get("page_width_mm");
            const auto* height = input.get("page_height_mm");
            return JsonValue::Object{{"status", "ok"}, {"document_id", "created-document"},
                {"page_width_mm", width ? *width : JsonValue(0)},
                {"page_height_mm", height ? *height : JsonValue(0)}};
        }
        if (*operation == "session.disconnect") {
            disconnected.store(true, std::memory_order_release);
            return JsonValue::Object{{"status", "ok"}};
        }
        return JsonValue::Object{{"status", "ok"}, {"operation", *operation}};
    }, "read,create,edit,file-read,file-write", error), "pipe transport starts for UI queue fixture");

    pikdraw::desktop::EmbeddedMcpHttpHost host;
    const auto instance_provider = [] {
        JsonValue::Array scopes{JsonValue("document.read"), JsonValue("document.create"),
            JsonValue("document.edit"), JsonValue("file.read"), JsonValue("file.write")};
        JsonValue::Array instances{JsonValue(JsonValue::Object{
            {"instance_id", "test-instance"}, {"granted_scopes", std::move(scopes)}})};
        return JsonValue::Object{{"status", "ok"}, {"instances", std::move(instances)}};
    };
    const auto app_caller = [&pipe, &app_call_count](std::string_view instance_id, const JsonValue& input,
        JsonValue& response, std::string& call_error, const std::atomic<bool>* cancellation) {
        if (instance_id != "test-instance") { call_error = "wrong fixture instance"; return false; }
        app_call_count.fetch_add(1, std::memory_order_release);
        return pipe.invoke(input, response, call_error, cancellation);
    };
    check(host.start(instance_provider, app_caller, error), "embedded MCP server starts inside app owner process");
    const auto endpoint = host.endpoint();
    const auto token = host.access_token();
    check(host.active() && endpoint.starts_with("http://127.0.0.1:") && endpoint.ends_with("/mcp"),
          "server exposes only a loopback Streamable HTTP endpoint");
    check(token.size() == 64, "server creates a per-start cryptographically random bearer token");

    const JsonValue modern_meta(JsonValue::Object{
        {"io.modelcontextprotocol/protocolVersion", "2026-07-28"},
        {"io.modelcontextprotocol/clientCapabilities", JsonValue::Object{}},
        {"io.modelcontextprotocol/clientInfo", JsonValue::Object{
            {"name", "pikdraw-http-test"}, {"version", "1"}}}});
    const JsonValue modern_discover(JsonValue::Object{{"jsonrpc", "2.0"}, {"id", 20},
        {"method", "server/discover"}, {"params", JsonValue::Object{{"_meta", modern_meta}}}});
    auto modern_without_route_header = request(endpoint, token, "POST", modern_discover.serialize(),
        {}, "2026-07-28");
    check(modern_without_route_header.status == 400,
          "stateless MCP requests require the routing method header");
    auto modern_mismatched_route = request(endpoint, token, "POST", modern_discover.serialize(),
        {}, "2026-07-28", {}, "application/json, text/event-stream", "tools/list");
    check(modern_mismatched_route.status == 400,
          "stateless MCP rejects a routing method that disagrees with JSON-RPC");
    const JsonValue modern_missing_meta(JsonValue::Object{{"jsonrpc", "2.0"}, {"id", 20},
        {"method", "server/discover"}, {"params", JsonValue::Object{}}});
    auto missing_modern_meta = request(endpoint, token, "POST", modern_missing_meta.serialize(),
        {}, "2026-07-28", {}, "application/json, text/event-stream", "server/discover");
    check(missing_modern_meta.status == 400,
          "stateless MCP requires matching body protocol version and client capabilities");
    auto modern_discovered = request(endpoint, token, "POST", modern_discover.serialize(),
        {}, "2026-07-28", {}, "application/json, text/event-stream", "server/discover");
    check(modern_discovered.status == 200 &&
          modern_discovered.headers.find("mcp-session-id") == modern_discovered.headers.end(),
          "MCP 2026 stateless discovery succeeds without creating a transport session");
    const auto modern_discovery_json = parse(modern_discovered.body);
    check(modern_discovery_json.get("result") && modern_discovery_json.get("result")->get("supportedVersions"),
          "stateless discovery returns the modern capability result");
    const JsonValue modern_tools(JsonValue::Object{{"jsonrpc", "2.0"}, {"id", 21},
        {"method", "tools/list"}, {"params", JsonValue::Object{{"_meta", modern_meta}}}});
    auto modern_tool_list = request(endpoint, token, "POST", modern_tools.serialize(),
        {}, "2026-07-28", {}, "application/json, text/event-stream", "tools/list");
    check(modern_tool_list.status == 200, "stateless MCP tools/list uses modern protocol metadata");
    const auto modern_tool_json = parse(modern_tool_list.body);
    const auto* modern_tools_result = modern_tool_json.get("result");
    const auto* modern_tool_array = modern_tools_result ? modern_tools_result->get("tools") : nullptr;
    check(modern_tool_array && modern_tool_array->array() && modern_tool_array->array()->size() == 20,
          "modern MCP advertises the same 20 semantic authoring and verification tools");

    const auto modern_connect_body = call_tool_body(22, "pikdraw.connect", JsonValue::Object{
        {"instance_id", "test-instance"}, {"scopes", JsonValue::Array{
            JsonValue("document.read"), JsonValue("document.create")}}}, true).serialize();
    auto modern_mismatched_name = request(endpoint, token, "POST", modern_connect_body, {},
        "2026-07-28", {}, "application/json, text/event-stream", "tools/call", "pikdraw.other");
    check(modern_mismatched_name.status == 400,
          "stateless MCP rejects a tool routing name that disagrees with JSON-RPC");
    auto modern_connect_future = std::async(std::launch::async, [&] {
        return request(endpoint, token, "POST", modern_connect_body, {}, "2026-07-28", {},
            "application/json, text/event-stream", "tools/call", "pikdraw.connect");
    });
    const auto modern_connected = pump_until_ready(modern_connect_future);
    check(modern_connected.status == 200,
          "stateless tools/call reaches the in-process UI-owner dispatcher");
    const auto modern_connected_json = parse(modern_connected.body);
    const auto* modern_connected_content = modern_connected_json.get("result") &&
        modern_connected_json.get("result")->get("structuredContent")
        ? modern_connected_json.get("result")->get("structuredContent") : nullptr;
    const auto* modern_handle_value = modern_connected_content
        ? modern_connected_content->get("session_handle") : nullptr;
    const auto* modern_handle = modern_handle_value ? modern_handle_value->string() : nullptr;
    check(modern_handle && *modern_handle == "ui-session-1",
          "modern MCP carries document state through an explicit app session handle");
    const auto modern_create_body = call_tool_body(23, "pikdraw.document.create", JsonValue::Object{
        {"session_handle", "ui-session-1"}, {"title", "Modern embedded MCP design"},
        {"page_width_mm", 420.0}, {"page_height_mm", 297.0}}, true).serialize();
    auto modern_create_future = std::async(std::launch::async, [&] {
        return request(endpoint, token, "POST", modern_create_body, {}, "2026-07-28", {},
            "application/json, text/event-stream", "tools/call", "pikdraw.document.create");
    });
    const auto modern_created = pump_until_ready(modern_create_future);
    check(modern_created.status == 200,
          "a later stateless request reuses its explicit logical session for scoped document creation");
    const auto modern_created_json = parse(modern_created.body);
    const auto* modern_created_content = modern_created_json.get("result") &&
        modern_created_json.get("result")->get("structuredContent")
        ? modern_created_json.get("result")->get("structuredContent") : nullptr;
    check(modern_created_content && modern_created_content->get("page_width_mm") &&
          *modern_created_content->get("page_width_mm")->number() == 420.0,
          "modern stateless authoring reaches the UI-thread document operation");

    const JsonValue initialize(JsonValue::Object{{"jsonrpc", "2.0"}, {"id", 1},
        {"method", "initialize"}, {"params", JsonValue::Object{{"protocolVersion", "2025-11-25"},
            {"capabilities", JsonValue::Object{}},
            {"clientInfo", JsonValue::Object{{"name", "pikdraw-http-test"}, {"version", "1"}}}}}});
    auto denied = request(endpoint, "wrong-token", "POST", initialize.serialize());
    check(denied.status == 401, "invalid bearer token is rejected");
    auto bad_origin = request(endpoint, token, "POST", initialize.serialize(), {}, {}, "https://attacker.invalid");
    check(bad_origin.status == 403, "non-loopback Origin is rejected before MCP dispatch");
    auto bad_host = request(endpoint, token, "POST", initialize.serialize(), {}, {}, {},
        "application/json, text/event-stream", {}, {}, "attacker.invalid");
    check(bad_host.status == 403, "a non-loopback Host header cannot reach the MCP endpoint");
    auto initialized = request(endpoint, token, "POST", initialize.serialize());
    check(initialized.status == 200, "MCP initialize succeeds over HTTP");
    const auto session_header = initialized.headers.find("mcp-session-id");
    check(session_header != initialized.headers.end() && !session_header->second.empty(),
          "initialize returns an MCP session header");
    const auto session = session_header == initialized.headers.end() ? std::string{} : session_header->second;
    auto initialized_notification = request(endpoint, token, "POST",
        R"({"jsonrpc":"2.0","method":"notifications/initialized","params":{}})",
        session, "2025-11-25");
    check(initialized_notification.status == 202 && initialized_notification.body.empty(),
          "MCP notifications receive HTTP 202 with no body");
    auto get_stream = request(endpoint, token, "GET", {}, session, "2025-11-25", {}, "text/event-stream");
    check(get_stream.status == 405, "the endpoint explicitly declines optional GET SSE");

    const auto connect_body = call_tool_body(2, "pikdraw.connect", JsonValue::Object{
        {"instance_id", "test-instance"}, {"scopes", JsonValue::Array{
            JsonValue("document.read"), JsonValue("document.create")}}}).serialize();
    auto connect_future = std::async(std::launch::async, [&] {
        return request(endpoint, token, "POST", connect_body, session, "2025-11-25");
    });
    const auto connected = pump_until_ready(connect_future);
    check(connected.status == 200, "connect reaches the app automation queue");
    const auto connected_json = parse(connected.body);
    const auto* connected_result = connected_json.get("result");
    const auto* connected_content = connected_result && connected_result->get("structuredContent")
        ? connected_result->get("structuredContent") : nullptr;
    const auto* connected_handle_value = connected_content ? connected_content->get("session_handle") : nullptr;
    const auto* connected_handle = connected_handle_value ? connected_handle_value->string() : nullptr;
    check(connected_handle && *connected_handle == "ui-session-1",
          "MCP connects through the in-process dispatcher");
    check(handler_thread.load(std::memory_order_acquire) == owner_thread,
          "semantic application dispatch runs on the UI-owner thread");

    const auto create_body = call_tool_body(3, "pikdraw.document.create", JsonValue::Object{
        {"session_handle", "ui-session-1"}, {"title", "Embedded MCP design"},
        {"page_width_mm", 600.0}, {"page_height_mm", 300.0}}).serialize();
    auto create_future = std::async(std::launch::async, [&] {
        return request(endpoint, token, "POST", create_body, session, "2025-11-25");
    });
    const auto created = pump_until_ready(create_future);
    check(created.status == 200, "document.create is routed to the UI-owner writer");
    const auto created_json = parse(created.body);
    const auto* created_content = created_json.get("result") && created_json.get("result")->get("structuredContent")
        ? created_json.get("result")->get("structuredContent") : nullptr;
    check(created_content && created_content->get("page_width_mm") &&
          *created_content->get("page_width_mm")->number() == 600.0 &&
          created_content->get("page_height_mm") &&
          *created_content->get("page_height_mm")->number() == 300.0,
          "MCP reaches the app's document-create operation with real millimeter dimensions");

    auto delete_future = std::async(std::launch::async, [&] {
        return request(endpoint, token, "DELETE", {}, session, "2025-11-25");
    });
    const auto deleted = pump_until_ready(delete_future);
    check(deleted.status == 204 && disconnected.load(std::memory_order_acquire),
          "DELETE disconnects app sessions before removing the MCP transport session");
    const auto expired = request(endpoint, token, "POST", create_body, session, "2025-11-25");
    check(expired.status == 404, "deleted MCP session cannot be reused");

    const auto before_revoke_calls = handler_calls.load(std::memory_order_acquire);
    const auto before_revoke_app_calls = app_call_count.load(std::memory_order_acquire);
    const JsonValue second_initialize(JsonValue::Object{{"jsonrpc", "2.0"}, {"id", 10},
        {"method", "initialize"}, {"params", JsonValue::Object{{"protocolVersion", "2025-11-25"},
            {"capabilities", JsonValue::Object{}},
            {"clientInfo", JsonValue::Object{{"name", "revoke-race-test"}, {"version", "1"}}}}}});
    const auto second_init_response = request(endpoint, token, "POST", second_initialize.serialize());
    check(second_init_response.status == 200, "a second HTTP session can initialize before revoke");
    const auto second_session_it = second_init_response.headers.find("mcp-session-id");
    check(second_session_it != second_init_response.headers.end(), "second MCP session receives its own identifier");
    const auto second_session = second_session_it == second_init_response.headers.end()
        ? std::string{} : second_session_it->second;
    const auto racing_body = call_tool_body(4, "pikdraw.connect", JsonValue::Object{
        {"instance_id", "test-instance"}}).serialize();
    auto racing_future = std::async(std::launch::async, [&] {
        return request(endpoint, token, "POST", racing_body, second_session, "2025-11-25");
    });
    for (int i = 0; i < 200 && app_call_count.load(std::memory_order_acquire) == before_revoke_app_calls; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(app_call_count.load(std::memory_order_acquire) == before_revoke_app_calls + 1,
          "the revoke race reached the internal app dispatcher and queued work");
    host.stop();
    check(racing_future.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
          "revocation releases an in-flight HTTP request without blocking the UI thread");
    (void)racing_future.get();
    check(!host.active() && host.access_token().empty(), "revocation stops the listener and erases its access token");
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    check(handler_calls.load(std::memory_order_acquire) == before_revoke_calls,
          "revocation cancels a queued request before it reaches the document writer");
    const auto stopped = request(endpoint, token, "POST", initialize.serialize());
    check(stopped.status == 0, "revoked loopback endpoint no longer accepts connections");

    check(host.start(instance_provider, app_caller, error), "the user can explicitly enable a fresh MCP session");
    const auto renewed_endpoint = host.endpoint();
    const auto renewed_token = host.access_token();
    check(host.active() && renewed_token.size() == 64 && renewed_token != token,
          "re-enabling the endpoint rotates the bearer token");
    const auto stale_key = request(renewed_endpoint, token, "POST", initialize.serialize());
    check(stale_key.status == 401, "the token from before revoke cannot authenticate after re-enable");
    host.stop();

    pipe.stop();
    DestroyWindow(owner_window);
    UnregisterClassW(class_name, window_class.hInstance);
    WSACleanup();
    return 0;
#endif
}
