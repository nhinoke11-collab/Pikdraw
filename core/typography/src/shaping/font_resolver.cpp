#include "system_font.hpp"
#include <algorithm>
#include <cctype>

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

std::vector<std::string> FontResolver::available_font_families() const {
#ifdef _WIN32
    system_font::ComPtr<IDWriteFontCollection> collection;
    if (FAILED(system_font::factory()->GetSystemFontCollection(collection.put())) || !collection.get())
        return {"Be Vietnam Pro", "Segoe UI", "Arial", "Times New Roman", "JetBrains Mono"};
    const UINT32 count = collection->GetFontFamilyCount();
    std::vector<std::string> result;
    result.reserve(count);
    WCHAR locale_name[LOCALE_NAME_MAX_LENGTH]{};
    GetUserDefaultLocaleName(locale_name, LOCALE_NAME_MAX_LENGTH);

    for (UINT32 i = 0; i < count; ++i) {
        system_font::ComPtr<IDWriteFontFamily> family;
        if (FAILED(collection->GetFontFamily(i, family.put()))) continue;
        system_font::ComPtr<IDWriteLocalizedStrings> names;
        if (FAILED(family->GetFamilyNames(names.put()))) continue;
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (FAILED(names->FindLocaleName(locale_name, &index, &exists)) || !exists) {
            if (FAILED(names->FindLocaleName(L"en-us", &index, &exists)) || !exists)
                index = 0;
        }
        UINT32 length = 0;
        if (FAILED(names->GetStringLength(index, &length)) || length == 0) continue;
        std::wstring name_w(length + 1, L'\0');
        if (FAILED(names->GetString(index, name_w.data(), length + 1))) continue;
        name_w.resize(length);
        const std::string name = system_font::utf8(name_w);
        if (!name.empty()) result.push_back(name);
    }
    std::sort(result.begin(), result.end(), [](const std::string& a, const std::string& b) {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
            [](char c1, char c2) {
                return std::tolower(static_cast<unsigned char>(c1)) <
                       std::tolower(static_cast<unsigned char>(c2));
            });
    });
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
#else
    return {"Be Vietnam Pro", "Segoe UI", "Arial", "Times New Roman", "JetBrains Mono"};
#endif
}

FontResolver &FontResolver::instance() {
    static FontResolver resolver;
    return resolver;
}
} // namespace pikdraw::typography
