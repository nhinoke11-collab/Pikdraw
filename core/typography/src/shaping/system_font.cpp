#include "system_font.hpp"
#include <cmath>
#include <limits>
#ifdef _WIN32
namespace pikdraw::typography::system_font {
void check(HRESULT result, const char *operation) {
    if (FAILED(result))
        throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
}
IDWriteFactory *factory() {
    static ComPtr<IDWriteFactory> value;
    static const bool initialized = [] {
        check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                  reinterpret_cast<IUnknown **>(value.put())),
              "DWriteCreateFactory");
        return true;
    }();
    (void)initialized;
    return value.get();
}
std::wstring wide(std::string_view value) {
    if (value.empty())
        return {};
    if (value.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("UTF-8 input too long");
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size == 0)
        throw std::invalid_argument("Invalid UTF-8");
    std::wstring result(static_cast<size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                            result.data(), size) != size)
        throw std::invalid_argument("Invalid UTF-8");
    return result;
}
std::string utf8(std::wstring_view value) {
    if (value.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size == 0)
        throw std::invalid_argument("Invalid UTF-16");
    std::string result(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                            result.data(), size, nullptr, nullptr) != size)
        throw std::invalid_argument("Invalid UTF-16");
    return result;
}
std::string family_name(IDWriteFont *font) {
    ComPtr<IDWriteFontFamily> family;
    check(font->GetFontFamily(family.put()), "GetFontFamily");
    ComPtr<IDWriteLocalizedStrings> names;
    check(family->GetFamilyNames(names.put()), "GetFamilyNames");
    UINT32 index = 0;
    BOOL exists = FALSE;
    check(names->FindLocaleName(L"en-us", &index, &exists), "FindLocaleName");
    if (!exists)
        index = 0;
    UINT32 length = 0;
    check(names->GetStringLength(index, &length), "GetStringLength");
    std::wstring result(static_cast<size_t>(length) + 1, L'\0');
    check(names->GetString(index, result.data(), length + 1), "GetString");
    result.resize(length);
    return utf8(result);
}
FontIdentity describe(IDWriteFont *font, double size_pt) {
    FontIdentity result{family_name(font), "", static_cast<uint16_t>(font->GetWeight()),
                        font->GetStyle() != DWRITE_FONT_STYLE_NORMAL, size_pt};
    ComPtr<IDWriteLocalizedStrings> names;
    BOOL exists = FALSE;
    check(font->GetInformationalStrings(DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME, names.put(), &exists),
          "GetPostscriptName");
    if (exists) {
        UINT32 length = 0;
        check(names->GetStringLength(0, &length), "GetPostscriptLength");
        std::wstring name(static_cast<size_t>(length) + 1, L'\0');
        check(names->GetString(0, name.data(), length + 1), "GetPostscript");
        name.resize(length);
        result.postscript_name = utf8(name);
    }
    if (result.postscript_name.empty())
        throw std::runtime_error("Installed face has no stable PostScript identity");
    return result;
}
void load_font(const FontIdentity &identity, ComPtr<IDWriteFont> &font, ComPtr<IDWriteFontFace> &face) {
    if (!std::isfinite(identity.size_pt) || identity.size_pt <= 0 || identity.size_pt > 100000 ||
        identity.weight < 100 || identity.weight > 900 || identity.family_name.empty())
        throw std::invalid_argument("Invalid font identity or size");
    if (identity.family_name.find('\0') != std::string::npos)
        throw std::invalid_argument("Embedded NUL in font family");
    ComPtr<IDWriteFontCollection> collection;
    check(factory()->GetSystemFontCollection(collection.put()), "GetSystemFontCollection");
    UINT32 index = 0;
    BOOL exists = FALSE;
    check(collection->FindFamilyName(wide(identity.family_name).c_str(), &index, &exists), "FindFamilyName");
    if (!exists)
        throw std::runtime_error("Font family is not installed: " + identity.family_name);
    ComPtr<IDWriteFontFamily> family;
    check(collection->GetFontFamily(index, family.put()), "GetFontFamily");
    check(family->GetFirstMatchingFont(
              static_cast<DWRITE_FONT_WEIGHT>(identity.weight), DWRITE_FONT_STRETCH_NORMAL,
              identity.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, font.put()),
          "GetFirstMatchingFont");
    if (font->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE)
        throw std::runtime_error("Synthetic font styles are unsupported");
    check(font->CreateFontFace(face.put()), "CreateFontFace");
}
std::vector<size_t> utf16_byte_offsets(std::string_view value) {
    (void)wide(value);
    std::vector<size_t> offsets;
    for (size_t i = 0; i < value.size();) {
        const auto lead = static_cast<unsigned char>(value[i]);
        const size_t bytes = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        offsets.push_back(i);
        if (bytes == 4)
            offsets.push_back(i);
        i += bytes;
    }
    offsets.push_back(value.size());
    return offsets;
}
} // namespace pikdraw::typography::system_font
#endif
