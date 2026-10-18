#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pikdraw::interop {

struct PdfAiExtractionResult {
    bool success{false};
    std::string error_message;
    std::vector<uint8_t> thumbnail_png; // PNG or JPEG raster image bytes
    double page_width_mm{297.0};
    double page_height_mm{210.0};
    bool is_ai{false};
    bool is_pdf{false};
};

class PdfAiExtractor {
public:
    [[nodiscard]] static bool is_pdf_or_ai_container(std::span<const uint8_t> bytes) noexcept;
    [[nodiscard]] static PdfAiExtractionResult extract_preview(std::span<const uint8_t> bytes);
};

} // namespace pikdraw::interop
