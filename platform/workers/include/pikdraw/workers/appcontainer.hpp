#pragma once

#include "win32_common.hpp"
#include <userenv.h>
#include <string>
#include <utility>

namespace pikdraw::workers {

class AppContainerProfileGuard {
public:
    AppContainerProfileGuard() noexcept = default;
    ~AppContainerProfileGuard() noexcept {
        Cleanup();
    }

    AppContainerProfileGuard(const AppContainerProfileGuard&) = delete;
    AppContainerProfileGuard& operator=(const AppContainerProfileGuard&) = delete;

    AppContainerProfileGuard(AppContainerProfileGuard&& other) noexcept
        : profile_name_(std::exchange(other.profile_name_, {})),
          appcontainer_sid_(std::move(other.appcontainer_sid_))
    {}

    AppContainerProfileGuard& operator=(AppContainerProfileGuard&& other) noexcept {
        if (this != &other) {
            Cleanup();
            profile_name_ = std::exchange(other.profile_name_, {});
            appcontainer_sid_ = std::move(other.appcontainer_sid_);
        }
        return *this;
    }

    [[nodiscard]] bool Create(const std::wstring& profile_name);
    void Cleanup() noexcept;

    [[nodiscard]] PSID GetSid() const noexcept { return appcontainer_sid_.Get(); }
    [[nodiscard]] const std::wstring& GetProfileName() const noexcept { return profile_name_; }
    [[nodiscard]] bool IsValid() const noexcept { return appcontainer_sid_.IsValid(); }

    // Grants read/execute access to test worker binary directory only
    [[nodiscard]] static bool GrantDirectoryAccess(const std::wstring& directory_path, PSID appcontainer_sid) noexcept;

private:
    std::wstring profile_name_;
    ScopedSid appcontainer_sid_;
};

} // namespace pikdraw::workers
