#include "pikdraw/com_server/automation_dispatch.hpp"

#ifdef _WIN32

#include "pikdraw/automation/json_value.hpp"

#include <windows.h>
#include <oleauto.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
using pikdraw::automation::JsonValue;

void check(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::string to_utf8(BSTR value) {
    const UINT length = SysStringLen(value);
    if (length == 0) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value,
        static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string result(static_cast<size_t>(needed), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value,
            static_cast<int>(length), result.data(), needed, nullptr, nullptr) != needed) return {};
    return result;
}

JsonValue parse_bstr(BSTR value) {
    JsonValue parsed;
    std::string error;
    const auto json = to_utf8(value);
    check(!json.empty() && JsonValue::parse(json, parsed, error), "COM output is valid UTF-8 JSON");
    return parsed;
}

HRESULT get_dispid(IDispatch* dispatch, const wchar_t* name, DISPID& id) {
    LPOLESTR mutable_name = const_cast<LPOLESTR>(name);
    return dispatch->GetIDsOfNames(IID_NULL, &mutable_name, 1, LOCALE_USER_DEFAULT, &id);
}

HRESULT invoke_no_args(IDispatch* dispatch, DISPID id, VARIANT& result) {
    DISPPARAMS parameters{};
    return dispatch->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD,
        &parameters, &result, nullptr, nullptr);
}

}

int main() {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    check(SUCCEEDED(initialized), "COM test client initializes in STA");

    IDispatch* dispatch = nullptr;
    check(SUCCEEDED(pikdraw::com_server::create_automation_dispatch(&dispatch)),
          "dispatch object can be created without touching the registry");

    UINT type_info_count = 1;
    check(SUCCEEDED(dispatch->GetTypeInfoCount(&type_info_count)) && type_info_count == 0,
          "late-binding interface intentionally has no registered type library");

    DISPID get_instances = DISPID_UNKNOWN;
    DISPID get_tools = DISPID_UNKNOWN;
    DISPID call_tool = DISPID_UNKNOWN;
    check(SUCCEEDED(get_dispid(dispatch, L"getinstances", get_instances)) && get_instances == 1,
          "method lookup is case-insensitive for GetInstances");
    check(SUCCEEDED(get_dispid(dispatch, L"GetTools", get_tools)) && get_tools == 2,
          "GetTools has a stable DISPID");
    check(SUCCEEDED(get_dispid(dispatch, L"CallTool", call_tool)) && call_tool == 3,
          "CallTool has a stable DISPID");
    DISPID missing = DISPID_UNKNOWN;
    check(get_dispid(dispatch, L"ExecuteScript", missing) == DISP_E_UNKNOWNNAME,
          "unknown arbitrary-execution methods are not exposed");

    VARIANT output;
    VariantInit(&output);
    check(SUCCEEDED(invoke_no_args(dispatch, get_tools, output)) && V_VT(&output) == VT_BSTR,
          "GetTools returns JSON as BSTR");
    const auto tools = parse_bstr(V_BSTR(&output));
    check(tools.array() && tools.array()->size() == 20,
          "COM advertises exactly the same implemented MCP tools");
    bool has_batch = false;
    bool has_object_get = false;
    bool has_history_undo = false;
    bool has_history_redo = false;
    bool has_file_open = false;
    for (const auto& tool : *tools.array()) {
        const auto* name = tool.get("name");
        if (!name || !name->string()) continue;
        if (*name->string() == "pikdraw.batch.apply") has_batch = true;
        if (*name->string() == "pikdraw.objects.get") has_object_get = true;
        if (*name->string() == "pikdraw.history.undo") has_history_undo = true;
        if (*name->string() == "pikdraw.history.redo") has_history_redo = true;
        if (*name->string() == "pikdraw.file.open") has_file_open = true;
    }
    check(has_batch && has_object_get && has_history_undo && has_history_redo && has_file_open,
          "COM discovers the expanded object lookup, guarded history, typed batch and safe file.open operations");
    VariantClear(&output);

    VariantInit(&output);
    check(SUCCEEDED(invoke_no_args(dispatch, get_instances, output)) && V_VT(&output) == VT_BSTR,
          "GetInstances returns JSON as BSTR");
    const auto instances = parse_bstr(V_BSTR(&output));
    check(instances.get("status") && instances.get("status")->string() &&
              *instances.get("status")->string() == "ok" && instances.get("instances") &&
              instances.get("instances")->array(),
          "instance discovery uses the MCP adapter schema");
    VariantClear(&output);

    VARIANT arguments[2];
    VariantInit(&arguments[0]);
    VariantInit(&arguments[1]);
    V_VT(&arguments[0]) = VT_BSTR;
    V_BSTR(&arguments[0]) = SysAllocString(L"{}");
    V_VT(&arguments[1]) = VT_BSTR;
    V_BSTR(&arguments[1]) = SysAllocString(L"pikdraw.instances");
    DISPPARAMS call_parameters{arguments, nullptr, 2, 0};
    VariantInit(&output);
    check(SUCCEEDED(dispatch->Invoke(call_tool, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD,
              &call_parameters, &output, nullptr, nullptr)) && V_VT(&output) == VT_BSTR,
          "CallTool forwards an MCP tool call");
    const auto call_result = parse_bstr(V_BSTR(&output));
    check(call_result.get("structuredContent") &&
              call_result.get("structuredContent")->get("status") &&
              *call_result.get("structuredContent")->get("status")->string() == "ok",
          "CallTool preserves the MCP structured result and app scopes");
    VariantClear(&output);
    VariantClear(&arguments[0]);
    VariantClear(&arguments[1]);

    V_VT(&arguments[0]) = VT_BSTR;
    V_BSTR(&arguments[0]) = SysAllocString(L"[]");
    V_VT(&arguments[1]) = VT_BSTR;
    V_BSTR(&arguments[1]) = SysAllocString(L"pikdraw.instances");
    call_parameters.rgvarg = arguments;
    check(dispatch->Invoke(call_tool, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD,
              &call_parameters, nullptr, nullptr, nullptr) == E_INVALIDARG,
          "CallTool rejects non-object JSON arguments before reaching the app");
    VariantClear(&arguments[0]);
    VariantClear(&arguments[1]);

    dispatch->Release();
    CoUninitialize();
    return 0;
}

#else
int main() { return 0; }
#endif
