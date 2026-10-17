#include "pikdraw/com_server/automation_dispatch.hpp"
#include "pikdraw/com_server/com_ids.hpp"

#ifdef _WIN32

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <sddl.h>

#include <atomic>
#include <mutex>
#include <new>
#include <string>
#include <vector>

namespace pikdraw::com_server {
LSTATUS register_com_server();
LSTATUS unregister_com_server();
}

namespace {

class ServerLifetime final {
public:
    explicit ServerLifetime(HANDLE idle_event) : idle_event_(idle_event) {}

    bool acquire() {
        std::lock_guard lock(mutex_);
        if (stopping_) return false;
        ++active_;
        ResetEvent(idle_event_);
        return true;
    }

    void release() {
        std::lock_guard lock(mutex_);
        if (active_ == 0) return;
        --active_;
        if (active_ == 0) SetEvent(idle_event_);
    }

    bool stop_if_idle() {
        std::lock_guard lock(mutex_);
        if (active_ != 0) {
            ResetEvent(idle_event_);
            return false;
        }
        stopping_ = true;
        return true;
    }

    void resume() {
        std::lock_guard lock(mutex_);
        stopping_ = false;
        if (active_ == 0) SetEvent(idle_event_);
        else ResetEvent(idle_event_);
    }

private:
    HANDLE idle_event_{};
    std::mutex mutex_;
    size_t active_{};
    bool stopping_{};
};

class ClassFactory final : public IClassFactory {
public:
    explicit ClassFactory(ServerLifetime& lifetime) : lifetime_(lifetime) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid == IID_IUnknown || iid == IID_IClassFactory) {
            *result = static_cast<IClassFactory*>(this);
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

    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        if (!lifetime_.acquire()) return CO_E_SERVER_STOPPING;
        IDispatch* dispatch = nullptr;
        const HRESULT create_result = pikdraw::com_server::create_automation_dispatch(
            &dispatch, [this] { lifetime_.release(); });
        if (FAILED(create_result)) {
            lifetime_.release();
            return create_result;
        }
        const HRESULT query_result = dispatch->QueryInterface(iid, result);
        dispatch->Release();
        return query_result;
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock) return lifetime_.acquire() ? S_OK : CO_E_SERVER_STOPPING;
        lifetime_.release();
        return S_OK;
    }

private:
    ~ClassFactory() = default;
    std::atomic<ULONG> references_{1};
    ServerLifetime& lifetime_;
};

bool initialize_current_user_com_security(HRESULT& result) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        result = HRESULT_FROM_WIN32(GetLastError());
        return false;
    }
    DWORD bytes = 0;
    (void)GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    if (bytes == 0) {
        result = HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(token);
        return false;
    }
    std::vector<BYTE> token_user(bytes);
    if (!GetTokenInformation(token, TokenUser, token_user.data(), bytes, &bytes)) {
        result = HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(token);
        return false;
    }
    const auto* user = reinterpret_cast<const TOKEN_USER*>(token_user.data());
    const DWORD acl_bytes = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD) +
                            GetLengthSid(user->User.Sid);
    std::vector<BYTE> acl_storage(acl_bytes);
    auto* acl = reinterpret_cast<PACL>(acl_storage.data());
    SECURITY_DESCRIPTOR descriptor{};
    bool ok = InitializeAcl(acl, acl_bytes, ACL_REVISION) &&
        AddAccessAllowedAceEx(acl, ACL_REVISION, 0,
            COM_RIGHTS_EXECUTE | COM_RIGHTS_EXECUTE_LOCAL | COM_RIGHTS_ACTIVATE_LOCAL,
            user->User.Sid) &&
        InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) &&
        SetSecurityDescriptorOwner(&descriptor, user->User.Sid, FALSE) &&
        SetSecurityDescriptorGroup(&descriptor, user->User.Sid, FALSE) &&
        SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE);
    if (!ok) {
        result = HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(token);
        return false;
    }
    result = CoInitializeSecurity(&descriptor, -1, nullptr, nullptr,
        RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IDENTIFY, nullptr,
        EOAC_DISABLE_AAA | EOAC_NO_CUSTOM_MARSHAL, nullptr);
    CloseHandle(token);
    return SUCCEEDED(result);
}

bool command_is(int argc, wchar_t** argv, const wchar_t* expected) {
    return argc == 2 && CompareStringOrdinal(argv[1], -1, expected, -1, TRUE) == CSTR_EQUAL;
}

int report_registry_result(LSTATUS status, const wchar_t* action) {
    if (status == ERROR_SUCCESS) return 0;
    const std::wstring message = std::wstring(action) + L" failed. Windows error: " +
                                 std::to_wstring(status) + L".";
    MessageBoxW(nullptr, message.c_str(), L"PikDraw COM Automation", MB_OK | MB_ICONERROR);
    return static_cast<int>(status);
}

int run_com_server() {
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(apartment)) return static_cast<int>(apartment);
    HRESULT security = S_OK;
    if (!initialize_current_user_com_security(security)) {
        CoUninitialize();
        return static_cast<int>(security);
    }

    HANDLE idle_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!idle_event) {
        const DWORD error = GetLastError();
        CoUninitialize();
        return static_cast<int>(HRESULT_FROM_WIN32(error));
    }
    ServerLifetime lifetime(idle_event);
    auto* factory = new (std::nothrow) ClassFactory(lifetime);
    if (!factory) {
        CloseHandle(idle_event);
        CoUninitialize();
        return static_cast<int>(E_OUTOFMEMORY);
    }
    DWORD cookie = 0;
    HRESULT status = CoRegisterClassObject(pikdraw::com_server::kPikDrawAutomationClsid,
        factory, CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE | REGCLS_SUSPENDED, &cookie);
    factory->Release();
    if (SUCCEEDED(status)) status = CoResumeClassObjects();
    if (FAILED(status)) {
        if (cookie != 0) (void)CoRevokeClassObject(cookie);
        CloseHandle(idle_event);
        CoUninitialize();
        return static_cast<int>(status);
    }

    // Give the activation that launched this process time to attach; after a
    // client releases its object, keep the server warm briefly for another call.
    for (;;) {
        DWORD wait = WaitForSingleObject(idle_event, 60'000);
        if (wait == WAIT_OBJECT_0) {
            ResetEvent(idle_event);
            wait = WaitForSingleObject(idle_event, 30'000);
        }
        if (wait != WAIT_TIMEOUT) continue;
        status = CoSuspendClassObjects();
        if (FAILED(status)) break;
        if (lifetime.stop_if_idle()) break;
        lifetime.resume();
        status = CoResumeClassObjects();
        if (FAILED(status)) break;
    }

    (void)CoRevokeClassObject(cookie);
    CloseHandle(idle_event);
    CoUninitialize();
    return FAILED(status) ? static_cast<int>(status) : 0;
}

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return static_cast<int>(HRESULT_FROM_WIN32(GetLastError()));
    int result = 0;
    if (command_is(argc, argv, L"/RegServer") || command_is(argc, argv, L"-RegServer")) {
        result = report_registry_result(pikdraw::com_server::register_com_server(),
                                        L"Per-user COM registration");
    } else if (command_is(argc, argv, L"/UnregServer") ||
               command_is(argc, argv, L"-UnregServer")) {
        result = report_registry_result(pikdraw::com_server::unregister_com_server(),
                                        L"Per-user COM removal");
    } else if ((argc == 2 && CompareStringOrdinal(argv[1], -1, L"-Embedding", -1, TRUE) == CSTR_EQUAL) ||
               (argc == 2 && CompareStringOrdinal(argv[1], -1, L"/Embedding", -1, TRUE) == CSTR_EQUAL)) {
        result = run_com_server();
    } else {
        MessageBoxW(nullptr,
            L"Use /RegServer to register PikDraw.Automation for this user or /UnregServer to remove it.",
            L"PikDraw COM Automation", MB_OK | MB_ICONINFORMATION);
        result = E_INVALIDARG;
    }
    LocalFree(argv);
    return result;
}

#endif
