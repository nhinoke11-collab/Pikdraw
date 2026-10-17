#include "system_font.hpp"
namespace pikdraw::typography {
FontResolver::FontResolver() = default;
ResolvedFont FontResolver::resolve(const FontIdentity &requested) const {
#ifdef _WIN32
    if (requested.family_name.empty())
        throw std::invalid_argument("Empty requested font family");
    if (requested.family_name.find('\0') != std::string::npos ||
        requested.postscript_name.find('\0') != std::string::npos)
        throw std::invalid_argument("Embedded NUL in font identity");
    (void)system_font::wide(requested.postscript_name);
    auto lookup = requested;
    if (!is_font_available(requested.family_name))
        lookup.family_name = "Segoe UI";
    system_font::ComPtr<IDWriteFont> font;
    system_font::ComPtr<IDWriteFontFace> face;
    system_font::load_font(lookup, font, face);
    ResolvedFont result;
    result.requested = requested;
    result.actual = system_font::describe(font.get(), requested.size_pt);
    result.is_fallback =
        result.actual.family_name != requested.family_name || result.actual.weight != requested.weight ||
        result.actual.italic != requested.italic ||
        (!requested.postscript_name.empty() && result.actual.postscript_name != requested.postscript_name);
    if (result.is_fallback)
        result.warning_message = "Requested font '" + requested.family_name +
                                 "' unavailable exactly; using '" + result.actual.postscript_name + "'";
    return result;
#else
    (void)requested;
    throw std::runtime_error("Typography system backend unavailable on this platform");
#endif
}
void FontResolver::register_available_font(std::string family_name) {
    if (!is_font_available(family_name))
        throw std::invalid_argument("Font registration requires a real installed font");
}
bool FontResolver::is_font_available(const std::string &family_name) const {
#ifdef _WIN32
    if (family_name.find('\0') != std::string::npos)
        throw std::invalid_argument("Embedded NUL in font family");
    system_font::ComPtr<IDWriteFontCollection> collection;
    system_font::check(system_font::factory()->GetSystemFontCollection(collection.put()),
                       "GetSystemFontCollection");
    UINT32 index = 0;
    BOOL exists = FALSE;
    system_font::check(collection->FindFamilyName(system_font::wide(family_name).c_str(), &index, &exists),
                       "FindFamilyName");
    return exists != FALSE;
#else
    (void)family_name;
    return false;
#endif
}
FontResolver &FontResolver::instance() {
    static FontResolver resolver;
    return resolver;
}
} // namespace pikdraw::typography
