#include "pikdraw/com_server/automation_dispatch.hpp"

#ifdef _WIN32

#include "native_window_automation.hpp"
#include "pikdraw/automation/json_value.hpp"
#include "pikdraw/desktop/shell/native_window.hpp"

#include <windows.h>
#include <objbase.h>
#include <oleauto.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using pikdraw::automation::JsonValue;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    require(count > 0, "UTF-8 argument can be converted to UTF-16");
    std::wstring result(static_cast<size_t>(count), L'\0');
    require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), count) == count,
        "UTF-8 argument conversion completes");
    return result;
}

std::string bstr_to_utf8(BSTR value) {
    const UINT length = SysStringLen(value);
    if (length == 0) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value,
        static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    require(count > 0, "COM BSTR response can be converted to UTF-8");
    std::string result(static_cast<size_t>(count), '\0');
    require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value,
        static_cast<int>(length), result.data(), count, nullptr, nullptr) == count,
        "COM BSTR response conversion completes");
    return result;
}

JsonValue parse_json(const std::string& encoded) {
    JsonValue result;
    std::string error;
    require(JsonValue::parse(encoded, result, error), "COM returned valid JSON: " + error);
    return result;
}

JsonValue invoke(IDispatch* dispatch, const wchar_t* method,
                 const std::vector<std::wstring>& arguments = {}) {
    LPOLESTR name = const_cast<LPOLESTR>(method);
    DISPID id = DISPID_UNKNOWN;
    require(SUCCEEDED(dispatch->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &id)),
        "COM method name resolves");

    std::vector<VARIANTARG> values(arguments.size());
    for (size_t index = 0; index < arguments.size(); ++index) {
        VariantInit(&values[index]);
        V_VT(&values[index]) = VT_BSTR;
        V_BSTR(&values[index]) = SysAllocStringLen(arguments[index].data(),
            static_cast<UINT>(arguments[index].size()));
        require(V_BSTR(&values[index]) != nullptr, "COM argument BSTR allocates");
    }
    DISPPARAMS parameters{values.data(), nullptr, static_cast<UINT>(values.size()), 0};
    VARIANT result;
    VariantInit(&result);
    const HRESULT called = dispatch->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD,
        &parameters, &result, nullptr, nullptr);
    for (auto& value : values) VariantClear(&value);
    require(SUCCEEDED(called) && V_VT(&result) == VT_BSTR,
        "COM method returns a JSON BSTR");
    JsonValue parsed = parse_json(bstr_to_utf8(V_BSTR(&result)));
    VariantClear(&result);
    return parsed;
}

JsonValue call_tool(IDispatch* dispatch, std::string_view name,
                    JsonValue::Object arguments = {}) {
    const auto encoded_name = utf8_to_wide(std::string(name));
    const auto encoded_arguments = utf8_to_wide(JsonValue(std::move(arguments)).serialize());
    auto response = invoke(dispatch, L"CallTool", {encoded_arguments, encoded_name});
    const auto* structured = response.get("structuredContent");
    require(structured && structured->object(), "COM CallTool preserves MCP structuredContent");
    return *structured;
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

std::filesystem::path automation_file_path(const std::string& file_name) {
    DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    require(size > 0, "LOCALAPPDATA is available for test cleanup");
    std::wstring base(size, L'\0');
    const DWORD copied = GetEnvironmentVariableW(L"LOCALAPPDATA", base.data(), size);
    require(copied > 0 && copied < size, "LOCALAPPDATA is read for test cleanup");
    base.resize(copied);
    return std::filesystem::path(base) / L"PikDraw" / L"automation" / L"files" /
        (utf8_to_wide(file_name) + L".pikdraw");
}

void exercise_com_client(DWORD ui_thread, std::string& failure, std::string file_name) {
    bool initialized = false;
    IDispatch* dispatch = nullptr;
    try {
        const HRESULT com_status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        require(SUCCEEDED(com_status), "COM test client initializes in MTA");
        initialized = true;
        require(SUCCEEDED(pikdraw::com_server::create_automation_dispatch(&dispatch)) && dispatch,
            "MCP-backed COM dispatch object is created without registry changes");

        const auto tools = invoke(dispatch, L"GetTools");
        require(tools.array() && tools.array()->size() == 20,
            "COM exposes the current semantic MCP tool catalog");
        const auto has_tool = [&](std::string_view expected) {
            return std::any_of(tools.array()->begin(), tools.array()->end(), [&](const JsonValue& tool) {
                const auto* name = tool.get("name");
                return name && name->string() && *name->string() == expected;
            });
        };
        require(has_tool("pikdraw.objects.get") && has_tool("pikdraw.history.undo") &&
                has_tool("pikdraw.history.redo") && has_tool("pikdraw.file.open"),
            "COM exposes object lookup, guarded history and file.open from the shared MCP catalog");

        const auto instances = invoke(dispatch, L"GetInstances");
        require_ok(instances, "GetInstances");
        const auto* entries = instances.get("instances");
        require(entries && entries->array(), "instance discovery returns an array");
        std::string instance_id;
        const auto current_pid = std::to_string(GetCurrentProcessId());
        for (const auto& entry : *entries->array()) {
            const auto* id = entry.get("instance_id");
            if (id && id->string() && *id->string() == current_pid) instance_id = *id->string();
        }
        require(!instance_id.empty(), "COM discovers the fixture's opted-in desktop instance");

        JsonValue::Array read_only_scopes{JsonValue("document.read")};
        JsonValue::Array scopes{JsonValue("document.read"), JsonValue("document.create"),
            JsonValue("document.edit"), JsonValue("file.read"), JsonValue("file.write")};
        const auto first_session = call_tool(dispatch, "pikdraw.connect",
            { {"instance_id", instance_id}, {"scopes", read_only_scopes} });
        require_ok(first_session, "initial session creation");
        const auto first_handle = string_value(first_session, "session_handle");

        const auto denied_create = call_tool(dispatch, "pikdraw.document.create", {
            {"session_handle", first_handle}, {"title", "Read-only must not create"},
            {"page_width_mm", 600.0}, {"page_height_mm", 300.0}});
        require(string_value(denied_create, "code") == "SCOPE_DENIED",
            "read-only COM session cannot create a document");

        const auto create_session = call_tool(dispatch, "pikdraw.connect",
            {{"instance_id", instance_id}, {"scopes", scopes}});
        require_ok(create_session, "document-create session creation");
        const auto create_handle = string_value(create_session, "session_handle");
        const auto created = call_tool(dispatch, "pikdraw.document.create", {
            {"session_handle", create_handle}, {"title", "MCP COM · CỬA HÀNG ÁNH DƯƠNG"},
            {"page_width_mm", 600.0}, {"page_height_mm", 300.0}});
        require_ok(created, "automation document creation");
        const auto document_id = string_value(created, "document_id");
        require(!document_id.empty(), "new automation document has an identity");

        const auto session = call_tool(dispatch, "pikdraw.connect",
            {{"instance_id", instance_id}, {"scopes", scopes}});
        require_ok(session, "session creation for the new document");
        const auto session_handle = string_value(session, "session_handle");
        const auto inspected = call_tool(dispatch, "pikdraw.document.inspect",
            {{"session_handle", session_handle}});
        require_ok(inspected, "new document inspection");
        require(string_value(inspected, "document_id") == document_id &&
                *inspected.get("page_width_mm")->number() == 600.0 &&
                *inspected.get("page_height_mm")->number() == 300.0,
            "COM connects to the created 600 by 300 mm document");

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
        const auto validation = call_tool(dispatch, "pikdraw.batch.validate", {
            {"session_handle", session_handle}, {"expected_epoch", epoch},
            {"expected_revision", revision}, {"operations", operations}});
        require_ok(validation, "batch validation");
        require(validation.get("valid") && *validation.get("valid")->boolean(),
            "design batch validates before editing");

        const std::string request_id = "com-runtime-e2e-" + std::to_string(GetTickCount64());
        const std::string idempotency_key = request_id + "-key";
        JsonValue::Object apply_arguments{{"session_handle", session_handle},
            {"request_id", request_id}, {"idempotency_key", idempotency_key},
            {"expected_epoch", epoch}, {"expected_revision", revision}, {"operations", operations}};
        const auto applied = call_tool(dispatch, "pikdraw.batch.apply", apply_arguments);
        require_ok(applied, "atomic design batch apply");
        const auto* created_ids = applied.get("created_ids");
        require(created_ids && created_ids->array() && created_ids->array()->size() == 3,
            "COM batch apply returns IDs for all editable objects");
        const auto committed_revision = string_value(applied, "revision_after");

        const auto repeated = call_tool(dispatch, "pikdraw.batch.apply", apply_arguments);
        require_ok(repeated, "idempotent batch retry");
        require(string_value(repeated, "revision_after") == committed_revision &&
                repeated.get("created_ids") && repeated.get("created_ids")->array()->size() == 3,
            "retry returns the committed receipt without another document revision");
        const auto receipt = call_tool(dispatch, "pikdraw.request.get", {
            {"session_handle", session_handle}, {"idempotency_key", idempotency_key}});
        require_ok(receipt, "write receipt query");

        const auto objects = call_tool(dispatch, "pikdraw.objects.query",
            {{"session_handle", session_handle}});
        require_ok(objects, "object query after commit");
        const auto* queried_objects = objects.get("objects");
        bool found_unicode_text = false;
        if (queried_objects && queried_objects->array()) {
            for (const auto& object : *queried_objects->array()) {
                const auto* text = object.get("text");
                if (text && text->string() && *text->string() == "CỬA HÀNG ÁNH DƯƠNG")
                    found_unicode_text = true;
            }
        }
        require(found_unicode_text, "query returns the committed Unicode text object");
        const auto document_validation = call_tool(dispatch, "pikdraw.document.validate",
            {{"session_handle", session_handle}});
        require_ok(document_validation, "document validation");
        require(document_validation.get("valid") && *document_validation.get("valid")->boolean(),
            "completed design passes supported document validation");

        const auto saved = call_tool(dispatch, "pikdraw.file.save",
            {{"session_handle", session_handle}, {"file_name", file_name}});
        require_ok(saved, "native document save");
        const auto reopened = call_tool(dispatch, "pikdraw.file.verify_reopen",
            {{"session_handle", session_handle}, {"file_name", file_name}});
        require_ok(reopened, "native file verify-reopen");
        require(reopened.get("reopened") && *reopened.get("reopened")->boolean() &&
                reopened.get("object_count") && *reopened.get("object_count")->number() == 3.0 &&
                string_value(reopened, "document_id") == document_id &&
                reopened.get("page_width_mm") && *reopened.get("page_width_mm")->number() == 600.0,
            "saved native file reopens with source identity, page size and all three objects");

        const auto read_only_open = call_tool(dispatch, "pikdraw.file.open",
            {{"session_handle", first_handle}, {"file_name", file_name}});
        require(string_value(read_only_open, "code") == "SCOPE_DENIED",
            "file.open requires the session to hold file.read");

        const auto opened = call_tool(dispatch, "pikdraw.file.open",
            {{"session_handle", session_handle}, {"file_name", file_name}});
        require_ok(opened, "open granted native file into a new tab");
        const auto opened_document_id = string_value(opened, "document_id");
        require(opened_document_id != document_id &&
                opened.get("tab_count") && *opened.get("tab_count")->number() == 3.0 &&
                opened.get("object_count") && *opened.get("object_count")->number() == 3.0 &&
                opened.get("dirty") && !*opened.get("dirty")->boolean() &&
                opened.get("page_width_mm") && *opened.get("page_width_mm")->number() == 600.0,
            "file.open creates exactly one clean, separate tab and preserves native page/object content");
        const auto opened_session = call_tool(dispatch, "pikdraw.connect", {
            {"instance_id", instance_id}, {"scopes", scopes}});
        require_ok(opened_session, "connect to the newly opened native document");
        const auto opened_handle = string_value(opened_session, "session_handle");
        const auto opened_inspect = call_tool(dispatch, "pikdraw.document.inspect",
            {{"session_handle", opened_handle}});
        require_ok(opened_inspect, "inspect opened tab");
        require(string_value(opened_inspect, "document_id") == opened_document_id &&
                string_value(opened_inspect, "document_id") != document_id &&
                opened_inspect.get("page_width_mm") && *opened_inspect.get("page_width_mm")->number() == 600.0,
            "reconnect targets the new tab while the original document session remains distinct");
        const auto opened_objects = call_tool(dispatch, "pikdraw.objects.query",
            {{"session_handle", opened_handle}});
        require_ok(opened_objects, "query objects in opened tab");
        require(opened_objects.get("objects") && opened_objects.get("objects")->array() &&
                opened_objects.get("objects")->array()->size() == 3,
            "opened tab retains all three stable native objects");
        const auto original_inspect = call_tool(dispatch, "pikdraw.document.inspect",
            {{"session_handle", session_handle}});
        require_ok(original_inspect, "verify original document remains connected");
        require(string_value(original_inspect, "document_id") == document_id,
            "opening a file does not replace or retarget the existing document session");

        require_ok(call_tool(dispatch, "pikdraw.disconnect", {{"session_handle", opened_handle}}),
            "opened document session disconnect");
        require_ok(call_tool(dispatch, "pikdraw.disconnect", {{"session_handle", session_handle}}),
            "new document session disconnect");
        require_ok(call_tool(dispatch, "pikdraw.disconnect", {{"session_handle", first_handle}}),
            "initial document session disconnect");
    } catch (const std::exception& error) {
        failure = error.what();
    }
    if (dispatch) dispatch->Release();
    if (initialized) CoUninitialize();
    PostThreadMessageW(ui_thread, WM_QUIT, 0, 0);
}

} // namespace

int main() {
    const DWORD ui_thread = GetCurrentThreadId();
    MSG queue_ready{};
    PeekMessageW(&queue_ready, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    constexpr wchar_t class_name[] = L"PikDrawComRuntimeE2EWindow";
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = class_name;
    const ATOM atom = RegisterClassW(&window_class);
    require(atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS,
        "test message window class registers");
    HWND host_window = CreateWindowExW(0, class_name, L"", 0, 0, 0, 1, 1,
        HWND_MESSAGE, nullptr, window_class.hInstance, nullptr);
    require(host_window != nullptr, "hidden UI-owner message window is created");

    pikdraw::desktop::NativeWindow window(800, 600);
    pikdraw::desktop::NativeWindowAutomationRuntime runtime;
    runtime.granted_scopes = {"document.read", "document.create", "document.edit",
                              "file.read", "file.write"};
    std::string pipe_error;
    require(runtime.pipe.start(host_window, [&window, &runtime](const JsonValue& request) {
        return runtime.handle(window, request);
    }, "read,edit,create,file-read,file-write", pipe_error),
        "same-user desktop automation pipe starts: " + pipe_error);

    const std::string file_name = "com-runtime-e2e-" + std::to_string(GetTickCount64());
    std::string client_failure;
    std::thread client(exercise_com_client, ui_thread, std::ref(client_failure), file_name);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    client.join();
    runtime.pipe.stop();
    DestroyWindow(host_window);
    UnregisterClassW(class_name, window_class.hInstance);

    std::error_code cleanup_error;
    std::filesystem::remove(automation_file_path(file_name), cleanup_error);
    require(!cleanup_error, "generated native test file is removed");
    if (!client_failure.empty()) {
        std::cerr << "COM-to-desktop automation E2E failed: " << client_failure << '\n';
        return 1;
    }
    std::cout << "COM IDispatch -> MCP tools -> same-user UI-owner pipe -> document writer -> native save/reopen passed.\n";
    return 0;
}

#else
int main() { return 0; }
#endif
