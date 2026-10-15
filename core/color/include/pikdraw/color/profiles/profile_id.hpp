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
        return name == "sRGB" || name == "Display P3" ||
               name == "ISO Coated v2 300% (ECI)" || name == "U.S. Web Coated (SWOP) v2";
    }

    bool operator==(const ProfileIdentifier& o) const noexcept {
        return name == o.name && color_space == o.color_space;
    }
    bool operator!=(const ProfileIdentifier& o) const noexcept {
        return !(*this == o);
    }
};

} // namespace pikdraw::color
