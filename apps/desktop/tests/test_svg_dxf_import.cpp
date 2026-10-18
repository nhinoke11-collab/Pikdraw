#include "pikdraw/desktop/workflows/document_workflow.hpp"
#include "pikdraw/desktop/runtime/composition_root.hpp"
#include "pikdraw/interop/adapters/raster_worker_adapter.hpp"

#include <cassert>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <vector>

using namespace pikdraw;
using namespace pikdraw::desktop;

namespace {
std::filesystem::path local_corpus() {
    auto current = std::filesystem::current_path();
    for (int depth = 0; depth < 6; ++depth) {
        if (std::filesystem::is_directory(current / "cdr")) return current / "cdr";
        if (current == current.parent_path()) break;
        current = current.parent_path();
    }
    return {};
}
constexpr char kSvgFixture[] = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="30mm" height="20mm" viewBox="0 0 30 20">
  <g id="art-layer"><rect id="panel" x="2" y="3" width="12" height="8" fill="#2575fc"/></g>
</svg>)SVG";
constexpr char kDxfFixture[] = R"DXF(0
SECTION
2
HEADER
9
$INSUNITS
70
4
0
ENDSEC
0
SECTION
2
TABLES
0
TABLE
2
LAYER
70
2
0
LAYER
2
CUT
70
0
62
1
6
CONTINUOUS
0
LAYER
2
ENGRAVE
70
0
62
5
6
CONTINUOUS
0
ENDTAB
0
ENDSEC
0
SECTION
2
ENTITIES
0
LINE
5
10
8
CUT
10
0
20
0
30
0
11
20
21
0
31
0
0
LWPOLYLINE
5
11
8
CUT
90
3
70
1
10
20
20
0
42
0.414213562373095
10
30
20
10
10
20
20
20
0
ARC
5
12
8
ENGRAVE
10
50
20
50
30
0
40
10
50
0
51
90
0
CIRCLE
5
13
8
ENGRAVE
10
80
20
50
30
0
40
5
0
ENDSEC
0
EOF
)DXF";

void append_png_be32(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(static_cast<uint8_t>(value >> 24));
    bytes.push_back(static_cast<uint8_t>(value >> 16));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value));
}

uint32_t png_crc32(std::span<const uint8_t> bytes) {
    uint32_t crc = 0xFFFFFFFFu;
    for (const auto byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return ~crc;
}

void append_png_chunk(std::vector<uint8_t>& png, const char type[4],
                      std::span<const uint8_t> payload) {
    append_png_be32(png, static_cast<uint32_t>(payload.size()));
    const size_t crc_start = png.size();
    for (size_t index = 0; index < 4; ++index)
        png.push_back(static_cast<uint8_t>(type[index]));
    png.insert(png.end(), payload.begin(), payload.end());
    append_png_be32(png, png_crc32(std::span<const uint8_t>(png).subspan(crc_start)));
}

std::vector<uint8_t> make_png_fixture() {
    std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> header;
    append_png_be32(header, 2);
    append_png_be32(header, 1);
    header.insert(header.end(), {8, 6, 0, 0, 0});
    append_png_chunk(png, "IHDR", header);

    const std::vector<uint8_t> scanline{0, 255, 0, 0, 255, 0, 255, 0, 128};
    std::vector<uint8_t> zlib{0x78, 0x01, 0x01};
    const auto length = static_cast<uint16_t>(scanline.size());
    zlib.push_back(static_cast<uint8_t>(length));
    zlib.push_back(static_cast<uint8_t>(length >> 8));
    const auto inverse = static_cast<uint16_t>(~length);
    zlib.push_back(static_cast<uint8_t>(inverse));
    zlib.push_back(static_cast<uint8_t>(inverse >> 8));
    zlib.insert(zlib.end(), scanline.begin(), scanline.end());
    uint32_t a = 1, b = 0;
    for (const auto byte : scanline) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    append_png_be32(zlib, (b << 16) | a);
    append_png_chunk(png, "IDAT", zlib);
    append_png_chunk(png, "IEND", {});
    return png;
}

void test_isolated_import_and_atomic_document_commit() {
    const auto source_path = std::filesystem::temp_directory_path() /
                             L"pikdraw-desktop-import-fixture.svg";
    {
        std::ofstream source(source_path, std::ios::binary | std::ios::trunc);
        assert(source && source.write(kSvgFixture, sizeof(kSvgFixture) - 1));
    }

    CompositionRoot root;
    root.initialize(900.0, 600.0, 1.0);
    DocumentWorkflow workflow(root);
    const auto initial_page = root.document().current_snapshot()->page();
    const auto metadata = workflow.begin_import_job(1);
    const auto imported = run_import_worker_job(source_path.wstring(), ImportFormat::Svg,
                                                metadata, std::stop_token{});
    if (!imported.success) std::cerr << "SVG import failed: " << imported.error_message << '\n';
    assert(imported.success && !imported.cancelled);
    assert(imported.proposal.profile_id == "svg-hierarchy-v2");
    assert(imported.proposal.source_page_width_mm == 30.0);
    assert(imported.proposal.source_page_height_mm == 20.0);
    assert(imported.proposal.layers.size() == 1 && !imported.proposal.objects.empty());

    const size_t imported_count = imported.proposal.objects.size();
    const auto commit = workflow.commit_import_proposal(metadata, imported.proposal);
    assert(commit == document::ProposalStatus::Accepted);
    auto snapshot = root.document().current_snapshot();
    assert(snapshot->object_count() == imported_count);
    assert(snapshot->page().layers.size() == initial_page.layers.size() + 1);
    assert(snapshot->page().width_mm == initial_page.width_mm);
    assert(snapshot->page().height_mm == initial_page.height_mm);
    assert(root.document().undo());
    snapshot = root.document().current_snapshot();
    assert(snapshot->object_count() == 0);
    assert(snapshot->page().layers.size() == initial_page.layers.size());
    assert(root.document().redo());
    assert(root.document().current_snapshot()->object_count() == imported_count);

    const auto stale_metadata = workflow.begin_import_job(2);
    document::PathObject intervening;
    intervening.id = document::generate_object_id();
    intervening.path.move_to(0, 0);
    intervening.path.line_to(2, 2);
    assert(root.document().execute_command(
        std::make_unique<document::AddObjectCommand>(intervening)).success);
    const auto stale_commit = workflow.commit_import_proposal(stale_metadata, imported.proposal);
    assert(stale_commit == document::ProposalStatus::StaleRevision);
    assert(root.document().current_snapshot()->object_count() == imported_count + 1);
}

void test_dxf_profile_and_source_signature_rejection() {
    const auto dxf_path = std::filesystem::temp_directory_path() / L"pikdraw-desktop-import-fixture.dxf";
    {
        std::ofstream source(dxf_path, std::ios::binary | std::ios::trunc);
        assert(source && source.write(kDxfFixture, sizeof(kDxfFixture) - 1));
    }
    CompositionRoot root;
    root.initialize(800.0, 600.0, 1.0);
    DocumentWorkflow workflow(root);
    const auto metadata = workflow.begin_import_job(1);
    const auto imported = run_import_worker_job(dxf_path.wstring(), ImportFormat::Dxf,
                                                metadata, std::stop_token{});
    std::error_code ignored;
    std::filesystem::remove(dxf_path, ignored);
    if (!imported.success) std::cerr << "DXF import failed: " << imported.error_message << '\n';
    assert(imported.success && imported.proposal.profile_id == "dxf-entities-v1");
    assert(imported.proposal.layers.size() == 2);
    assert(imported.proposal.layers.front().name == "CUT");
    assert(imported.proposal.source_page_width_mm > 0.0);
    assert(imported.proposal.source_page_height_mm > 0.0);
    assert(workflow.commit_import_proposal(metadata, imported.proposal) ==
           document::ProposalStatus::Accepted);
    const auto snapshot = root.document().current_snapshot();
    assert(snapshot->object_count() == 4);
    assert(snapshot->page().layers[snapshot->page().layers.size() - 2].name == "CUT");
    assert(snapshot->page().layers.back().name == "ENGRAVE");
    assert(root.document().undo() && root.document().current_snapshot()->object_count() == 0);

    const auto mismatch_path = std::filesystem::temp_directory_path() / L"pikdraw-import-signature-mismatch.dxf";
    {
        std::ofstream source(mismatch_path, std::ios::binary | std::ios::trunc);
        assert(source && source.write(kSvgFixture, sizeof(kSvgFixture) - 1));
    }
    const auto mismatch_metadata = workflow.begin_import_job(2);
    const auto mismatch = run_import_worker_job(mismatch_path.wstring(), ImportFormat::Dxf,
                                                mismatch_metadata, std::stop_token{});
    std::filesystem::remove(mismatch_path, ignored);
    assert(!mismatch.success && !mismatch.cancelled);
    assert(mismatch.error_message.find("signature") != std::string::npos);
    assert(root.document().current_snapshot()->object_count() == 0);
    workflow.cancel_import_job(mismatch_metadata.job_id);

    const auto cancel_metadata = workflow.begin_import_job(3);
    std::stop_source cancellation;
    cancellation.request_stop();
    const auto cancelled = run_import_worker_job(mismatch_path.wstring(), ImportFormat::Dxf,
                                                 cancel_metadata, cancellation.get_token());
    assert(cancelled.cancelled && !cancelled.success);
    assert(root.document().current_snapshot()->object_count() == 0);
    workflow.cancel_import_job(cancel_metadata.job_id);
}

void test_png_raster_import_retains_source_and_is_undoable() {
    const auto source_path = std::filesystem::temp_directory_path() /
                             L"pikdraw-desktop-import-fixture.png";
    const auto source_bytes = make_png_fixture();
    {
        std::ofstream source(source_path, std::ios::binary | std::ios::trunc);
        assert(source && source.write(reinterpret_cast<const char*>(source_bytes.data()),
                                      static_cast<std::streamsize>(source_bytes.size())));
    }

    CompositionRoot root;
    root.initialize(900.0, 600.0, 1.0);
    DocumentWorkflow workflow(root);
    const auto initial_page = root.document().current_snapshot()->page();
    const auto metadata = workflow.begin_import_job(1);
    // Svg is the existing generic non-DXF dialog route; the workflow chooses its worker by signature.
    const auto imported = run_import_worker_job(source_path.wstring(), ImportFormat::Svg,
                                                metadata, std::stop_token{});
    std::error_code ignored;
    std::filesystem::remove(source_path, ignored);
    if (!imported.success) std::cerr << "PNG import failed: " << imported.error_message << '\n';
    assert(imported.success && imported.proposal.format_name == "PNG");
    assert(imported.proposal.profile_id == "raster-rgba8-v1");
    assert(imported.proposal.layers.size() == 1 && imported.proposal.objects.size() == 1);
    const auto* image = std::get_if<document::ImageObject>(&imported.proposal.objects.front());
    assert(image && image->width_px == 2 && image->height_px == 1);
    assert(image->pixels_rgba == std::vector<uint32_t>({0xFF0000FFu, 0x00FF0080u}));
    assert(image->source.media_type == "image/png");
    assert(image->source.original_bytes == source_bytes);
    assert(image->source.frame_count == 1 && image->source.frame_index == 0);
    assert(image->source.dpi_x > 0 && image->source.dpi_y > 0);

    assert(workflow.commit_import_proposal(metadata, imported.proposal) ==
           document::ProposalStatus::Accepted);
    auto snapshot = root.document().current_snapshot();
    assert(snapshot->object_count() == 1);
    assert(snapshot->page().layers.size() == initial_page.layers.size() + 1);
    assert(snapshot->page().width_mm == initial_page.width_mm);
    assert(snapshot->page().height_mm == initial_page.height_mm);
    assert(root.document().undo());
    assert(root.document().current_snapshot()->object_count() == 0);
    assert(root.document().redo());
    snapshot = root.document().current_snapshot();
    const auto obj_entry = *snapshot->objects().all().begin();
    const auto* committed_image = std::get_if<document::ImageObject>(&obj_entry.second);
    assert(committed_image && committed_image->source.original_bytes == source_bytes);
}

void test_cdr_fast_preview_import() {
    if (local_corpus().empty()) return;
    const std::vector<std::filesystem::path> paths = {
        local_corpus() / "KC12.cdr",
        local_corpus() / "s11.cdr",
        local_corpus() / "[ PikDecor.vn ] 260312_283.cdr",
        local_corpus() / (const char8_t*)u8"Bóc tách sân khấu Minh Châu Event (1).cdr"
    };
    for (const auto& p : paths) {
        if (!std::filesystem::exists(p)) continue;
        std::wcout << L"Testing CDR: " << p.filename().wstring() << std::endl;
        CompositionRoot root;
        root.initialize();
        DocumentWorkflow workflow(root);
        workflow.new_document(210.0, 297.0, color::DocumentColorMode::Rgb, 96);
        const auto metadata = workflow.begin_import_job(1);
        const auto imported = run_import_worker_job(p.wstring(), ImportFormat::Svg,
                                                    metadata, std::stop_token{}, nullptr);
        if (!imported.success) {
            std::wcerr << L"CDR import failed for " << p.filename().wstring() << L": "
                       << std::wstring(imported.error_message.begin(), imported.error_message.end()) << L'\n';
        }
        assert(imported.success);
        assert(imported.proposal.format_name == "CorelDRAW");
        assert(imported.proposal.profile_id == "cdr-preview-v1");
        assert(imported.proposal.objects.size() == 1);
        const auto& image = std::get<document::ImageObject>(imported.proposal.objects.front());
        assert(std::abs(image.bounds_rect.width - imported.proposal.source_page_width_mm) < 0.01);
        assert(std::abs(image.bounds_rect.height - imported.proposal.source_page_height_mm) < 0.01);
        assert(workflow.commit_import_proposal(metadata, imported.proposal) == document::ProposalStatus::Accepted);
        assert(root.document().undo() && root.document().current_snapshot()->object_count() == 0);
    }
}

void test_pdf_ai_preview_import() {
    if (local_corpus().empty()) return;
    const std::vector<std::filesystem::path> paths = {
        local_corpus() / "11695530.ai"
    };
    for (const auto& p : paths) {
        if (!std::filesystem::exists(p)) continue;
        std::wcout << L"Testing PDF/AI: " << p.filename().wstring() << std::endl;
        CompositionRoot root;
        root.initialize();
        DocumentWorkflow workflow(root);
        workflow.new_document(210.0, 297.0, color::DocumentColorMode::Rgb, 96);
        const auto metadata = workflow.begin_import_job(1);
        const auto imported = run_import_worker_job(p.wstring(), ImportFormat::Svg,
                                                    metadata, std::stop_token{}, nullptr);
        if (!imported.success) {
            std::wcerr << L"PDF/AI import failed for " << p.filename().wstring() << L": "
                       << std::wstring(imported.error_message.begin(), imported.error_message.end()) << L'\n';
        }
        assert(imported.success);
        assert(imported.proposal.format_name == "AI" || imported.proposal.format_name == "PDF");
        assert(imported.proposal.profile_id == "pdf-ai-preview-v1");
        assert(imported.proposal.source_page_width_mm > 0.0);
        assert(imported.proposal.source_page_height_mm > 0.0);
        assert(imported.proposal.objects.size() == 1);
        const auto& image = std::get<document::ImageObject>(imported.proposal.objects.front());
        assert(image.width_px == 256 && image.height_px == 180);
        assert(image.source.media_type == "image/jpeg");
        assert(std::any_of(image.pixels_rgba.begin(), image.pixels_rgba.end(),
            [](uint32_t pixel) { return ((pixel >> 24) & 255) < 128; }));
        assert(std::abs(image.bounds_rect.width - imported.proposal.source_page_width_mm) < 0.01);
        assert(workflow.commit_import_proposal(metadata, imported.proposal) == document::ProposalStatus::Accepted);
        assert(root.document().undo() && root.document().current_snapshot()->object_count() == 0);
    }
}
void test_large_local_jpeg_import() {
    if (local_corpus().empty()) return;
    size_t checked = 0;
    for (const auto& entry : std::filesystem::directory_iterator(local_corpus())) {
        if (entry.path().extension() != ".jpg") continue;
        CompositionRoot root;
        root.initialize();
        DocumentWorkflow workflow(root);
        const auto metadata = workflow.begin_import_job(1);
        auto imported = run_import_worker_job(entry.path().wstring(), ImportFormat::Svg,
            metadata, std::stop_token{});
        if (!imported.success) {
            std::cerr << "JPEG corpus import failed: " << imported.error_message << '\n';
            std::ifstream input(entry.path(), std::ios::binary);
            const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
            std::vector<interop::CanonicalImportRecord> records;
            std::string diagnostic;
            const bool decoded = interop::BuildRasterImportRecords(bytes, records, diagnostic);
            std::cerr << "Host adapter result: " << decoded << ": " << diagnostic << '\n';
        }
        assert(imported.success);
        const auto& image = std::get<document::ImageObject>(imported.proposal.objects.front());
        assert(!image.source.original_bytes.empty());
        if (image.pixels_rgba.size() > 4u * 1024u * 1024u) ++checked;
        assert(workflow.commit_import_proposal(metadata, std::move(imported.proposal)) == document::ProposalStatus::Accepted);
        assert(root.document().undo());
    }
    assert(checked >= 2);
}

}

int main() {
    test_isolated_import_and_atomic_document_commit();
    test_dxf_profile_and_source_signature_rejection();
    test_png_raster_import_retains_source_and_is_undoable();
    test_cdr_fast_preview_import();
    test_pdf_ai_preview_import();
    test_large_local_jpeg_import();
    std::cout << "Desktop isolated SVG/DXF/raster/CDR/PDF/AI import workflow tests passed\n";
    return 0;
}
