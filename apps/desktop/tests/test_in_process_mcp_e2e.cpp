#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include "native_window_automation.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include "pikdraw/automation/json_value.hpp"
#include "pikdraw/desktop/shell/native_window.hpp"
#include "../src/clipboard/native_clipboard.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <future>
#include <iomanip>
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
    static size_t queued_requests(NativeWindow& window) {
        return window.mcp_runtime_ ? window.mcp_runtime_->pipe.pending_request_count_for_e2e() : 0;
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

double number_value(const JsonValue& value, std::string_view field) {
    const auto* item = value.get(field);
    require(item && item->number(), "response has numeric field '" + std::string(field) + "'");
    return *item->number();
}

JsonValue rgb_color(double red, double green, double blue) {
    return JsonValue::Object{{"model", "rgb"}, {"channels", JsonValue::Object{
        {"r", red}, {"g", green}, {"b", blue}, {"alpha", 1.0}}}};
}

void require_ok(const JsonValue& value, std::string_view operation) {
    const auto* status = value.get("status");
    if (status && status->string() && *status->string() == "ok") return;
    const auto* message = value.get("message");
    throw std::runtime_error(std::string(operation) + " failed: " +
        (message && message->string() ? *message->string() : value.serialize()));
}

void dismiss_recovery_modal(pikdraw::desktop::NativeWindow& window) {
    if (window.is_recovery_modal_open())
        SendMessageW(window.hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
    require(!window.is_recovery_modal_open(),
        "Settings E2E starts after dismissing the non-destructive recovery prompt");
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
    BYTE keys[256] = {};
    SetKeyboardState(keys);
    dismiss_recovery_modal(window);
    window.open_ai_connections_settings();
    SendMessageW(window.hwnd(), WM_KEYDOWN, VK_TAB, 0);
    SendMessageW(window.hwnd(), WM_KEYDOWN, VK_TAB, 0);
    require(window.ai_connections_focus_index() == 2,
        "Tab reaches Copy URL after access choices (focus=" +
            std::to_string(window.ai_connections_focus_index()) +
            ", settings=" + std::to_string(window.is_settings_modal_open()) +
            ", tab=" + std::to_string(window.settings_active_tab()) + ")");
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
                  std::string_view access_key, std::string_view file_name,
                  std::string_view original_file_name) {
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
    const std::string read_only_file_name = std::string(file_name) + "-read-only";
    require(!std::filesystem::exists(granted_file_path(read_only_file_name)),
        "read-only file-write denial fixture starts without a native file");
    const auto denied_file_write = call_tool(window, endpoint, access_key, id++, "pikdraw.file.save",
        {{"session_handle", string_value(read_session, "session_handle")},
         {"file_name", read_only_file_name}});
    require(string_value(denied_file_write, "code") == "SCOPE_DENIED" &&
            !std::filesystem::exists(granted_file_path(read_only_file_name)),
        "AC05 read-only scope cannot save or create a file in the granted directory");
    const auto original_state = call_tool(window, endpoint, access_key, id++, "pikdraw.document.inspect",
        {{"session_handle", string_value(read_session, "session_handle")}});
    require_ok(original_state, "inspect the user's original document before agent creation");
    const auto original_document_id = string_value(original_state, "document_id");
    const auto original_object_id = window.workflow().add_rectangle(
        32.0, 28.0, 24.0, 16.0, "#00f2c3", "#0ea5e9", 1.0);
    require(original_object_id != pikdraw::document::kInvalidObjectId,
        "AC09 creates a dirty local document state before automation starts a new tab");
    const auto dirty_original = call_tool(window, endpoint, access_key, id++, "pikdraw.document.inspect",
        {{"session_handle", string_value(read_session, "session_handle")}});
    require_ok(dirty_original, "inspect the user's dirty document before agent creation");
    require(string_value(dirty_original, "document_id") == original_document_id &&
            dirty_original.get("dirty") && dirty_original.get("dirty")->boolean() &&
            *dirty_original.get("dirty")->boolean() &&
            number_value(dirty_original, "object_count") == 1.0,
        "AC09 records the user's dirty revision and object before opening an agent document");

    JsonValue::Array edit_scopes{JsonValue("document.read"), JsonValue("document.create"),
        JsonValue("document.edit"), JsonValue("file.read"), JsonValue("file.write")};
    const auto create_session = call_tool(window, endpoint, access_key, id++, "pikdraw.connect",
        {{"instance_id", current_pid}, {"scopes", edit_scopes}});
    require_ok(create_session, "authoring connection");
    const auto before_text_draft = call_tool(window, endpoint, access_key, id++, "pikdraw.document.inspect",
        {{"session_handle", string_value(create_session, "session_handle")}});
    require_ok(before_text_draft, "inspect before opening a native text draft");
    window.start_inline_text_editing({120.0, 54.0});
    require(window.is_inline_editing(), "AC03 creates an active native inline-text draft");
    const JsonValue::Array busy_operations{JsonValue::Object{{"op", "object.create_rectangle"},
        {"x_mm", 72.0}, {"y_mm", 80.0}, {"width_mm", 12.0}, {"height_mm", 8.0}}};
    const auto busy_batch = call_tool(window, endpoint, access_key, id++, "pikdraw.batch.apply",
        {{"session_handle", string_value(create_session, "session_handle")},
         {"request_id", "ac03-busy-text-draft"}, {"idempotency_key", "ac03-busy-text-draft-key"},
         {"expected_epoch", string_value(before_text_draft, "epoch")},
         {"expected_revision", string_value(before_text_draft, "revision")},
         {"operations", busy_operations}});
    require(string_value(busy_batch, "code") == "BUSY_INTERACTION" && window.is_inline_editing(),
        "AC03 rejects an agent batch while preserving the user's active inline-text draft");
    const auto busy_create = call_tool(window, endpoint, access_key, id++, "pikdraw.document.create",
        {{"session_handle", string_value(create_session, "session_handle")},
         {"title", "Must not interrupt text draft"}, {"page_width_mm", 600.0}, {"page_height_mm", 300.0}});
    require(string_value(busy_create, "code") == "BUSY_INTERACTION" && window.is_inline_editing(),
        "AC03 also blocks document creation until the native text draft is resolved");
    const auto during_text_draft = call_tool(window, endpoint, access_key, id++, "pikdraw.document.inspect",
        {{"session_handle", string_value(create_session, "session_handle")}});
    require_ok(during_text_draft, "inspect while an MCP write was blocked by the text draft");
    require(string_value(during_text_draft, "revision") == string_value(before_text_draft, "revision") &&
            number_value(during_text_draft, "object_count") == number_value(before_text_draft, "object_count"),
        "AC03 blocked calls preserve committed document revision and objects");
    window.cancel_inline_text_editing();
    require(!window.is_inline_editing(), "AC03 test cancels only the draft it created");
    const auto original_file_path = granted_file_path(original_file_name);
    require(!std::filesystem::exists(original_file_path),
        "unique AC09 output path must not overwrite an existing native document");
    const auto saved_user_revision = call_tool(window, endpoint, access_key, id++, "pikdraw.file.save",
        {{"session_handle", string_value(create_session, "session_handle")},
         {"file_name", std::string(original_file_name)}});
    require_ok(saved_user_revision, "save user document revision R through the scoped MCP grant");
    const auto r_plus_one_id = window.workflow().add_rectangle(
        68.0, 28.0, 24.0, 16.0, "#f59e0b", "#d97706", 1.0);
    require(r_plus_one_id != pikdraw::document::kInvalidObjectId,
        "AC09 native workflow commits a user edit after saved revision R");
    const auto r_plus_one_state = call_tool(window, endpoint, access_key, id++, "pikdraw.document.inspect",
        {{"session_handle", string_value(create_session, "session_handle")}});
    require_ok(r_plus_one_state, "inspect user revision R+1 after the save");
    require(r_plus_one_state.get("dirty") && r_plus_one_state.get("dirty")->boolean() &&
            *r_plus_one_state.get("dirty")->boolean() &&
            std::stoull(string_value(r_plus_one_state, "revision")) ==
                std::stoull(string_value(saved_user_revision, "revision")) + 1 &&
            number_value(r_plus_one_state, "object_count") == 2.0,
        "AC09 revision R+1 remains dirty after the agent saved revision R");
    const auto disk_revision_r = call_tool(window, endpoint, access_key, id++, "pikdraw.file.verify_reopen",
        {{"session_handle", string_value(create_session, "session_handle")},
         {"file_name", std::string(original_file_name)}});
    require_ok(disk_revision_r, "verify that native file still contains saved revision R");
    const auto* disk_revision_ids = disk_revision_r.get("object_ids");
    require(disk_revision_ids && disk_revision_ids->array() && disk_revision_ids->array()->size() == 1 &&
            disk_revision_r.get("object_count") && *disk_revision_r.get("object_count")->number() == 1.0 &&
            string_value(disk_revision_r, "document_id") == original_document_id,
        "AC09 saved revision R retains its native snapshot and document identity");
    bool disk_has_original_id = false;
    for (const auto& saved_id : *disk_revision_ids->array()) {
        if (saved_id.string() && *saved_id.string() == std::to_string(original_object_id))
            disk_has_original_id = true;
        require(!saved_id.string() || *saved_id.string() != std::to_string(r_plus_one_id),
            "AC09 revision R on disk does not absorb the later R+1 user object");
    }
    require(disk_has_original_id,
        "AC09 saved revision R retains the user's original stable object ID");
    const auto created = call_tool(window, endpoint, access_key, id++, "pikdraw.document.create",
        {{"session_handle", string_value(create_session, "session_handle")},
         {"title", "PikDraw in-process MCP E2E"}, {"page_width_mm", 600.0}, {"page_height_mm", 300.0}});
    require_ok(created, "create a separate 600 by 300 mm document");
    const auto document_id = string_value(created, "document_id");
    require(!document_id.empty(), "created document has a stable identity");
    const auto dirty_after_create = call_tool(window, endpoint, access_key, id++, "pikdraw.document.inspect",
        {{"session_handle", string_value(read_session, "session_handle")}});
    require_ok(dirty_after_create, "inspect the original tab after agent document creation");
    const auto original_objects_after_create = call_tool(window, endpoint, access_key, id++, "pikdraw.objects.query",
        {{"session_handle", string_value(read_session, "session_handle")}});
    require_ok(original_objects_after_create, "query original objects after agent document creation");
    const auto* preserved_original_objects = original_objects_after_create.get("objects");
    require(string_value(dirty_after_create, "document_id") == original_document_id &&
            dirty_after_create.get("dirty") && *dirty_after_create.get("dirty")->boolean() &&
            number_value(dirty_after_create, "object_count") == 2.0 && preserved_original_objects &&
            preserved_original_objects->array() && preserved_original_objects->array()->size() == 2,
        "AC09 agent document creation preserves the original dirty tab and both local revisions");
    bool original_id_after_create = false;
    bool r_plus_one_id_after_create = false;
    for (const auto& object : *preserved_original_objects->array()) {
        const auto object_id = string_value(object, "id");
        if (object_id == std::to_string(original_object_id)) original_id_after_create = true;
        if (object_id == std::to_string(r_plus_one_id)) r_plus_one_id_after_create = true;
    }
    require(original_id_after_create && r_plus_one_id_after_create,
        "AC09 agent document creation keeps both user revision IDs in the original tab");

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
    const auto unsafe_save = call_tool(window, endpoint, access_key, id++, "pikdraw.file.save",
        {{"session_handle", design_handle}, {"file_name", "../ac05-outside-grant"}});
    const auto unsafe_open = call_tool(window, endpoint, access_key, id++, "pikdraw.file.open",
        {{"session_handle", design_handle}, {"file_name", "..\\..\\ac05-outside-grant"}});
    require(string_value(unsafe_save, "code") == "PATH_DENIED" &&
            string_value(unsafe_open, "code") == "PATH_DENIED",
        "AC05 native save/open reject traversal outside the fixed automation files grant");
    const auto after_path_denials = call_tool(window, endpoint, access_key, id++, "pikdraw.document.inspect",
        {{"session_handle", design_handle}});
    require_ok(after_path_denials, "inspect document after file grant path denials");
    require(string_value(after_path_denials, "revision") == string_value(inspected, "revision") &&
            number_value(after_path_denials, "object_count") == 0.0,
        "AC05 path-denied file operations do not mutate the connected document");

    const auto font = call_tool(window, endpoint, access_key, id++, "pikdraw.font.query",
        {{"session_handle", design_handle}, {"font_family", "Segoe UI"}, {"font_size_pt", 28.0}});
    require_ok(font, "resolve installed Vietnamese-capable font");
    require(!string_value(font, "actual_family").empty() &&
            font.get("fallback") && font.get("fallback")->boolean() &&
            !*font.get("fallback")->boolean(),
        "AC01 font resolution uses the requested installed font without fallback");

    const std::string title_text = "CỬA HÀNG ÁNH DƯƠNG";
    const std::string subtitle_text = "Nội thất đẹp • Chất lượng bền lâu • Thiết kế theo yêu cầu";
    const auto fallback_metrics = call_tool(window, endpoint, access_key, id++, "pikdraw.text.measure",
        {{"session_handle", design_handle}, {"text", subtitle_text},
         {"font_family", "PikDrawDefinitelyAbsentFontXYZ"}, {"font_size_pt", 16.0}});
    require_ok(fallback_metrics, "measure Vietnamese text with an unavailable requested font");
    require(fallback_metrics.get("fallback") && fallback_metrics.get("fallback")->boolean() &&
            *fallback_metrics.get("fallback")->boolean() &&
            !string_value(fallback_metrics, "warning").empty() &&
            !string_value(fallback_metrics, "actual_family").empty(),
        "AC09 missing-font fallback is explicit and carries a warning instead of silent substitution");
    const auto title_metrics = call_tool(window, endpoint, access_key, id++, "pikdraw.text.measure",
        {{"session_handle", design_handle}, {"text", title_text}, {"font_family", "Segoe UI"},
         {"font_size_pt", 28.0}});
    require_ok(title_metrics, "measure Vietnamese title before placement");
    const auto subtitle_metrics = call_tool(window, endpoint, access_key, id++, "pikdraw.text.measure",
        {{"session_handle", design_handle}, {"text", subtitle_text}, {"font_family", "Segoe UI"},
         {"font_size_pt", 16.0}});
    require_ok(subtitle_metrics, "measure full Vietnamese subtitle before placement");
    require(number_value(title_metrics, "missing_glyph_count") == 0.0 &&
            number_value(subtitle_metrics, "missing_glyph_count") == 0.0 &&
            number_value(subtitle_metrics, "glyph_count") > 20.0,
        "AC01 typography metrics cover Vietnamese glyphs without missing-glyph substitution");
    const auto* title_bounds = title_metrics.get("bounds_mm");
    const auto* subtitle_bounds = subtitle_metrics.get("bounds_mm");
    require(title_bounds && subtitle_bounds, "text measure returns source font bounds for layout");
    const double title_width = number_value(*title_bounds, "width_mm");
    const double subtitle_width = number_value(*subtitle_bounds, "width_mm");
    require(title_width > 0.0 && title_width < 560.0 && subtitle_width > 0.0 && subtitle_width < 560.0,
        "measured title and subtitle fit inside the 10 mm page-safe area");

    JsonValue::Array operations{
        JsonValue::Object{{"op", "object.create_rectangle"}, {"client_ref", "background"},
            {"name", "AC01 background"}, {"x_mm", 10.0}, {"y_mm", 10.0},
            {"width_mm", 580.0}, {"height_mm", 280.0}, {"fill", rgb_color(0.04, 0.08, 0.16)}},
        JsonValue::Object{{"op", "object.create_text"}, {"client_ref", "title"},
            {"name", "AC01 title"}, {"text", title_text}, {"font_family", "Segoe UI"},
            {"font_size_pt", 28.0}, {"x_mm", (600.0 - title_width) / 2.0}, {"y_mm", 72.0},
            {"alignment", "center"}, {"fill", rgb_color(1.0, 1.0, 1.0)}},
        JsonValue::Object{{"op", "object.create_text"}, {"client_ref", "subtitle"},
            {"name", "AC01 subtitle"}, {"text", subtitle_text}, {"font_family", "Segoe UI"},
            {"font_size_pt", 16.0}, {"x_mm", (600.0 - subtitle_width) / 2.0}, {"y_mm", 105.0},
            {"alignment", "center"}, {"fill", rgb_color(1.0, 0.72, 0.24)}},
        JsonValue::Object{{"op", "object.create_ellipse"}, {"client_ref", "icon_sun"},
            {"name", "AC01 icon sun"}, {"x_mm", 132.0}, {"y_mm", 145.0},
            {"width_mm", 36.0}, {"height_mm", 36.0}, {"fill", rgb_color(1.0, 0.72, 0.24)}},
        JsonValue::Object{{"op", "object.create_rectangle"}, {"client_ref", "icon_home"},
            {"name", "AC01 icon home"}, {"x_mm", 282.0}, {"y_mm", 145.0},
            {"width_mm", 36.0}, {"height_mm", 36.0}, {"fill", rgb_color(0.24, 0.68, 0.92)}},
        JsonValue::Object{{"op", "object.create_polyline"}, {"client_ref", "icon_roof"},
            {"name", "AC01 icon roof"}, {"points_mm", JsonValue::Array{
                JsonValue::Object{{"x_mm", 432.0}, {"y_mm", 145.0}},
                JsonValue::Object{{"x_mm", 468.0}, {"y_mm", 145.0}},
                JsonValue::Object{{"x_mm", 450.0}, {"y_mm", 181.0}}}},
            {"closed", true}, {"fill", rgb_color(0.35, 0.82, 0.54)}},
        JsonValue::Object{{"op", "object.create_text"}, {"client_ref", "tagline"},
            {"name", "AC01 production tagline"}, {"text", "Thiết kế và thi công theo yêu cầu"},
            {"font_family", "Segoe UI"}, {"font_size_pt", 14.0},
            {"x_mm", 135.0}, {"y_mm", 225.0}, {"alignment", "center"},
            {"fill", rgb_color(0.9, 0.93, 0.98)}}
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
            applied.get("created_ids")->array()->size() == operations.size(),
        "app writer commits all AC01 objects with stable IDs in one transaction");
    const auto receipt = call_tool(window, endpoint, access_key, id++, "pikdraw.request.get",
        {{"session_handle", design_handle}, {"idempotency_key", idempotency_key}});
    require_ok(receipt, "query idempotent write receipt");

    const auto objects = call_tool(window, endpoint, access_key, id++, "pikdraw.objects.query",
        {{"session_handle", design_handle}});
    require_ok(objects, "query committed objects");
    const auto* object_list = objects.get("objects");
    require(object_list && object_list->array() && object_list->array()->size() == operations.size(),
        "the desktop document contains the complete measured and authored AC01 layout");
    bool found_unicode = false;
    bool found_safe_background = false;
    bool found_source_color = false;
    std::vector<double> icon_centers;
    std::string title_id;
    double title_x_before_save = 0.0;
    for (const auto& object : *object_list->array()) {
        const auto* text = object.get("text");
        if (text && text->string() && *text->string() == title_text) {
            found_unicode = true;
            require(number_value(object, "missing_glyph_count") == 0.0,
                "saved title object exposes complete Vietnamese glyph coverage");
            title_id = string_value(object, "id");
            const auto* bounds = object.get("bounds");
            require(bounds, "title object reports measured geometry");
            title_x_before_save = number_value(*bounds, "x_mm");
        }
        const auto& name = string_value(object, "name");
        if (name == "AC01 background") {
            const auto* bounds = object.get("bounds");
            require(bounds && number_value(*bounds, "x_mm") == 10.0 &&
                    number_value(*bounds, "y_mm") == 10.0 &&
                    number_value(*bounds, "width_mm") == 580.0 &&
                    number_value(*bounds, "height_mm") == 280.0,
                "background geometry matches the 600x300 mm safe layout");
            found_safe_background = true;
        }
        if (name == "AC01 icon sun" || name == "AC01 icon home" || name == "AC01 icon roof") {
            const auto* bounds = object.get("bounds");
            require(bounds, "each semantic icon reports its vector bounds");
            const double width = number_value(*bounds, "width_mm");
            const double height = number_value(*bounds, "height_mm");
            require(std::abs(width - 36.0) < 1e-6 && std::abs(height - 36.0) < 1e-6,
                "icon '" + name + "' preserves 36x36 mm geometry: " + bounds->serialize());
            icon_centers.push_back(number_value(*bounds, "x_mm") + 18.0);
        }
        if (name == "AC01 icon home") {
            const auto* fill = object.get("fill");
            const auto* channels = fill ? fill->get("channels") : nullptr;
            found_source_color = channels && number_value(*channels, "r") == 0.24 &&
                number_value(*channels, "g") == 0.68 && number_value(*channels, "b") == 0.92;
        }
    }
    require(found_unicode && found_safe_background && found_source_color && icon_centers.size() == 3,
        "AC01 object query preserves Unicode, source color, safe background and all three vector icons");
    std::sort(icon_centers.begin(), icon_centers.end());
    require(std::abs((icon_centers[1] - icon_centers[0]) - 150.0) < 1e-6 &&
            std::abs((icon_centers[2] - icon_centers[1]) - 150.0) < 1e-6,
        "AC01 geometric icons are evenly distributed across the sign");
    require(!title_id.empty(), "title has a stable object ID for native reopen verification");
    const auto document_validation = call_tool(window, endpoint, access_key, id++, "pikdraw.document.validate",
        {{"session_handle", design_handle}, {"safe_margin_mm", 10.0}});
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
            reopened.get("object_count") && *reopened.get("object_count")->number() ==
                static_cast<double>(operations.size()) &&
            string_value(reopened, "document_id") == document_id &&
            *reopened.get("page_width_mm")->number() == 600.0,
        "saved .pikdraw reopens with its document identity, dimensions and complete editable AC01 layout");
    const auto* expected_ids = applied.get("created_ids");
    const auto* reopened_ids = reopened.get("object_ids");
    require(expected_ids && expected_ids->array() && reopened_ids && reopened_ids->array(),
        "native reopen reports persisted stable object IDs");
    std::vector<std::string> expected_id_list;
    std::vector<std::string> reopened_id_list;
    for (const auto& value : *expected_ids->array()) {
        require(value.string(), "batch receipt IDs are strings");
        expected_id_list.push_back(*value.string());
    }
    for (const auto& value : *reopened_ids->array()) {
        require(value.string(), "native file object IDs are strings");
        reopened_id_list.push_back(*value.string());
    }
    std::sort(expected_id_list.begin(), expected_id_list.end());
    std::sort(reopened_id_list.begin(), reopened_id_list.end());
    require(expected_id_list == reopened_id_list,
        "native save and reopen preserve every stable ID created by the MCP batch");
    const auto opened = call_tool(window, endpoint, access_key, id++, "pikdraw.file.open",
        {{"session_handle", design_handle}, {"file_name", std::string(file_name)}});
    require_ok(opened, "open saved native design into a separate app tab");
    require(string_value(opened, "document_id") != document_id &&
            *opened.get("object_count")->number() == static_cast<double>(operations.size()) &&
            *opened.get("page_width_mm")->number() == 600.0,
        "native file open preserves content in a new editable desktop tab");
    const auto dirty_after_open = call_tool(window, endpoint, access_key, id++, "pikdraw.document.inspect",
        {{"session_handle", string_value(read_session, "session_handle")}});
    require_ok(dirty_after_open, "inspect the original dirty document after opening an automation file");
    require(string_value(dirty_after_open, "document_id") == original_document_id &&
            dirty_after_open.get("dirty") && *dirty_after_open.get("dirty")->boolean() &&
            number_value(dirty_after_open, "object_count") == 2.0,
        "AC09 opening the agent's saved file leaves the user's dirty document untouched");
    const auto reopened_session = call_tool(window, endpoint, access_key, id++, "pikdraw.connect",
        {{"instance_id", current_pid}, {"scopes", edit_scopes}});
    require_ok(reopened_session, "connect to the newly opened native document tab");
    const auto reopened_handle = string_value(reopened_session, "session_handle");
    const auto opened_objects = call_tool(window, endpoint, access_key, id++, "pikdraw.objects.query",
        {{"session_handle", reopened_handle}});
    require_ok(opened_objects, "query editable objects in the reopened desktop tab");
    const auto* opened_object_list = opened_objects.get("objects");
    require(opened_object_list && opened_object_list->array() &&
            opened_object_list->array()->size() == operations.size(),
        "new desktop tab exposes all native design objects for semantic editing");
    std::string reopened_title_id;
    double reopened_title_x = 0.0;
    for (const auto& object : *opened_object_list->array()) {
        if (string_value(object, "name") == "AC01 title") {
            reopened_title_id = string_value(object, "id");
            const auto* bounds = object.get("bounds");
            require(bounds, "reopened title retains geometry bounds");
            reopened_title_x = number_value(*bounds, "x_mm");
        }
    }
    require(reopened_title_id == title_id && reopened_title_x == title_x_before_save,
        "the reopened native title retains its stable ID and original position");

    const auto opened_state = call_tool(window, endpoint, access_key, id++, "pikdraw.document.inspect",
        {{"session_handle", reopened_handle}});
    require_ok(opened_state, "inspect reopened editable document");
    const auto user_revision = string_value(opened_state, "revision");
    const auto user_epoch = string_value(opened_state, "epoch");
    const auto user_object_id = window.workflow().add_rectangle(
        24.0, 42.0, 18.0, 12.0, "#00f2c3", "#0ea5e9", 1.0);
    require(user_object_id != pikdraw::document::kInvalidObjectId,
        "AC03 native document workflow commits the user's concurrent edit");
    const auto rejected_stale_write = call_tool(window, endpoint, access_key, id++, "pikdraw.batch.apply",
        {{"session_handle", reopened_handle}, {"request_id", "ac03-stale-write"},
         {"idempotency_key", "ac03-stale-write-receipt"}, {"expected_epoch", user_epoch},
         {"expected_revision", user_revision}, {"operations", JsonValue::Array{
             JsonValue::Object{{"op", "object.create_rectangle"}, {"client_ref", "stale_agent_object"},
                 {"name", "AC03 stale agent object"}, {"x_mm", 60.0}, {"y_mm", 45.0},
                 {"width_mm", 18.0}, {"height_mm", 12.0}}}}});
    require(string_value(rejected_stale_write, "code") == "REVISION_CONFLICT",
        "AC03 rejects an agent batch based on a revision changed by the app workflow");
    const auto after_stale_write = call_tool(window, endpoint, access_key, id++, "pikdraw.objects.query",
        {{"session_handle", reopened_handle}});
    require_ok(after_stale_write, "query the document after rejecting a stale agent batch");
    const auto* after_stale_objects = after_stale_write.get("objects");
    require(after_stale_objects && after_stale_objects->array() &&
            after_stale_objects->array()->size() == operations.size() + 1,
        "AC03 stale batch has no partial effect and preserves the user's added object");
    bool user_edit_preserved = false;
    bool stale_agent_object_created = false;
    for (const auto& object : *after_stale_objects->array()) {
        const auto object_id = string_value(object, "id");
        if (object_id == std::to_string(user_object_id)) user_edit_preserved = true;
        if (string_value(object, "name") == "AC03 stale agent object") stale_agent_object_created = true;
    }
    require(user_edit_preserved && !stale_agent_object_created,
        "AC03 retains the native user's stable object ID and does not create the stale agent object");
    const auto current_opened_state = call_tool(window, endpoint, access_key, id++, "pikdraw.document.inspect",
        {{"session_handle", reopened_handle}});
    require_ok(current_opened_state, "inspect the current document revision after conflict");
    const auto move_request_id = "ac01-reopen-edit-" + std::to_string(GetTickCount64());
    const auto moved_title = call_tool(window, endpoint, access_key, id++, "pikdraw.batch.apply",
        {{"session_handle", reopened_handle}, {"request_id", move_request_id},
         {"idempotency_key", move_request_id + "-receipt"},
         {"expected_epoch", string_value(current_opened_state, "epoch")},
         {"expected_revision", string_value(current_opened_state, "revision")},
         {"operations", JsonValue::Array{JsonValue::Object{
             {"op", "object.transform"}, {"target_ids", JsonValue::Array{JsonValue(reopened_title_id)}},
             {"translate_x_mm", 1.0}}}}});
    require_ok(moved_title, "edit the reopened native title through semantic MCP");
    const auto moved_state = call_tool(window, endpoint, access_key, id++, "pikdraw.document.inspect",
        {{"session_handle", reopened_handle}});
    require_ok(moved_state, "inspect revision after reopened-document edit");
    const auto undo_request_id = "ac01-reopen-undo-" + std::to_string(GetTickCount64());
    const auto undo_edit = call_tool(window, endpoint, access_key, id++, "pikdraw.history.undo",
        {{"session_handle", reopened_handle}, {"request_id", undo_request_id},
         {"idempotency_key", undo_request_id + "-receipt"},
         {"expected_epoch", string_value(moved_state, "epoch")},
         {"expected_revision", string_value(moved_state, "revision")}});
    require_ok(undo_edit, "undo semantic edit in reopened native document");
    const auto restored_objects = call_tool(window, endpoint, access_key, id++, "pikdraw.objects.query",
        {{"session_handle", reopened_handle}});
    require_ok(restored_objects, "verify the reopened title after undo");
    const auto* restored_list = restored_objects.get("objects");
    require(restored_list && restored_list->array(), "undo leaves a readable native object list");
    bool restored_title = false;
    for (const auto& object : *restored_list->array()) {
        if (string_value(object, "id") == title_id) {
            const auto* bounds = object.get("bounds");
            require(bounds && std::abs(number_value(*bounds, "x_mm") - title_x_before_save) < 1e-6,
                "undo restores the reopened object's original geometry");
            restored_title = true;
        }
    }
    require(restored_title, "reopened native object remains addressable by its stable ID after edit and undo");

    const auto before_close_state = call_tool(window, endpoint, access_key, id++, "pikdraw.document.inspect",
        {{"session_handle", reopened_handle}});
    require_ok(before_close_state, "inspect before queuing an asynchronous edit");
    const auto delayed_request_id = "ac03-delayed-close-" + std::to_string(GetTickCount64());
    JsonValue::Object delayed_operation{{"op", "object.create_rectangle"},
        {"client_ref", "late_after_close"}, {"x_mm", 100.0}, {"y_mm", 100.0},
        {"width_mm", 16.0}, {"height_mm", 9.0}};
    JsonValue::Array delayed_operations{JsonValue(std::move(delayed_operation))};
    JsonValue::Object delayed_arguments{{"session_handle", reopened_handle},
        {"request_id", delayed_request_id}, {"idempotency_key", delayed_request_id + "-receipt"},
        {"expected_epoch", string_value(before_close_state, "epoch")},
        {"expected_revision", string_value(before_close_state, "revision")},
        {"operations", std::move(delayed_operations)}};
    JsonValue::Object delayed_params{{"name", "pikdraw.batch.apply"}, {"_meta", modern_meta()},
        {"arguments", std::move(delayed_arguments)}};
    JsonValue delayed_body(JsonValue::Object{{"jsonrpc", "2.0"}, {"id", id++},
        {"method", "tools/call"}, {"params", std::move(delayed_params)}});
    const auto delayed_payload = delayed_body.serialize();
    auto delayed_request = std::async(std::launch::async,
        [endpoint, access_key, delayed_payload] {
            return post_json(endpoint, access_key, delayed_payload, "tools/call", "pikdraw.batch.apply");
        });
    const auto queued_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (pikdraw::desktop::NativeWindowMcpE2eAccess::queued_requests(window) == 0 &&
           std::chrono::steady_clock::now() < queued_deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(pikdraw::desktop::NativeWindowMcpE2eAccess::queued_requests(window) == 1,
        "AC03 asynchronous MCP write reaches the UI-owner queue before closing its document");
    window.close_tab(window.active_tab_index());
    window.create_new_tab("AC03 close-reopen target", 600.0, 300.0);
    const auto delayed_response = pump_until_ready(delayed_request);
    require(delayed_response.status == 200, "queued pre-close MCP write receives an application response");
    const auto delayed_envelope = parse_json(delayed_response.body);
    const auto* delayed_result = delayed_envelope.get("result");
    const auto* delayed_content = delayed_result ? delayed_result->get("structuredContent") : nullptr;
    require(delayed_content && delayed_content->get("code") &&
            *delayed_content->get("code")->string() == "SESSION_EXPIRED",
        "AC03 queued MCP work cannot apply through a session whose document closed before dispatch");
    const auto replacement_session = call_tool(window, endpoint, access_key, id++, "pikdraw.connect",
        {{"instance_id", current_pid}, {"scopes", edit_scopes}});
    require_ok(replacement_session, "connect to the replacement document after the delayed request");
    const auto replacement_state = call_tool(window, endpoint, access_key, id++, "pikdraw.document.inspect",
        {{"session_handle", string_value(replacement_session, "session_handle")}});
    require_ok(replacement_state, "inspect the replacement document after rejecting delayed work");
    require(string_value(replacement_state, "document_id") != string_value(opened, "document_id") &&
            number_value(replacement_state, "object_count") == 0.0,
        "AC03 close/reopen rejects the late batch without creating an object in the replacement document");

    const auto original_disconnect = call_tool(window, endpoint, access_key, id++, "pikdraw.disconnect",
        {{"session_handle", string_value(read_session, "session_handle")}});
    require_ok(original_disconnect, "disconnect read-only session");
    const auto create_disconnect = call_tool(window, endpoint, access_key, id++, "pikdraw.disconnect",
        {{"session_handle", string_value(create_session, "session_handle")}});
    require_ok(create_disconnect, "disconnect document-create session");
    const auto design_disconnect = call_tool(window, endpoint, access_key, id++, "pikdraw.disconnect",
        {{"session_handle", design_handle}});
    require_ok(design_disconnect, "disconnect design session");
    const auto reopened_disconnect = call_tool(window, endpoint, access_key, id++, "pikdraw.disconnect",
        {{"session_handle", reopened_handle}});
    require(string_value(reopened_disconnect, "code") == "SESSION_EXPIRED",
        "closed document session remains expired during disconnect");
    const auto replacement_disconnect = call_tool(window, endpoint, access_key, id++, "pikdraw.disconnect",
        {{"session_handle", string_value(replacement_session, "session_handle")}});
    require_ok(replacement_disconnect, "disconnect replacement document session");
}

#include "billboard_5x2m_design.hpp"

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
    std::filesystem::path original_document_file;
    std::filesystem::path billboard_file;
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
        const std::string original_file_name = file_name + "-user-r";
        original_document_file = granted_file_path(original_file_name);
        run_live_mcp(window, endpoint, access_key, file_name, original_file_name);

        const std::string billboard_file_name = "bien_hieu_quang_cao_5x2m_e2e-" +
            std::to_string(GetTickCount64());
        billboard_file = granted_file_path(billboard_file_name);
        run_billboard_5x2m_design(window, endpoint, access_key, billboard_file_name);

        BYTE keys[256] = {};
        SetKeyboardState(keys);
        dismiss_recovery_modal(window);
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
    if (!original_document_file.empty()) {
        std::error_code cleanup_error;
        std::filesystem::remove(original_document_file, cleanup_error);
        if (cleanup_error && failure.empty())
            failure = "generated original-document E2E file cleanup failed: " + cleanup_error.message();
    }
    if (!billboard_file.empty()) {
        std::error_code cleanup_error;
        std::filesystem::remove(billboard_file, cleanup_error);
        if (cleanup_error && failure.empty())
            failure = "generated billboard E2E file cleanup failed: " + cleanup_error.message();
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
