#pragma once

#include "pikdraw/interop/adapters/svg_worker_adapter.hpp"

#include <cstdint>
#include <functional>
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

struct PdfRenderedPageRgba8 {
    uint32_t width_px{0};
    uint32_t height_px{0};
    std::vector<uint8_t> rgba;
};

using PdfRenderedPageConsumer =
    std::function<bool(uint32_t, PdfRenderedPageRgba8&&)>;

[[nodiscard]] bool InspectPdfSource(
    std::span<const uint8_t> source,
    PdfSourceManifest& manifest,
    std::string& diagnostic);

/// Renders one page inside the isolated PDF worker at an explicit DPI.
/// The original source remains authoritative; preview pixels are not print output.
[[nodiscard]] bool RenderPdfSourcePageRgba8(
    std::span<const uint8_t> source, uint32_t source_page_index, uint32_t dpi,
    uint64_t max_pixels, PdfRenderedPageRgba8& rendered, std::string& diagnostic);

/// Renders selected pages with one PDFium document session and releases each
/// bitmap after the consumer has serialized or retained its pixels.
[[nodiscard]] bool RenderPdfSourcePagesRgba8(
    std::span<const uint8_t> source, std::span<const uint32_t> source_page_indices,
    uint32_t dpi, uint64_t max_pixels_per_page, uint64_t max_total_pixels,
    const PdfRenderedPageConsumer& consumer, std::string& diagnostic);

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

/// Emits only the source page manifest for a PDF-print-locked document.
/// Page content stays in the original PDF asset; no path/image preview records are created.
[[nodiscard]] bool BuildPdfSourceManifestImportRecords(
    std::span<const uint8_t> source,
    std::vector<CanonicalImportRecord>& records,
    std::string& diagnostic,
    std::optional<uint32_t> source_page_index = std::nullopt);

} // namespace pikdraw::interop
