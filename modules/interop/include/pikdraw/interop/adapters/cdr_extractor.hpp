#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pikdraw::interop {

struct CdrExtractionResult {
    bool success{false};
    std::string error_message;
    std::vector<uint8_t> thumbnail_png;
    double page_width_mm{297.0};
    double page_height_mm{210.0};
};

class CdrExtractor {
public:
    [[nodiscard]] static bool is_cdr_zip_container(std::span<const uint8_t> bytes) noexcept;
    [[nodiscard]] static CdrExtractionResult extract_preview(std::span<const uint8_t> cdr_bytes);
};

} // namespace pikdraw::interop
