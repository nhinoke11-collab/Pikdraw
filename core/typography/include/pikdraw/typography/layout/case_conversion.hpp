#pragma once

#include <string>
#include <string_view>

namespace pikdraw::typography {

enum class ChangeCaseMode {
    SentenceCase,
    Lowercase,
    Uppercase,
    TitleCase,
    ToggleCase
};

/// Converts UTF-8 string according to ChangeCaseMode with full Unicode & Vietnamese diacritics support.
[[nodiscard]] std::string convert_case_utf8(std::string_view text, ChangeCaseMode mode);

/// Cycles through common case modes: Lowercase -> TitleCase -> Uppercase -> Lowercase
[[nodiscard]] ChangeCaseMode next_cycled_case_mode(std::string_view current_text);

} // namespace pikdraw::typography
