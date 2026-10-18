#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include "native_window_automation.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include "pikdraw/automation/json_value.hpp"
#include "pikdraw/desktop/shell/native_window.hpp"
#include "../src/clipboard/native_clipboard.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <future>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace pikdraw::desktop {

struct NativeWindowMcpE2eAccess {
    static std::string endpoint(const NativeWindow& window) {
        return window.mcp_runtime_ ? window.mcp_runtime_->http.endpoint() : std::string{};
    }
    static std::string access_key(const NativeWindow& window) {
        return window.mcp_runtime_ ? window.mcp_runtime_->http.access_token() : std::string{};
    }
};

} // namespace pikdraw::desktop

namespace {
using pikdraw::automation::JsonValue;

void require(bool condition, std::string message) {
    if (!condition) throw std::runtime_error(std::move(message));
}

JsonValue parse_json(std::string_view encoded) {
    JsonValue value;
    std::string error;
    require(JsonValue::parse(encoded, value, error), "HTTP response is valid JSON: " + error);
    return value;
}

const std::string& string_value(const JsonValue& value, std::string_view field) {
    const auto* item = value.get(field);
    require(item && item->string(), "response has string field '" + std::string(field) + "'");
    return *item->string();
}

void require_ok(const JsonValue& value, std::string_view operation) {
    const auto* status = value.get("status");
    if (status && status->string() && *status->string() == "ok") return;
    const auto* message = value.get("message");
    throw std::runtime_error(std::string(operation) + " failed: " +
        (message && message->string() ? *message->string() : value.serialize()));
}

#ifdef _WIN32

struct HttpResponse {
    int status{0};
    std::string body;
};

bool send_all(SOCKET socket_handle, std::string_view bytes) {
    size_t sent_total = 0;
    while (sent_total < bytes.size()) {
        const int sent = send(socket_handle, bytes.data() + sent_total,
            static_cast<int>(bytes.size() - sent_total), 0);
        if (sent <= 0) return false;
        sent_total += static_cast<size_t>(sent);
    }
    return true;
}

HttpResponse post_json(std::string_view endpoint, std::string_view access_key,
                       std::string_view body, std::string_view method,
                       std::string_view name) {
    const auto scheme = endpoint.find("://");
    const auto path_at = endpoint.find('/', scheme == std::string_view::npos ? 0 : scheme + 3);
    require(scheme != std::string_view::npos && path_at != std::string_view::npos,
        "Settings endpoint includes an HTTP path");
    const std::string authority(endpoint.substr(scheme + 3, path_at - scheme - 3));
    const auto colon = authority.rfind(':');
    require(colon != std::string::npos, "Settings endpoint includes a TCP port");
    const auto port = static_cast<unsigned short>(std::stoi(authority.substr(colon + 1)));

    SOCKET socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_handle == INVALID_SOCKET)
        return {-1, "socket() failed with Winsock error " + std::to_string(WSAGetLastError())};
    const DWORD timeout_ms = 5000;
    setsockopt(socket_handle, SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
    setsockopt(socket_handle, SOL_SOCKET, SO_SNDTIMEO,
        reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (connect(socket_handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        const int error = WSAGetLastError();
        closesocket(socket_handle);
        return {-2, "connect() failed with Winsock error " + std::to_string(error)};
    }

    std::string request = "POST " + std::string(endpoint.substr(path_at)) + " HTTP/1.1\r\nHost: " + authority +
        "\r\nAccept: application/json, text/event-stream\r\nContent-Type: application/json\r\n"+
        "Authorization: Bearer " + std::string(access_key) +
        "\r\nMCP-Protocol-Version: 2026-07-28\r\nMcp-Method: " + std::string(method) +
        (name.empty() ? std::string{} : "\r\nMcp-Name: " + std::string(name)) +
        "\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\nConnection: close\r\n\r\n" + std::string(body);
    if (!send_all(socket_handle, request)) {
        const int error = WSAGetLastError();
        closesocket(socket_handle);
        return {-3, "send() failed with Winsock error " + std::to_string(error)};
    }

    std::string received;
    char buffer[4096];
    int recv_error = 0;
    for (;;) {
        const int count = recv(socket_handle, buffer, sizeof(buffer), 0);
        if (count <= 0) {
            if (count == SOCKET_ERROR) recv_error = WSAGetLastError();
            break;
        }
        received.append(buffer, static_cast<size_t>(count));
    }
    closesocket(socket_handle);

    const auto header_end = received.find("\r\n\r\n");
    const auto first_end = received.find("\r\n");
    if (header_end == std::string::npos || first_end == std::string::npos)
        return {-4, received.empty() ? "server closed without an HTTP response; recv error " +
            std::to_string(recv_error) : received};
    const auto first = received.substr(0, first_end);
    const auto space = first.find(' ');
    if (space == std::string::npos) return {};
    HttpResponse response;
    response.status = std::stoi(first.substr(space + 1, 3));
    response.body = received.substr(header_end + 4);
    return response;
}

template <typename Future>
auto pump_until_ready(Future& future) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        require(std::chrono::steady_clock::now() < deadline,
            "MCP request completed before its integration-test deadline");
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return future.get();
}

class ClipboardTextSnapshot final {
public:
    static bool is_safe_format(UINT format) {
        if (format == CF_UNICODETEXT || format == CF_TEXT || format == CF_OEMTEXT || format == CF_LOCALE)
            return true;
        wchar_t name[256] = {};
        if (GetClipboardFormatNameW(format, name, 256) > 0) {
            std::wstring_view sv(name);
            if (sv == L"DataObject" || sv == L"Ole Private Data" || sv == L"System.String" ||
                sv == L"CanIncludeInClipboardHistory" || sv == L"CanUploadToCloudClipboard" ||
                sv == L"PikDraw.DocumentSnapshot.v1" || sv == L"image/svg+xml")
                return true;
        }
        return false;
    }

    explicit ClipboardTextSnapshot(HWND owner) : owner_(owner) {
        if (!OpenClipboard(owner_)) return;
        UINT format = 0;
        bool safe = true;
        while ((format = EnumClipboardFormats(format)) != 0) {
            if (!is_safe_format(format)) {
                safe = false;
                break;
            }
        }
        if (safe && IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            HANDLE data = GetClipboardData(CF_UNICODETEXT);
            const SIZE_T size = data ? GlobalSize(data) : 0;
            const auto* bytes = data && size ? static_cast<const std::uint8_t*>(GlobalLock(data)) : nullptr;
            if (!bytes) {
                safe = false;
            } else {
                original_.assign(bytes, bytes + size);
                had_text_ = true;
                GlobalUnlock(data);
            }
        }
        safe_to_restore_ = safe;
        CloseClipboard();
    }

    ClipboardTextSnapshot(const ClipboardTextSnapshot&) = delete;
    ClipboardTextSnapshot& operator=(const ClipboardTextSnapshot&) = delete;

    ~ClipboardTextSnapshot() { restore(); }

    [[nodiscard]] bool safe() const noexcept { return safe_to_restore_; }

    void note_app_write() noexcept { changed_ = true; }

    void restore() noexcept {
        if (!safe_to_restore_ || !changed_ || !owner_) return;
        // If another app has taken ownership since the test copy, leave its newer clipboard alone.
        if (GetClipboardOwner() != owner_ || !OpenClipboard(owner_)) return;
        if (EmptyClipboard() && had_text_ && !original_.empty()) {
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, original_.size());
            if (memory) {
                void* destination = GlobalLock(memory);
                if (destination) {
                    std::memcpy(destination, original_.data(), original_.size());
                    GlobalUnlock(memory);
                    if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
                } else {
                    GlobalFree(memory);
                }
            }
        }
        CloseClipboard();
        changed_ = false;
    }

private:
    HWND owner_{nullptr};
    bool safe_to_restore_{false};
    bool had_text_{false};
    bool changed_{false};
    std::vector<std::uint8_t> original_;
};

JsonValue modern_meta() {
    return JsonValue::Object{
        {"io.modelcontextprotocol/protocolVersion", "2026-07-28"},
        {"io.modelcontextprotocol/clientCapabilities", JsonValue::Object{}},
        {"io.modelcontextprotocol/clientInfo", JsonValue::Object{
            {"name", "pikdraw-in-process-e2e"}, {"version", "1"}}}};
}

JsonValue call_tool(pikdraw::desktop::NativeWindow& window, std::string_view endpoint,
                    std::string_view access_key, int id, std::string_view name,
                    JsonValue::Object arguments) {
    JsonValue body(JsonValue::Object{{"jsonrpc", "2.0"}, {"id", id}, {"method", "tools/call"},
        {"params", JsonValue::Object{{"name", std::string(name)},
            {"arguments", std::move(arguments)}, {"_meta", modern_meta()}}}});
    auto future = std::async(std::launch::async, [=, body = body.serialize()] {
        return post_json(endpoint, access_key, body, "tools/call", name);
    });
    const auto response = pump_until_ready(future);
    require(response.status == 200, std::string(name) + " returned HTTP " + std::to_string(response.status));
    const auto envelope = parse_json(response.body);
    const auto* result = envelope.get("result");
    const auto* content = result ? result->get("structuredContent") : nullptr;
    require(content && content->object(), std::string(name) + " returned structuredContent");
    (void)window;
    return *content;
}

std::filesystem::path granted_file_path(std::string_view name) {
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    require(size > 0, "LOCALAPPDATA is available for E2E cleanup");
    std::wstring local_app_data(size, L'\0');
    const DWORD copied = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data.data(), size);
    require(copied > 0 && copied < size, "LOCALAPPDATA is readable for E2E cleanup");
    local_app_data.resize(copied);
    return std::filesystem::path(local_app_data) / L"PikDraw" / L"automation" / L"files" /
        (pikdraw::desktop::NativeWindow::utf8_to_wide(std::string(name)) + L".pikdraw");
}

void verify_settings_copy_controls(pikdraw::desktop::NativeWindow& window,
                                   ClipboardTextSnapshot& clipboard,
                                   std::string_view endpoint, std::string_view access_key) {
    if (!clipboard.safe()) {
        std::cout << "SKIP: credential-copy UI check because the existing clipboard contains non-text formats.\n";
        return;
    }
    window.open_ai_connections_settings();
    SendMessageW(window.hwnd(), WM_KEYDOWN, VK_TAB, 0);
    SendMessageW(window.hwnd(), WM_KEYDOWN, VK_TAB, 0);
    require(window.ai_connections_focus_index() == 2, "Tab reaches Copy URL after access choices");
    SendMessageW(window.hwnd(), WM_KEYDOWN, VK_RETURN, 0);
    clipboard.note_app_write();
    const auto copied_endpoint = pikdraw::desktop::NativeClipboard::read_text();
    require(copied_endpoint && *copied_endpoint == endpoint,
        "Enter copies the live MCP endpoint from Settings");

    SendMessageW(window.hwnd(), WM_KEYDOWN, VK_TAB, 0);
    require(window.ai_connections_focus_index() == 3, "Tab reaches Copy key after Copy URL");
    SendMessageW(window.hwnd(), WM_KEYDOWN, VK_SPACE, 0);
    const auto copied_key = pikdraw::desktop::NativeClipboard::read_text();
    require(copied_key && *copied_key == access_key, "Space copies the live bearer key from Settings");
    window.set_settings_tab(6);
}

void run_live_mcp(pikdraw::desktop::NativeWindow& window, std::string_view endpoint,
                  std::string_view access_key, std::string_view file_name) {
    const int protocol_id = 1;
    JsonValue discovery(JsonValue::Object{{"jsonrpc", "2.0"}, {"id", protocol_id},
        {"method", "server/discover"}, {"params", JsonValue::Object{{"_meta", modern_meta()}}}});
    const auto discovery_response = post_json(endpoint, access_key, discovery.serialize(),
        "server/discover", {});
    require(discovery_response.status == 200, "current stateless MCP discovery succeeds");
    require(window.mcp_listener_active(), "the in-app listener remains active after discovery");

    JsonValue list_body(JsonValue::Object{{"jsonrpc", "2.0"}, {"id", 2},
        {"method", "tools/list"}, {"params", JsonValue::Object{{"_meta", modern_meta()}}}});
    const auto list_response = post_json(endpoint, access_key, list_body.serialize(), "tools/list", {});
    require(list_response.status == 200, "MCP tools/list succeeds on the app endpoint: HTTP " +
        std::to_string(list_response.status) + " body=" + list_response.body);
    const auto list_envelope = parse_json(list_response.body);
    const auto* result = list_envelope.get("result");
    const auto* tools = result ? result->get("tools") : nullptr;
    require(tools && tools->array() && tools->array()->size() == 20,
        "desktop HTTP endpoint advertises the shared 20-tool catalog");

    int id = 3;
    const auto instances = call_tool(window, endpoint, access_key, id++, "pikdraw.instances", {});
    require_ok(instances, "pikdraw.instances");
    const auto* instance_list = instances.get("instances");
    require(instance_list && instance_list->array(), "desktop instance discovery returns a list");
    const std::string current_pid = std::to_string(GetCurrentProcessId());
    bool found_instance = false;
    for (const auto& instance : *instance_list->array()) {
        const auto* instance_id = instance.get("instance_id");
        if (instance_id && instance_id->string() && *instance_id->string() == current_pid)
            found_instance = true;
    }
    require(found_instance, "endpoint identifies the owning desktop process");

    JsonValue::Array read_scopes{JsonValue("document.read")};
    const auto read_session = call_tool(window, endpoint, access_key, id++, "pikdraw.connect",
        {{"instance_id", current_pid}, {"scopes", read_scopes}});
    require_ok(read_session, "read-only connection");
    const auto denied_create = call_tool(window, endpoint, access_key, id++, "pikdraw.document.create",
        {{"session_handle", string_value(read_session, "session_handle")},
         {"title", "Read-only scope must not create"}, {"page_width_mm", 600.0}, {"page_height_mm", 300.0}});
    require(string_value(denied_create, "code") == "SCOPE_DENIED",
        "read-only MCP session cannot create a document");

    JsonValue::Array edit_scopes{JsonValue("document.read"), JsonValue("document.create"),
        JsonValue("document.edit"), JsonValue("file.read"), JsonValue("file.write")};
    const auto create_session = call_tool(window, endpoint, access_key, id++, "pikdraw.connect",
        {{"instance_id", current_pid}, {"scopes", edit_scopes}});
    require_ok(create_session, "authoring connection");
    const auto created = call_tool(window, endpoint, access_key, id++, "pikdraw.document.create",
        {{"session_handle", string_value(create_session, "session_handle")},
         {"title", "PikDraw in-process MCP E2E"}, {"page_width_mm", 600.0}, {"page_height_mm", 300.0}});
    require_ok(created, "create a separate 600 by 300 mm document");
    const auto document_id = string_value(created, "document_id");
    require(!document_id.empty(), "created document has a stable identity");

    const auto design_session = call_tool(window, endpoint, access_key, id++, "pikdraw.connect",
        {{"instance_id", current_pid}, {"scopes", edit_scopes}});
    require_ok(design_session, "reconnect to the new document tab");
    const auto design_handle = string_value(design_session, "session_handle");
    const auto inspected = call_tool(window, endpoint, access_key, id++, "pikdraw.document.inspect",
        {{"session_handle", design_handle}});
    require_ok(inspected, "inspect created document");
    require(string_value(inspected, "document_id") == document_id &&
            *inspected.get("page_width_mm")->number() == 600.0 &&
            *inspected.get("page_height_mm")->number() == 300.0,
        "MCP reconnect targets the created page with the requested physical dimensions");

    JsonValue::Array operations{
        JsonValue::Object{{"op", "object.create_rectangle"}, {"client_ref", "panel"},
            {"x_mm", 20.0}, {"y_mm", 20.0}, {"width_mm", 240.0}, {"height_mm", 120.0}},
        JsonValue::Object{{"op", "object.create_ellipse"}, {"client_ref", "sun"},
            {"x_mm", 430.0}, {"y_mm", 35.0}, {"width_mm", 90.0}, {"height_mm", 90.0}},
        JsonValue::Object{{"op", "object.create_text"}, {"client_ref", "title"},
            {"text", "CỬA HÀNG ÁNH DƯƠNG"}, {"font_family", "Segoe UI"},
            {"font_size_pt", 28.0}, {"x_mm", 50.0}, {"y_mm", 205.0}}
    };
    const auto epoch = string_value(inspected, "epoch");
    const auto revision = string_value(inspected, "revision");
    const auto validation = call_tool(window, endpoint, access_key, id++, "pikdraw.batch.validate",
        {{"session_handle", design_handle}, {"expected_epoch", epoch},
         {"expected_revision", revision}, {"operations", operations}});
    require_ok(validation, "validate design batch before editing");
    require(validation.get("valid") && *validation.get("valid")->boolean(),
        "rectangle, ellipse and Unicode text batch validates");
    const std::string request_id = "in-process-e2e-" + std::to_string(GetTickCount64());
    const std::string idempotency_key = request_id + "-receipt";
    JsonValue::Object apply_arguments{{"session_handle", design_handle}, {"request_id", request_id},
        {"idempotency_key", idempotency_key}, {"expected_epoch", epoch},
        {"expected_revision", revision}, {"operations", operations}};
    const auto applied = call_tool(window, endpoint, access_key, id++, "pikdraw.batch.apply", apply_arguments);
    require_ok(applied, "commit authoring batch through the app document writer");
    require(applied.get("created_ids") && applied.get("created_ids")->array() &&
            applied.get("created_ids")->array()->size() == 3,
        "app writer commits three editable objects with stable IDs");
    const auto receipt = call_tool(window, endpoint, access_key, id++, "pikdraw.request.get",
        {{"session_handle", design_handle}, {"idempotency_key", idempotency_key}});
    require_ok(receipt, "query idempotent write receipt");

    const auto objects = call_tool(window, endpoint, access_key, id++, "pikdraw.objects.query",
        {{"session_handle", design_handle}});
    require_ok(objects, "query committed objects");
    const auto* object_list = objects.get("objects");
    require(object_list && object_list->array() && object_list->array()->size() == 3,
        "the desktop document now contains all three authored objects");
    bool found_unicode = false;
    for (const auto& object : *object_list->array()) {
        const auto* text = object.get("text");
        if (text && text->string() && *text->string() == "CỬA HÀNG ÁNH DƯƠNG") found_unicode = true;
    }
    require(found_unicode, "query preserves the Unicode text object");
    const auto document_validation = call_tool(window, endpoint, access_key, id++, "pikdraw.document.validate",
        {{"session_handle", design_handle}});
    require_ok(document_validation, "validate committed document");
    require(document_validation.get("valid") && *document_validation.get("valid")->boolean(),
        "the completed supported design passes document validation");

    const auto saved = call_tool(window, endpoint, access_key, id++, "pikdraw.file.save",
        {{"session_handle", design_handle}, {"file_name", std::string(file_name)}});
    require_ok(saved, "save editable native document through MCP");
    const auto reopened = call_tool(window, endpoint, access_key, id++, "pikdraw.file.verify_reopen",
        {{"session_handle", design_handle}, {"file_name", std::string(file_name)}});
    require_ok(reopened, "verify saved native document");
    require(reopened.get("reopened") && *reopened.get("reopened")->boolean() &&
            reopened.get("object_count") && *reopened.get("object_count")->number() == 3.0 &&
            string_value(reopened, "document_id") == document_id &&
            *reopened.get("page_width_mm")->number() == 600.0,
        "saved .pikdraw reopens with its document identity, dimensions and three editable objects");
    const auto opened = call_tool(window, endpoint, access_key, id++, "pikdraw.file.open",
        {{"session_handle", design_handle}, {"file_name", std::string(file_name)}});
    require_ok(opened, "open saved native design into a separate app tab");
    require(string_value(opened, "document_id") != document_id &&
            *opened.get("object_count")->number() == 3.0 &&
            *opened.get("page_width_mm")->number() == 600.0,
        "native file open preserves content in a new editable desktop tab");

    const auto original_disconnect = call_tool(window, endpoint, access_key, id++, "pikdraw.disconnect",
        {{"session_handle", string_value(read_session, "session_handle")}});
    require_ok(original_disconnect, "disconnect read-only session");
    const auto create_disconnect = call_tool(window, endpoint, access_key, id++, "pikdraw.disconnect",
        {{"session_handle", string_value(create_session, "session_handle")}});
    require_ok(create_disconnect, "disconnect document-create session");
    const auto design_disconnect = call_tool(window, endpoint, access_key, id++, "pikdraw.disconnect",
        {{"session_handle", design_handle}});
    require_ok(design_disconnect, "disconnect design session");
}

#endif

} // namespace

int main() {
#ifndef _WIN32
    std::cout << "SKIP: in-process MCP desktop runtime is Windows-only.\n";
    return 0;
#else
    WSADATA winsock{};
    require(WSAStartup(MAKEWORD(2, 2), &winsock) == 0, "test client initializes Winsock");
    std::string failure;
    std::filesystem::path generated_file;
    try {
        pikdraw::desktop::NativeWindow window(1024, 720);
        require(window.initialize("PikDraw in-process MCP E2E"), "NativeWindow initializes its real Win32 runtime");
        ShowWindow(window.hwnd(), SW_HIDE);
        ClipboardTextSnapshot clipboard(window.hwnd());
        require(pikdraw::desktop::actions::ActionId::kSettingsAiConnectionsCopyEndpoint ==
                    "action.settings.ai_connections.copy_endpoint" &&
                pikdraw::desktop::actions::ActionId::kSettingsAiConnectionsCopyAccessKey ==
                    "action.settings.ai_connections.copy_access_key",
            "Settings credential-copy actions have stable IDs");
        require(window.set_mcp_access(true, true),
            "the actual NativeWindow starts the in-process Streamable HTTP endpoint");
        const auto endpoint = pikdraw::desktop::NativeWindowMcpE2eAccess::endpoint(window);
        const auto access_key = pikdraw::desktop::NativeWindowMcpE2eAccess::access_key(window);
        require(!endpoint.empty() && !access_key.empty(), "live endpoint and bearer key are available");
        verify_settings_copy_controls(window, clipboard, endpoint, access_key);

        const std::string file_name = "mcp-inapp-e2e-" + std::to_string(GetTickCount64());
        generated_file = granted_file_path(file_name);
        run_live_mcp(window, endpoint, access_key, file_name);

        window.open_ai_connections_settings();
        for (int step = 0; step < 4; ++step)
            SendMessageW(window.hwnd(), WM_KEYDOWN, VK_TAB, 0);
        require(window.ai_connections_focus_index() == 4, "Tab reaches Settings Reset control");
        SendMessageW(window.hwnd(), WM_KEYDOWN, VK_RETURN, 0);
        require(!window.ai_connections_read_choice() && !window.ai_connections_edit_choice(),
            "Enter drafts a full MCP revoke in Settings");
        SendMessageW(window.hwnd(), WM_KEYDOWN, VK_TAB, 0);
        SendMessageW(window.hwnd(), WM_KEYDOWN, VK_TAB, 0);
        require(window.ai_connections_focus_index() == 6, "Tab reaches Settings Apply after Reset");
        SendMessageW(window.hwnd(), WM_KEYDOWN, VK_RETURN, 0);
        require(!window.mcp_listener_active() && !window.mcp_edit_access_active(),
            "Settings Apply revokes the direct in-process MCP listener");
        const JsonValue stale_body(JsonValue::Object{{"jsonrpc", "2.0"}, {"id", 999},
            {"method", "server/discover"}, {"params", JsonValue::Object{{"_meta", modern_meta()}}}});
        const auto after_revoke = post_json(endpoint, access_key, stale_body.serialize(), "server/discover", {});
        require(after_revoke.status < 0 || after_revoke.status == 401,
            "revoked endpoint or bearer cannot serve another MCP request: HTTP " +
                std::to_string(after_revoke.status) + " body=" + after_revoke.body);
        window.close_settings_modal();
    } catch (const std::exception& error) {
        failure = error.what();
    }
    if (!generated_file.empty()) {
        std::error_code cleanup_error;
        std::filesystem::remove(generated_file, cleanup_error);
        if (cleanup_error && failure.empty())
            failure = "generated native E2E file cleanup failed: " + cleanup_error.message();
    }
    WSACleanup();
    if (!failure.empty()) {
        std::cerr << "In-process MCP E2E failed: " << failure << '\n';
        return 1;
    }
    std::cout << "NativeWindow Settings copy/revoke -> in-process MCP HTTP -> UI-owner writer -> create/edit/validate/save/reopen passed.\n";
    return 0;
#endif
}
