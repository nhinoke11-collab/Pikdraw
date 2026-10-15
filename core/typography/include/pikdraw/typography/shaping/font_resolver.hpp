#pragma once

#include "pikdraw/typography/shaping/font_identity.hpp"
#include <vector>
#include <string>

namespace pikdraw::typography {

struct ResolvedFont {
    FontIdentity requested;
    FontIdentity actual;
    bool is_fallback{false};
    std::string warning_message;
};

class FontResolver {
public:
    FontResolver();

    [[nodiscard]] ResolvedFont resolve(const FontIdentity& font) const;
    void register_available_font(std::string family_name);

    [[nodiscard]] bool is_font_available(const std::string& family_name) const;

    static FontResolver& instance();

private:
    std::vector<std::string> available_families_;
};

} // namespace pikdraw::typography
