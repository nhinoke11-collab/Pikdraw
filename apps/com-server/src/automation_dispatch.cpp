#include "pikdraw/com_server/automation_dispatch.hpp"

#ifdef _WIN32

#include "pikdraw/mcp_server.hpp"

#include <windows.h>
#include <oleauto.h>

#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace pikdraw::com_server {
namespace {
using automation::JsonValue;
constexpr size_t kMaxJsonBytes = 1024 * 1024;
constexpr std::string_view kProtocolVersion = "2026-07-28";

bool bstr_to_utf8(BSTR input, std::string& output) {
    const UINT length = input ? SysStringLen(input) : 0;
    if (length == 0) {
        output.clear();
        return true;
    }
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input,
        static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (required <= 0 || static_cast<size_t>(required) > kMaxJsonBytes) return false;
    output.resize(static_cast<size_t>(required));
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input,
        static_cast<int>(length), output.data(), required, nullptr, nullptr) == required;
}

bool utf8_to_bstr(std::string_view input, BSTR& output) {
    output = nullptr;
    if (input.size() > std::numeric_limits<int>::max()) return false;
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
        static_cast<int>(input.size()), nullptr, 0);
    if (required < 0) return false;
    output = SysAllocStringLen(nullptr, static_cast<UINT>(required));
    if (!output && required != 0) return false;
    if (required == 0) return true;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
            static_cast<int>(input.size()), output, required) != required) {
        SysFreeString(output);
        output = nullptr;
        return false;
    }
    return true;
}

bool equal_name(LPOLESTR name, const wchar_t* expected) {
    return name && CompareStringOrdinal(name, -1, expected, -1, TRUE) == CSTR_EQUAL;
}

DISPID lookup_name(LPOLESTR name) {
    if (equal_name(name, L"GetInstances")) return 1;
    if (equal_name(name, L"GetTools")) return 2;
    if (equal_name(name, L"CallTool")) return 3;
    return DISPID_UNKNOWN;
}

class AutomationDispatch final : public IDispatch {
public:
    explicit AutomationDispatch(std::function<void()> on_destroy)
        : on_destroy_(std::move(on_destroy)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid == IID_IUnknown || iid == IID_IDispatch) {
            *result = static_cast<IDispatch*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return references_.fetch_add(1) + 1; }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = references_.fetch_sub(1) - 1;
        if (remaining == 0) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override {
        if (!count) return E_POINTER;
        *count = 0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT index, LCID, ITypeInfo** info) override {
        if (!info) return E_POINTER;
        *info = nullptr;
        return index == 0 ? TYPE_E_ELEMENTNOTFOUND : DISP_E_BADINDEX;
    }

    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID iid, LPOLESTR* names, UINT count,
                                              LCID, DISPID* ids) override {
        if (iid != IID_NULL) return DISP_E_UNKNOWNINTERFACE;
        if (!names || !ids) return E_POINTER;
        HRESULT result = S_OK;
        for (UINT index = 0; index < count; ++index) {
            ids[index] = lookup_name(names[index]);
            if (ids[index] == DISPID_UNKNOWN) result = DISP_E_UNKNOWNNAME;
        }
        return result;
    }

    HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID iid, LCID, WORD flags,
                                     DISPPARAMS* parameters, VARIANT* result,
                                     EXCEPINFO* exception, UINT* argument_error) override {
        if (iid != IID_NULL) return DISP_E_UNKNOWNINTERFACE;
        if ((flags & DISPATCH_METHOD) == 0) return DISP_E_MEMBERNOTFOUND;
        if (!parameters) return E_POINTER;
        if (result) VariantInit(result);
        if (exception) ZeroMemory(exception, sizeof(*exception));
        if (argument_error) *argument_error = 0;

        std::string output;
        try {
            std::lock_guard lock(mutex_);
            if (id == 1) {
                if (parameters->cArgs != 0) return DISP_E_BADPARAMCOUNT;
                output = tool_call("pikdraw.instances", JsonValue::Object{}, true);
            } else if (id == 2) {
                if (parameters->cArgs != 0) return DISP_E_BADPARAMCOUNT;
                output = list_tools();
            } else if (id == 3) {
                if (parameters->cArgs != 2) return DISP_E_BADPARAMCOUNT;
                const VARIANTARG& arguments_json = parameters->rgvarg[0];
                const VARIANTARG& tool_name = parameters->rgvarg[1];
                if (arguments_json.vt != VT_BSTR) {
                    if (argument_error) *argument_error = 0;
                    return DISP_E_TYPEMISMATCH;
                }
                if (tool_name.vt != VT_BSTR) {
                    if (argument_error) *argument_error = 1;
                    return DISP_E_TYPEMISMATCH;
                }
                std::string utf8_name;
                std::string utf8_args;
                if (!bstr_to_utf8(tool_name.bstrVal, utf8_name) || utf8_name.empty() ||
                    utf8_name.size() > 128 || !bstr_to_utf8(arguments_json.bstrVal, utf8_args) ||
                    utf8_args.size() > kMaxJsonBytes) return E_INVALIDARG;
                JsonValue parsed_arguments;
                std::string parse_error;
                if (!JsonValue::parse(utf8_args, parsed_arguments, parse_error) ||
                    !parsed_arguments.object()) return E_INVALIDARG;
                output = tool_call(utf8_name, std::move(parsed_arguments), false);
            } else {
                return DISP_E_MEMBERNOTFOUND;
            }
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        } catch (...) {
            return E_UNEXPECTED;
        }

        if (output.size() > kMaxJsonBytes * 4) return DISP_E_OVERFLOW;
        if (!result) return S_OK;
        BSTR encoded = nullptr;
        if (!utf8_to_bstr(output, encoded)) return E_OUTOFMEMORY;
        V_VT(result) = VT_BSTR;
        V_BSTR(result) = encoded;
        return S_OK;
    }

private:
    ~AutomationDispatch() {
        if (on_destroy_) on_destroy_();
    }

    std::string tool_call(std::string_view name, JsonValue arguments, bool structured_only) {
        JsonValue::Object metadata{
            {"io.modelcontextprotocol/protocolVersion", std::string(kProtocolVersion)},
            {"io.modelcontextprotocol/clientCapabilities", JsonValue::Object{}}};
        JsonValue request(JsonValue::Object{
            {"jsonrpc", "2.0"},
            {"id", "com-" + std::to_string(next_request_id_++)},
            {"method", "tools/call"},
            {"params", JsonValue::Object{
                {"name", std::string(name)},
                {"arguments", std::move(arguments)},
                {"_meta", JsonValue(std::move(metadata))}}}});
        const JsonValue response = mcp_.handle(request);
        const JsonValue* tool_result = response.get("result");
        if (!tool_result) return response.serialize();
        if (structured_only) {
            const JsonValue* structured = tool_result->get("structuredContent");
            return structured ? structured->serialize() : tool_result->serialize();
        }
        return tool_result->serialize();
    }

    std::string list_tools() {
        JsonValue::Object metadata{
            {"io.modelcontextprotocol/protocolVersion", std::string(kProtocolVersion)},
            {"io.modelcontextprotocol/clientCapabilities", JsonValue::Object{}}};
        const JsonValue request(JsonValue::Object{
            {"jsonrpc", "2.0"},
            {"id", "com-" + std::to_string(next_request_id_++)},
            {"method", "tools/list"},
            {"params", JsonValue::Object{{"_meta", JsonValue(std::move(metadata))}}}});
        const JsonValue response = mcp_.handle(request);
        const JsonValue* result = response.get("result");
        const JsonValue* tools = result ? result->get("tools") : nullptr;
        return tools ? tools->serialize() : response.serialize();
    }

    std::atomic<ULONG> references_{1};
    std::mutex mutex_;
    mcp::McpServer mcp_;
    uint64_t next_request_id_{1};
    std::function<void()> on_destroy_;
};

} // namespace

HRESULT create_automation_dispatch(IDispatch** result,
                                   std::function<void()> on_destroy) noexcept {
    if (!result) return E_POINTER;
    *result = nullptr;
    try {
        *result = new AutomationDispatch(std::move(on_destroy));
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_UNEXPECTED;
    }
}

} // namespace pikdraw::com_server

#endif
