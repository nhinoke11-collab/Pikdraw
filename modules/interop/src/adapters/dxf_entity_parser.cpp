#include "dxf_importer_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace pikdraw::interop::dxf_detail {

constexpr double kPi = 3.14159265358979323846;

struct Vertex {
  double x{0};
  double y{0};
  double bulge{0};
  bool has_y{false};
};
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

bool ReadPolylineVertices(const EntityInfo &entity, double scale, int count,
                         std::vector<Vertex> &vertices) {
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

  return true;
}

bool AppendPolylineSegments(const std::vector<Vertex> &vertices, bool closed,
                            geometry::Path2D &path) {
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
  if (!ReadPolylineVertices(entity, scale, count, vertices))
    return false;
  const bool closed = (flags & 1) != 0;
  return AppendPolylineSegments(vertices, closed, path);
}

bool ParseLineEntity(const EntityInfo &entity, double scale,
                     geometry::Path2D &path) {
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

bool ParseArcEntity(const EntityInfo &entity, double scale,
                    geometry::Path2D &path) {
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

bool ParseCircleEntity(const EntityInfo &entity, double scale,
                       geometry::Path2D &path) {
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

bool ParseEntity(const EntityInfo &entity, double scale,
                 geometry::Path2D &path) {
  if (entity.type == "LINE")
    return ParseLineEntity(entity, scale, path);
  if (entity.type == "ARC")
    return ParseArcEntity(entity, scale, path);
  if (entity.type == "CIRCLE")
    return ParseCircleEntity(entity, scale, path);
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

} // namespace pikdraw::interop::dxf_detail
