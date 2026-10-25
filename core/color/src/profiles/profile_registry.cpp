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
        if (p == id) {
            p = std::move(id);
            invalidate();
            return;
        }
    }
    profiles_.push_back(std::move(id));
    invalidate();
}

bool ProfileRegistry::register_resource(std::shared_ptr<const ProfileResource> resource) {
    if (!resource || resource->identity().checksum_sha256.size() != 64) return false;
    for (auto& existing : resources_) {
        if (existing->identity() != resource->identity()) continue;
        existing = std::move(resource);
        register_profile(existing->identity());
        return true;
    }
    resources_.push_back(resource);
    register_profile(resource->identity());
    return true;
}

bool ProfileRegistry::has_profile(const std::string& name) const {
    for (const auto& p : profiles_) {
        if (p.name == name) return true;
    }
    return false;
}

std::optional<ProfileIdentifier> ProfileRegistry::find_profile(const std::string& name) const {
    std::optional<ProfileIdentifier> found;
    for (const auto& p : profiles_) {
        if (p.name != name) continue;
        if (found) return std::nullopt; // Ambiguous labels must not resolve a profile resource.
        found = p;
    }
    return found;
}

std::optional<ProfileIdentifier> ProfileRegistry::find_profile(
    const ProfileIdentifier& identity) const {
    for (const auto& profile : profiles_)
        if (profile == identity) return profile;
    return std::nullopt;
}

std::shared_ptr<const ProfileResource> ProfileRegistry::find_resource(
    const ProfileIdentifier& identity) const {
    if (identity.checksum_sha256.size() != 64) return {};
    for (const auto& resource : resources_)
        if (resource->identity() == identity) return resource;
    return {};
}

ProfileRegistry& ProfileRegistry::instance() {
    static ProfileRegistry reg;
    return reg;
}

} // namespace pikdraw::color
