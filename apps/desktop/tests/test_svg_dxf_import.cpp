#include "pikdraw/desktop/workflows/document_workflow.hpp"
#include "pikdraw/desktop/runtime/composition_root.hpp"
#include "pikdraw/interop/adapters/cdr_extractor.hpp"
#include "pikdraw/interop/adapters/cdr_worker_adapter.hpp"
#include "pikdraw/interop/adapters/pdf_ai_extractor.hpp"
#include "pikdraw/interop/adapters/raster_worker_adapter.hpp"
#include "pikdraw/document/commands/page_commands.hpp"

#include <cassert>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <utility>
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

void append_zip_le16(std::vector<uint8_t>& bytes, uint16_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
}

void append_zip_le32(std::vector<uint8_t>& bytes, uint32_t value) {
    append_zip_le16(bytes, static_cast<uint16_t>(value));
    append_zip_le16(bytes, static_cast<uint16_t>(value >> 16));
}

std::vector<uint8_t> make_cdr_preview_fixture() {
    const auto png = make_png_fixture();
    constexpr std::string_view name = "previews/thumbnail.png";
    const auto crc = png_crc32(png);
    std::vector<uint8_t> cdr;
    append_zip_le32(cdr, 0x04034b50);
    append_zip_le16(cdr, 20);
    append_zip_le16(cdr, 0);
    append_zip_le16(cdr, 0);
    append_zip_le16(cdr, 0);
    append_zip_le16(cdr, 0);
    append_zip_le32(cdr, crc);
    append_zip_le32(cdr, static_cast<uint32_t>(png.size()));
    append_zip_le32(cdr, static_cast<uint32_t>(png.size()));
    append_zip_le16(cdr, static_cast<uint16_t>(name.size()));
    append_zip_le16(cdr, 0);
    cdr.insert(cdr.end(), name.begin(), name.end());
    cdr.insert(cdr.end(), png.begin(), png.end());

    const uint32_t central_directory_offset = static_cast<uint32_t>(cdr.size());
    append_zip_le32(cdr, 0x02014b50);
    append_zip_le16(cdr, 20);
    append_zip_le16(cdr, 20);
    append_zip_le16(cdr, 0);
    append_zip_le16(cdr, 0);
    append_zip_le16(cdr, 0);
    append_zip_le16(cdr, 0);
    append_zip_le32(cdr, crc);
    append_zip_le32(cdr, static_cast<uint32_t>(png.size()));
    append_zip_le32(cdr, static_cast<uint32_t>(png.size()));
    append_zip_le16(cdr, static_cast<uint16_t>(name.size()));
    append_zip_le16(cdr, 0);
    append_zip_le16(cdr, 0);
    append_zip_le16(cdr, 0);
    append_zip_le16(cdr, 0);
    append_zip_le32(cdr, 0);
    append_zip_le32(cdr, 0);
    cdr.insert(cdr.end(), name.begin(), name.end());

    const uint32_t central_directory_size = static_cast<uint32_t>(cdr.size()) - central_directory_offset;
    append_zip_le32(cdr, 0x06054b50);
    append_zip_le16(cdr, 0);
    append_zip_le16(cdr, 0);
    append_zip_le16(cdr, 1);
    append_zip_le16(cdr, 1);
    append_zip_le32(cdr, central_directory_size);
    append_zip_le32(cdr, central_directory_offset);
    append_zip_le16(cdr, 0);
    return cdr;
}

std::string base64_encode(std::span<const uint8_t> bytes) {
    constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((bytes.size() + 2) / 3) * 4);
    for (size_t offset = 0; offset < bytes.size(); offset += 3) {
        const uint32_t remaining = static_cast<uint32_t>(bytes.size() - offset);
        const uint32_t block = (static_cast<uint32_t>(bytes[offset]) << 16) |
            (remaining > 1 ? static_cast<uint32_t>(bytes[offset + 1]) << 8 : 0) |
            (remaining > 2 ? static_cast<uint32_t>(bytes[offset + 2]) : 0);
        encoded.push_back(alphabet[(block >> 18) & 63]);
        encoded.push_back(alphabet[(block >> 12) & 63]);
        encoded.push_back(remaining > 1 ? alphabet[(block >> 6) & 63] : '=');
        encoded.push_back(remaining > 2 ? alphabet[block & 63] : '=');
    }
    return encoded;
}

std::vector<uint8_t> make_ai_with_preview_fixture() {
    const auto png = make_png_fixture();
    const std::string source =
        "%PDF-1.5\n/Creator (Adobe Illustrator 24.0) /MediaBox [0 0 100 100]\n"
        "<x:xmpmeta><xmpGImg:image>" + base64_encode(png) +
        "</xmpGImg:image></x:xmpmeta>\n%%EOF\n";
    return {source.begin(), source.end()};
}

std::vector<uint8_t> make_dos_eps_fixture() {
    constexpr std::string_view postscript =
        "%!PS-Adobe-3.0 EPSF-3.0\n"
        "%%BoundingBox: 0 0 100 100\n"
        "newpath 10 10 moveto 90 10 lineto 90 90 lineto closepath "
        "1 0 0 setrgbcolor fill showpage\n%%EOF\n";
    std::vector<uint8_t> source(30 + postscript.size(), 0);
    constexpr std::array<uint8_t, 4> magic{0xC5, 0xD0, 0xD3, 0xC6};
    std::copy(magic.begin(), magic.end(), source.begin());
    const auto write_u32 = [&](size_t offset, uint32_t value) {
        source[offset] = static_cast<uint8_t>(value);
        source[offset + 1] = static_cast<uint8_t>(value >> 8);
        source[offset + 2] = static_cast<uint8_t>(value >> 16);
        source[offset + 3] = static_cast<uint8_t>(value >> 24);
    };
    write_u32(4, 30);
    write_u32(8, static_cast<uint32_t>(postscript.size()));
    source[28] = 0xFF;
    source[29] = 0xFF;
    std::copy(postscript.begin(), postscript.end(), source.begin() + 30);
    return source;
}

std::vector<uint8_t> make_vector_pdf_fixture(bool ai_representation = false,
                                             bool include_blank_second_page = false) {
    const std::string stream =
        "q\n1 0 0 rg\n10 10 40 20 re\nf\nQ\n"
        "BT\n/F1 12 Tf\n0 0 0 rg\n20 60 Td\n(Editable text) Tj\nET\n";
    std::string page_tree = "<< /Type /Pages /Kids [3 0 R";
    if (include_blank_second_page) page_tree += " 6 0 R";
    page_tree += "] /Count " + std::string(include_blank_second_page ? "2" : "1") + " >>";
    std::vector<std::string> objects{
        ai_representation
            ? "<< /Type /Catalog /Pages 2 0 R /Creator (Adobe Illustrator 2025) >>"
            : "<< /Type /Catalog /Pages 2 0 R >>",
        std::move(page_tree),
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] "
        "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
        "<< /Length " + std::to_string(stream.size()) + ">>\nstream\n" + stream + "endstream",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"
    };
    if (include_blank_second_page)
        objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 150] >>");
    std::string pdf = "%PDF-1.4\n";
    std::vector<size_t> offsets(objects.size() + 1, 0);
    for (size_t index = 0; index < objects.size(); ++index) {
        offsets[index + 1] = pdf.size();
        pdf += std::to_string(index + 1) + " 0 obj\n" + objects[index] + "\nendobj\n";
    }
    const size_t xref_offset = pdf.size();
    pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (size_t index = 1; index < offsets.size(); ++index) {
        char line[32]{};
        std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offsets[index]);
        pdf += line;
    }
    pdf += "trailer\n<< /Size " + std::to_string(objects.size() + 1) +
        " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref_offset) + "\n%%EOF\n";
    return {pdf.begin(), pdf.end()};
}

class TemporaryImportSource final {
public:
    explicit TemporaryImportSource(std::string_view extension) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("pikdraw-import-capability-" + std::to_string(stamp) + std::string(extension));
    }

    ~TemporaryImportSource() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    bool write(std::span<const uint8_t> bytes) const {
        std::ofstream file(path_, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(file);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

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
    assert(imported.proposal.profile_id == "svg-hierarchy-v3");
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

void test_import_job_keeps_its_captured_page_target() {
    const auto source_path = std::filesystem::temp_directory_path() /
                             L"pikdraw-import-page-target-fixture.svg";
    {
        std::ofstream source(source_path, std::ios::binary | std::ios::trunc);
        assert(source && source.write(kSvgFixture, sizeof(kSvgFixture) - 1));
    }

    CompositionRoot root;
    root.initialize(900.0, 600.0, 1.0);
    DocumentWorkflow workflow(root);
    auto& doc = root.document();
    const auto captured_page_id = doc.active_page_id();
    auto second_page = document::make_document_page("Page 2", document::Page::make_default());
    const auto second_page_id = second_page.id;
    assert(doc.execute_command(std::make_unique<document::AddPageCommand>(
        std::move(second_page), false)).success);

    const auto metadata = workflow.begin_import_job(17);
    assert(metadata.target_page_id == captured_page_id);
    const auto imported = run_import_worker_job(source_path.wstring(), ImportFormat::Svg,
        metadata, std::stop_token{});
    std::error_code ignored;
    std::filesystem::remove(source_path, ignored);
    assert(imported.success && !imported.cancelled);
    const size_t imported_count = imported.proposal.objects.size();
    assert(imported_count > 0);

    const auto navigation_generation = doc.page_navigation_generation();
    assert(doc.set_active_page(second_page_id));
    assert(doc.page_navigation_generation() == navigation_generation + 1);
    const auto targeted_commit = workflow.commit_import_proposal(metadata, imported.proposal);
    if (targeted_commit != document::ProposalStatus::Accepted)
        std::cerr << "Captured-page import commit failed with status "
                  << static_cast<int>(targeted_commit) << '\n';
    assert(targeted_commit == document::ProposalStatus::Accepted);

    auto snapshot = doc.current_snapshot();
    assert(snapshot->active_page_id() == second_page_id);
    const auto* imported_page = snapshot->find_page(captured_page_id);
    const auto* active_page = snapshot->find_page(second_page_id);
    assert(imported_page && imported_page->objects.size() == imported_count);
    assert(active_page && active_page->objects.empty());
    assert(snapshot->page().width_mm == active_page->layout.width_mm);

    assert(doc.undo());
    snapshot = doc.current_snapshot();
    assert(snapshot->active_page_id() == second_page_id);
    assert(snapshot->find_page(captured_page_id)->objects.empty());
    assert(snapshot->find_page(second_page_id)->objects.empty());
    assert(doc.redo());
    snapshot = doc.current_snapshot();
    assert(snapshot->active_page_id() == second_page_id);
    assert(snapshot->find_page(captured_page_id)->objects.size() == imported_count);
    assert(snapshot->find_page(second_page_id)->objects.empty());
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

void test_pdf_ai_eps_import_and_cdr_reject() {
    const auto cdr = make_cdr_preview_fixture();
    assert(interop::CdrExtractor::extract_preview(cdr).success);
    std::vector<interop::CanonicalImportRecord> cdr_preview_records;
    std::string cdr_preview_error;
    assert(!interop::BuildCdrImportRecords(cdr, cdr_preview_records, cdr_preview_error));

    const auto ai = make_ai_with_preview_fixture();
    const auto ai_preview = interop::PdfAiExtractor::extract_preview(ai);
    assert(ai_preview.success && ai_preview.is_ai && !ai_preview.thumbnail_png.empty());
    for (const bool ai_representation : {false, true}) {
        for (const bool open_action : {false, true}) {
            const std::string_view extension = ai_representation ? ".ai" : ".pdf";
            TemporaryImportSource source(extension);
            const auto source_bytes = make_vector_pdf_fixture(ai_representation, true);
            assert(source.write(source_bytes));
            CompositionRoot root;
            root.initialize();
            DocumentWorkflow workflow(root);
            workflow.new_document(210.0, 297.0, color::DocumentColorMode::Rgb, 96);
            const auto original_page = root.document().current_snapshot()->page();
            const auto metadata = workflow.begin_import_job(
                9 + (ai_representation ? 2 : 0) + (open_action ? 1 : 0));
            const auto imported = run_import_worker_job(source.path().wstring(), ImportFormat::Svg,
                metadata, std::stop_token{}, nullptr);
            if (!imported.success) std::cerr << "PDF/AI import failed: " << imported.error_message << '\n';
            assert(imported.success && !imported.cancelled);
            assert(imported.proposal.profile_id == (ai_representation
                ? "ai-pdf-compatible-page-set-v3" : "pdf-page-set-v3"));
            assert(imported.proposal.pages.size() == 2);
            assert(imported.proposal.pages[1].objects.empty());
            assert(imported.proposal.pages[1].layout.layers.size() == 1);
            assert(imported.proposal.pages[1].layout.layers.front().object_ids.empty());
            assert(std::abs(imported.proposal.pages[1].layout.width_mm - 300.0 * 25.4 / 72.0) < 0.01);
            assert(std::abs(imported.proposal.pages[1].layout.height_mm - 150.0 * 25.4 / 72.0) < 0.01);
            assert(imported.proposal.layers.size() == 2);
            const auto& imported_objects = imported.proposal.pages.front().objects;
            assert(imported_objects.size() >= 2);
            assert(std::any_of(imported_objects.begin(), imported_objects.end(),
                [](const auto& object) { return std::holds_alternative<document::PathObject>(object); }));
            assert(std::any_of(imported_objects.begin(), imported_objects.end(),
                [](const auto& object) { return std::holds_alternative<document::TextObject>(object); }));
            const auto imported_text = std::find_if(imported_objects.begin(),
                imported_objects.end(), [](const auto& object) {
                    return std::holds_alternative<document::TextObject>(object);
                });
            assert(imported_text != imported_objects.end());
            const auto& source_text = std::get<document::TextObject>(*imported_text);
            assert(!source_text.font.family_name.empty());
            assert(!source_text.font.postscript_name.empty());
            if (ai_representation) {
                assert(std::any_of(imported.proposal.issues.begin(), imported.proposal.issues.end(),
                    [](const auto& issue) { return issue.feature_code == "ai-authoring-data-not-read"; }));
            }
            assert(workflow.commit_import_proposal(metadata, imported.proposal, open_action) ==
                   document::ProposalStatus::Accepted);
            const auto committed_snapshot = root.document().current_snapshot();
            assert(committed_snapshot->page_count() == (open_action ? 2u : 3u));
            const size_t imported_page_index = open_action ? 0u : 1u;
            const size_t imported_blank_page_index = open_action ? 1u : 2u;
            assert(committed_snapshot->pages()[imported_page_index].objects.size() >= 2);
            const auto committed_text = committed_snapshot->pages()[imported_page_index]
                .objects.get(source_text.id);
            assert(committed_text && std::holds_alternative<document::TextObject>(*committed_text));
            assert(std::get<document::TextObject>(*committed_text).font.postscript_name ==
                   source_text.font.postscript_name);
            assert(committed_snapshot->pages()[imported_blank_page_index].objects.empty());
            assert(committed_snapshot->pages()[imported_blank_page_index].layout.layers.size() == 1);
            assert(committed_snapshot->pages()[imported_blank_page_index].layout.layers.front().object_ids.empty());
            if (open_action) {
                assert(root.document().current_snapshot()->page().width_mm ==
                       imported.proposal.source_page_width_mm);
                assert(root.document().current_snapshot()->page().height_mm ==
                       imported.proposal.source_page_height_mm);
            } else {
                assert(root.document().current_snapshot()->page().width_mm == original_page.width_mm);
                assert(root.document().current_snapshot()->page().height_mm == original_page.height_mm);
            }
            assert(root.document().undo());
            assert(root.document().current_snapshot()->object_count() == 0);
            assert(root.document().current_snapshot()->page_count() == 1);
            assert(root.document().current_snapshot()->page().width_mm == original_page.width_mm);
            assert(root.document().current_snapshot()->page().height_mm == original_page.height_mm);
        }
    }

    constexpr std::string_view eps_text =
        "%!PS-Adobe-3.0 EPSF-3.0\n"
        "%%BoundingBox: 10 20 110 70\n"
        "%%EndComments\n"
        "0 1 1 0 setcmykcolor\n"
        "10 20 moveto 110 20 lineto 60 70 lineto closepath fill\n"
        "showpage\n";
    const auto eps = std::vector<uint8_t>(eps_text.begin(), eps_text.end());
    TemporaryImportSource eps_source(".eps");
    assert(eps_source.write(eps));
    CompositionRoot eps_root;
    eps_root.initialize();
    DocumentWorkflow eps_workflow(eps_root);
    eps_workflow.new_document(210.0, 297.0, color::DocumentColorMode::Rgb, 96);
    const auto eps_original_page = eps_root.document().current_snapshot()->page();
    const auto eps_metadata = eps_workflow.begin_import_job(11);
    const auto eps_imported = run_import_worker_job(eps_source.path().wstring(), ImportFormat::Svg,
        eps_metadata, std::stop_token{}, nullptr);
    if (!eps_imported.success) std::cerr << "EPS import failed: " << eps_imported.error_message << '\n';
    assert(eps_imported.success && !eps_imported.cancelled);
    assert(eps_imported.proposal.format_name == "EPS");
    assert(eps_imported.proposal.profile_id == "eps-path-v1");
    assert(eps_imported.proposal.layers.size() == 1);
    assert(eps_imported.proposal.layers.front().name == "EPS Artwork");
    assert(eps_imported.proposal.objects.size() == 1);
    const auto* eps_path = std::get_if<document::PathObject>(&eps_imported.proposal.objects.front());
    assert(eps_path && eps_path->has_fill && !eps_path->has_stroke);
    assert(std::holds_alternative<color::CMYKColor>(eps_path->fill.payload()));
    assert(eps_workflow.commit_import_proposal(eps_metadata, eps_imported.proposal, true) ==
           document::ProposalStatus::Accepted);
    const auto eps_page_width = 100.0 * 25.4 / 72.0;
    assert(std::abs(eps_root.document().current_snapshot()->page().width_mm - eps_page_width) < 1e-8);
    assert(eps_root.document().undo());
    assert(eps_root.document().current_snapshot()->object_count() == 0);
    assert(eps_root.document().current_snapshot()->page().width_mm == eps_original_page.width_mm);
    assert(eps_root.document().redo());
    assert(eps_root.document().current_snapshot()->object_count() == 1);

    TemporaryImportSource dos_eps_source(".eps");
    const auto dos_eps_bytes = make_dos_eps_fixture();
    assert(dos_eps_source.write(dos_eps_bytes));
    const auto dos_eps_metadata = eps_workflow.begin_import_job(12);
    const auto dos_eps_imported = run_import_worker_job(dos_eps_source.path().wstring(),
        ImportFormat::Svg, dos_eps_metadata, std::stop_token{}, nullptr);
    if (!dos_eps_imported.success)
        std::cerr << "DOS EPS import failed: " << dos_eps_imported.error_message << '\n';
    assert(dos_eps_imported.success && !dos_eps_imported.cancelled);
    assert(dos_eps_imported.proposal.format_name == "EPS");
    assert(dos_eps_imported.proposal.layers.size() == 1);
    assert(dos_eps_imported.proposal.pages.size() == 1);
    assert(!dos_eps_imported.proposal.pages.front().objects.empty());

    constexpr std::string_view malicious_eps_text =
        "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 10 10\n%%EndComments\n"
        "deletefile\n";
    const std::vector<uint8_t> malicious_eps(malicious_eps_text.begin(), malicious_eps_text.end());
    TemporaryImportSource malicious_source(".eps");
    assert(malicious_source.write(malicious_eps));
    const auto rejected = run_import_worker_job(malicious_source.path().wstring(), ImportFormat::Svg,
        eps_workflow.begin_import_job(12), std::stop_token{}, nullptr);
    assert(!rejected.success);
    assert(rejected.error_message.find("PostScript operators") != std::string::npos);
}

void test_real_cdr_import_from_environment() {
#ifdef _WIN32
    std::vector<std::filesystem::path> fixtures;
    if (const wchar_t* path = _wgetenv(L"PIKDRAW_CDR_REAL_FIXTURE"); path && *path)
        fixtures.emplace_back(path);
    if (const wchar_t* directory = _wgetenv(L"PIKDRAW_CDR_REAL_FIXTURE_DIR");
        directory && *directory) {
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file()) continue;
            auto extension = entry.path().extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
            if (extension == L".cdr") fixtures.push_back(entry.path());
        }
    }
    if (fixtures.empty()) return;

    size_t failures = 0;
    for (const auto& fixture : fixtures) {
        const auto encoded_name = fixture.filename().u8string();
        const std::string fixture_name(reinterpret_cast<const char*>(encoded_name.data()),
                                       encoded_name.size());
        CompositionRoot root;
        root.initialize();
        DocumentWorkflow workflow(root);
        workflow.new_document(210.0, 297.0, color::DocumentColorMode::Rgb, 96);
        const auto metadata = workflow.begin_import_job(30);
        const auto imported = run_import_worker_job(fixture.wstring(), ImportFormat::Svg, metadata,
            std::stop_token{}, nullptr);
        if (!imported.success || imported.cancelled) {
            std::cerr << "Real CDR import failed [" << fixture_name << "]: "
                      << imported.error_message << '\n';
            ++failures;
            continue;
        }
        assert(imported.proposal.format_name == "CorelDRAW");
        assert(imported.proposal.profile_id == "cdr-libcdr-svg-v2");
        assert(!imported.proposal.layers.empty());
        assert(!imported.proposal.objects.empty());
        const size_t path_count = static_cast<size_t>(std::count_if(
            imported.proposal.objects.begin(), imported.proposal.objects.end(),
            [](const auto& object) { return std::holds_alternative<document::PathObject>(object); }));
        const size_t text_count = static_cast<size_t>(std::count_if(
            imported.proposal.objects.begin(), imported.proposal.objects.end(),
            [](const auto& object) { return std::holds_alternative<document::TextObject>(object); }));
        const size_t image_count = static_cast<size_t>(std::count_if(
            imported.proposal.objects.begin(), imported.proposal.objects.end(),
            [](const auto& object) { return std::holds_alternative<document::ImageObject>(object); }));
        size_t clipping_group_count = 0;
        assert(path_count + text_count + image_count > 0);
        for (const auto& object : imported.proposal.objects) {
            if (const auto* group = std::get_if<document::GroupObject>(&object); group && group->clip) {
                ++clipping_group_count;
                assert(std::find(group->children_ids.begin(), group->children_ids.end(),
                                 group->clip->mask_object_id) != group->children_ids.end());
                const auto mask = std::find_if(imported.proposal.objects.begin(), imported.proposal.objects.end(),
                    [&](const auto& candidate) {
                        return std::visit([&](const auto& typed) {
                            return typed.id == group->clip->mask_object_id &&
                                std::holds_alternative<document::PathObject>(candidate);
                        }, candidate);
                    });
                assert(mask != imported.proposal.objects.end());
            }
            if (const auto* image = std::get_if<document::ImageObject>(&object)) {
                assert(image->rgba_pixels().size() ==
                       static_cast<size_t>(image->width_px) * image->height_px);
                assert(image->source.shared_original_bytes &&
                       !image->source.original_data().empty());
                assert(image->transform.is_finite() && image->transform.is_invertible());
                const auto placement = image->bounds();
                assert(std::isfinite(placement.x) && std::isfinite(placement.y) &&
                       std::isfinite(placement.width) && std::isfinite(placement.height) &&
                       placement.width > 0.0 && placement.height > 0.0);
            }
        }
        const auto proposal_objects = imported.proposal.objects.size();
        const auto proposal_width = imported.proposal.source_page_width_mm;
        const auto proposal_height = imported.proposal.source_page_height_mm;
        const size_t proposal_layers = imported.proposal.layers.size();
        assert(workflow.commit_import_proposal(metadata, imported.proposal, true) ==
               document::ProposalStatus::Accepted);
        assert(root.document().current_snapshot()->object_count() == proposal_objects);
        assert(root.document().current_snapshot()->page().width_mm == proposal_width);
        assert(root.document().current_snapshot()->page().height_mm == proposal_height);
        assert(root.document().undo());
        assert(root.document().current_snapshot()->object_count() == 0);
        std::cout << "Real CDR imported [" << fixture_name << "]: "
                  << proposal_objects << " editable objects (" << path_count << " paths, "
                  << text_count << " text, " << image_count << " embedded images), "
                  << proposal_layers << " layers and " << clipping_group_count
                  << " clipping groups.\n";
    }
    assert(failures == 0);
#endif
}

void test_real_pdf_ai_eps_import_from_corpus() {
#ifdef _WIN32
    const auto directory = local_corpus();
    if (directory.empty()) return;

    std::vector<std::filesystem::path> fixtures;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        auto extension = entry.path().extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        if (extension == L".pdf" || extension == L".ai" || extension == L".eps")
            fixtures.push_back(entry.path());
    }
    if (const wchar_t* only_name = _wgetenv(L"PIKDRAW_REAL_VECTOR_FIXTURE");
        only_name && *only_name) {
        const std::filesystem::path requested(only_name);
        std::erase_if(fixtures, [&](const auto& fixture) {
            return fixture.filename() != requested.filename();
        });
    }
    assert(!fixtures.empty());

    size_t failures = 0;
    for (const auto& fixture : fixtures) {
        const auto encoded_name = fixture.filename().u8string();
        const std::string fixture_name(reinterpret_cast<const char*>(encoded_name.data()),
                                       encoded_name.size());
        std::cerr << "Opening real vector fixture [" << fixture_name << "]...\n";
        CompositionRoot root;
        root.initialize();
        DocumentWorkflow workflow(root);
        workflow.new_document(210.0, 297.0, color::DocumentColorMode::Rgb, 96);
        const auto metadata = workflow.begin_import_job(40);
        const auto imported = run_import_worker_job(fixture.wstring(), ImportFormat::Svg, metadata,
            std::stop_token{}, nullptr);
        if (!imported.success || imported.cancelled) {
            std::cerr << "Real vector import failed [" << fixture_name << "]: "
                      << imported.error_message << '\n';
            ++failures;
            continue;
        }
        const auto extension = fixture.extension().wstring();
        if (extension == L".pdf")
            assert(imported.proposal.profile_id == "pdf-page-set-v3");
        else if (extension == L".ai")
            assert(imported.proposal.profile_id == "ai-pdf-compatible-page-set-v3");
        else
            assert(imported.proposal.profile_id == "eps-path-v1");
        assert(!imported.proposal.layers.empty());
        size_t object_count = imported.proposal.objects.size();
        if (!imported.proposal.pages.empty()) {
            object_count = 0;
            for (const auto& page : imported.proposal.pages)
                object_count += page.objects.size();
        }
        assert(object_count > 0);
        if (fixture.filename() == L"[ PikDecor.vn ] 260312_283.pdf") {
            const bool retained_pdf_layer = std::any_of(
                imported.proposal.layers.begin(), imported.proposal.layers.end(),
                [](const auto& layer) { return layer.name == "PikDecor"; });
            if (!retained_pdf_layer) {
                for (const auto& layer : imported.proposal.layers)
                    std::cerr << "PDF layer imported as [" << layer.name << "] with "
                              << layer.object_ids.size() << " objects.\n";
            }
            assert(retained_pdf_layer);
        }
        const auto width = imported.proposal.source_page_width_mm;
        const auto height = imported.proposal.source_page_height_mm;
        assert(workflow.commit_import_proposal(metadata, imported.proposal, true) ==
               document::ProposalStatus::Accepted);
        size_t committed_object_count = 0;
        for (const auto& page : root.document().current_snapshot()->pages())
            committed_object_count += page.objects.size();
        assert(committed_object_count == object_count);
        assert(root.document().current_snapshot()->page_count() ==
               (imported.proposal.pages.empty() ? 1u : imported.proposal.pages.size()));
        assert(root.document().current_snapshot()->page().width_mm == width);
        assert(root.document().current_snapshot()->page().height_mm == height);
        assert(root.document().undo());
        assert(root.document().current_snapshot()->object_count() == 0);
        std::cout << "Real vector imported [" << fixture_name << "]: " << object_count
                  << " editable objects, profile " << imported.proposal.profile_id << ".\n";
        std::cout.flush();
    }
    assert(failures == 0);
#endif
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
    test_import_job_keeps_its_captured_page_target();
    test_dxf_profile_and_source_signature_rejection();
    test_png_raster_import_retains_source_and_is_undoable();
    test_pdf_ai_eps_import_and_cdr_reject();
    test_real_cdr_import_from_environment();
    test_real_pdf_ai_eps_import_from_corpus();
    test_large_local_jpeg_import();
    std::cout << "Desktop SVG/DXF/raster/PDF/AI/EPS imports and editable CDR subset tests passed\n";
    return 0;
}
