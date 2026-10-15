#pragma once

#include "pikdraw/color/profiles/profile_id.hpp"
#include <vector>
#include <optional>
#include <memory>

namespace pikdraw::color {

class ProfileRegistry {
public:
    ProfileRegistry();

    void register_profile(ProfileIdentifier id);
    [[nodiscard]] bool has_profile(const std::string& name) const;
    [[nodiscard]] std::optional<ProfileIdentifier> find_profile(const std::string& name) const;

    [[nodiscard]] const ProfileIdentifier& default_rgb_profile() const noexcept { return default_rgb_; }
    [[nodiscard]] const ProfileIdentifier& default_cmyk_profile() const noexcept { return default_cmyk_; }

    [[nodiscard]] uint64_t generation() const noexcept { return generation_; }
    void invalidate() noexcept { ++generation_; }

    static ProfileRegistry& instance();

private:
    std::vector<ProfileIdentifier> profiles_;
    ProfileIdentifier default_rgb_;
    ProfileIdentifier default_cmyk_;
    uint64_t generation_{1};
};

} // namespace pikdraw::color
