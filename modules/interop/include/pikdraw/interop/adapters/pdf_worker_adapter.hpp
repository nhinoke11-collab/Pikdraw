#pragma once

#include "pikdraw/interop/adapters/svg_worker_adapter.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pikdraw::interop {

/// A page entry discovered from the PDF page tree before materializing import records.
/// source_index is zero-based; width/height use the page bounding box before rotation.
struct PdfSourcePageDescriptor {
    uint32_t source_index{0};
    double left_pt{0.0};
    double bottom_pt{0.0};
    double right_pt{0.0};
    double top_pt{0.0};
    double width_mm{0.0};
    double height_mm{0.0};
    uint8_t rotation_quarters_clockwise{0};
    int32_t page_object_count{0};
};

/// Page-tree manifest for a PDF or PDF-compatible AI representation.
/// A successful inspection reports every page, including blank pages.
struct PdfSourceManifest {
    uint32_t exact_page_count{0};
    std::vector<PdfSourcePageDescriptor> pages;
};

[[nodiscard]] bool InspectPdfSource(
    std::span<const uint8_t> source,
    PdfSourceManifest& manifest,
    std::string& diagnostic);

/// Imports a single-page PDF representation through PDFium as native C6 path/text records.
/// AI files use this only for their PDF-compatible representation, never AIPrivateData.
[[nodiscard]] bool BuildPdfImportRecords(
    std::span<const uint8_t> source,
    bool is_ai_representation,
    std::vector<CanonicalImportRecord>& records,
    std::string& diagnostic,
    uint32_t source_page_index = 0);

/// Imports every source page (or one explicit source index) into a page-set
/// proposal. Objects and source references are remapped to document-wide IDs.
[[nodiscard]] bool BuildPdfPageSetImportRecords(
    std::span<const uint8_t> source,
    bool is_ai_representation,
    std::vector<CanonicalImportRecord>& records,
    std::string& diagnostic,
    std::optional<uint32_t> source_page_index = std::nullopt);

} // namespace pikdraw::interop
