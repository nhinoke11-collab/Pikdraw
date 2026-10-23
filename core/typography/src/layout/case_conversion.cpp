#include "pikdraw/typography/layout/case_conversion.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cwctype>
#endif

#include <algorithm>
#include <cctype>
#include <vector>

namespace pikdraw::typography {

#ifdef _WIN32
namespace {

std::wstring utf8_to_wide(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), len);
    return wide;
}

std::string wide_to_utf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string utf8(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), len, nullptr, nullptr);
    return utf8;
}

std::wstring map_wide_case(std::wstring_view wide, DWORD map_flags) {
    if (wide.empty()) return {};
    const int needed = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, map_flags, wide.data(),
                                     static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr, 0);
    if (needed <= 0) return std::wstring(wide);
    std::wstring result(static_cast<size_t>(needed), L'\0');
    LCMapStringEx(LOCALE_NAME_USER_DEFAULT, map_flags, wide.data(),
                  static_cast<int>(wide.size()), result.data(), needed, nullptr, nullptr, 0);
    return result;
}

size_t code_unit_count(std::wstring_view input, size_t offset) {
    const auto first = static_cast<uint16_t>(input[offset]);
    if (first >= 0xD800 && first <= 0xDBFF && offset + 1 < input.size()) {
        const auto second = static_cast<uint16_t>(input[offset + 1]);
        if (second >= 0xDC00 && second <= 0xDFFF) return 2;
    }
    return 1;
}

WORD character_type(std::wstring_view value) {
    WORD types[2]{};
    if (!value.empty() && GetStringTypeW(CT_CTYPE1, value.data(),
                                       static_cast<int>(value.size()), types))
        return static_cast<WORD>(types[0] | (value.size() > 1 ? types[1] : 0));
    return 0;
}

bool is_unicode_letter(std::wstring_view value) {
    if ((character_type(value) & C1_ALPHA) != 0) return true;
    return !value.empty() && std::iswalpha(value.front()) != 0;
}

bool is_unicode_space_or_punctuation(std::wstring_view value) {
    const WORD type = character_type(value);
    if ((type & (C1_SPACE | C1_PUNCT)) != 0) return true;
    return !value.empty() &&
           (std::iswspace(value.front()) != 0 || std::iswpunct(value.front()) != 0);
}

std::wstring map_single_codepoint_case(std::wstring_view value, DWORD map_flags) {
    if (value.empty()) return {};
    const int needed = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, map_flags, value.data(),
                                     static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr, 0);
    if (needed <= 0) return std::wstring(value);
    std::wstring result(static_cast<size_t>(needed), L'\0');
    const int written = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, map_flags, value.data(),
                                     static_cast<int>(value.size()), result.data(), needed,
                                     nullptr, nullptr, 0);
    if (written <= 0) return std::wstring(value);
    result.resize(static_cast<size_t>(written));
    return result;
}

bool is_sentence_terminator(std::wstring_view value) {
    return value == L"." || value == L"!" || value == L"?" || value == L"\r" || value == L"\n";
}

bool is_title_word_separator(std::wstring_view value) {
    if (value == L"'" || value == L"\u2019") return false;
    return is_unicode_space_or_punctuation(value);
}

std::wstring to_sentence_case(std::wstring_view input) {
    if (input.empty()) return {};
    const std::wstring lowered = map_wide_case(input, LCMAP_LOWERCASE);
    std::wstring result;
    result.reserve(lowered.size());
    bool new_sentence = true;

    for (size_t i = 0; i < lowered.size();) {
        const size_t count = code_unit_count(lowered, i);
        const std::wstring_view character(lowered.data() + i, count);
        if (is_sentence_terminator(character)) {
            result.append(character);
            new_sentence = true;
        } else if (new_sentence && is_unicode_letter(character)) {
            result += map_single_codepoint_case(character, LCMAP_UPPERCASE);
            new_sentence = false;
        } else {
            result.append(character);
        }
        i += count;
    }
    return result;
}

std::wstring to_title_case(std::wstring_view input) {
    if (input.empty()) return {};
    const std::wstring lowered = map_wide_case(input, LCMAP_LOWERCASE);
    std::wstring result;
    result.reserve(lowered.size());
    bool start_word = true;

    for (size_t i = 0; i < lowered.size();) {
        const size_t count = code_unit_count(lowered, i);
        const std::wstring_view character(lowered.data() + i, count);
        if (is_unicode_letter(character)) {
            if (start_word) result += map_single_codepoint_case(character, LCMAP_UPPERCASE);
            else result.append(character);
            start_word = false;
        } else {
            result.append(character);
            if (is_title_word_separator(character)) start_word = true;
        }
        i += count;
    }
    return result;
}

std::wstring to_toggle_case(std::wstring_view input) {
    if (input.empty()) return {};
    std::wstring result;
    result.reserve(input.size());

    for (size_t i = 0; i < input.size();) {
        const size_t count = code_unit_count(input, i);
        const std::wstring_view character(input.data() + i, count);
        if (is_unicode_letter(character)) {
            const auto upper = map_single_codepoint_case(character, LCMAP_UPPERCASE);
            result += upper != character ? upper : map_single_codepoint_case(character, LCMAP_LOWERCASE);
        } else {
            result.append(character);
        }
        i += count;
    }
    return result;
}

} // namespace

std::string convert_case_utf8(std::string_view text, ChangeCaseMode mode) {
    if (text.empty()) return {};
    const auto wide = utf8_to_wide(text);
    if (wide.empty()) return std::string(text);

    std::wstring converted;
    switch (mode) {
        case ChangeCaseMode::Uppercase:
            converted = map_wide_case(wide, LCMAP_UPPERCASE);
            break;
        case ChangeCaseMode::Lowercase:
            converted = map_wide_case(wide, LCMAP_LOWERCASE);
            break;
        case ChangeCaseMode::TitleCase:
            converted = to_title_case(wide);
            break;
        case ChangeCaseMode::SentenceCase:
            converted = to_sentence_case(wide);
            break;
        case ChangeCaseMode::ToggleCase:
            converted = to_toggle_case(wide);
            break;
    }
    return wide_to_utf8(converted);
}

#else

std::string convert_case_utf8(std::string_view text, ChangeCaseMode mode) {
    std::string result(text);
    if (mode == ChangeCaseMode::Uppercase) {
        for (char& c : result) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    } else if (mode == ChangeCaseMode::Lowercase) {
        for (char& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

#endif

ChangeCaseMode next_cycled_case_mode(std::string_view current_text) {
    if (current_text.empty()) return ChangeCaseMode::Uppercase;
    const std::string upper = convert_case_utf8(current_text, ChangeCaseMode::Uppercase);
    if (current_text == upper) return ChangeCaseMode::Lowercase;
    const std::string lower = convert_case_utf8(current_text, ChangeCaseMode::Lowercase);
    if (current_text == lower) return ChangeCaseMode::TitleCase;
    return ChangeCaseMode::Uppercase;
}

} // namespace pikdraw::typography
