#include "pikdraw/interop/format_capability_matrix.hpp"
namespace pikdraw::interop {
std::vector<FormatCapabilityEntry> FormatCapabilityRegistry::GetMatrix() {
    std::vector<FormatCapabilityEntry> result;
    for (const auto& format : std::vector<std::pair<std::string, std::string>>{
        {"SVG Native Subset", ".svg"}, {"PDF Vector Representation", ".pdf"},
        {"Adobe Illustrator (PDF-Compatible)", ".ai"}, {"CorelDRAW Vector Graphics", ".cdr"},
        {"AutoCAD DXF", ".dxf"}}) {
        result.push_back({format.first, format.second, "unverified",
            FormatCapabilityStatus::UnsupportedReject, FormatCapabilityStatus::UnsupportedReject,
            "unselected", "unpinned", "audit pending", "OS containment required; not_measured",
            "No source-format adapter implemented. IPC encoding is not import/export support."});
    }
    return result;
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
bool FormatCapabilityRegistry::IsFormatSupported(std::string_view) noexcept { return false; }
std::string FormatCapabilityRegistry::GetDiagnosticForFormat(std::string_view) {
    return "Unsupported: adapter, licensed corpus and fidelity measurements are unavailable.";
}
}
