#include "byte_codec.hpp"

namespace pikdraw::document::storage_detail {
namespace {
void write_point(Writer &writer, geometry::Point2D point) {
  writer.f64(point.x);
  writer.f64(point.y);
}
geometry::Point2D read_point(Reader &reader) {
  return {reader.f64(), reader.f64()};
}
} // namespace

void write_editable_path(Writer &writer, const EditablePath &source) {
  if (const auto error = source.validation_error())
    throw std::invalid_argument(*error);
  writer.u8(static_cast<uint8_t>(source.winding_rule));
  writer.count(source.subpaths.size(), max_objects);
  size_t elements = 0;
  for (const auto &subpath : source.subpaths) {
    writer.u64(subpath.id.value());
    writer.flag(subpath.closed);
    writer.count(subpath.nodes.size(), max_elements);
    elements += subpath.nodes.size();
    if (elements > max_elements)
      throw std::length_error("Too many authoring path elements");
    for (const auto &node : subpath.nodes) {
      writer.u64(node.id.value());
      write_point(writer, node.anchor);
      writer.u8(static_cast<uint8_t>(node.mode));
      writer.flag(node.has_incoming_handle);
      if (node.has_incoming_handle)
        write_point(writer, node.incoming_handle);
      writer.flag(node.has_outgoing_handle);
      if (node.has_outgoing_handle)
        write_point(writer, node.outgoing_handle);
      writer.f64(node.auto_smooth_tension);
    }
    writer.count(subpath.segments.size(), max_elements);
    elements += subpath.segments.size();
    if (elements > max_elements)
      throw std::length_error("Too many authoring path elements");
    for (const auto &segment : subpath.segments) {
      writer.u64(segment.id.value());
      writer.u8(static_cast<uint8_t>(segment.kind));
      if (segment.kind == PathSegmentKind::QuadraticBezier)
        write_point(writer, segment.quadratic_control);
      else if (segment.kind == PathSegmentKind::EllipticArc) {
        write_point(writer, segment.arc.center);
        write_point(writer, segment.arc.axis_u);
        write_point(writer, segment.arc.axis_v);
        writer.f64(segment.arc.start_angle_rad);
        writer.f64(segment.arc.sweep_angle_rad);
      }
    }
  }
}

EditablePath read_editable_path(Reader &reader, uint32_t &remaining_elements) {
  EditablePath source;
  const auto winding = reader.u8();
  if (winding > 1)
    throw std::invalid_argument("Unknown authoring winding-rule tag");
  source.winding_rule = static_cast<geometry::WindingRule>(winding);
  const auto subpath_count = reader.count(max_objects, 17);
  source.subpaths.reserve(subpath_count);
  for (uint32_t i = 0; i < subpath_count; ++i) {
    EditableSubpath subpath;
    subpath.id = SubpathId{reader.u64()};
    subpath.closed = reader.flag();
    const auto node_count = reader.count(remaining_elements, 28);
    remaining_elements -= node_count;
    subpath.nodes.reserve(node_count);
    for (uint32_t j = 0; j < node_count; ++j) {
      PathNode node;
      node.id = NodeId{reader.u64()};
      node.anchor = read_point(reader);
      const auto mode = reader.u8();
      if (mode > static_cast<uint8_t>(PathNodeMode::AutoSmooth))
        throw std::invalid_argument("Unknown authoring path-node mode");
      node.mode = static_cast<PathNodeMode>(mode);
      node.has_incoming_handle = reader.flag();
      if (node.has_incoming_handle)
        node.incoming_handle = read_point(reader);
      node.has_outgoing_handle = reader.flag();
      if (node.has_outgoing_handle)
        node.outgoing_handle = read_point(reader);
      node.auto_smooth_tension = reader.f64();
      subpath.nodes.push_back(node);
    }
    const auto segment_count = reader.count(remaining_elements, 9);
    remaining_elements -= segment_count;
    subpath.segments.reserve(segment_count);
    for (uint32_t j = 0; j < segment_count; ++j) {
      EditableSegment segment;
      segment.id = SegmentId{reader.u64()};
      const auto kind = reader.u8();
      if (kind > static_cast<uint8_t>(PathSegmentKind::EllipticArc))
        throw std::invalid_argument("Unknown authoring path-segment tag");
      segment.kind = static_cast<PathSegmentKind>(kind);
      if (segment.kind == PathSegmentKind::QuadraticBezier)
        segment.quadratic_control = read_point(reader);
      else if (segment.kind == PathSegmentKind::EllipticArc) {
        segment.arc.center = read_point(reader);
        segment.arc.axis_u = read_point(reader);
        segment.arc.axis_v = read_point(reader);
        segment.arc.start_angle_rad = reader.f64();
        segment.arc.sweep_angle_rad = reader.f64();
      }
      subpath.segments.push_back(segment);
    }
    source.subpaths.push_back(std::move(subpath));
  }
  if (const auto error = source.validation_error())
    throw std::invalid_argument("Invalid authoring path: " + *error);
  return source;
}
} // namespace pikdraw::document::storage_detail
