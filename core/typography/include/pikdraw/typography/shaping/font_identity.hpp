#pragma once

#include <string>
#include <cstdint>

namespace pikdraw::typography {

/// Font descriptor according to Contract C4
struct FontIdentity {
    std::string family_name{"Plus Jakarta Sans"};
    std::string postscript_name{"PlusJakartaSans-Regular"};
    uint16_t weight{400}; // 100 - 900
    bool italic{false};
    double size_pt{12.0}; // in points

    /// Converts font size points to millimeters (1 pt = 25.4 / 72 mm = ~0.352778 mm)
    [[nodiscard]] double size_mm() const noexcept {
        return size_pt * (25.4 / 72.0);
    }

    static FontIdentity make_default() {
        return FontIdentity{};
    }

    static FontIdentity make_mono(double pt = 10.0) {
        return FontIdentity{"JetBrains Mono", "JetBrainsMono-Regular", 400, false, pt};
    }

    bool operator==(const FontIdentity& o) const noexcept {
        return family_name == o.family_name && weight == o.weight &&
               italic == o.italic && size_pt == o.size_pt;
    }
    bool operator!=(const FontIdentity& o) const noexcept {
        return !(*this == o);
    }
};

} // namespace pikdraw::typography
