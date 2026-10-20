#include "pikdraw/interop/adapters/eps_worker_adapter.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace pikdraw::interop;

std::vector<uint8_t> Bytes(std::string_view source) {
    return {source.begin(), source.end()};
}

bool Import(std::string_view source, std::vector<CanonicalImportRecord>& records,
            std::string& diagnostic) {
    const auto bytes = Bytes(source);
    return BuildEpsImportRecords(bytes, records, diagnostic);
}

void TestEditablePathsAndAliases() {
    const std::string source =
        "%!PS-Adobe-3.0 EPSF-3.0\n"
        "%%BoundingBox: 10 20 110 70\n"
        "%%HiResBoundingBox: 10.5 20.5 110.5 70.5\n"
        "%%EndComments\n"
        "/m {moveto} bind def /l {lineto} bind def /c {curveto} bind def /f {fill} bind def\n"
        "1 0 0 setrgbcolor\n"
        "10.5 20.5 m 40 20.5 l 50 35 50 45 40 50 c closepath f showpage\n";
    std::vector<CanonicalImportRecord> records;
    std::string diagnostic;
    assert(Import(source, records, diagnostic));
    assert(diagnostic.empty());
    assert(records.size() >= 5);
    assert(records[0].type == ImportRecordType::Page);
    const auto page = std::get<ImportPageRecord>(*DeserializeImportRecord(records[0].type, records[0].payload));
    assert(std::abs(page.width_mm - 100.0 * 25.4 / 72.0) < 1e-8);
    assert(std::abs(page.height_mm - 50.0 * 25.4 / 72.0) < 1e-8);
    const auto layer = std::get<ImportLayerRecord>(*DeserializeImportRecord(records[1].type, records[1].payload));
    assert(layer.name == "EPS Artwork" && layer.object_local_ids == std::vector<uint32_t>{1});
    const auto path = std::get<ImportPathRecord>(*DeserializeImportRecord(records[2].type, records[2].payload));
    assert(path.has_fill && !path.has_stroke && path.elements.size() == 4);
    assert(path.elements[0].verb == ImportPathVerb::Move);
    assert(path.elements[0].values[0] == 0.0);
    assert(std::abs(path.elements[0].values[1] - page.height_mm) < 1e-8);
    assert(path.elements[2].verb == ImportPathVerb::Cubic);
    assert(path.elements.back().verb == ImportPathVerb::Close);
    assert(path.fill.model == ImportColorModel::Rgb);
    assert(path.fill.components[0] == 1.0 && path.fill.components[1] == 0.0);
    bool saw_report = false;
    for (const auto& record : records) {
        if (record.type != ImportRecordType::FidelityReport) continue;
        const auto report = std::get<ImportFidelityReportRecord>(*DeserializeImportRecord(record.type, record.payload));
        assert(report.coverage_profile == "eps-path-v1");
        assert(report.issue_count >= 2);
        saw_report = true;
    }
    assert(saw_report);
}

void TestCmykAndTransformedStroke() {
    const std::string source =
        "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 100 100\n%%EndComments\n"
        "gsave 2 2 scale 0.1 0.2 0.3 0.4 setcmykcolor 2 setlinewidth\n"
        "0 0 moveto 10 10 lineto stroke grestore\n";
    std::vector<CanonicalImportRecord> records;
    std::string diagnostic;
    assert(Import(source, records, diagnostic));
    const auto path = std::get<ImportPathRecord>(*DeserializeImportRecord(records[2].type, records[2].payload));
    assert(!path.has_fill && path.has_stroke);
    assert(path.stroke.model == ImportColorModel::Cmyk);
    assert(std::abs(path.stroke_width_mm - 4.0 * 25.4 / 72.0) < 1e-8);
    assert(std::abs(path.elements[1].values[0] - 20.0 * 25.4 / 72.0) < 1e-8);
}

void TestUnknownCodeRejectsAtomically() {
    const std::string source =
        "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 100 100\n%%EndComments\n"
        "10 10 moveto 20 20 lineto stroke setpagedevice\n";
    std::vector<CanonicalImportRecord> records;
    std::string diagnostic;
    assert(!Import(source, records, diagnostic));
    assert(records.empty());
    assert(diagnostic.find("setpagedevice") != std::string::npos);
}

void TestBinaryAndMissingBoundsAreRejected() {
    std::vector<CanonicalImportRecord> records;
    std::string diagnostic;
    assert(!Import("%!PS-Adobe-3.0 EPSF-3.0\n%%BeginBinary: 4\n", records, diagnostic));
    assert(records.empty());
    assert(!Import("%!PS-Adobe-3.0 EPSF-3.0\n%%EndComments\n", records, diagnostic));
    assert(records.empty());
}

} // namespace

int main() {
    TestEditablePathsAndAliases();
    TestCmykAndTransformedStroke();
    TestUnknownCodeRejectsAtomically();
    TestBinaryAndMissingBoundsAreRejected();
    std::cout << "test_eps_worker_adapter: safe EPS path subset passed\n";
    return 0;
}
