#include "dxf_importer_internal.hpp"
#include "pikdraw/geometry/units/length_unit.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace pikdraw::interop::dxf_detail {

constexpr double kDefaultLineweightMm = 0.25;
std::string_view Trim(std::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
    value.remove_prefix(1);
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
    value.remove_suffix(1);
  return value;
}

bool ValidUtf8(std::string_view value) {
  size_t index = 0;
  while (index < value.size()) {
    const uint8_t lead = static_cast<uint8_t>(value[index++]);
    if (lead < 0x80) {
      if (lead < 0x20 && lead != '\t' && lead != '\r' && lead != '\n')
        return false;
      continue;
    }
    uint32_t codepoint = 0;
    size_t continuation = 0;
    if ((lead & 0xe0) == 0xc0) {
      codepoint = lead & 0x1f;
      continuation = 1;
    } else if ((lead & 0xf0) == 0xe0) {
      codepoint = lead & 0x0f;
      continuation = 2;
    } else if ((lead & 0xf8) == 0xf0) {
      codepoint = lead & 0x07;
      continuation = 3;
    } else
      return false;
    if (continuation > value.size() - index)
      return false;
    for (size_t i = 0; i < continuation; ++i) {
      const uint8_t next = static_cast<uint8_t>(value[index++]);
      if ((next & 0xc0) != 0x80)
        return false;
      codepoint = (codepoint << 6) | (next & 0x3f);
    }
    if ((continuation == 1 && codepoint < 0x80) ||
        (continuation == 2 && codepoint < 0x800) ||
        (continuation == 3 && codepoint < 0x10000) || codepoint > 0x10ffff ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff))
      return false;
  }
  return true;
}

bool ReadLine(std::string_view source, size_t &offset, std::string_view &line) {
  if (offset > source.size())
    return false;
  const size_t end = source.find('\n', offset);
  const size_t line_end = end == std::string_view::npos ? source.size() : end;
  line = source.substr(offset, line_end - offset);
  if (!line.empty() && line.back() == '\r')
    line.remove_suffix(1);
  offset = end == std::string_view::npos ? source.size() : end + 1;
  return line.size() <= 4096;
}

bool ParsePairs(std::string_view source, std::vector<Pair> &pairs) {
  if (source.starts_with("\xef\xbb\xbf"))
    source.remove_prefix(3);
  if (source.empty() || !ValidUtf8(source))
    return false;
  size_t offset = 0;
  while (offset < source.size()) {
    std::string_view code_line, value_line;
    if (!ReadLine(source, offset, code_line) || offset >= source.size() ||
        !ReadLine(source, offset, value_line))
      return false;
    code_line = Trim(code_line);
    int code = -1;
    const auto parsed = std::from_chars(
        code_line.data(), code_line.data() + code_line.size(), code);
    if (code_line.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != code_line.data() + code_line.size() || code < 0 ||
        code > 1071 || pairs.size() >= DxfImporter::kMaxPairs)
      return false;
    pairs.push_back(Pair{code, value_line});
  }
  return !pairs.empty();
}

bool ParseDouble(std::string_view source, double &value) {
  source = Trim(source);
  if (source.empty())
    return false;
  const auto parsed =
      std::from_chars(source.data(), source.data() + source.size(), value,
                      std::chars_format::general);
  return parsed.ec == std::errc{} &&
         parsed.ptr == source.data() + source.size() && std::isfinite(value);
}

bool ParseInt(std::string_view source, int &value) {
  source = Trim(source);
  if (source.empty())
    return false;
  const auto parsed =
      std::from_chars(source.data(), source.data() + source.size(), value);
  return parsed.ec == std::errc{} &&
         parsed.ptr == source.data() + source.size();
}

std::vector<std::string_view> Values(const EntityInfo &entity, int code) {
  std::vector<std::string_view> values;
  for (const auto &field : entity.fields)
    if (field.code == code)
      values.push_back(field.value);
  return values;
}

bool SingleDouble(const EntityInfo &entity, int code, double fallback,
                  double &value) {
  const auto values = Values(entity, code);
  if (values.empty()) {
    value = fallback;
    return true;
  }
  return values.size() == 1 && ParseDouble(values.front(), value);
}

bool SingleInt(const EntityInfo &entity, int code, int fallback, int &value) {
  const auto values = Values(entity, code);
  if (values.empty()) {
    value = fallback;
    return true;
  }
  return values.size() == 1 && ParseInt(values.front(), value);
}

bool AllowedCodes(const EntityInfo &entity, std::initializer_list<int> codes) {
  return std::all_of(
      entity.fields.begin(), entity.fields.end(), [&](const Pair &field) {
        return std::find(codes.begin(), codes.end(), field.code) != codes.end();
      });
}

bool UnitScale(int code, double &scale) {
  using pikdraw::geometry::units::LengthUnit;
  LengthUnit unit{};
  switch (code) {
  case 1:
    unit = LengthUnit::Inch;
    break;
  case 2:
    unit = LengthUnit::Foot;
    break;
  case 4:
    unit = LengthUnit::Millimeter;
    break;
  case 5:
    unit = LengthUnit::Centimeter;
    break;
  case 6:
    unit = LengthUnit::Meter;
    break;
  case 10:
    unit = LengthUnit::Yard;
    break;
  default:
    return false;
  }
  const auto factor = pikdraw::geometry::units::millimeters_per_unit(unit);
  if (!factor) return false;
  scale = *factor;
  return true;
}

bool ParseUnits(std::span<const Pair> section, int &units) {
  bool seen = false;
  for (size_t i = 0; i < section.size(); ++i) {
    if (section[i].code != 9 || Trim(section[i].value) != "$INSUNITS")
      continue;
    if (seen || i + 1 >= section.size() || section[i + 1].code != 70 ||
        !ParseInt(section[i + 1].value, units))
      return false;
    seen = true;
  }
  return seen;
}

bool ParseLineweight(const EntityInfo &entity, int code, double fallback,
                     double &mm) {
  int raw = 0;
  if (!SingleInt(entity, code, -3, raw))
    return false;
  if (raw == -1 || raw == -3) {
    mm = fallback;
    return true;
  }
  if (raw < 0 || raw > 211)
    return false;
  mm = static_cast<double>(raw) / 100.0;
  return std::isfinite(mm) && mm >= 0;
}

bool ParseLayer(const EntityInfo &entity, LayerInfo &layer) {
  if (!AllowedCodes(entity, {5, 100, 2, 70, 62, 6, 290, 370, 330}))
    return false;
  const auto names = Values(entity, 2);
  int flags = 0, color = 7, plot = 1;
  if (names.size() != 1 || names[0].empty() || names[0].size() > 255 ||
      !ValidUtf8(names[0]) || !SingleInt(entity, 70, 0, flags) ||
      !SingleInt(entity, 62, 7, color) || !SingleInt(entity, 290, 1, plot) ||
      plot != 1 || (flags & ~69) != 0)
    return false;
  const auto linetypes = Values(entity, 6);
  if (linetypes.size() > 1 ||
      (!linetypes.empty() && Trim(linetypes.front()) != "CONTINUOUS"))
    return false;
  layer.name.assign(names.front());
  layer.color_index = std::abs(color);
  if (layer.color_index < 1 || layer.color_index > 9)
    return false;
  layer.visible = (flags & 1) == 0 && color > 0;
  layer.locked = (flags & 4) != 0;
  return ParseLineweight(entity, 370, kDefaultLineweightMm,
                         layer.lineweight_mm);
}

bool AddLayerTable(std::span<const Pair> section,
                   std::vector<LayerInfo> &layers) {
  for (size_t i = 0; i < section.size();) {
    if (section[i].code != 0 || Trim(section[i].value) != "TABLE") {
      ++i;
      continue;
    }
    if (i + 1 >= section.size() || section[i + 1].code != 2)
      return false;
    const bool layer_table = Trim(section[i + 1].value) == "LAYER";
    i += 2;
    while (i < section.size() && section[i].code != 0)
      ++i;
    while (i < section.size() &&
           !(section[i].code == 0 && Trim(section[i].value) == "ENDTAB")) {
      if (section[i].code != 0)
        return false;
      const size_t begin = ++i;
      while (i < section.size() && section[i].code != 0)
        ++i;
      if (layer_table) {
        LayerInfo layer;
        const EntityInfo entity{"LAYER", section.subspan(begin, i - begin)};
        if (!ParseLayer(entity, layer))
          return false;
        if (std::any_of(
                layers.begin(), layers.end(),
                [&](const LayerInfo &item) { return item.name == layer.name; }))
          return false;
        layers.push_back(std::move(layer));
        if (layers.size() > DxfImporter::kMaxObjects)
          return false;
      }
    }
    if (i >= section.size())
      return false;
    ++i;
  }
  return true;
}

bool ParseHeaderSection(std::span<const Pair> section,
                        std::optional<int> &units) {
  int value = 0;
  if (ParseUnits(section, value)) {
    if (units)
      return false;
    units = value;
    return true;
  }
  bool has_units_marker = false;
  for (size_t i = 0; i + 1 < section.size(); ++i) {
    if (section[i].code == 9 && Trim(section[i].value) == "$INSUNITS")
      has_units_marker = true;
  }
  return !has_units_marker;
}

bool ParseEntitySection(std::span<const Pair> section,
                        std::vector<EntityInfo> &entities) {
  for (size_t i = 0; i < section.size();) {
    if (section[i].code != 0)
      return false;
    const auto type = Trim(section[i].value);
    const size_t field_begin = ++i;
    while (i < section.size() && section[i].code != 0)
      ++i;
    if (type == "ENDSEC")
      return false;
    entities.push_back(
        EntityInfo{type, section.subspan(field_begin, i - field_begin)});
    if (entities.size() > DxfImporter::kMaxObjects)
      return false;
  }
  return true;
}

bool ParseSections(std::span<const Pair> pairs, std::optional<int> &units,
                   std::vector<LayerInfo> &layers,
                   std::vector<EntityInfo> &entities) {
  bool has_entities = false, has_eof = false;
  for (size_t i = 0; i < pairs.size();) {
    if (pairs[i].code != 0)
      return false;
    if (Trim(pairs[i].value) == "EOF") {
      has_eof = i + 1 == pairs.size();
      break;
    }
    if (Trim(pairs[i].value) != "SECTION" || i + 1 >= pairs.size() ||
        pairs[i + 1].code != 2)
      return false;
    const auto name = Trim(pairs[i + 1].value);
    const size_t begin = i + 2;
    size_t end = begin;
    while (end < pairs.size() &&
           !(pairs[end].code == 0 && Trim(pairs[end].value) == "ENDSEC"))
      ++end;
    if (end == pairs.size())
      return false;
    const auto section = pairs.subspan(begin, end - begin);
    if (name == "HEADER") {
      if (!ParseHeaderSection(section, units))
        return false;
    } else if (name == "TABLES") {
      if (!AddLayerTable(section, layers))
        return false;
    } else if (name == "ENTITIES") {
      if (has_entities || !ParseEntitySection(section, entities))
        return false;
      has_entities = true;
    } else {
      return false;
    }
    i = end + 1;
  }
  return has_eof && has_entities && units.has_value() && !layers.empty() &&
         !entities.empty();
}

} // namespace pikdraw::interop::dxf_detail
