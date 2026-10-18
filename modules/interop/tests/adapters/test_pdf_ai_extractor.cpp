#include "pikdraw/interop/adapters/pdf_ai_extractor.hpp"
#include "pikdraw/interop/adapters/raster_image_decoder.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> read_file_if_exists(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return {};
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
}

std::filesystem::path find_cdr_dir() {
    std::filesystem::path cur = std::filesystem::current_path();
    for (int i = 0; i < 4; ++i) {
        if (std::filesystem::exists(cur / "cdr")) {
            return cur / "cdr";
        }
        if (!cur.has_parent_path()) break;
        cur = cur.parent_path();
    }
    return {};
}

void test_is_pdf_or_ai_container() {
    using namespace pikdraw::interop;
    assert(!PdfAiExtractor::is_pdf_or_ai_container({}));
    const uint8_t garbage[] = {0x11, 0x22, 0x33, 0x44};
    assert(!PdfAiExtractor::is_pdf_or_ai_container(garbage));

    const uint8_t pdf_header[] = {'%', 'P', 'D', 'F', '-', '1', '.', '5'};
    assert(PdfAiExtractor::is_pdf_or_ai_container(pdf_header));

    const uint8_t padded_header[] = {'\n', '\r', '%', 'P', 'D', 'F', '-', '1', '.', '7'};
    assert(PdfAiExtractor::is_pdf_or_ai_container(padded_header));
}

void test_synthetic_pdf_with_mediabox() {
    using namespace pikdraw::interop;
    // 595.276 pt x 841.89 pt = 210.0 mm x 297.0 mm (standard A4)
    const std::string synthetic =
        "%PDF-1.4\n"
        "1 0 obj\n"
        "<< /Type /Catalog /Pages 2 0 R >>\n"
        "endobj\n"
        "2 0 obj\n"
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>\n"
        "endobj\n"
        "3 0 obj\n"
        "<< /Type /Page /Parent 2 0 R /MediaBox [0.0 0.0 595.276 841.89] >>\n"
        "endobj\n"
        "xref\n"
        "0 4\n"
        "trailer\n"
        "<< /Root 1 0 R >>\n"
        "%%EOF\n";

    std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(synthetic.data()), synthetic.size());
    assert(PdfAiExtractor::is_pdf_or_ai_container(bytes));

    const auto result = PdfAiExtractor::extract_preview(bytes);
    assert(result.success);
    assert(result.is_pdf);
    assert(!result.is_ai);
    assert(std::abs(result.page_width_mm - 210.0) < 0.1);
    assert(std::abs(result.page_height_mm - 297.0) < 0.1);
    // When no embedded image exists, placeholder PNG is generated
    assert(!result.thumbnail_png.empty());
    assert(result.thumbnail_png.size() >= 8);
    assert(result.thumbnail_png[0] == 0x89 && result.thumbnail_png[1] == 'P' &&
           result.thumbnail_png[2] == 'N'  && result.thumbnail_png[3] == 'G');
}

void test_synthetic_ai_with_xmp_thumbnail() {
    using namespace pikdraw::interop;
    // Base64 encoding of {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00, 0x01}
    // /9j/4AAQSkZJRgAB
    const std::string synthetic_ai =
        "%PDF-1.5\n"
        "1 0 obj\n"
        "<< /Creator (Adobe Illustrator 24.0) /MediaBox [0 0 750.0 500.0] >>\n"
        "endobj\n"
        "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
        "  <xmpGImg:format>JPEG</xmpGImg:format>\n"
        "  <xmpGImg:image>\n"
        "    /9j/4AAQSkZJRgAB&#xA;\n"
        "  </xmpGImg:image>\n"
        "</x:xmpmeta>\n"
        "%%EOF\n";

    std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(synthetic_ai.data()), synthetic_ai.size());
    assert(PdfAiExtractor::is_pdf_or_ai_container(bytes));

    const auto result = PdfAiExtractor::extract_preview(bytes);
    assert(result.success);
    assert(result.is_ai);
    assert(!result.is_pdf);
    // 750 pt = 264.583 mm; 500 pt = 176.388 mm
    assert(std::abs(result.page_width_mm - (750.0 * 25.4 / 72.0)) < 0.1);
    assert(std::abs(result.page_height_mm - (500.0 * 25.4 / 72.0)) < 0.1);
    assert(!result.thumbnail_png.empty());
    assert(result.thumbnail_png.size() >= 8);
    assert(result.thumbnail_png[0] == 0x89 && result.thumbnail_png[1] == 'P');
}

void test_synthetic_pdf_with_dct_stream() {
    using namespace pikdraw::interop;
    const std::string header =
        "%PDF-1.4\n"
        "4 0 obj\n"
        "<< /Type /XObject /Subtype /Image /Filter /DCTDecode /Length 12 >>\n"
        "stream\r\n";
    const uint8_t sample_jpeg[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0xFF, 0xD9};
    const std::string footer =
        "\r\nendstream\n"
        "endobj\n"
        "5 0 obj\n"
        "<< /CropBox [0 0 100 200] >>\n"
        "endobj\n"
        "%%EOF\n";

    std::vector<uint8_t> doc;
    doc.insert(doc.end(), header.begin(), header.end());
    doc.insert(doc.end(), std::begin(sample_jpeg), std::end(sample_jpeg));
    doc.insert(doc.end(), footer.begin(), footer.end());

    const auto result = PdfAiExtractor::extract_preview(doc);
    assert(result.success);
    assert(result.is_pdf);
    assert(std::abs(result.page_width_mm - (100.0 * 25.4 / 72.0)) < 0.1);
    assert(std::abs(result.page_height_mm - (200.0 * 25.4 / 72.0)) < 0.1);
    assert(!result.thumbnail_png.empty());
    assert(result.thumbnail_png.size() >= 8);
    assert(result.thumbnail_png[0] == 0x89 && result.thumbnail_png[1] == 'P');
}

void test_repo_files() {
    using namespace pikdraw::interop;
    const auto cdr_dir = find_cdr_dir();
    if (cdr_dir.empty()) {
        std::cout << "[test_pdf_ai_extractor] cdr directory not found, skipping repo files.\n";
        return;
    }

    // 1. 11695530.ai
    const auto ai_path = cdr_dir / "11695530.ai";
    if (std::filesystem::exists(ai_path)) {
        const auto ai_bytes = read_file_if_exists(ai_path);
        assert(!ai_bytes.empty());
        assert(PdfAiExtractor::is_pdf_or_ai_container(ai_bytes));
        const auto result = PdfAiExtractor::extract_preview(ai_bytes);
        assert(result.success);
        assert(result.is_ai);
        assert(!result.is_pdf);
        // MediaBox: [0.0 0.0 750.0 500.0] -> 264.58 x 176.39 mm
        assert(std::abs(result.page_width_mm - 264.58) < 0.5);
        assert(std::abs(result.page_height_mm - 176.39) < 0.5);
        assert(!result.thumbnail_png.empty());
        assert(result.thumbnail_png.size() >= 8);
        assert(result.thumbnail_png[0] == 0x89 && result.thumbnail_png[1] == 'P');
        const auto decoded = RasterImageDecoder::decode(result.thumbnail_png);
        assert(decoded.success);
        std::cout << "[test_pdf_ai_extractor] 11695530.ai successfully parsed ("
                  << result.page_width_mm << " x " << result.page_height_mm
                  << " mm, thumb=" << result.thumbnail_png.size() << " bytes)\n";
    }

    // 2. job HVAM.pdf (vector PDF without DCT stream)
    const auto job_path = cdr_dir / "job  HVAM.pdf";
    if (std::filesystem::exists(job_path)) {
        const auto pdf_bytes = read_file_if_exists(job_path);
        assert(!pdf_bytes.empty());
        assert(PdfAiExtractor::is_pdf_or_ai_container(pdf_bytes));
        const auto result = PdfAiExtractor::extract_preview(pdf_bytes);
        assert(result.success);
        assert(result.is_pdf);
        assert(result.page_width_mm > 1000.0);
        assert(result.page_height_mm > 1000.0);
        assert(!result.thumbnail_png.empty());
        std::cout << "[test_pdf_ai_extractor] job HVAM.pdf parsed ("
                  << result.page_width_mm << " x " << result.page_height_mm << " mm)\n";
    }

    // 3. [ PikDecor.vn ] 260312_283.pdf (PDF with DCT stream)
    const auto pik_path = cdr_dir / "[ PikDecor.vn ] 260312_283.pdf";
    if (std::filesystem::exists(pik_path)) {
        const auto pdf_bytes = read_file_if_exists(pik_path);
        assert(!pdf_bytes.empty());
        assert(PdfAiExtractor::is_pdf_or_ai_container(pdf_bytes));
        const auto result = PdfAiExtractor::extract_preview(pdf_bytes);
        assert(result.success);
        assert(result.is_pdf);
        assert(result.page_width_mm > 400.0);
        assert(result.page_height_mm > 500.0);
        assert(!result.thumbnail_png.empty());
        std::cout << "[test_pdf_ai_extractor] PikDecor.pdf parsed ("
                  << result.page_width_mm << " x " << result.page_height_mm << " mm)\n";
    }
}

} // namespace

int main() {
    std::cout << "[test_pdf_ai_extractor] Running PDF/AI extractor unit tests...\n";
    test_is_pdf_or_ai_container();
    test_synthetic_pdf_with_mediabox();
    test_synthetic_ai_with_xmp_thumbnail();
    test_synthetic_pdf_with_dct_stream();
    test_repo_files();
    std::cout << "[test_pdf_ai_extractor] All PDF/AI extractor unit tests passed!\n";
    return 0;
}
