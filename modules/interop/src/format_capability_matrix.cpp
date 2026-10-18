#include "pikdraw/interop/format_capability_matrix.hpp"
#include <algorithm>
#include <cctype>
#include <utility>

namespace pikdraw::interop {
std::vector<FormatCapabilityEntry> FormatCapabilityRegistry::GetMatrix() {
    const auto unsupported = [](std::string name, std::string extension, std::string note) {
        return FormatCapabilityEntry{
            std::move(name), std::move(extension), "unsupported",
            FormatCapabilityStatus::UnsupportedReject,
            FormatCapabilityStatus::UnsupportedReject,
            "none", "none", "not applicable", "not applicable", std::move(note)};
    };
    return {
        {"SVG static subset", ".svg", "static SVG 1.0/1.1 subset",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::SupportedPrototype,
         "built-in", "repository source", "not applicable", "worker process for import",
         "Static SVG subset only. Export maps Path/Text paint and object opacity; import does not yet accept SVG opacity attributes. Export rejects effect nodes and non-Normal blend modes."},
        {"AutoCAD DXF 2D subset", ".dxf", "ASCII DXF dxf-entities-v1",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "built-in", "repository source", "not applicable", "worker process for import",
         "Import only for the explicit 2D entity/unit profile; no DXF export."},
        {"PDF", ".pdf", "Fast reference preview pdf-ai-preview-v1",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "PdfAiExtractor", "repository source", "not applicable", "isolated worker for import",
         "Import only: fast reference preview from embedded page/thumbnail and metadata dimensions. No PDF export."},
        {"Adobe Illustrator", ".ai", "Fast reference preview pdf-ai-preview-v1",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "PdfAiExtractor", "repository source", "not applicable", "isolated worker for import",
         "Import only: fast reference preview from embedded page/thumbnail and metadata dimensions. No AI export."},
        {"CorelDRAW", ".cdr", "Fast reference preview cdr-preview-v1",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "CdrExtractor", "repository source", "not applicable", "isolated worker for import",
         "Import only: fast reference preview from embedded thumbnail/page PNG and metadata dimensions. No CDR export."},
        unsupported("EPS", ".eps", "No EPS/PostScript adapter or round-trip guarantee."),
        {"PNG", ".png", "single-frame SDR PNG to embedded RGBA8 image asset",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "Windows Imaging Component", "Windows built-in PNG decoder", "system component",
         "isolated worker for import",
         "Import only: RGBA8 preview, up to 4M pixels/16 MiB decoded pixels, one frame. Original bytes, DPI/orientation and ICC metadata are retained; color-managed appearance is not certified. No PNG export or CNC vector-cut claim."},
        {"JPEG", ".jpg", "single-frame SDR JPEG to embedded RGBA8 image asset",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "Windows Imaging Component", "Windows built-in JPEG decoder", "system component",
         "isolated worker for import",
         "Import only: RGBA8 preview, up to 4M pixels/16 MiB decoded pixels, one frame. Original bytes, DPI/orientation and ICC metadata are retained; color-managed appearance is not certified. No JPEG export or CNC vector-cut claim."},
        {"TIFF", ".tif", "single-frame SDR TIFF to embedded RGBA8 image asset",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "Windows Imaging Component", "Windows built-in TIFF decoder", "system component",
         "isolated worker for import",
         "Import only: single-frame source up to 8 bits per channel converted to RGBA8, up to 4M pixels/16 MiB decoded pixels. Original bytes, DPI/orientation and ICC metadata are retained; multipage, wider precision and color-managed appearance are not supported. No TIFF export or CNC vector-cut claim."}
    };
}
std::vector<ThirdPartyProvenance> FormatCapabilityRegistry::GetCandidateLibraries() {
    return {
        {"libcdr", "unpinned", "https://gerrit.libreoffice.org/plugins/gitiles/libcdr", "audit pending",
         "Audit selected version and transitive obligations before use", "candidate isolated parser", false},
        {"pdfium", "unpinned", "https://pdfium.googlesource.com/pdfium", "audit pending",
         "Audit selected version and third-party notices before use", "candidate PDF representation parser", false},
        {"libdxfrw", "unpinned", "https://github.com/LibreCAD/libdxfrw", "audit pending",
         "Process isolation does not waive license obligations", "candidate converter", false}
    };
}
bool FormatCapabilityRegistry::IsFormatSupported(std::string_view format_name) noexcept {
    const auto equal_ascii_case_insensitive = [](std::string_view left, std::string_view right) noexcept {
        return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(),
            [](unsigned char a, unsigned char b) noexcept { return std::tolower(a) == std::tolower(b); });
    };
    return equal_ascii_case_insensitive(format_name, "SVG") ||
           equal_ascii_case_insensitive(format_name, ".svg") ||
           equal_ascii_case_insensitive(format_name, "AutoCAD DXF 2D subset") ||
           equal_ascii_case_insensitive(format_name, ".dxf") ||
           equal_ascii_case_insensitive(format_name, "PNG") ||
           equal_ascii_case_insensitive(format_name, ".png") ||
           equal_ascii_case_insensitive(format_name, "JPEG") ||
           equal_ascii_case_insensitive(format_name, ".jpg") ||
           equal_ascii_case_insensitive(format_name, ".jpeg") ||
           equal_ascii_case_insensitive(format_name, "TIFF") ||
           equal_ascii_case_insensitive(format_name, ".tif") ||
           equal_ascii_case_insensitive(format_name, ".tiff") ||
           equal_ascii_case_insensitive(format_name, "CorelDRAW") ||
           equal_ascii_case_insensitive(format_name, "CDR") ||
           equal_ascii_case_insensitive(format_name, ".cdr") ||
           equal_ascii_case_insensitive(format_name, "PDF") ||
           equal_ascii_case_insensitive(format_name, ".pdf") ||
           equal_ascii_case_insensitive(format_name, "Adobe Illustrator") ||
           equal_ascii_case_insensitive(format_name, "AI") ||
           equal_ascii_case_insensitive(format_name, ".ai");
}
std::string FormatCapabilityRegistry::GetDiagnosticForFormat(std::string_view format_name) {
    const auto matrix = GetMatrix();
    const auto equal_ascii_case_insensitive = [](std::string_view left, std::string_view right) {
        return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(),
            [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
    };
    const auto entry = std::find_if(matrix.begin(), matrix.end(), [&](const auto& item) {
        return equal_ascii_case_insensitive(format_name, item.format_name) ||
               equal_ascii_case_insensitive(format_name, item.extension);
    });
    if (entry == matrix.end()) return "Unsupported: format is not listed in the capability matrix.";
    return entry->notes;
}
}
