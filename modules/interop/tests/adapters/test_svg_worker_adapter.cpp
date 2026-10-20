#include "pikdraw/interop/adapters/svg_worker_adapter.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace pikdraw::interop;

namespace {

void TestTypedRecordRoundTrip() {
    const std::string source = R"(<svg xmlns="http://www.w3.org/2000/svg"
        xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" width="100mm" height="50mm" viewBox="0 0 400 200">
        <g id="art-layer" inkscape:groupmode="layer" inkscape:label="Artwork">
            <g id="group">
                <path id="outline" d="M0 0 C20 0 20 20 40 20 Q60 40 80 20 Z" fill="#2480c0" stroke="#123" stroke-width="2"/>
                <text id="label" x="20" y="100" font-family="Noto Sans" font-size="24">Tiếng Việt &amp; vectors</text>
            </g>
        </g>
    </svg>)";
    std::vector<CanonicalImportRecord> records;
    std::string diagnostic;
    assert(BuildSvgImportRecords(std::span(reinterpret_cast<const uint8_t*>(source.data()), source.size()), records, diagnostic));
    assert(diagnostic.empty());
    assert(records.size() == 7);
    assert(records[0].type == ImportRecordType::Page);
    assert(records[1].type == ImportRecordType::Layer);
    assert(records[2].type == ImportRecordType::Group);
    assert(records[3].type == ImportRecordType::Path);
    assert(records[4].type == ImportRecordType::Text);
    assert(records[5].type == ImportRecordType::FidelityIssue);
    assert(records[6].type == ImportRecordType::FidelityReport);

    std::vector<ImportRecordValue> decoded;
    for (const auto& record : records) {
        auto value = DeserializeImportRecord(record.type, record.payload);
        assert(value.has_value());
        decoded.push_back(std::move(*value));
    }
    const auto& page = std::get<ImportPageRecord>(decoded[0]);
    assert(page.width_mm == 100.0 && page.height_mm == 50.0);
    const auto& layer = std::get<ImportLayerRecord>(decoded[1]);
    assert(layer.local_id == 1 && layer.source_id == "art-layer" && layer.name == "Artwork");
    assert(layer.object_local_ids == std::vector<uint32_t>{1});
    const auto& group = std::get<ImportGroupRecord>(decoded[2]);
    assert(group.local_id == 1 && group.source_id == "group");
    assert(group.child_local_ids == std::vector<uint32_t>({2, 3}));
    const auto& path = std::get<ImportPathRecord>(decoded[3]);
    assert(path.source_id == "outline" && path.local_id == 2 && path.elements.size() == 4);
    assert(path.elements[1].verb == ImportPathVerb::Cubic);
    assert(path.fill.model == ImportColorModel::Rgb && path.has_stroke);
    const auto& text = std::get<ImportTextRecord>(decoded[4]);
    assert(text.source_id == "label" && text.local_id == 3 && text.utf8_text == "Tiếng Việt & vectors");
    assert(text.font_family == "Noto Sans");
    assert(std::abs(text.font_size_pt - (6.0 * 72.0 / 25.4)) < 1e-9);
    const auto& issue = std::get<ImportFidelityIssueRecord>(decoded[5]);
    assert(issue.source_locator == "label" && issue.feature_code == "font_resolution");
    const auto& report = std::get<ImportFidelityReportRecord>(decoded[6]);
    assert(report.coverage_profile == "svg-hierarchy-v3" && report.issue_count == 1);
    assert(report.coverage == ImportCoverage::Partial && report.appearance == ImportAppearance::Partial);
}

void TestClipMaskSurvivesCanonicalRecords() {
    const std::string source = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100mm" height="100mm">
        <defs><clipPath id="frame"><rect x="10" y="10" width="80" height="80"/></clipPath></defs>
        <g id="clipped-content" clip-path="url(#frame)">
            <rect id="oversized-image-frame" x="0" y="0" width="100" height="100" fill="#123456"/>
        </g>
    </svg>)svg";
    std::vector<CanonicalImportRecord> records;
    std::string diagnostic;
    assert(BuildSvgImportRecords(
        std::span(reinterpret_cast<const uint8_t*>(source.data()), source.size()), records, diagnostic));
    assert(diagnostic.empty());

    bool found_clipping_group = false;
    bool found_mask_path = false;
    for (const auto& record : records) {
        const auto decoded = DeserializeImportRecord(record.type, record.payload);
        assert(decoded.has_value());
        if (const auto* group = std::get_if<ImportGroupRecord>(&*decoded)) {
            if (group->source_id != "clipped-content") continue;
            found_clipping_group = true;
            assert(group->clip_mask_local_id != 0);
            assert(!group->child_local_ids.empty());
            assert(group->child_local_ids.front() == group->clip_mask_local_id);
        } else if (const auto* path = std::get_if<ImportPathRecord>(&*decoded)) {
            found_mask_path |= path->source_id == "frame";
        }
    }
    assert(found_clipping_group && found_mask_path);

    ImportGroupRecord legacy_group{1, "legacy", "Legacy", {2}, 0};
    const auto legacy_encoded = SerializeImportRecord(legacy_group);
    assert(legacy_encoded.has_value());
    const auto legacy_decoded = DeserializeImportRecord(legacy_encoded->type, legacy_encoded->payload);
    assert(legacy_decoded.has_value());
    assert(std::get<ImportGroupRecord>(*legacy_decoded).clip_mask_local_id == 0);
}

void TestAffineTransformCoordinatesRemainStable() {
    const std::string source = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100mm" height="100mm" viewBox="0 0 100 100">
        <g id="rotated" transform="translate(20 0) rotate(90)">
            <rect id="rotated-rect" x="0" y="0" width="10" height="20" fill="#123456"/>
        </g>
    </svg>)svg";
    std::vector<CanonicalImportRecord> records;
    std::string diagnostic;
    assert(BuildSvgImportRecords(
        std::span(reinterpret_cast<const uint8_t*>(source.data()), source.size()), records, diagnostic));
    std::optional<ImportPathRecord> path;
    for (const auto& record : records) {
        if (record.type != ImportRecordType::Path) continue;
        const auto decoded = DeserializeImportRecord(record.type, record.payload);
        assert(decoded.has_value());
        const auto* decoded_path = std::get_if<ImportPathRecord>(&*decoded);
        assert(decoded_path && decoded_path->source_id == "rotated-rect");
        path = *decoded_path;
        break;
    }
    assert(path && path->elements.size() == 5);
    double min_x = std::numeric_limits<double>::infinity();
    double min_y = std::numeric_limits<double>::infinity();
    double max_x = -std::numeric_limits<double>::infinity();
    double max_y = -std::numeric_limits<double>::infinity();
    for (const auto& element : path->elements) {
        if (element.verb == ImportPathVerb::Close) continue;
        min_x = std::min(min_x, element.values[0]);
        min_y = std::min(min_y, element.values[1]);
        max_x = std::max(max_x, element.values[0]);
        max_y = std::max(max_y, element.values[1]);
    }
    assert(std::abs(min_x - 0.0) < 1e-9 && std::abs(max_x - 20.0) < 1e-9);
    assert(std::abs(min_y - 90.0) < 1e-9 && std::abs(max_y - 100.0) < 1e-9);
}

void TestSemanticDecoderRejectsInvalidRecords() {
    ImportPageRecord page{100, 50};
    auto encoded = SerializeImportRecord(page);
    assert(encoded.has_value());
    encoded->payload.push_back(0);
    assert(!DeserializeImportRecord(ImportRecordType::Page, encoded->payload));

    ImportTextRecord text;
    text.local_id = 1;
    text.source_id = "label";
    text.name = "label";
    text.utf8_text = "bad";
    text.font_family = "test";
    text.font_size_pt = std::numeric_limits<double>::quiet_NaN();
    text.fill.components = {0, 0, 0, 1, 0};
    assert(!SerializeImportRecord(text));

    ImportPathRecord path;
    path.local_id = 1;
    path.source_id = "path";
    path.name = "Path";
    path.elements = {{ImportPathVerb::Line, 0, {1, 1}}, {ImportPathVerb::Close, 0, {}}};
    assert(!SerializeImportRecord(path)); // A drawable path must begin with Move.
    path.elements = {{ImportPathVerb::Move, 0, {}}, {ImportPathVerb::Line, 0, {1, 1}}};
    path.elements[1].values[2] = 1;
    assert(!SerializeImportRecord(path)); // Unused command fields must be zero.

    text.font_size_pt = 12;
    text.font_postscript_name = "NotoSans-Regular";
    const auto text_record = SerializeImportRecord(text);
    assert(text_record);
    const auto restored_text = DeserializeImportRecord(text_record->type, text_record->payload);
    assert(restored_text && std::get<ImportTextRecord>(*restored_text).font_postscript_name ==
           "NotoSans-Regular");
    text.font_postscript_name.clear();
    text.fill.alternate_rgb = {1, 0, 0, 1};
    assert(!SerializeImportRecord(text)); // Alternate RGB is only valid for a Spot color.
    const std::array<uint8_t, 1> invalid_payload{0};
    assert(!DeserializeImportRecord(static_cast<ImportRecordType>(99), invalid_payload));

    ImportGroupRecord group;
    group.local_id = 7;
    group.source_id = "group";
    group.name = "Group";
    group.child_local_ids = {8, 8};
    assert(!SerializeImportRecord(group));
    group.child_local_ids = {7};
    assert(!SerializeImportRecord(group));

    ImportLayerRecord layer;
    layer.local_id = 1;
    layer.source_id = "layer";
    layer.name = "Layer";
    layer.object_local_ids = {2, 2};
    assert(!SerializeImportRecord(layer));
}

void TestUnsupportedSvgProducesNoProposalRecords() {
    const std::string source = R"(<svg width="20mm" height="20mm"><filter id="blur"/><rect width="5" height="5"/></svg>)";
    std::vector<CanonicalImportRecord> records;
    std::string diagnostic;
    assert(!BuildSvgImportRecords(std::span(reinterpret_cast<const uint8_t*>(source.data()), source.size()), records, diagnostic));
    assert(records.empty());
    assert(!diagnostic.empty());
}

} // namespace

int main() {
    TestTypedRecordRoundTrip();
    TestClipMaskSurvivesCanonicalRecords();
    TestAffineTransformCoordinatesRemainStable();
    TestSemanticDecoderRejectsInvalidRecords();
    TestUnsupportedSvgProducesNoProposalRecords();
    std::cout << "SVG worker proposal record tests passed\n";
    return 0;
}
