#include "pikdraw/com_server/com_ids.hpp"

#ifdef _WIN32

#include <windows.h>
#include <objbase.h>
#include <oleauto.h>

#include <iostream>
#include <string>

namespace {

bool invoke_json(IDispatch* dispatch, const wchar_t* method, VARIANTARG* arguments,
                 UINT argument_count, std::wstring& output) {
    LPOLESTR mutable_name = const_cast<LPOLESTR>(method);
    DISPID id = DISPID_UNKNOWN;
    HRESULT result = dispatch->GetIDsOfNames(IID_NULL, &mutable_name, 1,
        LOCALE_USER_DEFAULT, &id);
    if (FAILED(result)) return false;
    DISPPARAMS parameters{arguments, nullptr, argument_count, 0};
    VARIANT returned;
    VariantInit(&returned);
    result = dispatch->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD,
        &parameters, &returned, nullptr, nullptr);
    if (FAILED(result) || V_VT(&returned) != VT_BSTR) {
        VariantClear(&returned);
        return false;
    }
    output.assign(V_BSTR(&returned), SysStringLen(V_BSTR(&returned)));
    VariantClear(&returned);
    return true;
}

}

int run_client(DWORD apartment, const char* apartment_name) {
    const HRESULT initialized = CoInitializeEx(nullptr, apartment);
    if (FAILED(initialized)) return 1;
    IDispatch* dispatch = nullptr;
    const HRESULT activated = CoCreateInstance(pikdraw::com_server::kPikDrawAutomationClsid,
        nullptr, CLSCTX_LOCAL_SERVER, IID_IDispatch, reinterpret_cast<void**>(&dispatch));
    if (FAILED(activated) || !dispatch) {
        std::cerr << apartment_name << " CoCreateInstance failed: 0x" << std::hex << activated << '\n';
        CoUninitialize();
        return 2;
    }

    std::wstring tools;
    if (!invoke_json(dispatch, L"GetTools", nullptr, 0, tools) ||
        tools.find(L"pikdraw.batch.apply") == std::wstring::npos ||
        tools.find(L"pikdraw.document.validate") == std::wstring::npos) {
        std::cerr << apartment_name << " GetTools did not return the semantic MCP capability list.\n";
        dispatch->Release();
        CoUninitialize();
        return 3;
    }

    VARIANTARG arguments[2];
    VariantInit(&arguments[0]);
    VariantInit(&arguments[1]);
    V_VT(&arguments[0]) = VT_BSTR;
    V_BSTR(&arguments[0]) = SysAllocString(L"{}");
    V_VT(&arguments[1]) = VT_BSTR;
    V_BSTR(&arguments[1]) = SysAllocString(L"pikdraw.instances");
    std::wstring instances;
    const bool called = invoke_json(dispatch, L"CallTool", arguments, 2, instances) &&
                        instances.find(L"\"status\":\"ok\"") != std::wstring::npos;
    VariantClear(&arguments[0]);
    VariantClear(&arguments[1]);
    dispatch->Release();
    CoUninitialize();
    if (!called) {
        std::cerr << apartment_name << " CallTool failed to return the MCP instances result.\n";
        return 4;
    }
    return 0;
}

int main() {
    const int sta = run_client(COINIT_APARTMENTTHREADED, "STA");
    if (sta != 0) return sta;
    const int mta = run_client(COINIT_MULTITHREADED, "MTA");
    if (mta != 0) return mta;
    std::cout << "COM local activation, STA/MTA clients, GetTools, and CallTool passed.\n";
    return 0;
}

#else
int main() { return 0; }
#endif
