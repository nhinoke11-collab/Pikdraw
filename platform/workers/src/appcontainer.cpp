#include "pikdraw/workers/appcontainer.hpp"
#include <sddl.h>
#include <aclapi.h>

namespace pikdraw::workers {

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

    if (SUCCEEDED(hr) && sid != nullptr) {
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
    ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
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
    return (res == ERROR_SUCCESS);
}

} // namespace pikdraw::workers
