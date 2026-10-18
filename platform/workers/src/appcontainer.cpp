#include "pikdraw/workers/appcontainer.hpp"
#include <sddl.h>
#include <aclapi.h>

namespace pikdraw::workers {

namespace {

void GrantObjectAccess(HANDLE h, PSID sid, DWORD access) noexcept {
    if (!h || !sid) return;
    PACL old_dacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetSecurityInfo(h, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &old_dacl, nullptr, &sd) == ERROR_SUCCESS) {
        EXPLICIT_ACCESS_W ea{};
        ea.grfAccessPermissions = access;
        ea.grfAccessMode = GRANT_ACCESS;
        ea.grfInheritance = NO_INHERITANCE;
        ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        ea.Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
        ea.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid);
        PACL new_dacl = nullptr;
        if (SetEntriesInAclW(1, &ea, old_dacl, &new_dacl) == ERROR_SUCCESS && new_dacl != nullptr) {
            SetSecurityInfo(h, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, new_dacl, nullptr);
            LocalFree(new_dacl);
        }
        if (sd) LocalFree(sd);
    }
}

void GrantSessionAccess(PSID sid) noexcept {
    if (!sid) return;
    GrantObjectAccess(GetProcessWindowStation(), sid, GENERIC_READ | GENERIC_EXECUTE);
    GrantObjectAccess(GetThreadDesktop(GetCurrentThreadId()), sid, GENERIC_READ | GENERIC_EXECUTE);
}

} // namespace

bool AppContainerProfileGuard::Create(const std::wstring& profile_name) {
    Cleanup();
    if (profile_name.empty()) return false;

    PSID sid = nullptr;
    HRESULT hr = CreateAppContainerProfile(
        profile_name.c_str(),
        profile_name.c_str(),
        L"PikDraw Spike C Test Isolation Container",
        nullptr, // 0 capabilities: NO network, no internet client, no private network
        0,
        &sid
    );
    if (hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
        DeleteAppContainerProfile(profile_name.c_str());
        hr = CreateAppContainerProfile(
            profile_name.c_str(),
            profile_name.c_str(),
            L"PikDraw Spike C Test Isolation Container",
            nullptr,
            0,
            &sid
        );
    }

    if (SUCCEEDED(hr) && sid != nullptr) {
        GrantSessionAccess(sid);
        profile_name_ = profile_name; // Only delete profiles this instance created.
        appcontainer_sid_ = ScopedSid(sid);
        return true;
    }
    if (sid) FreeSid(sid);
    return false;
}

void AppContainerProfileGuard::Cleanup() noexcept {
    if (!profile_name_.empty()) {
        DeleteAppContainerProfile(profile_name_.c_str());
        profile_name_.clear();
    }
    appcontainer_sid_.Free();
}

bool AppContainerProfileGuard::GrantDirectoryAccess(const std::wstring& directory_path, PSID appcontainer_sid) noexcept {
    if (!appcontainer_sid) return false;
    GrantSessionAccess(appcontainer_sid);

    PACL old_dacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;

    DWORD res = GetNamedSecurityInfoW(
        directory_path.c_str(),
        SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION,
        nullptr, nullptr, &old_dacl, nullptr, &sd
    );

    if (res != ERROR_SUCCESS) return false;

    EXPLICIT_ACCESS_W ea{};
    ea.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
    ea.grfAccessMode = GRANT_ACCESS;
    ea.grfInheritance = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
    ea.Trustee.ptstrName = reinterpret_cast<LPWSTR>(appcontainer_sid);

    PACL new_dacl = nullptr;
    res = SetEntriesInAclW(1, &ea, old_dacl, &new_dacl);
    if (res == ERROR_SUCCESS && new_dacl != nullptr) {
        res = SetNamedSecurityInfoW(
            const_cast<LPWSTR>(directory_path.c_str()),
            SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION,
            nullptr, nullptr, new_dacl, nullptr
        );
        LocalFree(new_dacl);
    }

    if (sd) LocalFree(sd);
    return (res == ERROR_SUCCESS && new_dacl != nullptr);
}

} // namespace pikdraw::workers
