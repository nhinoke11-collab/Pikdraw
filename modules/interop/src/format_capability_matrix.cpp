#include "pikdraw/interop/format_capability_matrix.hpp"
#include <algorithm>
#include <cctype>

namespace pikdraw::interop {
std::vector<FormatCapabilityEntry> FormatCapabilityRegistry::GetMatrix() {
    return {
        {"SVG static subset", ".svg", "static SVG 1.0/1.1 subset",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::SupportedPrototype,
         "built-in", "repository source", "not applicable", "worker process for import",
         "Static SVG subset only. Export maps Path/Text paint and object opacity; import does not yet accept SVG opacity attributes. Export rejects effect nodes and non-Normal blend modes."},
        {"AutoCAD DXF 2D subset", ".dxf", "ASCII DXF dxf-entities-v1",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "built-in", "repository source", "not applicable", "worker process for import",
         "Import only for the explicit 2D entity/unit profile; no DXF export."},
        {"PDF page-set vector subset", ".pdf", "PDF page-set path/text PDFium subset",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "PDFium", "157.0.8100.0 (pinned binary)", "BSD-3-Clause + bundled notices", "isolated worker process",
         "Desktop uses pdf-page-set-v3: every inspected source page is imported with its own dimensions, page-scoped layers, and blank-page entries; a protocol caller may request one source index. Supported paths/text become editable objects and placed images become RGBA8 ImageObjects. Page rotations 0/90/180/270 are normalized into PikDraw's Y-up coordinates; text rotation follows the page rotation. Text retains PDFium family and PostScript base-font names when exposed; missing font identity is rejected instead of silently labeled Arial. Embedded font bytes, glyph placement/source layout, object-level rotated/sheared text, form flattening fidelity, transparency, clipping, source layers, ICC/overprint and round-trip export are not fully preserved; production readiness is not assessed."},
        {"Adobe Illustrator PDF-compatible page-set subset", ".ai", "PDF-compatible AI page-set path/text subset",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "PDFium", "157.0.8100.0 (pinned binary)", "BSD-3-Clause + bundled notices", "isolated worker process",
         "Desktop uses ai-pdf-compatible-page-set-v3: every inspected page from the embedded PDF-compatible representation is imported with its own dimensions, page-scoped layers, and blank-page entries; a protocol caller may request one source index. Supported paths/text become editable objects and placed images become RGBA8 ImageObjects. Page rotations 0/90/180/270 are normalized into PikDraw's Y-up coordinates; text rotation follows the page rotation. Text retains PDFium family and PostScript base-font names when exposed; missing identity is rejected instead of silently labeled Arial. Object-level rotated/sheared text, Illustrator private authoring data/artboards, embedded font bytes, glyph source layout, source layers, effects, clipping, ICC/overprint and round-trip export are not preserved in full; production readiness is not assessed."},
        {"CorelDRAW editable vector subset", ".cdr", "CDR RIFF/ZIP through libcdr 0.1.9 and SVG normalization",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "libcdr + librevenge", "0.1.9 / 0.0.6 (pinned MSYS2 UCRT64 runtime)", "MPL-2.0 and bundled notices", "isolated worker process",
         "Supported RIFF CDR and ZIP/ZCF versions accepted by libcdr are converted to editable path/text/group objects; top-level layers are mapped where libcdr emits layer IDs. Embedded PNG/JPEG/TIFF/BMP assets become native ImageObjects, one per placement, with shared pixel storage for repeated assets. No thumbnail fallback is imported. Geometric SVG clip paths that libcdr emits as path masks are retained as editable clipping groups. This is a one-page bounded subset: live effects, other clipping semantics, source color management, exact source layer names, lossless appearance and round-trip export are not preserved; unsupported generated SVG features reject the whole source."},
        {"EPS/PostScript editable path subset", ".eps", "DSC EPSF path/color/transform operators in eps-path-v1",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "PikDraw EPS adapter; optional system Ghostscript for DOS EPS", "eps-path-v1", "repository source; external tool is not bundled", "isolated AppContainer workers",
         "Plain DSC EPS uses the allowlisted native path/color/transform subset; unsupported operators reject the source. DOS EPS PostScript is interpreted by an externally installed Ghostscript process with AppContainer isolation, no network capability, no child process and bounded stdin/stdout; its PDF output is normalized by PDFium. Text, placed images, clipping, gradients, live effects, source layers, color management and round-trip export are not preserved in full; appearance and production are not assessed. Ghostscript is not bundled, so DOS EPS requires a readable system installation."},
        {"PNG", ".png", "single-frame SDR PNG to embedded RGBA8 image asset",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "Windows Imaging Component", "Windows built-in PNG decoder", "system component",
         "isolated worker for import",
         "Import only: 8-bit non-interlaced PNG to straight-alpha RGBA8 preview; explicit alpha and tRNS transparency for RGB, grayscale and indexed color are preserved. Original encoded bytes, DPI/orientation and ICC metadata are retained separately. No fixed file-byte or pixel quota; U32 protocol/storage representation bounds remain and import stops at 90% whole-system physical RAM. Color-managed appearance is not certified; no PNG export or CNC vector-cut claim."},
        {"JPEG", ".jpg", "single-frame SDR JPEG to embedded RGBA8 image asset",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "Windows Imaging Component", "Windows built-in JPEG decoder", "system component",
         "isolated worker for import",
         "Import only: one opaque SDR frame converted to straight-alpha RGBA8 preview. Original encoded bytes, DPI/orientation and ICC metadata are retained separately. No fixed file-byte or pixel quota; U32 protocol/storage representation bounds remain and import stops at 90% whole-system physical RAM. Color-managed appearance is not certified; no JPEG export or CNC vector-cut claim."},
        {"TIFF", ".tif", "single-frame SDR/HDR TIFF to embedded RGBA8 image asset",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "Windows Imaging Component", "Windows built-in TIFF decoder", "system component",
         "isolated worker for import",
         "Import only: source frame up to 32 bits per channel converted to straight-alpha RGBA8. Original encoded bytes, DPI/orientation and ICC metadata are retained separately. No fixed file-byte or pixel quota; U32 protocol/storage representation bounds remain and import stops at 90% whole-system physical RAM. Multipage TIFF requires frame selection; no TIFF export or CNC vector-cut claim."},
        {"BMP", ".bmp", "single-frame BMP to embedded RGBA8 image asset",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "Windows Imaging Component", "Windows built-in BMP decoder", "system component",
         "isolated worker for import",
         "Import only: standard BMP frame converted to straight-alpha RGBA8 preview. Original encoded bytes, DPI/orientation and ICC metadata are retained separately. No fixed file-byte or pixel quota; U32 protocol/storage representation bounds remain and import stops at 90% whole-system physical RAM. No BMP export or CNC vector-cut claim."}
    };
}
std::vector<ThirdPartyProvenance> FormatCapabilityRegistry::GetCandidateLibraries() {
    return {
        {"libcdr", "0.1.9-1 / librevenge 0.0.6-1 (MSYS2 UCRT64)", "https://github.com/msys2/MINGW-packages/tree/master/mingw-w64-libcdr", "MPL-2.0; transitive notices bundled",
         "Pinned package archive SHA-256 values are recorded in third_party/libcdr.lock.json", "isolated CDR parser and SVG conversion", true},
        {"pdfium", "157.0.8100.0 / chromium/8100", "https://github.com/bblanchon/pdfium-binaries/releases/tag/chromium%2F8100", "BSD-3-Clause + bundled third-party notices; package MIT",
         "Pinned Windows x64 binary SHA-256 and bundled notices recorded in third_party/pdfium.lock.json", "isolated PDF path/text parser", true},
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
           equal_ascii_case_insensitive(format_name, "PDF") ||
           equal_ascii_case_insensitive(format_name, ".pdf") ||
           equal_ascii_case_insensitive(format_name, "Adobe Illustrator") ||
           equal_ascii_case_insensitive(format_name, ".ai") ||
           equal_ascii_case_insensitive(format_name, "CorelDRAW") ||
           equal_ascii_case_insensitive(format_name, ".cdr") ||
           equal_ascii_case_insensitive(format_name, "EPS/PostScript editable path subset") ||
           equal_ascii_case_insensitive(format_name, ".eps") ||
           equal_ascii_case_insensitive(format_name, "PNG") ||
           equal_ascii_case_insensitive(format_name, ".png") ||
           equal_ascii_case_insensitive(format_name, "JPEG") ||
           equal_ascii_case_insensitive(format_name, ".jpg") ||
           equal_ascii_case_insensitive(format_name, ".jpeg") ||
           equal_ascii_case_insensitive(format_name, "TIFF") ||
           equal_ascii_case_insensitive(format_name, ".tif") ||
           equal_ascii_case_insensitive(format_name, ".tiff") ||
           equal_ascii_case_insensitive(format_name, "BMP") ||
           equal_ascii_case_insensitive(format_name, ".bmp");
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
