#include "automation_pipe_host.hpp"
#include "native_window_automation.hpp"
#include "pikdraw/automation/json_value.hpp"

#include <atomic>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
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
    const auto typed_created = runtime.handle(window, JsonValue(JsonValue::Object{
        {"operation", "document.create"}, {"session_handle", *edit_handle->string()},
        {"title", "Agent inch document"}, {"page_width", 10.0},
        {"page_height", 12.0}, {"length_unit", "in"}}));
    const auto* typed_width = typed_created.get("page_width_mm");
    const auto* typed_height = typed_created.get("page_height_mm");
    check(typed_created.get("status") && typed_created.get("status")->string() &&
              *typed_created.get("status")->string() == "ok" && typed_width && typed_width->number() &&
              typed_height && typed_height->number() &&
              std::abs(*typed_width->number() - 254.0) < 1e-9 &&
              std::abs(*typed_height->number() - 304.8) < 1e-9,
          "AC-UNIT11 document.create converts typed inches to canonical millimeters");
    check(window.tabs().size() == 4, "typed document creation preserves every existing tab");

    struct UnitFixture {
        std::string_view unit;
        double width;
        double height;
    };
    constexpr std::array<UnitFixture, 8> physical_units{{
        {"mm", 254.0, 304.8}, {"cm", 25.4, 30.48}, {"m", 0.254, 0.3048},
        {"in", 10.0, 12.0}, {"ft", 10.0 / 12.0, 1.0},
        {"yd", 10.0 / 36.0, 1.0 / 3.0}, {"pt", 720.0, 864.0}, {"pc", 60.0, 72.0}}};
    bool all_units_resolve_to_canonical_mm = true;
    for (const auto& fixture : physical_units) {
        const auto converted = runtime.handle(window, JsonValue(JsonValue::Object{
            {"operation", "document.create"}, {"session_handle", *edit_handle->string()},
            {"page_width", fixture.width}, {"page_height", fixture.height},
            {"length_unit", std::string(fixture.unit)}}));
        const auto* width_mm = converted.get("page_width_mm");
        const auto* height_mm = converted.get("page_height_mm");
        all_units_resolve_to_canonical_mm = all_units_resolve_to_canonical_mm &&
            converted.get("status") && converted.get("status")->string() &&
            *converted.get("status")->string() == "ok" && width_mm && width_mm->number() &&
            height_mm && height_mm->number() && std::abs(*width_mm->number() - 254.0) < 1e-8 &&
            std::abs(*height_mm->number() - 304.8) < 1e-8;
    }
    check(all_units_resolve_to_canonical_mm,
          "AC-UNIT11 document.create accepts all eight physical units and returns canonical millimeters");

    const auto tabs_before_invalid = window.tabs().size();
    const auto rejected_pixel = runtime.handle(window, JsonValue(JsonValue::Object{
        {"operation", "document.create"}, {"session_handle", *edit_handle->string()},
        {"page_width", 10.0}, {"page_height", 12.0}, {"length_unit", "px"}}));
    check(rejected_pixel.get("code") && rejected_pixel.get("code")->string() &&
              *rejected_pixel.get("code")->string() == "INVALID_ARGUMENT" &&
              window.tabs().size() == tabs_before_invalid,
          "AC-UNIT11 document.create rejects pixel dimensions without an explicit DPI basis");

    const auto rejected_mixed = runtime.handle(window, JsonValue(JsonValue::Object{
        {"operation", "document.create"}, {"session_handle", *edit_handle->string()},
        {"page_width_mm", 254.0}, {"page_height_mm", 304.8},
        {"page_width", 10.0}, {"page_height", 12.0}, {"length_unit", "in"}}));
    check(rejected_mixed.get("code") && rejected_mixed.get("code")->string() &&
              *rejected_mixed.get("code")->string() == "INVALID_ARGUMENT" &&
              window.tabs().size() == tabs_before_invalid,
          "AC-UNIT11 document.create rejects mixed mm and typed fields without opening a tab");

    const auto rejected_unknown_unit = runtime.handle(window, JsonValue(JsonValue::Object{
        {"operation", "document.create"}, {"session_handle", *edit_handle->string()},
        {"page_width", 10.0}, {"page_height", 12.0}, {"length_unit", "furlong"}}));
    check(rejected_unknown_unit.get("code") && rejected_unknown_unit.get("code")->string() &&
              *rejected_unknown_unit.get("code")->string() == "INVALID_ARGUMENT" &&
              window.tabs().size() == tabs_before_invalid,
          "AC-UNIT11 document.create rejects unknown units without opening a tab");

    const auto rejected_small_page = runtime.handle(window, JsonValue(JsonValue::Object{
        {"operation", "document.create"}, {"session_handle", *edit_handle->string()},
        {"page_width", 0.1}, {"page_height", 12.0}, {"length_unit", "in"}}));
    check(rejected_small_page.get("code") && rejected_small_page.get("code")->string() &&
              *rejected_small_page.get("code")->string() == "INVALID_ARGUMENT" &&
              window.tabs().size() == tabs_before_invalid,
          "AC-UNIT11 document.create enforces page bounds after canonical conversion");

    const auto rejected_invalid_title = runtime.handle(window, JsonValue(JsonValue::Object{
        {"operation", "document.create"}, {"session_handle", *edit_handle->string()},
        {"title", 42}, {"page_width_mm", 254.0}, {"page_height_mm", 304.8}}));
    check(rejected_invalid_title.get("code") && rejected_invalid_title.get("code")->string() &&
              *rejected_invalid_title.get("code")->string() == "INVALID_ARGUMENT" &&
              window.tabs().size() == tabs_before_invalid,
          "document.create rejects non-string titles without opening a tab");
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
