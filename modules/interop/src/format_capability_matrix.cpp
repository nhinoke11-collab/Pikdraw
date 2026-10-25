#include "pikdraw/interop/format_capability_matrix.hpp"
#include <algorithm>
#include <cctype>

namespace pikdraw::interop {
std::vector<FormatCapabilityEntry> FormatCapabilityRegistry::GetMatrix() {
    return {
        {"SVG static subset", ".svg", "static SVG 1.0/1.1 subset",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::SupportedPrototype,
         "built-in", "repository source", "not applicable", "worker process for import",
         "Static SVG 1.0/1.1 subset only. Import maps presentation opacity/fill-opacity/stroke-opacity attributes on Path and Rect, and opacity/fill-opacity on Text, into canonical Appearance values; percentages are accepted and values clamp to [0,1]. Group opacity, CSS style declarations, gradients/stop opacity, effects and non-Normal blend modes remain unsupported and reject before apply. The shared import RAM gate pauses at 95% usage, resumes when headroom returns, and waits up to five cumulative minutes before timeout; structural profile and IPC bounds still apply. Export maps Path/Text paint and object opacity; export rejects effect nodes and non-Normal blend modes."},
        {"AutoCAD DXF 2D subset", ".dxf", "ASCII DXF dxf-entities-v1",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "built-in", "repository source", "not applicable", "worker process for import",
         "Import only for the explicit 2D entity/unit profile; no DXF export. The shared import RAM gate pauses at 95% usage, resumes when headroom returns, and waits up to five cumulative minutes before timeout; DXF entity/profile and IPC representational bounds remain."},
        {"PDF page-set vector subset", ".pdf", "PDF page-set path/text PDFium subset",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "PDFium", "157.0.8100.0 (pinned binary)", "BSD-3-Clause + bundled notices", "isolated worker process",
         "Editable intent uses pdf-page-set-v3: every inspected source page is imported with its own dimensions, page-scoped layers, and blank-page entries. The worker exposes pdf-print-source-v1 for locked-source import, retaining exact PDF bytes and attaching an RGBA8 preview cache to each page; desktop renders this cache on canvas, while print/export authority remains the source PDF. Cache DPI adapts to page size and set size within a screen-cache pixel budget. The shared import RAM gate pauses at 95% usage, resumes when headroom returns, and waits up to five cumulative minutes before timeout; IPC and format representational bounds remain. No print-quality TIFF/JPEG export from the source PDF is wired yet. Supported editable paths/text become objects and placed images become RGBA8 ImageObjects. Page rotations 0/90/180/270 are normalized into PikDraw's Y-up coordinates; text rotation follows the page rotation. Text retains PDFium family and PostScript base-font names when exposed; missing font identity is rejected instead of silently labeled Arial. Embedded font bytes, glyph placement/source layout, object-level rotated/sheared text, form flattening fidelity, transparency, clipping, source layers, ICC/overprint and round-trip export are not fully preserved in editable intent; production readiness is not assessed."},
        {"Adobe Illustrator PDF-compatible page-set subset", ".ai", "PDF-compatible AI page-set path/text subset",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "PDFium", "157.0.8100.0 (pinned binary)", "BSD-3-Clause + bundled notices", "isolated worker process",
         "Desktop uses ai-pdf-compatible-page-set-v3: every inspected page from the embedded PDF-compatible representation is imported with its own dimensions, page-scoped layers, and blank-page entries; a protocol caller may request one source index. The shared import RAM gate pauses at 95% usage, resumes when headroom returns, and waits up to five cumulative minutes before timeout; IPC and format representational bounds remain. Supported paths/text become editable objects and placed images become RGBA8 ImageObjects. Page rotations 0/90/180/270 are normalized into PikDraw's Y-up coordinates; text rotation follows the page rotation. Text retains PDFium family and PostScript base-font names when exposed; missing identity is rejected instead of silently labeled Arial. Object-level rotated/sheared text, Illustrator private authoring data/artboards, embedded font bytes, glyph source layout, source layers, effects, clipping, ICC/overprint and round-trip export are not preserved in full; production readiness is not assessed."},
        {"CorelDRAW editable vector subset", ".cdr", "CDR RIFF/ZIP through libcdr 0.1.9 and SVG normalization",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "libcdr + librevenge", "0.1.9 / 0.0.6 (pinned MSYS2 UCRT64 runtime)", "MPL-2.0 and bundled notices", "isolated worker process",
         "Supported RIFF CDR and ZIP/ZCF versions accepted by libcdr are converted to editable path/text/group objects; top-level layers are mapped where libcdr emits layer IDs. The shared import RAM gate pauses at 95% usage, resumes when headroom returns, and waits up to five cumulative minutes before timeout; IPC and format representational bounds remain. Embedded PNG/JPEG/TIFF/BMP assets become native ImageObjects, one per placement, with shared pixel storage for repeated assets. No thumbnail fallback is imported. Geometric SVG clip paths that libcdr emits as path masks are retained as editable clipping groups. This is a one-page bounded subset: live effects, other clipping semantics, source color management, exact source layer names, lossless appearance and round-trip export are not preserved; unsupported generated SVG features reject the whole source."},
        {"EPS/PostScript editable path subset", ".eps", "DSC EPSF path/color/transform operators in eps-path-v1",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "PikDraw EPS adapter; optional system Ghostscript for DOS EPS", "eps-path-v1", "repository source; external tool is not bundled", "isolated AppContainer workers",
          "Plain DSC EPS uses the allowlisted native path/color/transform subset; unsupported operators reject the source. The shared import RAM gate pauses at 95% usage, resumes when headroom returns, and waits up to five cumulative minutes before timeout; IPC and profile representational bounds remain. DOS EPS PostScript is interpreted by an externally installed Ghostscript process with AppContainer isolation, no network capability, no child process and bounded stdin/stdout; its PDF output is normalized by PDFium. Text, placed images, clipping, gradients, live effects, source layers, color management and round-trip export are not preserved in full; appearance and production are not assessed. Ghostscript is not bundled, so DOS EPS requires a readable system installation."},
        {"PNG", ".png", "single-frame SDR PNG to embedded RGBA8 image asset",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "Windows Imaging Component", "Windows built-in PNG decoder", "system component",
         "isolated worker for import",
         "Import only: PNG to straight-alpha RGBA8 preview; explicit alpha and tRNS transparency for RGB, grayscale and indexed color are preserved. Original encoded bytes, DPI/orientation and ICC metadata are retained separately. Source and decoded-byte fields are limited by the 32-bit raster record representation; the isolated worker has no fixed per-job or staging byte quota; decoder and source/pixel chunk reservations use the shared 95% system-RAM waiter and resume when headroom returns, with a five-minute cumulative timeout. Host pixel/source staging also waits on refreshed headroom; full-frame buffers still remain resident while the proposal is decoded. Windows WIC copies output in 64-row bands and streams proposal chunks. Color-managed appearance is not certified; no PNG export or CNC vector-cut claim."},
        {"JPEG", ".jpg", "single-frame SDR JPEG to embedded RGBA8 image asset",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "Windows Imaging Component", "Windows built-in JPEG decoder", "system component",
         "isolated worker for import",
         "Import only: one opaque SDR frame converted to straight-alpha RGBA8 preview. Original encoded bytes, DPI/orientation and ICC metadata are retained separately. Source and decoded-byte fields are limited by the 32-bit raster record representation; the isolated worker has no fixed per-job or staging byte quota; decoder and source/pixel chunk reservations use the shared 95% system-RAM waiter and resume when headroom returns, with a five-minute cumulative timeout. Host pixel/source staging also waits on refreshed headroom; full-frame buffers still remain resident while the proposal is decoded. Windows WIC copies output in 64-row bands and streams proposal chunks. Color-managed appearance is not certified; no JPEG export or CNC vector-cut claim."},
        {"TIFF", ".tif", "single-frame SDR/HDR TIFF to embedded RGBA8 image asset",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "Windows Imaging Component", "Windows built-in TIFF decoder", "system component",
         "isolated worker for import",
         "Import only: source frame up to 32 bits per channel converted to straight-alpha RGBA8. Original encoded bytes, DPI/orientation and ICC metadata are retained separately. Source and decoded-byte fields are limited by the 32-bit raster record representation; the isolated worker has no fixed per-job or staging byte quota; decoder and source/pixel chunk reservations use the shared 95% system-RAM waiter and resume when headroom returns, with a five-minute cumulative timeout. Host pixel/source staging also waits on refreshed headroom; full-frame buffers still remain resident while the proposal is decoded. Windows WIC copies output in 64-row bands and streams proposal chunks. Multipage TIFF requires frame selection. BigTIFF supports only one uncompressed contiguous 8-bit RGB(A) strip with default orientation and no embedded ICC; the current worker/source representation still cannot carry source lengths above U32. No TIFF export or CNC vector-cut claim."},
        {"BMP", ".bmp", "single-frame BMP to embedded RGBA8 image asset",
         FormatCapabilityStatus::SupportedPrototype, FormatCapabilityStatus::UnsupportedReject,
         "Windows Imaging Component", "Windows built-in BMP decoder", "system component",
         "isolated worker for import",
         "Import only: standard BMP frame converted to straight-alpha RGBA8 preview. Original encoded bytes, DPI/orientation and ICC metadata are retained separately. Source and decoded-byte fields are limited by the 32-bit raster record representation; the isolated worker has no fixed per-job or staging byte quota; decoder and source/pixel chunk reservations use the shared 95% system-RAM waiter and resume when headroom returns, with a five-minute cumulative timeout. Host pixel/source staging also waits on refreshed headroom; full-frame buffers still remain resident while the proposal is decoded. Windows WIC copies output in 64-row bands and streams proposal chunks. No BMP export or CNC vector-cut claim."}
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