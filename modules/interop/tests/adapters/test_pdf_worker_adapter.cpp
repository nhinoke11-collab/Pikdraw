#include "pikdraw/interop/adapters/pdf_worker_adapter.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> MakePdf(std::string content, int page_count = 1,
                             bool blank_second_page = false,
                             int first_page_rotation_degrees = 0) {
    const std::string stream = content;
    std::string first_page =
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] "
        "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>";
    if (first_page_rotation_degrees != 0) {
        const auto contents_position = first_page.find(" /Contents");
        first_page.insert(contents_position,
            " /Rotate " + std::to_string(first_page_rotation_degrees));
    }
    std::vector<std::string> objects{
        "<< /Type /Catalog /Pages 2 0 R >>",
        std::string("<< /Type /Pages /Kids [3 0 R") +
            (page_count > 1 ? " 6 0 R" : "") + "] /Count " +
            std::to_string(page_count) + " >>",
        std::move(first_page),
        "<< /Length " + std::to_string(stream.size()) + " >>\nstream\n" +
            stream + "\nendstream",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"
    };
    if (page_count > 1) {
        objects.push_back(blank_second_page
            ? "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 150] >>"
            : "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 150] "
              "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>");
    }

    std::string pdf = "%PDF-1.4\n";
    std::vector<size_t> offsets(objects.size() + 1, 0);
    for (size_t index = 0; index < objects.size(); ++index) {
        offsets[index + 1] = pdf.size();
        pdf += std::to_string(index + 1) + " 0 obj\n" + objects[index] + "\nendobj\n";
    }
    const size_t xref_offset = pdf.size();
    pdf += "xref\n0 " + std::to_string(objects.size() + 1) +
        "\n0000000000 65535 f \n";
    for (size_t index = 1; index < offsets.size(); ++index) {
        char line[32]{};
        std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offsets[index]);
        pdf += line;
    }
    pdf += "trailer\n<< /Size " + std::to_string(objects.size() + 1) +
        " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref_offset) + "\n%%EOF\n";
    return {pdf.begin(), pdf.end()};
}

void TestPdfInspectionReportsExactPagesAndBlankPage() {
    using namespace pikdraw::interop;
    auto bytes = MakePdf("0 0 1 rg\n10 10 40 20 re\nf\n", 2, true);
    PdfSourceManifest manifest;
    std::string diagnostic;
    assert(InspectPdfSource(bytes, manifest, diagnostic));
    assert(diagnostic.empty());
    assert(manifest.exact_page_count == 2);
    assert(manifest.pages.size() == 2);
    assert(manifest.pages[0].source_index == 0);
    assert(manifest.pages[0].page_object_count > 0);
    assert(std::abs(manifest.pages[0].width_mm - 200.0 * 25.4 / 72.0) < 0.01);
    assert(manifest.pages[1].source_index == 1);
    assert(manifest.pages[1].page_object_count == 0);
    assert(std::abs(manifest.pages[1].width_mm - 300.0 * 25.4 / 72.0) < 0.01);
    assert(manifest.pages[1].rotation_quarters_clockwise == 0);
}

void TestPdfInspectionReportsPageRotation() {
    using namespace pikdraw::interop;
    const auto bytes = MakePdf("0 0 1 rg\n10 10 40 20 re\nf\n", 1, false, 90);
    PdfSourceManifest manifest;
    std::string diagnostic;
    assert(InspectPdfSource(bytes, manifest, diagnostic));
    assert(manifest.exact_page_count == 1);
    assert(manifest.pages.front().rotation_quarters_clockwise == 1);
}

void TestRotatedPdfPageMapsGeometryAndText() {
    using namespace pikdraw::interop;
    struct ExpectedRotation {
        int degrees;
        double page_width_pt;
        double page_height_pt;
        double path_left_pt;
        double path_right_pt;
        double path_bottom_pt;
        double path_top_pt;
        double text_x_pt;
        double text_y_pt;
        uint16_t text_rotation;
    };
    constexpr std::array expected_rotations{
        ExpectedRotation{0, 200, 100, 10, 50, 10, 30, 20, 60, 0},
        ExpectedRotation{90, 100, 200, 10, 30, 150, 190, 60, 180, 270},
        ExpectedRotation{180, 200, 100, 150, 190, 70, 90, 180, 40, 180},
        ExpectedRotation{270, 100, 200, 70, 90, 10, 50, 40, 20, 90}};
    constexpr double factor = 25.4 / 72.0;
    for (const auto& expected : expected_rotations) {
        const auto bytes = MakePdf(
            "0 0 1 rg\n10 10 40 20 re\nf\n"
            "BT\n/F1 12 Tf\n0 0 0 rg\n20 60 Td\n(Rotated) Tj\nET\n",
            1, false, expected.degrees);
        std::vector<CanonicalImportRecord> records;
        std::string diagnostic;
        assert(BuildPdfPageSetImportRecords(bytes, false, records, diagnostic));

        bool saw_rotated_page = false;
        bool saw_rotated_path = false;
        bool saw_rotated_text = false;
        for (const auto& encoded : records) {
            const auto decoded = DeserializeImportRecord(encoded.type, encoded.payload);
            assert(decoded);
            if (const auto* page = std::get_if<ImportPageRecord>(&*decoded)) {
                saw_rotated_page = page->rotation_degrees == expected.degrees &&
                    std::abs(page->width_mm - expected.page_width_pt * factor) < 0.01 &&
                    std::abs(page->height_mm - expected.page_height_pt * factor) < 0.01;
            } else if (const auto* path = std::get_if<ImportPathRecord>(&*decoded)) {
                double left = 1.0e9, bottom = 1.0e9, right = -1.0e9, top = -1.0e9;
                for (const auto& element : path->elements) {
                    if (element.verb == ImportPathVerb::Close) continue;
                    const size_t point_count = element.verb == ImportPathVerb::Cubic ? 3 : 1;
                    for (size_t point = 0; point < point_count; ++point) {
                        const double x = element.values[point * 2];
                        const double y = element.values[point * 2 + 1];
                        left = (std::min)(left, x);
                        bottom = (std::min)(bottom, y);
                        right = (std::max)(right, x);
                        top = (std::max)(top, y);
                    }
                }
                saw_rotated_path = std::abs(left - expected.path_left_pt * factor) < 0.01 &&
                    std::abs(right - expected.path_right_pt * factor) < 0.01 &&
                    std::abs(bottom - expected.path_bottom_pt * factor) < 0.01 &&
                    std::abs(top - expected.path_top_pt * factor) < 0.01;
            } else if (const auto* text = std::get_if<ImportTextRecord>(&*decoded)) {
                saw_rotated_text = text->utf8_text == "Rotated" &&
                    text->rotation_degrees == expected.text_rotation &&
                    std::abs(text->x_mm - expected.text_x_pt * factor) < 0.01 &&
                    std::abs(text->y_mm - expected.text_y_pt * factor) < 0.01;
            }
        }
        assert(saw_rotated_page && saw_rotated_path && saw_rotated_text);
    }
}

void TestRotatedPageCodecKeepsLegacyManifestPayloadsReadable() {
    using namespace pikdraw::interop;
    ImportPageRecord rotated{100.0, 200.0};
    rotated.local_id = 1;
    rotated.source_page_index = 2;
    rotated.name = "Rotated page";
    rotated.rotation_degrees = 90;
    rotated.has_manifest_metadata = true;
    auto encoded = SerializeImportRecord(rotated);
    assert(encoded);
    const auto decoded = DeserializeImportRecord(encoded->type, encoded->payload);
    assert(decoded);
    const auto* page = std::get_if<ImportPageRecord>(&*decoded);
    assert(page && page->rotation_degrees == 90 && page->name == "Rotated page");

    encoded->payload.resize(encoded->payload.size() - sizeof(uint16_t));
    const auto legacy_decoded = DeserializeImportRecord(encoded->type, encoded->payload);
    assert(legacy_decoded);
    const auto* legacy_page = std::get_if<ImportPageRecord>(&*legacy_decoded);
    assert(legacy_page && legacy_page->has_manifest_metadata &&
           legacy_page->rotation_degrees == 0 && legacy_page->name == "Rotated page");
}

void InspectOptionalLocalPdfCorpus() {
    using namespace pikdraw::interop;
    const char* corpus_value = std::getenv("PIKDRAW_VECTOR_IMPORT_CORPUS_DIR");
    if (!corpus_value || *corpus_value == '\0') return;

    const std::filesystem::path corpus(corpus_value);
    size_t inspected_pdf_count = 0;
    size_t corpus_file_index = 0;
    for (const auto& entry : std::filesystem::directory_iterator(corpus)) {
        if (!entry.is_regular_file()) continue;
        ++corpus_file_index;
        auto extension = entry.path().extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
        if (extension != L".pdf" && extension != L".ai") continue;

        std::ifstream input(entry.path(), std::ios::binary);
        if (!input) {
            std::cerr << "PDF corpus unreadable at entry " << corpus_file_index << '\n';
            continue;
        }
        const auto begin = std::istreambuf_iterator<char>(input);
        const std::istreambuf_iterator<char> end;
        const std::vector<uint8_t> source(begin, end);
        PdfSourceManifest manifest;
        std::string diagnostic;
        const bool inspected = InspectPdfSource(source, manifest, diagnostic);
        std::cout << "PDF_INSPECT entry=" << corpus_file_index
                  << " bytes=" << source.size()
                  << " representation=" << (extension == L".ai" ? "ai-pdf-compatible" : "pdf")
                  << " ok=" << (inspected ? "true" : "false")
                  << " pages=" << manifest.exact_page_count;
        if (!diagnostic.empty()) std::cout << " diagnostic=" << diagnostic;
        if (inspected) {
            const size_t blank_pages = static_cast<size_t>(std::count_if(
                manifest.pages.begin(), manifest.pages.end(),
                [](const auto& page) { return page.page_object_count == 0; }));
            const size_t rotated_pages = static_cast<size_t>(std::count_if(
                manifest.pages.begin(), manifest.pages.end(),
                [](const auto& page) { return page.rotation_quarters_clockwise != 0; }));
            std::cout << " blank_pages=" << blank_pages
                      << " rotated_pages=" << rotated_pages;
        }
        std::cout << '\n';
        if (extension == L".pdf") {
            ++inspected_pdf_count;
            assert(inspected);
        }
    }
    assert(inspected_pdf_count > 0);
}

void TestVectorPdfCreatesEditableRecords() {
    using namespace pikdraw::interop;
    const auto bytes = MakePdf(
        "q\n1 0 0 rg\n10 10 40 20 re\nf\n"
        "0 0 1 RG\n2 w\n60 10 m\n100 50 l\nS\n"
        "10 60 m\n20 90 50 90 60 60 c\nS\nQ\n"
        "BT\n/F1 12 Tf\n0 0 0 rg\n20 60 Td\n(Hello) Tj\nET\n");

    std::vector<CanonicalImportRecord> records;
    std::string diagnostic;
    const bool imported = BuildPdfImportRecords(bytes, false, records, diagnostic);
    if (!imported) std::cerr << "PDF adapter rejected vector fixture: " << diagnostic << '\n';
    assert(imported);
    assert(diagnostic.empty());
    assert(!records.empty());
    assert(records.front().type == ImportRecordType::Page);
    assert(records.back().type == ImportRecordType::FidelityReport);

    bool saw_layer = false;
    bool saw_fill_path = false;
    bool saw_cubic = false;
    bool saw_text = false;
    bool saw_conversion_warning = false;
    for (const auto& encoded : records) {
        const auto decoded = DeserializeImportRecord(encoded.type, encoded.payload);
        assert(decoded.has_value());
        if (const auto* page = std::get_if<ImportPageRecord>(&*decoded)) {
            assert(std::abs(page->width_mm - 200.0 * 25.4 / 72.0) < 0.01);
            assert(std::abs(page->height_mm - 100.0 * 25.4 / 72.0) < 0.01);
        } else if (const auto* layer = std::get_if<ImportLayerRecord>(&*decoded)) {
            saw_layer = true;
            assert(layer->name == "PDF Page 1");
            assert(layer->object_local_ids.size() >= 3);
        } else if (const auto* path = std::get_if<ImportPathRecord>(&*decoded)) {
            saw_fill_path |= path->has_fill && !path->elements.empty();
            saw_cubic |= std::any_of(path->elements.begin(), path->elements.end(),
                [](const auto& element) { return element.verb == ImportPathVerb::Cubic; });
            assert(path->elements.front().verb == ImportPathVerb::Move);
        } else if (const auto* text = std::get_if<ImportTextRecord>(&*decoded)) {
            saw_text = true;
            assert(text->utf8_text == "Hello");
            assert(!text->font_family.empty());
            assert(!text->font_postscript_name.empty());
        } else if (const auto* issue = std::get_if<ImportFidelityIssueRecord>(&*decoded)) {
            saw_conversion_warning |= issue->feature_code == "pdf-effects-and-clipping-not-mapped";
        } else if (const auto* report = std::get_if<ImportFidelityReportRecord>(&*decoded)) {
            assert(report->coverage_profile == "pdf-page-vector-v2");
            assert(report->editability == ImportEditability::NativeEditable);
            assert(report->appearance == ImportAppearance::Partial);
        }
    }
    assert(saw_layer && saw_fill_path && saw_cubic && saw_text && saw_conversion_warning);
}

void TestPdfCompatibleAiIsLabeledSeparately() {
    using namespace pikdraw::interop;
    const auto bytes = MakePdf("0 0 1 rg\n10 10 40 20 re\nf\n");
    std::vector<CanonicalImportRecord> records;
    std::string diagnostic;
    assert(BuildPdfImportRecords(bytes, true, records, diagnostic));
    bool saw_ai_limit = false;
    for (const auto& encoded : records) {
        const auto decoded = DeserializeImportRecord(encoded.type, encoded.payload);
        assert(decoded.has_value());
        if (const auto* issue = std::get_if<ImportFidelityIssueRecord>(&*decoded))
            saw_ai_limit |= issue->feature_code == "ai-authoring-data-not-read";
        if (const auto* report = std::get_if<ImportFidelityReportRecord>(&*decoded))
            assert(report->coverage_profile == "ai-pdf-compatible-vector-v2");
    }
    assert(saw_ai_limit);
}

void TestMultiPagePdfImportsFirstPageWithLossWarning() {
    using namespace pikdraw::interop;
    const auto bytes = MakePdf("0 0 1 rg\n10 10 40 20 re\nf\n", 2);
    std::vector<CanonicalImportRecord> records;
    std::string diagnostic;
    assert(BuildPdfImportRecords(bytes, false, records, diagnostic));
    bool warned_about_skipped_pages = false;
    for (const auto& encoded : records) {
        if (encoded.type != ImportRecordType::FidelityIssue) continue;
        const auto decoded = DeserializeImportRecord(encoded.type, encoded.payload);
        assert(decoded);
        if (const auto* issue = std::get_if<ImportFidelityIssueRecord>(&*decoded))
            warned_about_skipped_pages |= issue->feature_code == "pdf-pages-after-first-not-imported";
    }
    assert(warned_about_skipped_pages);
}

void TestPdfAdapterMaterializesSelectedNonFirstPage() {
    using namespace pikdraw::interop;
    const auto bytes = MakePdf("0 0 1 rg\n10 10 40 20 re\nf\n", 2);
    std::vector<CanonicalImportRecord> records;
    std::string diagnostic;
    assert(BuildPdfImportRecords(bytes, false, records, diagnostic, 1));
    bool saw_selected_size = false;
    bool saw_selected_layer = false;
    for (const auto& encoded : records) {
        const auto decoded = DeserializeImportRecord(encoded.type, encoded.payload);
        assert(decoded);
        if (const auto* page = std::get_if<ImportPageRecord>(&*decoded)) {
            saw_selected_size =
                std::abs(page->width_mm - 300.0 * 25.4 / 72.0) < 0.01 &&
                std::abs(page->height_mm - 150.0 * 25.4 / 72.0) < 0.01;
        } else if (const auto* layer = std::get_if<ImportLayerRecord>(&*decoded)) {
            saw_selected_layer = layer->name == "PDF Page 2" &&
                layer->source_id == "pdf-page-2";
        }
    }
    assert(saw_selected_size && saw_selected_layer);
}

void TestPdfPageSetKeepsEveryPageAndBlankPages() {
    using namespace pikdraw::interop;
    const auto bytes = MakePdf("0 0 1 rg\n10 10 40 20 re\nf\n", 2, true);
    std::vector<CanonicalImportRecord> records;
    std::string diagnostic;
    assert(BuildPdfPageSetImportRecords(bytes, false, records, diagnostic));
    std::vector<ImportPageRecord> pages;
    std::vector<ImportLayerRecord> layers;
    bool has_page_omission_warning = false;
    bool saw_report = false;
    for (const auto& encoded : records) {
        const auto decoded = DeserializeImportRecord(encoded.type, encoded.payload);
        assert(decoded);
        if (const auto* page = std::get_if<ImportPageRecord>(&*decoded)) {
            pages.push_back(*page);
        } else if (const auto* layer = std::get_if<ImportLayerRecord>(&*decoded)) {
            layers.push_back(*layer);
        } else if (const auto* issue = std::get_if<ImportFidelityIssueRecord>(&*decoded)) {
            has_page_omission_warning |=
                issue->feature_code == "pdf-pages-after-first-not-imported" ||
                issue->feature_code == "pdf-other-pages-not-selected";
        } else if (const auto* report = std::get_if<ImportFidelityReportRecord>(&*decoded)) {
            saw_report = true;
            assert(report->coverage_profile == "pdf-page-set-v3");
            assert(report->issue_count > 0);
        }
    }
    assert(pages.size() == 2 && pages[0].local_id == 1 && pages[1].local_id == 2);
    assert(pages[0].source_page_index == 0 && !pages[0].blank);
    assert(pages[1].source_page_index == 1 && pages[1].blank);
    assert(layers.size() == 2);
    assert(layers[0].page_local_id == 1 && layers[0].has_page_association);
    assert(layers[1].page_local_id == 2 && layers[1].object_local_ids.empty());
    assert(!has_page_omission_warning && saw_report);

    assert(BuildPdfPageSetImportRecords(bytes, false, records, diagnostic, 1));
    bool selected_blank_page = false;
    bool selected_warning = false;
    for (const auto& encoded : records) {
        const auto decoded = DeserializeImportRecord(encoded.type, encoded.payload);
        assert(decoded);
        if (const auto* page = std::get_if<ImportPageRecord>(&*decoded))
            selected_blank_page |= page->local_id == 1 && page->source_page_index == 1 && page->blank;
        else if (const auto* issue = std::get_if<ImportFidelityIssueRecord>(&*decoded))
            selected_warning |= issue->feature_code == "pdf-other-pages-not-selected";
    }
    assert(selected_blank_page && selected_warning);
}

} // namespace

int main() {
    TestPdfInspectionReportsExactPagesAndBlankPage();
    TestPdfInspectionReportsPageRotation();
    TestRotatedPdfPageMapsGeometryAndText();
    TestRotatedPageCodecKeepsLegacyManifestPayloadsReadable();
    InspectOptionalLocalPdfCorpus();
    TestVectorPdfCreatesEditableRecords();
    TestPdfCompatibleAiIsLabeledSeparately();
    TestMultiPagePdfImportsFirstPageWithLossWarning();
    TestPdfAdapterMaterializesSelectedNonFirstPage();
    TestPdfPageSetKeepsEveryPageAndBlankPages();
    std::cout << "test_pdf_worker_adapter: editable PDF vector records and rejection gates passed\n";
    return 0;
}
