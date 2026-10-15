#include "pikdraw/typography/shaping/font_resolver.hpp"

namespace pikdraw::typography {

FontResolver::FontResolver() {
    available_families_.push_back("Plus Jakarta Sans");
    available_families_.push_back("JetBrains Mono");
    available_families_.push_back("Segoe UI");
    available_families_.push_back("Roboto");
    available_families_.push_back("Arial");
}

ResolvedFont FontResolver::resolve(const FontIdentity& font) const {
    ResolvedFont result;
    result.requested = font;

    if (is_font_available(font.family_name)) {
        result.actual = font;
        result.is_fallback = false;
    } else {
        // Fallback to standard application UI font while preserving requested name
        result.actual = FontIdentity{"Plus Jakarta Sans", "PlusJakartaSans-Regular", font.weight, font.italic, font.size_pt};
        result.is_fallback = true;
        result.warning_message = "Font '" + font.family_name + "' not found on system; substituting with 'Plus Jakarta Sans'";
    }
    return result;
}

void FontResolver::register_available_font(std::string family_name) {
    if (!is_font_available(family_name)) {
        available_families_.push_back(std::move(family_name));
    }
}

bool FontResolver::is_font_available(const std::string& family_name) const {
    for (const auto& fam : available_families_) {
        if (fam == family_name) return true;
    }
    return false;
}

FontResolver& FontResolver::instance() {
    static FontResolver resolver;
    return resolver;
}

} // namespace pikdraw::typography
