#pragma once

#include "pikdraw/color/values/color_space.hpp"
#include <string>

namespace pikdraw::color {

/// Identifier for ICC color profiles
struct ProfileIdentifier {
    std::string name;
    ColorSpace color_space{ColorSpace::Rgb};
    std::string checksum_sha256;

    [[nodiscard]] bool empty() const noexcept { return name.empty(); }
    [[nodiscard]] bool is_standard() const noexcept {
        // Unresolved built-in slots are label-only compatibility identities.
        // A byte-backed resource must not be trusted as a standard profile
        // merely because its user-visible name matches a built-in label.
        return checksum_sha256.empty() &&
               (name == "sRGB" || name == "Display P3" ||
                name == "ISO Coated v2 300% (ECI)" || name == "U.S. Web Coated (SWOP) v2");
    }

    bool operator==(const ProfileIdentifier& o) const noexcept {
        if (color_space != o.color_space) return false;
        if (!checksum_sha256.empty() || !o.checksum_sha256.empty())
            return !checksum_sha256.empty() && checksum_sha256 == o.checksum_sha256;
        return name == o.name; // Legacy unresolved identities are label-based only.
    }
    bool operator!=(const ProfileIdentifier& o) const noexcept {
        return !(*this == o);
    }
};

} // namespace pikdraw::color
