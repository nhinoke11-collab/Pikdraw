#include "pikdraw/color/profiles/profile_registry.hpp"

namespace pikdraw::color {

ProfileRegistry::ProfileRegistry() {
    default_rgb_ = ProfileIdentifier{"sRGB", ColorSpace::Rgb, ""};
    default_cmyk_ = ProfileIdentifier{"ISO Coated v2 300% (ECI)", ColorSpace::Cmyk, ""};

    profiles_.push_back(default_rgb_);
    profiles_.push_back(ProfileIdentifier{"Display P3", ColorSpace::Rgb, ""});
    profiles_.push_back(default_cmyk_);
    profiles_.push_back(ProfileIdentifier{"U.S. Web Coated (SWOP) v2", ColorSpace::Cmyk, ""});
}

void ProfileRegistry::register_profile(ProfileIdentifier id) {
    for (auto& p : profiles_) {
        if (p.name == id.name) {
            p = std::move(id);
            invalidate();
            return;
        }
    }
    profiles_.push_back(std::move(id));
    invalidate();
}

bool ProfileRegistry::has_profile(const std::string& name) const {
    for (const auto& p : profiles_) {
        if (p.name == name) return true;
    }
    return false;
}

std::optional<ProfileIdentifier> ProfileRegistry::find_profile(const std::string& name) const {
    for (const auto& p : profiles_) {
        if (p.name == name) return p;
    }
    return std::nullopt;
}

ProfileRegistry& ProfileRegistry::instance() {
    static ProfileRegistry reg;
    return reg;
}

} // namespace pikdraw::color
