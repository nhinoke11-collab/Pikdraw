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
        unsupported("PDF", ".pdf", "No PDF adapter; IPC or preview output is not PDF support."),
        unsupported("Adobe Illustrator", ".ai", "No AI source adapter or round-trip guarantee."),
        unsupported("CorelDRAW", ".cdr", "No CDR source adapter or round-trip guarantee."),
        unsupported("EPS", ".eps", "No EPS/PostScript adapter or round-trip guarantee."),
        unsupported("PNG", ".png", "No raster import/export adapter is implemented."),
        unsupported("JPEG", ".jpg", "No raster import/export adapter is implemented.")
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
           equal_ascii_case_insensitive(format_name, ".dxf");
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
