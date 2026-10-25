#pragma once

#include <filesystem>
#include <string_view>

namespace pikdraw::document {

inline constexpr std::string_view kNativeDocumentExtension = ".pr";

/// The suffix routes native IO; NativeFormat validates the actual container.
[[nodiscard]] inline bool is_native_document_path(const std::filesystem::path& path) {
    auto extension = path.extension().u8string();
    for (auto& value : extension) {
        if (value >= u8'A' && value <= u8'Z')
            value = static_cast<char8_t>(value + (u8'a' - u8'A'));
    }
    return extension == u8".pr";
}

} // namespace pikdraw::document
