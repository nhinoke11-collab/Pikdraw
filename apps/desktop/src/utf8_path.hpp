#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace pikdraw::desktop::detail {

inline std::filesystem::path path_from_utf8(std::string_view value) {
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char byte : value)
        encoded.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(std::move(encoded));
}

} // namespace pikdraw::desktop::detail
