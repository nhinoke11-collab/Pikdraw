#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pikdraw::document {

enum class DocumentContentMode : uint8_t {
    Editable = 0,
    PdfPrintLocked = 1,
};

/// Immutable original PDF bytes shared by every source-backed page in a document.
struct PdfSourceAsset {
    std::string sha256_hex;
    uint32_t page_count{0};
    std::shared_ptr<const std::vector<uint8_t>> bytes;
};

/// Locates one original page. The PDF remains the appearance/render source.
struct PdfSourcePageReference {
    std::shared_ptr<const PdfSourceAsset> asset;
    uint32_t source_page_index{0};
    uint8_t rotation_quarters_clockwise{0};
};

} // namespace pikdraw::document
