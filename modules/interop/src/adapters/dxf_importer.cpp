#include "pikdraw/interop/adapters/dxf_importer.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace pikdraw::interop {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDefaultLineweightMm = 0.25;

struct Pair {
  int code;
  std::string_view value;
};

struct LayerInfo {
  std::string name;
  int color_index{7};
  double lineweight_mm{kDefaultLineweightMm};
  bool visible{true};
  bool locked{false};
};

struct Vertex {
  double x{0};
  double y{0};
  double bulge{0};
  bool has_y{false};
};

struct EntityInfo {
  std::string_view type;
  std::span<const Pair> fields;
};

DxfImportResult Failure(std::string message, std::string feature) {
  DxfImportResult result;
  result.error_message = std::move(message);
  result.report.add_issue(FidelitySeverity::Unsupported, {}, std::move(feature),
                          result.error_message);
  return result;
}

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
  switch (code) {
  case 1:
    scale = 25.4;
    return true;
  case 2:
    scale = 304.8;
    return true;
  case 4:
    scale = 1.0;
    return true;
  case 5:
    scale = 10.0;
    return true;
  case 6:
    scale = 1000.0;
    return true;
  case 10:
    scale = 914.4;
    return true;
  default:
    return false;
  }
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
      int value = 0;
      if (ParseUnits(section, value)) {
        if (units)
          return false;
        units = value;
      } else {
        bool has_units_marker = false;
        for (size_t p = 0; p + 1 < section.size(); ++p) {
          if (section[p].code == 9 && Trim(section[p].value) == "$INSUNITS")
            has_units_marker = true;
        }
        if (has_units_marker)
          return false;
      }
    } else if (name == "TABLES") {
      if (!AddLayerTable(section, layers))
        return false;
    } else if (name == "ENTITIES") {
      if (has_entities)
        return false;
      has_entities = true;
      for (size_t p = 0; p < section.size();) {
        if (section[p].code != 0)
          return false;
        const auto type = Trim(section[p].value);
        const size_t field_begin = ++p;
        while (p < section.size() && section[p].code != 0)
          ++p;
        if (type == "ENDSEC")
          return false;
        entities.push_back(
            EntityInfo{type, section.subspan(field_begin, p - field_begin)});
        if (entities.size() > DxfImporter::kMaxObjects)
          return false;
      }
    } else {
      return false;
    }
    i = end + 1;
  }
  return has_eof && has_entities && units.has_value() && !layers.empty() &&
         !entities.empty();
}

bool PointValue(const EntityInfo &entity, int code_x, int code_y, int code_z,
                double scale, geometry::Point2D &point) {
  double x = 0, y = 0, z = 0;
  if (!SingleDouble(entity, code_x, 0, x) ||
      !SingleDouble(entity, code_y, 0, y) ||
      !SingleDouble(entity, code_z, 0, z) || std::abs(z) > 1.0e-9)
    return false;
  point = {x * scale, y * scale};
  return std::isfinite(point.x) && std::isfinite(point.y) &&
         std::abs(point.x) <= DxfImporter::kMaxCoordinateMillimeters &&
         std::abs(point.y) <= DxfImporter::kMaxCoordinateMillimeters;
}

bool DefaultNormal(const EntityInfo &entity) {
  double x = 0, y = 0, z = 1;
  return SingleDouble(entity, 210, 0, x) && SingleDouble(entity, 220, 0, y) &&
         SingleDouble(entity, 230, 1, z) && std::abs(x) < 1.0e-12 &&
         std::abs(y) < 1.0e-12 && std::abs(z - 1.0) < 1.0e-12;
}

bool AddArc(geometry::Path2D &path, const geometry::Point2D &center,
            double radius, double start_degrees, double sweep_degrees) {
  if (!std::isfinite(radius) || radius <= 0 ||
      radius > DxfImporter::kMaxCoordinateMillimeters ||
      !std::isfinite(start_degrees) || !std::isfinite(sweep_degrees) ||
      std::abs(sweep_degrees) <= 1.0e-9 ||
      std::abs(sweep_degrees) >= 360.0 - 1.0e-9)
    return false;
  const double start = start_degrees * kPi / 180.0;
  const double end = (start_degrees + sweep_degrees) * kPi / 180.0;
  const geometry::Point2D first{center.x + radius * std::cos(start),
                                center.y + radius * std::sin(start)};
  const geometry::Point2D last{center.x + radius * std::cos(end),
                               center.y + radius * std::sin(end)};
  path.move_to(first);
  path.arc_to(radius, radius, 0, std::abs(sweep_degrees) > 180.0,
              sweep_degrees > 0, last.x, last.y);
  return true;
}

bool ReadPolyline(const EntityInfo &entity, double scale,
                  geometry::Path2D &path) {
  if (!AllowedCodes(entity, {5,  8,  6,  62, 60, 100, 330, 370, 39,  67, 90, 70,
                             38, 43, 40, 41, 10, 20,  42,  210, 220, 230}) ||
      !DefaultNormal(entity))
    return false;
  int flags = 0, count = 0;
  double elevation = 0, width = 0, thickness = 0;
  int paper_space = 0;
  if (!SingleInt(entity, 70, 0, flags) || (flags & ~1) != 0 ||
      !SingleInt(entity, 90, -1, count) || count < 2 ||
      count > static_cast<int>(DxfImporter::kMaxPathElements) ||
      !SingleDouble(entity, 38, 0, elevation) || std::abs(elevation) > 1.0e-9 ||
      !SingleDouble(entity, 43, 0, width) || std::abs(width) > 1.0e-9 ||
      !SingleDouble(entity, 39, 0, thickness) || std::abs(thickness) > 1.0e-9 ||
      !SingleInt(entity, 67, 0, paper_space) || paper_space != 0)
    return false;
  for (int code : {40, 41}) {
    for (const auto value : Values(entity, code)) {
      double parsed = 0;
      if (!ParseDouble(value, parsed) || std::abs(parsed) > 1.0e-9)
        return false;
    }
  }
  std::vector<Vertex> vertices;
  for (const auto &field : entity.fields) {
    if (field.code == 10) {
      if (!vertices.empty() && !vertices.back().has_y)
        return false;
      double x = 0;
      if (!ParseDouble(field.value, x))
        return false;
      vertices.push_back(Vertex{x * scale, 0, 0, false});
    } else if (field.code == 20) {
      if (vertices.empty() || vertices.back().has_y ||
          !ParseDouble(field.value, vertices.back().y))
        return false;
      vertices.back().y *= scale;
      vertices.back().has_y = true;
    } else if (field.code == 42) {
      if (vertices.empty() || !ParseDouble(field.value, vertices.back().bulge))
        return false;
    }
  }
  if (vertices.size() != static_cast<size_t>(count) ||
      std::any_of(vertices.begin(), vertices.end(),
                  [](const Vertex &vertex) { return !vertex.has_y; }))
    return false;
  const bool closed = (flags & 1) != 0;
  const size_t segment_count = closed ? vertices.size() : vertices.size() - 1;
  if (!closed && std::abs(vertices.back().bulge) > 1.0e-12)
    return false;
  path.move_to(vertices.front().x, vertices.front().y);
  for (size_t i = 0; i < segment_count; ++i) {
    const auto &current = vertices[i];
    const auto &next = vertices[(i + 1) % vertices.size()];
    const double dx = next.x - current.x, dy = next.y - current.y;
    const double chord = std::hypot(dx, dy);
    if (chord <= 1.0e-12)
      return false;
    if (std::abs(current.bulge) <= 1.0e-12) {
      path.line_to(next.x, next.y);
    } else {
      const double theta = 4.0 * std::atan(current.bulge);
      const double radius = chord * (1.0 + current.bulge * current.bulge) /
                            (4.0 * std::abs(current.bulge));
      if (!std::isfinite(radius) ||
          radius > DxfImporter::kMaxCoordinateMillimeters)
        return false;
      path.arc_to(radius, radius, 0, std::abs(theta) > kPi, current.bulge > 0,
                  next.x, next.y);
    }
  }
  if (closed)
    path.close();
  return path.size() <= DxfImporter::kMaxPathElements;
}

bool ParseEntity(const EntityInfo &entity, double scale,
                 geometry::Path2D &path) {
  if (entity.type == "LINE") {
    if (!AllowedCodes(entity, {5, 8, 6, 62, 60, 100, 330, 370, 39, 67, 10, 20,
                               30, 11, 21, 31}))
      return false;
    geometry::Point2D start, end;
    double thickness = 0;
    int paper_space = 0;
    if (!PointValue(entity, 10, 20, 30, scale, start) ||
        !PointValue(entity, 11, 21, 31, scale, end) ||
        !SingleDouble(entity, 39, 0, thickness) ||
        std::abs(thickness) > 1.0e-9 ||
        !SingleInt(entity, 67, 0, paper_space) || paper_space != 0 ||
        (std::abs(start.x - end.x) <= 1.0e-12 &&
         std::abs(start.y - end.y) <= 1.0e-12))
      return false;
    path.move_to(start);
    path.line_to(end);
    return true;
  }
  if (entity.type == "ARC") {
    if (!AllowedCodes(entity, {5, 8, 6, 62, 60, 100, 330, 370, 39, 67, 10, 20,
                               30, 40, 50, 51, 210, 220, 230}) ||
        !DefaultNormal(entity))
      return false;
    geometry::Point2D center;
    double radius = 0, start = 0, end = 0, thickness = 0;
    int paper_space = 0;
    if (!PointValue(entity, 10, 20, 30, scale, center) ||
        !SingleDouble(entity, 40, 0, radius) ||
        !SingleDouble(entity, 50, 0, start) ||
        !SingleDouble(entity, 51, 0, end) ||
        !SingleDouble(entity, 39, 0, thickness) ||
        std::abs(thickness) > 1.0e-9 ||
        !SingleInt(entity, 67, 0, paper_space) || paper_space != 0)
      return false;
    double sweep = std::fmod(end - start, 360.0);
    if (sweep <= 0)
      sweep += 360.0;
    return AddArc(path, center, radius * scale, start, sweep);
  }
  if (entity.type == "CIRCLE") {
    if (!AllowedCodes(entity, {5, 8, 6, 62, 60, 100, 330, 370, 39, 67, 10, 20,
                               30, 40, 210, 220, 230}) ||
        !DefaultNormal(entity))
      return false;
    geometry::Point2D center;
    double radius = 0, thickness = 0;
    int paper_space = 0;
    if (!PointValue(entity, 10, 20, 30, scale, center) ||
        !SingleDouble(entity, 40, 0, radius) ||
        !SingleDouble(entity, 39, 0, thickness) ||
        std::abs(thickness) > 1.0e-9 ||
        !SingleInt(entity, 67, 0, paper_space) || paper_space != 0)
      return false;
    radius *= scale;
    if (!std::isfinite(radius) || radius <= 0 ||
        radius > DxfImporter::kMaxCoordinateMillimeters)
      return false;
    path.move_to(center.x + radius, center.y);
    path.arc_to(radius, radius, 0, false, true, center.x - radius, center.y);
    path.arc_to(radius, radius, 0, false, true, center.x + radius, center.y);
    path.close();
    return true;
  }
  if (entity.type == "LWPOLYLINE")
    return ReadPolyline(entity, scale, path);
  return false;
}

bool AciColor(int index, color::ColorValue &color, bool &approximate) {
  approximate = false;
  switch (index) {
  case 1:
    color = color::RGBColor{1, 0, 0, 1};
    return true;
  case 2:
    color = color::RGBColor{1, 1, 0, 1};
    return true;
  case 3:
    color = color::RGBColor{0, 1, 0, 1};
    return true;
  case 4:
    color = color::RGBColor{0, 1, 1, 1};
    return true;
  case 5:
    color = color::RGBColor{0, 0, 1, 1};
    return true;
  case 6:
    color = color::RGBColor{1, 0, 1, 1};
    return true;
  case 7:
    color = color::RGBColor{1, 1, 1, 1};
    approximate = true;
    return true;
  case 8:
    color = color::RGBColor{0.5, 0.5, 0.5, 1};
    return true;
  case 9:
    color = color::RGBColor{0.75, 0.75, 0.75, 1};
    return true;
  default:
    return false;
  }
}

bool EntityStyle(const EntityInfo &entity, const LayerInfo &layer, double scale,
                 document::PathObject &object, int &aci) {
  int color_index = 256, hidden = 0, paper_space = 0;
  double width = 0;
  if (!SingleInt(entity, 62, 256, color_index) ||
      !SingleInt(entity, 60, 0, hidden) || hidden < 0 || hidden > 1 ||
      !SingleInt(entity, 67, 0, paper_space) || paper_space != 0 ||
      !ParseLineweight(entity, 370, layer.lineweight_mm, width))
    return false;
  const auto linetypes = Values(entity, 6);
  if (linetypes.size() > 1 ||
      (!linetypes.empty() && Trim(linetypes.front()) != "BYLAYER" &&
       Trim(linetypes.front()) != "CONTINUOUS"))
    return false;
  if (color_index == 0)
    return false;
  if (color_index == 256)
    aci = layer.color_index;
  else {
    aci = std::abs(color_index);
    object.visible = color_index > 0;
  }
  bool approximate = false;
  if (!AciColor(aci, object.stroke, approximate))
    return false;
  object.name = "Imported DXF Entity";
  object.stroke_width_mm = width;
  object.has_fill = false;
  object.has_stroke = true;
  object.visible = object.visible && hidden == 0 && layer.visible;
  object.locked = layer.locked;
  (void)approximate;
  return true;
}

bool EntityName(const EntityInfo &entity, size_t ordinal,
                std::string &source_id) {
  const auto handles = Values(entity, 5);
  if (handles.size() > 1)
    return false;
  if (handles.empty()) {
    source_id = "dxf-object-" + std::to_string(ordinal);
    return true;
  }
  if (handles[0].empty() || handles[0].size() > 32 || !ValidUtf8(handles[0]))
    return false;
  source_id = "dxf-object-";
  source_id.append(handles[0]);
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
  for (size_t i = 0; i < layers.size(); ++i) {
    const auto &layer = layers[i];
    if (!source_ids.insert("dxf-layer-" + std::to_string(i + 1)).second) {
      return Failure("DXF layer source identifiers are duplicated",
                     "invalid_source_id");
    }
    layer_indices.emplace(layer.name, i);
    result.layer_source_ids.push_back("dxf-layer-" + std::to_string(i + 1));
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

  double min_x = std::numeric_limits<double>::infinity();
  double min_y = std::numeric_limits<double>::infinity();
  double max_x = -std::numeric_limits<double>::infinity();
  double max_y = -std::numeric_limits<double>::infinity();
  std::unordered_set<std::string> entity_handles;
  for (size_t i = 0; i < entities.size(); ++i) {
    const auto &entity = entities[i];
    const auto names = Values(entity, 8);
    const std::string_view layer_name =
        names.empty() ? std::string_view("0") : Trim(names.front());
    if (names.size() > 1 || layer_name.empty())
      return Failure("DXF entity layer reference is malformed",
                     "invalid_layer_reference");
    const auto layer_it = layer_indices.find(std::string(layer_name));
    if (layer_it == layer_indices.end())
      return Failure("DXF entity references an unknown layer",
                     "unknown_layer_reference");
    const auto &layer = layers[layer_it->second];
    geometry::Path2D path;
    if (!ParseEntity(entity, scale, path) || path.empty() ||
        path.size() > DxfImporter::kMaxPathElements) {
      return Failure("DXF entity type or geometry is outside dxf-entities-v1",
                     "unsupported_entity_or_geometry");
    }
    document::PathObject object;
    object.id = document::generate_object_id();
    int aci = 0;
    if (!EntityStyle(entity, layer, scale, object, aci)) {
      return Failure("DXF entity style is unsupported or malformed",
                     "unsupported_entity_style");
    }
    std::string source_id;
    if (!EntityName(entity, i + 1, source_id) ||
        !entity_handles.insert(source_id).second ||
        source_ids.contains(source_id))
      return Failure("DXF entity handles are invalid or duplicated",
                     "invalid_source_id");
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
      min_x = std::min(min_x, bounds.x);
      min_y = std::min(min_y, bounds.y);
      max_x = std::max(max_x, bounds.x + bounds.width);
      max_y = std::max(max_y, bounds.y + bounds.height);
    }
    result.page.layers[layer_it->second].object_ids.push_back(object.id);
    result.objects.emplace_back(std::move(object));
    result.source_ids.push_back(std::move(source_id));
  }
  if (!std::isfinite(min_x) || !std::isfinite(min_y) || !std::isfinite(max_x) ||
      !std::isfinite(max_y)) {
    return Failure("DXF contains no measurable planar geometry",
                   "invalid_geometry");
  }
  const double width = max_x - min_x;
  const double height = max_y - min_y;
  if (width > DxfImporter::kMaxPageMillimeters ||
      height > DxfImporter::kMaxPageMillimeters) {
    return Failure("DXF drawing extents exceed the supported page range",
                   "resource_limit");
  }
  result.page.width_mm = std::max(width, 1.0);
  result.page.height_mm = std::max(height, 1.0);
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
