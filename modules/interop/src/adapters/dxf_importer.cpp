#include "pikdraw/interop/adapters/dxf_importer.hpp"

#include "dxf_importer_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace pikdraw::interop {
namespace {
DxfImportResult Failure(std::string message, std::string feature) {
  DxfImportResult result;
  result.error_message = std::move(message);
  result.report.add_issue(FidelitySeverity::Unsupported, {}, std::move(feature),
                          result.error_message);
  return result;
}

using namespace dxf_detail;

struct DrawingBounds {
  double min_x{std::numeric_limits<double>::infinity()};
  double min_y{std::numeric_limits<double>::infinity()};
  double max_x{-std::numeric_limits<double>::infinity()};
  double max_y{-std::numeric_limits<double>::infinity()};

  void include(double x, double y, double width, double height) {
    min_x = std::min(min_x, x);
    min_y = std::min(min_y, y);
    max_x = std::max(max_x, x + width);
    max_y = std::max(max_y, y + height);
  }
};

bool InitializeLayers(const std::vector<LayerInfo> &layers,
                      DxfImportResult &result,
                      std::unordered_map<std::string, size_t> &layer_indices,
                      std::unordered_set<std::string> &source_ids,
                      DxfImportResult &error) {
  for (size_t i = 0; i < layers.size(); ++i) {
    const auto &layer = layers[i];
    const std::string source_id = "dxf-layer-" + std::to_string(i + 1);
    if (!source_ids.insert(source_id).second) {
      error = Failure("DXF layer source identifiers are duplicated",
                      "invalid_source_id");
      return false;
    }
    layer_indices.emplace(layer.name, i);
    result.layer_source_ids.push_back(source_id);
    result.page.layers.push_back(
        document::Layer{layer.name, layer.visible, layer.locked, {}});
    if (layer.color_index == 7) {
      result.report.add_issue(FidelitySeverity::Warning,
                              result.layer_source_ids.back(),
                              "aci_color_7_context",
                              "ACI color 7 depends on the CAD background; "
                              "preview is mapped to white");
    }
  }
  return true;
}

bool ResolveLayer(const EntityInfo &entity,
                  const std::unordered_map<std::string, size_t> &layer_indices,
                  size_t &layer_index, DxfImportResult &error) {
  const auto names = Values(entity, 8);
  const std::string_view layer_name =
      names.empty() ? std::string_view("0") : Trim(names.front());
  if (names.size() > 1 || layer_name.empty()) {
    error = Failure("DXF entity layer reference is malformed",
                    "invalid_layer_reference");
    return false;
  }
  const auto layer_it = layer_indices.find(std::string(layer_name));
  if (layer_it == layer_indices.end()) {
    error = Failure("DXF entity references an unknown layer",
                    "unknown_layer_reference");
    return false;
  }
  layer_index = layer_it->second;
  return true;
}

bool AppendEntity(size_t ordinal, const EntityInfo &entity,
                  const std::vector<LayerInfo> &layers, double scale,
                  const std::unordered_map<std::string, size_t> &layer_indices,
                  std::unordered_set<std::string> &source_ids,
                  std::unordered_set<std::string> &entity_handles,
                  DxfImportResult &result, DrawingBounds &drawing_bounds,
                  DxfImportResult &error) {
  size_t layer_index = 0;
  if (!ResolveLayer(entity, layer_indices, layer_index, error))
    return false;
  const auto &layer = layers[layer_index];
  geometry::Path2D path;
  if (!ParseEntity(entity, scale, path) || path.empty() ||
      path.size() > DxfImporter::kMaxPathElements) {
    error = Failure("DXF entity type or geometry is outside dxf-entities-v1",
                    "unsupported_entity_or_geometry");
    return false;
  }
  document::PathObject object;
  object.id = document::generate_object_id();
  int aci = 0;
  if (!EntityStyle(entity, layer, object, aci)) {
    error = Failure("DXF entity style is unsupported or malformed",
                    "unsupported_entity_style");
    return false;
  }
  std::string source_id;
  if (!EntityName(entity, ordinal, source_id) ||
      !entity_handles.insert(source_id).second || source_ids.contains(source_id)) {
    error = Failure("DXF entity handles are invalid or duplicated",
                    "invalid_source_id");
    return false;
  }
  source_ids.insert(source_id);
  if (aci == 7) {
    result.report.add_issue(FidelitySeverity::Warning, source_id,
                            "aci_color_7_context",
                            "ACI color 7 depends on the CAD background; "
                            "preview is mapped to white");
  }
  object.path = std::move(path);
  const auto bounds = object.path.bounds();
  if (!bounds.is_empty()) {
    drawing_bounds.include(bounds.x, bounds.y, bounds.width, bounds.height);
  }
  result.page.layers[layer_index].object_ids.push_back(object.id);
  result.objects.emplace_back(std::move(object));
  result.source_ids.push_back(std::move(source_id));
  return true;
}

bool SetPageDimensions(const DrawingBounds &bounds, document::Page &page,
                       DxfImportResult &error) {
  if (!std::isfinite(bounds.min_x) || !std::isfinite(bounds.min_y) ||
      !std::isfinite(bounds.max_x) || !std::isfinite(bounds.max_y)) {
    error = Failure("DXF contains no measurable planar geometry",
                    "invalid_geometry");
    return false;
  }
  const double width = bounds.max_x - bounds.min_x;
  const double height = bounds.max_y - bounds.min_y;
  if (width > DxfImporter::kMaxPageMillimeters ||
      height > DxfImporter::kMaxPageMillimeters) {
    error = Failure("DXF drawing extents exceed the supported page range",
                    "resource_limit");
    return false;
  }
  page.width_mm = std::max(width, 1.0);
  page.height_mm = std::max(height, 1.0);
  return true;
}

DxfImportResult ImportDxf(std::string_view source) {
  if (source.empty() || source.size() > DxfImporter::kMaxSourceBytes) {
    return Failure("DXF source exceeds the bounded input profile",
                   "resource_limit");
  }
  std::vector<Pair> pairs;
  if (!ParsePairs(source, pairs))
    return Failure("DXF group-code/value pairs are malformed or exceed limits",
                   "malformed_dxf");
  std::optional<int> units;
  std::vector<LayerInfo> layers;
  std::vector<EntityInfo> entities;
  if (!ParseSections(pairs, units, layers, entities))
    return Failure("DXF sections, layers, or required units are unsupported",
                   "unsupported_dxf_structure");
  double scale = 0;
  if (!UnitScale(*units, scale))
    return Failure(
        "DXF $INSUNITS is absent, unitless, or outside the supported set",
        "unsupported_units");

  DxfImportResult result;
  result.page = document::Page::make_default();
  result.page.layers.clear();
  result.objects.reserve(entities.size());
  result.source_ids.reserve(entities.size());
  result.layer_source_ids.reserve(layers.size());
  std::unordered_map<std::string, size_t> layer_indices;
  std::unordered_set<std::string> source_ids;
  DxfImportResult error;
  if (!InitializeLayers(layers, result, layer_indices, source_ids, error))
    return error;

  DrawingBounds drawing_bounds;
  std::unordered_set<std::string> entity_handles;
  for (size_t i = 0; i < entities.size(); ++i) {
    if (!AppendEntity(i + 1, entities[i], layers, scale, layer_indices,
                      source_ids, entity_handles, result, drawing_bounds,
                      error)) {
      return error;
    }
  }
  if (!SetPageDimensions(drawing_bounds, result.page, error))
    return error;
  result.success = true;
  return result;
}

} // namespace

DxfImportResult DxfImporter::import_from_string(std::string_view source) {
  try {
    return ImportDxf(source);
  } catch (...) {
    return Failure("DXF parser allocation or conversion failure",
                   "resource_limit");
  }
}

} // namespace pikdraw::interop
