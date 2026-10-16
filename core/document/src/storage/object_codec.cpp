#include "byte_codec.hpp"

namespace pikdraw::document::storage_detail {
namespace {
void write_path(Writer &writer, const geometry::Path2D &path) {
  writer.u8(static_cast<uint8_t>(path.winding_rule()));
  writer.count(path.size(), max_elements);
  for (const auto &element : path.elements()) {
    writer.u8(static_cast<uint8_t>(element.verb));
    writer.f64(element.pt.x);
    writer.f64(element.pt.y);
    writer.f64(element.ctrl1.x);
    writer.f64(element.ctrl1.y);
    writer.f64(element.ctrl2.x);
    writer.f64(element.ctrl2.y);
    writer.f64(element.rx);
    writer.f64(element.ry);
    writer.f64(element.rotation_deg);
    writer.flag(element.large_arc);
    writer.flag(element.sweep);
  }
}

geometry::Path2D read_path(Reader &reader, uint32_t &budget) {
  geometry::Path2D path;
  const auto winding = reader.u8();
  if (winding > 1)
    throw std::invalid_argument("Invalid path winding rule");
  path.set_winding_rule(static_cast<geometry::WindingRule>(winding));
  const auto count = reader.count(budget, 75);
  budget -= count;
  for (uint32_t i = 0; i < count; ++i) {
    const auto verb = reader.u8();
    const geometry::Point2D point{reader.f64(), reader.f64()};
    const geometry::Point2D control1{reader.f64(), reader.f64()};
    const geometry::Point2D control2{reader.f64(), reader.f64()};
    const double rx = reader.f64();
    const double ry = reader.f64();
    const double rotation = reader.f64();
    const bool large = reader.flag();
    const bool sweep = reader.flag();
    switch (static_cast<geometry::PathVerb>(verb)) {
    case geometry::PathVerb::MoveTo:
      path.move_to(point);
      break;
    case geometry::PathVerb::LineTo:
      path.line_to(point);
      break;
    case geometry::PathVerb::QuadTo:
      path.quad_to(control1.x, control1.y, point.x, point.y);
      break;
    case geometry::PathVerb::CubicTo:
      path.cubic_to(control1.x, control1.y, control2.x, control2.y, point.x,
                    point.y);
      break;
    case geometry::PathVerb::ArcTo:
      path.arc_to(rx, ry, rotation, large, sweep, point.x, point.y);
      break;
    case geometry::PathVerb::Close:
      path.close();
      break;
    default:
      throw std::invalid_argument("Unknown native path verb");
    }
  }
  return path;
}

template <class T> void read_common(Reader &reader, T &object) {
  object.id = reader.u64();
  object.name = reader.str();
  object.transform = reader.transform();
  object.visible = reader.flag();
  object.locked = reader.flag();
}
} // namespace

void write_object(Writer &writer, const ObjectVariant &value,
                  bool include_authoring_source,
                  const EditablePath *source_override) {
  writer.u8(static_cast<uint8_t>(value.index()));
  std::visit(
      [&](const auto &object) {
        writer.u64(object.id);
        writer.str(object.name);
        writer.transform(object.transform);
        writer.flag(object.visible);
        writer.flag(object.locked);
        using T = std::decay_t<decltype(object)>;
        if constexpr (std::is_same_v<T, PathObject>) {
          write_color(writer, object.fill);
          write_color(writer, object.stroke);
          writer.f64(object.stroke_width_mm);
          writer.flag(object.has_fill);
          writer.flag(object.has_stroke);
          write_path(writer, object.path);
          if (include_authoring_source) {
            const auto *source = source_override
                                     ? source_override
                                     : object.authoring_source.get();
            if (!source)
              throw std::invalid_argument(
                  "Path object " + std::to_string(object.id) +
                  " has no stable authoring source for native v3");
            write_editable_path(writer, *source);
          }
        } else if constexpr (std::is_same_v<T, TextObject>) {
          writer.str(object.text);
          writer.str(object.font.family_name);
          writer.str(object.font.postscript_name);
          writer.u32(object.font.weight);
          writer.flag(object.font.italic);
          writer.f64(object.font.size_pt);
          writer.f64(object.position.x);
          writer.f64(object.position.y);
          write_color(writer, object.fill);
        } else {
          writer.count(object.children_ids.size(), max_objects);
          for (auto id : object.children_ids)
            writer.u64(id);
        }
      },
      value);
}

ObjectVariant read_object(Reader &reader, uint32_t &remaining_elements,
                          bool include_authoring_source) {
  const auto type = reader.u8();
  if (type == 0) {
    PathObject object;
    read_common(reader, object);
    object.fill = read_color(reader);
    object.stroke = read_color(reader);
    object.stroke_width_mm = reader.f64();
    object.has_fill = reader.flag();
    object.has_stroke = reader.flag();
    object.path = read_path(reader, remaining_elements);
    if (include_authoring_source) {
      uint32_t authoring_budget = max_elements;
      auto source = read_editable_path(reader, authoring_budget);
      if (!editable_path_matches_adapter(source, object.path))
        throw std::invalid_argument(
            "Native v3 authoring source disagrees with its derived path adapter");
      object.set_authoring_source(std::move(source));
    }
    return object;
  }
  if (type == 1) {
    TextObject object;
    read_common(reader, object);
    object.text = reader.str();
    object.font.family_name = reader.str();
    object.font.postscript_name = reader.str();
    const auto weight = reader.u32();
    if (weight == 0 || weight > 999)
      throw std::invalid_argument("Invalid native font weight");
    object.font.weight = static_cast<uint16_t>(weight);
    object.font.italic = reader.flag();
    object.font.size_pt = reader.f64();
    object.position = {reader.f64(), reader.f64()};
    object.fill = read_color(reader);
    return object;
  }
  if (type == 2) {
    GroupObject object;
    read_common(reader, object);
    const auto count = reader.count(max_objects, 8);
    object.children_ids.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
      object.children_ids.push_back(reader.u64());
    return object;
  }
  throw std::invalid_argument("Unknown native object tag");
}

} // namespace pikdraw::document::storage_detail
