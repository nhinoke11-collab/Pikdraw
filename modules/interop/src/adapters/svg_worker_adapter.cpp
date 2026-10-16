#include "pikdraw/interop/adapters/svg_worker_adapter.hpp"

#include <cmath>
#include <functional>
#include <unordered_map>

namespace pikdraw::interop {
namespace {
ImportColorRecord FromColor(const color::ColorValue& value) {
    ImportColorRecord result;
    result.overprint_flags = static_cast<uint8_t>((value.overprint().fill ? 1 : 0) | (value.overprint().stroke ? 2 : 0));
    std::visit([&](const auto& color_value) {
        using T = std::decay_t<decltype(color_value)>;
        if constexpr (std::is_same_v<T, color::RGBColor>) {
            result.model = ImportColorModel::Rgb;
            result.components = {color_value.r, color_value.g, color_value.b, color_value.a, 0};
        } else if constexpr (std::is_same_v<T, color::CMYKColor>) {
            result.model = ImportColorModel::Cmyk;
            result.components = {color_value.c, color_value.m, color_value.y, color_value.k, color_value.a};
        } else if constexpr (std::is_same_v<T, color::SpotColor>) {
            result.model = ImportColorModel::Spot;
            result.components = {color_value.tint, color_value.alternate_rgb.a, 0, 0, 0};
            result.spot_name = color_value.spot_name;
            result.alternate_rgb = {color_value.alternate_rgb.r, color_value.alternate_rgb.g,
                                    color_value.alternate_rgb.b, color_value.alternate_rgb.a};
            if (color_value.overprint) result.overprint_flags |= 1;
        } else {
            result.model = ImportColorModel::Gray;
            result.components = {color_value.gray, color_value.a, 0, 0, 0};
        }
    }, value.payload());
    return result;
}

} // namespace

bool BuildSvgImportRecords(std::span<const uint8_t> source,
                           std::vector<CanonicalImportRecord>& records,
                           std::string& diagnostic) {
    records.clear();
    diagnostic.clear();
    try {
    if (source.empty() || source.size() > SvgImporter::kMaxSourceBytes) {
        diagnostic = "SVG source exceeds the bounded input profile";
        return false;
    }
    const std::string input(reinterpret_cast<const char*>(source.data()), source.size());
    const auto imported = SvgImporter::import_from_string(input);
    if (!imported.success) {
        diagnostic = imported.error_message.empty() ? "SVG parser rejected the source" : imported.error_message;
        return false;
    }

    const auto fail = [&](const char* reason) {
        records.clear();
        diagnostic = reason;
        return false;
    };

    const auto add_record = [&](const ImportRecordValue& value) {
        auto encoded = SerializeImportRecord(value);
        if (!encoded) return false;
        records.push_back(std::move(*encoded));
        return records.size() <= SvgImporter::kMaxObjects * 2 + 2;
    };

    if (imported.source_ids.size() != imported.objects.size() ||
        imported.layer_source_ids.size() != imported.page.layers.size() ||
        imported.page.layers.empty() || imported.page.layers.size() > SvgImporter::kMaxObjects) {
        return fail("SVG importer returned inconsistent object or layer provenance");
    }

    std::unordered_map<document::ObjectId, size_t> object_indices;
    object_indices.reserve(imported.objects.size());
    for (size_t index = 0; index < imported.objects.size(); ++index) {
        const document::ObjectId id = std::visit([](const auto& object) { return object.id; }, imported.objects[index]);
        if (id == document::kInvalidObjectId || !object_indices.emplace(id, index).second) {
            return fail("SVG importer returned a duplicate or invalid object ID");
        }
    }

    std::unordered_map<document::ObjectId, uint32_t> local_ids;
    local_ids.reserve(imported.objects.size());
    std::vector<document::ObjectId> object_order;
    object_order.reserve(imported.objects.size());
    uint32_t next_local_id = 1;
    std::function<bool(document::ObjectId, size_t)> visit_object;
    visit_object = [&](document::ObjectId id, size_t depth) {
        if (depth > 128 || object_order.size() >= SvgImporter::kMaxObjects || local_ids.contains(id)) return false;
        const auto found = object_indices.find(id);
        if (found == object_indices.end()) return false;
        local_ids.emplace(id, next_local_id++);
        object_order.push_back(id);
        const auto* group = std::get_if<document::GroupObject>(&imported.objects[found->second]);
        if (!group) return true;
        for (const document::ObjectId child_id : group->children_ids) {
            if (!visit_object(child_id, depth + 1)) return false;
        }
        return true;
    };

    for (const auto& layer : imported.page.layers) {
        for (const document::ObjectId id : layer.object_ids) {
            if (!visit_object(id, 0)) return fail("SVG layer and group references are inconsistent");
        }
    }
    if (object_order.size() != imported.objects.size()) {
        return fail("SVG object tree contains objects outside every layer");
    }

    if (!add_record(ImportPageRecord{imported.page.width_mm, imported.page.height_mm})) {
        return fail("SVG page record exceeds C6 record limits");
    }
    for (size_t index = 0; index < imported.page.layers.size(); ++index) {
        const auto& source_layer = imported.page.layers[index];
        ImportLayerRecord record;
        record.local_id = static_cast<uint32_t>(index + 1);
        record.source_id = imported.layer_source_ids[index];
        record.name = source_layer.name;
        record.visible = source_layer.visible;
        record.locked = source_layer.locked;
        record.object_local_ids.reserve(source_layer.object_ids.size());
        for (const document::ObjectId id : source_layer.object_ids) {
            const auto found = local_ids.find(id);
            if (found == local_ids.end()) return fail("SVG layer references an unknown object");
            record.object_local_ids.push_back(found->second);
        }
        if (!add_record(record)) return fail("SVG layer record exceeds C6 record limits");
    }

    for (const document::ObjectId id : object_order) {
        const size_t index = object_indices.at(id);
        const std::string& source_id = imported.source_ids[index];
        const auto& object = imported.objects[index];
        if (const auto* group = std::get_if<document::GroupObject>(&object)) {
            ImportGroupRecord record;
            record.local_id = local_ids.at(id);
            record.source_id = source_id;
            record.name = group->name;
            record.child_local_ids.reserve(group->children_ids.size());
            for (const document::ObjectId child_id : group->children_ids) {
                const auto found = local_ids.find(child_id);
                if (found == local_ids.end()) return fail("SVG group references an unknown child");
                record.child_local_ids.push_back(found->second);
            }
            if (!add_record(record)) return fail("SVG group record exceeds C6 record limits");
        } else if (const auto* path = std::get_if<document::PathObject>(&object)) {
            ImportPathRecord record;
            record.local_id = local_ids.at(id);
            record.source_id = source_id;
            record.name = path->name;
            record.winding_rule = static_cast<uint8_t>(path->path.winding_rule());
            record.has_fill = path->has_fill;
            record.has_stroke = path->has_stroke;
            record.stroke_width_mm = path->stroke_width_mm;
            record.fill = FromColor(path->fill);
            record.stroke = FromColor(path->stroke);
            record.elements.reserve(path->path.size());
            for (const auto& element : path->path.elements()) {
                ImportPathElementRecord converted;
                if (element.verb == geometry::PathVerb::MoveTo) converted.verb = ImportPathVerb::Move;
                else if (element.verb == geometry::PathVerb::LineTo) converted.verb = ImportPathVerb::Line;
                else if (element.verb == geometry::PathVerb::QuadTo) converted.verb = ImportPathVerb::Quad;
                else if (element.verb == geometry::PathVerb::CubicTo) converted.verb = ImportPathVerb::Cubic;
                else if (element.verb == geometry::PathVerb::ArcTo) converted.verb = ImportPathVerb::Arc;
                else converted.verb = ImportPathVerb::Close;
                converted.values = {element.pt.x, element.pt.y, element.ctrl1.x, element.ctrl1.y,
                                    element.ctrl2.x, element.ctrl2.y, element.rx, element.ry, element.rotation_deg};
                converted.flags = static_cast<uint8_t>((element.large_arc ? 1 : 0) | (element.sweep ? 2 : 0));
                record.elements.push_back(converted);
            }
            if (!add_record(record)) return fail("SVG path record exceeds C6 record limits");
        } else if (const auto* text = std::get_if<document::TextObject>(&object)) {
            ImportTextRecord record;
            record.local_id = local_ids.at(id);
            record.source_id = source_id;
            record.name = text->name;
            record.utf8_text = text->text;
            record.font_family = text->font.family_name;
            record.font_weight = text->font.weight;
            record.italic = text->font.italic;
            record.font_size_pt = text->font.size_pt;
            record.x_mm = text->position.x;
            record.y_mm = text->position.y;
            record.fill = FromColor(text->fill);
            if (!add_record(record)) return fail("SVG text record exceeds C6 record limits");
        }
    }

    for (const auto& issue : imported.report.issues()) {
        ImportFidelityIssueRecord record;
        record.severity = issue.severity == FidelitySeverity::Info ? ImportFidelitySeverity::Info :
            issue.severity == FidelitySeverity::Unsupported ? ImportFidelitySeverity::Unsupported : ImportFidelitySeverity::Warning;
        record.source_locator = issue.element_name;
        record.feature_code = issue.feature;
        record.diagnostic_code = "appearance_or_fidelity_unverified";
        if (!add_record(record)) return fail("SVG fidelity report exceeds C6 record limits");
    }
    ImportFidelityReportRecord report;
    report.coverage_profile = "svg-hierarchy-v2";
    report.parse_outcome = imported.report.is_clean() ? ImportParseOutcome::Complete : ImportParseOutcome::CompleteWithDiagnostics;
    report.coverage = ImportCoverage::Partial;
    report.appearance = imported.report.is_clean() ? ImportAppearance::Unknown : ImportAppearance::Partial;
    report.editability = ImportEditability::NativeEditable;
    report.production = ImportProduction::NotAssessed;
    report.issue_count = static_cast<uint32_t>(imported.report.issue_count());
    if (!add_record(report)) return fail("SVG fidelity summary exceeds C6 record limits");

    uint32_t expected_local_id = 1;
    uint32_t expected_layer_id = 1;
    uint32_t issue_count = 0;
    bool issues_started = false;
    bool report_seen = false;
    bool objects_started = false;
    for (size_t index = 0; index < records.size(); ++index) {
        const auto& record = records[index];
        const auto decoded = DeserializeImportRecord(record.type, record.payload);
        if (!decoded) return fail("SVG proposal record failed producer semantic validation");
        if (record.type == ImportRecordType::Page) {
            if (index != 0) return fail("SVG proposal page record is out of order");
        } else if (record.type == ImportRecordType::Layer) {
            const auto* layer = std::get_if<ImportLayerRecord>(&*decoded);
            if (!layer || objects_started || issues_started || layer->local_id != expected_layer_id++) {
                return fail("SVG proposal layer record order or ID is invalid");
            }
        } else if (record.type == ImportRecordType::Group) {
            const auto* group = std::get_if<ImportGroupRecord>(&*decoded);
            if (!group || issues_started || group->local_id != expected_local_id++) {
                return fail("SVG proposal group record order or ID is invalid");
            }
            objects_started = true;
        } else if (record.type == ImportRecordType::Path) {
            const auto* path = std::get_if<ImportPathRecord>(&*decoded);
            if (!path || issues_started || path->local_id != expected_local_id++) {
                return fail("SVG proposal path record order or ID is invalid");
            }
            objects_started = true;
        } else if (record.type == ImportRecordType::Text) {
            const auto* text = std::get_if<ImportTextRecord>(&*decoded);
            if (!text || issues_started || text->local_id != expected_local_id++) {
                return fail("SVG proposal text record order or ID is invalid");
            }
            objects_started = true;
        } else if (record.type == ImportRecordType::FidelityIssue) {
            issues_started = true;
            ++issue_count;
        } else if (record.type == ImportRecordType::FidelityReport) {
            const auto* decoded_report = std::get_if<ImportFidelityReportRecord>(&*decoded);
            if (!decoded_report || index + 1 != records.size() || report_seen || decoded_report->issue_count != issue_count) {
                return fail("SVG proposal fidelity report is absent, duplicated, or inconsistent");
            }
            report_seen = true;
        } else {
            return fail("SVG proposal contains an unknown record type");
        }
    }
    if (!report_seen) return fail("SVG proposal has no terminal fidelity report");
    return true;
    } catch (...) {
        records.clear();
        diagnostic = "SVG proposal allocation or codec failure";
        return false;
    }
}


} // namespace pikdraw::interop
