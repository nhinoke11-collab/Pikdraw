#include "automation_pipe_host.hpp"
#include "native_window_automation.hpp"
#include "pikdraw/automation/json_value.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

#ifdef _WIN32
bool transfer(HANDLE pipe, void* data, DWORD size, bool writing) {
    auto* bytes = static_cast<std::byte*>(data);
    DWORD done = 0;
    while (done < size) {
        DWORD amount = 0;
        const BOOL ok = writing ? WriteFile(pipe, bytes + done, size - done, &amount, nullptr)
                                : ReadFile(pipe, bytes + done, size - done, &amount, nullptr);
        if (!ok || amount == 0) return false;
        done += amount;
    }
    return true;
}

bool send_request(const std::wstring& name, DWORD ui_thread, std::string& response) {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (int i = 0; i < 250; ++i) {
        if (WaitNamedPipeW(name.c_str(), 50)) {
            pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, SECURITY_SQOS_PRESENT | SECURITY_IMPERSONATION, nullptr);
            if (pipe != INVALID_HANDLE_VALUE) break;
        }
        Sleep(20);
    }
    if (pipe == INVALID_HANDLE_VALUE) { PostThreadMessageW(ui_thread, WM_QUIT, 0, 0); return false; }
    const std::string body = R"({"operation":"test.echo","unicode":"Của"})";
    const auto size = static_cast<uint32_t>(body.size());
    uint8_t frame[4]{static_cast<uint8_t>(size), static_cast<uint8_t>(size >> 8),
                     static_cast<uint8_t>(size >> 16), static_cast<uint8_t>(size >> 24)};
    bool ok = transfer(pipe, frame, 4, true) &&
              transfer(pipe, const_cast<char*>(body.data()), size, true) &&
              transfer(pipe, frame, 4, false);
    const uint32_t reply_size = static_cast<uint32_t>(frame[0]) |
        (static_cast<uint32_t>(frame[1]) << 8) | (static_cast<uint32_t>(frame[2]) << 16) |
        (static_cast<uint32_t>(frame[3]) << 24);
    if (ok && reply_size > 0 && reply_size < 64 * 1024) {
        response.resize(reply_size);
        ok = transfer(pipe, response.data(), reply_size, false);
    } else ok = false;
    CloseHandle(pipe);
    PostThreadMessageW(ui_thread, WM_QUIT, 0, 0);
    return ok;
}
#endif

void test_agent_document_identity() {
    using pikdraw::automation::JsonValue;
    using pikdraw::desktop::NativeWindow;
    using pikdraw::desktop::NativeWindowAutomationRuntime;

    NativeWindow window(800, 600);
    NativeWindowAutomationRuntime runtime;
    runtime.granted_scopes = {"document.read", "document.create", "document.edit",
                              "file.read", "file.write"};
    JsonValue read_session_request;
    std::string parse_error;
    check(JsonValue::parse(
        R"({"operation":"session.create","scopes":["document.read"]})",
        read_session_request, parse_error), "read-only session request parses");

    const auto read_session = runtime.handle(window, read_session_request);
    const auto* read_handle = read_session.get("session_handle");
    check(read_handle && read_handle->string() && !read_handle->string()->empty(),
          "read-only session is created on the existing document");

    JsonValue denied_request(JsonValue::Object{{"operation", "document.create"},
        {"session_handle", *read_handle->string()}, {"title", "Should be denied"},
        {"page_width_mm", 600}, {"page_height_mm", 300}});
    const auto denied = runtime.handle(window, denied_request);
    check(denied.get("code") && denied.get("code")->string() &&
              *denied.get("code")->string() == "SCOPE_DENIED" && window.tabs().size() == 1,
          "read-only session cannot create a document even when the instance grants document.create");

    JsonValue edit_session_request;
    check(JsonValue::parse(
        R"({"operation":"session.create","scopes":["document.read","document.create"]})",
        edit_session_request, parse_error), "document-create session request parses");
    const auto edit_session = runtime.handle(window, edit_session_request);
    const auto* edit_handle = edit_session.get("session_handle");
    check(edit_handle && edit_handle->string() && !edit_handle->string()->empty(),
          "document-create session is created on the original document");
    auto create_request = [&] {
        return JsonValue(JsonValue::Object{{"operation", "document.create"},
            {"session_handle", *edit_handle->string()}, {"title", "Agent design"},
            {"page_width_mm", 600}, {"page_height_mm", 300}});
    };

    const auto first = runtime.handle(window, create_request());
    const auto second = runtime.handle(window, create_request());
    const auto* first_id = first.get("document_id");
    const auto* second_id = second.get("document_id");
    check(first.get("status") && first.get("status")->string() &&
              *first.get("status")->string() == "ok" && first_id && first_id->string() &&
              !first_id->string()->empty(),
          "first agent document is created with a stable identity");
    check(second.get("status") && second.get("status")->string() &&
              *second.get("status")->string() == "ok" && second_id && second_id->string() &&
              !second_id->string()->empty() && *first_id->string() != *second_id->string(),
          "separate agent documents receive distinct identities");
    check(window.tabs().size() == 3, "agent document creation preserves the original tab");
}
}

int main() {
#ifdef _WIN32
    using pikdraw::automation::JsonValue;
    using pikdraw::desktop::AutomationPipeHost;
    const DWORD ui_thread = GetCurrentThreadId();
    MSG queue_ready{};
    PeekMessageW(&queue_ready, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    constexpr wchar_t class_name[] = L"PikDrawAutomationPipeTestWindow";
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = class_name;
    RegisterClassW(&wc);
    HWND window = CreateWindowExW(0, class_name, L"", 0, 0, 0, 1, 1,
                                  HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    check(window != nullptr, "test message window is created");

    std::atomic<bool> handled{false};
    std::atomic<bool> owner_thread{false};
    AutomationPipeHost host;
    std::string error;
    check(host.start(window, [&](const JsonValue& request) {
        handled.store(request.get("unicode") && request.get("unicode")->string() &&
                      *request.get("unicode")->string() == "Của", std::memory_order_release);
        owner_thread.store(GetCurrentThreadId() == ui_thread, std::memory_order_release);
        return JsonValue::Object{{"status", "ok"}, {"echo", *request.get("unicode")->string()}};
    }, "read", error), "per-user local pipe host starts");

    std::string response;
    bool client_ok = false;
    std::thread client([&] { client_ok = send_request(host.pipe_name(), ui_thread, response); });
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    client.join();
    host.stop();
    DestroyWindow(window);

    JsonValue reply; std::string parse_error;
    if (!client_ok || !JsonValue::parse(response, reply, parse_error))
        std::cerr << "Pipe client failed=" << client_ok << " parse=" << parse_error
                  << " raw=" << response << '\n';
    check(client_ok && JsonValue::parse(response, reply, parse_error), "named-pipe client receives a valid response");
    check(handled.load(std::memory_order_acquire) && owner_thread.load(std::memory_order_acquire),
          "request handler executes on the document owner UI thread and preserves UTF-8");
    check(reply.get("status") && *reply.get("status")->string() == "ok" &&
              *reply.get("echo")->string() == "Của",
          "named-pipe response returns the handler result");
    test_agent_document_identity();
#endif
    return 0;
}
