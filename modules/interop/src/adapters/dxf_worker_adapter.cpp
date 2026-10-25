#include "pikdraw/interop/adapters/dxf_worker_adapter.hpp"

#include "pikdraw/interop/adapters/dxf_importer.hpp"

#include <unordered_map>

namespace pikdraw::interop {
namespace {

ImportColorRecord FromColor(const color::ColorValue &value) {
  ImportColorRecord result;
  result.overprint_flags = static_cast<uint8_t>(
      (value.overprint().fill ? 1 : 0) | (value.overprint().stroke ? 2 : 0));
  const auto *rgb = std::get_if<color::RGBColor>(&value.payload());
  if (rgb == nullptr)
    return result;
  result.model = ImportColorModel::Rgb;
  result.components = {rgb->r, rgb->g, rgb->b, rgb->a, 0};
  return result;
}

bool AddRecord(const ImportRecordValue &value,
               std::vector<CanonicalImportRecord> &records) {
  auto encoded = SerializeImportRecord(value);
  if (!encoded)
    return false;
  records.push_back(std::move(*encoded));
  return records.size() <= DxfImporter::kMaxObjects * 4 + 2;
}

} // namespace

bool BuildDxfImportRecords(std::span<const uint8_t> source,
                           std::vector<CanonicalImportRecord> &records,
                           std::string &diagnostic) {
  records.clear();
  diagnostic.clear();
  try {
    if (source.empty() || source.size() > DxfImporter::kMaxSourceBytes) {
      diagnostic = "DXF source exceeds the bounded input profile";
      return false;
    }
    const std::string_view input(reinterpret_cast<const char *>(source.data()),
                                 source.size());
    auto imported = DxfImporter::import_from_string(input);
    if (!imported.success) {
      diagnostic = imported.error_message.empty() ? "DXF subset rejected source"
                                                  : imported.error_message;
      return false;
    }
    if (imported.objects.size() != imported.source_ids.size() ||
        imported.page.layers.size() != imported.layer_source_ids.size() ||
        imported.page.layers.empty()) {
      diagnostic = "DXF importer returned inconsistent source provenance";
      return false;
    }

    std::unordered_map<document::ObjectId, size_t> indices;
    for (size_t i = 0; i < imported.objects.size(); ++i) {
      const auto *path =
          std::get_if<document::PathObject>(&imported.objects[i]);
      if (path == nullptr || path->id == document::kInvalidObjectId ||
          !indices.emplace(path->id, i).second) {
        diagnostic = "DXF importer returned a duplicate ID or non-path object";
        return false;
      }
    }

    std::unordered_map<document::ObjectId, uint32_t> local_ids;
    std::vector<document::ObjectId> order;
    order.reserve(imported.objects.size());
    for (const auto &layer : imported.page.layers) {
      for (const auto id : layer.object_ids) {
        if (!indices.contains(id) || local_ids.contains(id) ||
            order.size() >= DxfImporter::kMaxObjects) {
          diagnostic =
              "DXF layer contains an unknown or duplicate entity reference";
          return false;
        }
        local_ids.emplace(id, static_cast<uint32_t>(order.size() + 1));
        order.push_back(id);
      }
    }
    if (order.size() != imported.objects.size()) {
      diagnostic = "DXF contains an entity not owned by a source layer";
      return false;
    }

    if (!AddRecord(
            ImportPageRecord{imported.page.width_mm, imported.page.height_mm},
            records)) {
      diagnostic = "DXF page record exceeds C6 limits";
      records.clear();
      return false;
    }
    for (size_t i = 0; i < imported.page.layers.size(); ++i) {
      const auto &source_layer = imported.page.layers[i];
      ImportLayerRecord layer;
      layer.local_id = static_cast<uint32_t>(i + 1);
      layer.source_id = imported.layer_source_ids[i];
      layer.name = source_layer.name;
      layer.visible = source_layer.visible;
      layer.locked = source_layer.locked;
      for (const auto id : source_layer.object_ids)
        layer.object_local_ids.push_back(local_ids.at(id));
      if (!AddRecord(layer, records)) {
        diagnostic = "DXF layer record exceeds C6 limits";
        records.clear();
        return false;
      }
    }
    for (const auto id : order) {
      const size_t index = indices.at(id);
      auto &path =
          std::get<document::PathObject>(imported.objects[index]);
      ImportPathRecord record;
      record.local_id = local_ids.at(id);
      record.source_id = imported.source_ids[index];
      record.name = path.name;
      record.winding_rule = static_cast<uint8_t>(path.path.winding_rule());
      record.has_fill = path.has_fill;
      record.has_stroke = path.has_stroke;
      record.stroke_width_mm = path.stroke_width_mm;
      record.fill = FromColor(path.fill);
      record.stroke = FromColor(path.stroke);
      for (const auto &element : path.path.elements()) {
        ImportPathElementRecord converted;
        if (element.verb == geometry::PathVerb::MoveTo)
          converted.verb = ImportPathVerb::Move;
        else if (element.verb == geometry::PathVerb::LineTo)
          converted.verb = ImportPathVerb::Line;
        else if (element.verb == geometry::PathVerb::ArcTo)
          converted.verb = ImportPathVerb::Arc;
        else if (element.verb == geometry::PathVerb::Close)
          converted.verb = ImportPathVerb::Close;
        else {
          diagnostic = "DXF path contains a curve type outside dxf-entities-v1";
          records.clear();
          return false;
        }
        converted.values = {
            element.pt.x,    element.pt.y,    element.ctrl1.x,
            element.ctrl1.y, element.ctrl2.x, element.ctrl2.y,
            element.rx,      element.ry,      element.rotation_deg};
        converted.flags = static_cast<uint8_t>((element.large_arc ? 1 : 0) |
                                               (element.sweep ? 2 : 0));
        record.elements.push_back(converted);
      }
      if (record.elements.empty() ||
          record.elements.size() > DxfImporter::kMaxPathElements ||
          !AddRecord(record, records)) {
        diagnostic = "DXF path record exceeds C6 limits";
        records.clear();
        return false;
      }
      path.path = geometry::Path2D{};
    }

    for (const auto &issue : imported.report.issues()) {
      if (issue.severity == FidelitySeverity::Unsupported) {
        diagnostic = "DXF parser reported unsupported source content";
        records.clear();
        return false;
      }
      ImportFidelityIssueRecord issue_record;
      issue_record.severity = issue.severity == FidelitySeverity::Info
                                  ? ImportFidelitySeverity::Info
                                  : ImportFidelitySeverity::Warning;
      issue_record.source_locator = issue.element_name;
      issue_record.feature_code = issue.feature;
      issue_record.diagnostic_code = "appearance_approximation";
      if (!AddRecord(issue_record, records)) {
        diagnostic = "DXF fidelity records exceed C6 limits";
        records.clear();
        return false;
      }
    }
    ImportFidelityReportRecord report;
    report.coverage_profile = "dxf-entities-v1";
    report.parse_outcome = imported.report.is_clean()
                               ? ImportParseOutcome::Complete
                               : ImportParseOutcome::CompleteWithDiagnostics;
    report.coverage = ImportCoverage::Partial;
    report.appearance = imported.report.is_clean() ? ImportAppearance::Unknown
                                                   : ImportAppearance::Partial;
    report.editability = ImportEditability::NativeEditable;
    report.production = ImportProduction::NotAssessed;
    report.issue_count = static_cast<uint32_t>(imported.report.issue_count());
    if (!AddRecord(report, records)) {
      diagnostic = "DXF fidelity summary exceeds C6 limits";
      records.clear();
      return false;
    }
    return true;
  } catch (...) {
    records.clear();
    diagnostic = "DXF proposal allocation or serialization failure";
    return false;
  }
}

} // namespace pikdraw::interop
