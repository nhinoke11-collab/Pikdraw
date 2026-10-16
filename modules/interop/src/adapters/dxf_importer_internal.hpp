#pragma once

#include "pikdraw/interop/adapters/dxf_importer.hpp"

#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pikdraw::interop::dxf_detail {

struct Pair {
  int code;
  std::string_view value;
};

struct LayerInfo {
  std::string name;
  int color_index{7};
  double lineweight_mm{0.25};
  bool visible{true};
  bool locked{false};
};

struct EntityInfo {
  std::string_view type;
  std::span<const Pair> fields;
};

std::string_view Trim(std::string_view value);
bool ValidUtf8(std::string_view value);
bool ParsePairs(std::string_view source, std::vector<Pair> &pairs);
bool ParseDouble(std::string_view source, double &value);
bool ParseInt(std::string_view source, int &value);
std::vector<std::string_view> Values(const EntityInfo &entity, int code);
bool SingleDouble(const EntityInfo &entity, int code, double fallback,
                  double &value);
bool SingleInt(const EntityInfo &entity, int code, int fallback, int &value);
bool AllowedCodes(const EntityInfo &entity,
                  std::initializer_list<int> codes);
bool UnitScale(int code, double &scale);
bool ParseLineweight(const EntityInfo &entity, int code, double fallback,
                     double &millimeters);
bool ParseSections(std::span<const Pair> pairs, std::optional<int> &units,
                   std::vector<LayerInfo> &layers,
                   std::vector<EntityInfo> &entities);
bool ParseEntity(const EntityInfo &entity, double scale,
                 geometry::Path2D &path);
bool EntityStyle(const EntityInfo &entity, const LayerInfo &layer,
                 double scale, document::PathObject &object, int &aci);
bool EntityName(const EntityInfo &entity, size_t ordinal,
                std::string &source_id);

} // namespace pikdraw::interop::dxf_detail
