#include "pikdraw/document/commands/path_commands.hpp"

#include "path_command_support.hpp"

#include <algorithm>
#include <utility>

namespace pikdraw::document {
namespace {

bool valid_node_mode(PathNodeMode mode) noexcept {
    switch (mode) {
    case PathNodeMode::Corner:
    case PathNodeMode::Cusp:
    case PathNodeMode::Smooth:
    case PathNodeMode::Symmetric:
    case PathNodeMode::AutoSmooth:
        return true;
    }
    return false;
}

bool supported_extension_segment(PathSegmentKind kind) noexcept {
    return kind == PathSegmentKind::Line ||
        kind == PathSegmentKind::CubicBezier;
}

} // namespace

ExtendPathEndpointCommand::ExtendPathEndpointCommand(
    ObjectId object_id, SubpathId subpath_id, NodeId endpoint_id,
    std::vector<PathNode> extension_nodes,
    std::vector<PathSegmentKind> segment_kinds,
    std::shared_ptr<PathTopologyMapping> mapping_result) noexcept
    : PathTopologyCommand(object_id, std::move(mapping_result)),
      subpath_id_(subpath_id), endpoint_id_(endpoint_id),
      extension_nodes_(std::move(extension_nodes)),
      segment_kinds_(std::move(segment_kinds)) {}

bool ExtendPathEndpointCommand::apply_edit(
    EditablePath& path, PathTopologyMapping& mapping, std::string& error) {
    auto* subpath = detail::find_subpath(path, subpath_id_);
    if (!subpath) {
        error = "Subpath ID was not found";
        return false;
    }
    if (subpath->closed || subpath->nodes.size() < 2) {
        error = "Only an open subpath with at least two anchors can be extended";
        return false;
    }
    if (extension_nodes_.empty() ||
        segment_kinds_.size() != extension_nodes_.size()) {
        error = "Extension requires one line or cubic segment per new anchor";
        return false;
    }

    const auto endpoint = std::find_if(subpath->nodes.begin(), subpath->nodes.end(),
        [&](const PathNode& node) { return node.id == endpoint_id_; });
    if (endpoint == subpath->nodes.end()) {
        error = "Endpoint node ID was not found";
        return false;
    }
    const bool at_start = endpoint == subpath->nodes.begin();
    const bool at_end = endpoint + 1 == subpath->nodes.end();
    if (!at_start && !at_end) {
        error = "Only the first or last node of an open subpath can be extended";
        return false;
    }

    if (at_start) detail::reverse_open_subpath(*subpath);

    for (size_t index = 0; index < extension_nodes_.size(); ++index) {
        const auto& source_node = extension_nodes_[index];
        if (source_node.id.valid()) {
            error = "Extension node IDs must be unassigned; the command allocates them";
            return false;
        }
        if (!valid_node_mode(source_node.mode)) {
            error = "Extension node has an unsupported node mode";
            return false;
        }
        const auto segment_kind = segment_kinds_[index];
        if (!supported_extension_segment(segment_kind)) {
            error = "Pen endpoint extension supports line and cubic segments only";
            return false;
        }

        PathNode node = source_node;
        node.id = generate_path_element_id<NodeId>();
        EditableSegment segment;
        segment.id = generate_path_element_id<SegmentId>();
        segment.kind = segment_kind;
        if (!node.id.valid() || !segment.id.valid()) {
            error = "Path identity allocator is exhausted";
            return false;
        }
        if (node.anchor == subpath->nodes.back().anchor) {
            error = "A zero-length extension segment is not allowed";
            return false;
        }

        subpath->nodes.push_back(std::move(node));
        subpath->segments.push_back(segment);
        detail::add_mapping(mapping, PathElementKind::Node, std::nullopt,
                            subpath->nodes.back().id.value(),
                            IdMappingRelation::Created);
        detail::add_mapping(mapping, PathElementKind::Segment, std::nullopt,
                            segment.id.value(), IdMappingRelation::Created);
    }

    detail::add_mapping(mapping, PathElementKind::Subpath, subpath->id.value(),
                        subpath->id.value(), IdMappingRelation::Preserved);
    for (const auto& node : subpath->nodes) {
        const bool created = std::any_of(mapping.elements.begin(), mapping.elements.end(),
            [&](const PathElementIdMapping& entry) {
                return entry.element == PathElementKind::Node &&
                    entry.after == node.id.value() && !entry.before;
            });
        if (!created)
            detail::add_mapping(mapping, PathElementKind::Node, node.id.value(),
                                node.id.value(), IdMappingRelation::Preserved);
    }
    for (const auto& segment : subpath->segments) {
        const bool created = std::any_of(mapping.elements.begin(), mapping.elements.end(),
            [&](const PathElementIdMapping& entry) {
                return entry.element == PathElementKind::Segment &&
                    entry.after == segment.id.value() && !entry.before;
            });
        if (!created)
            detail::add_mapping(mapping, PathElementKind::Segment, segment.id.value(),
                                segment.id.value(), IdMappingRelation::Preserved);
    }
    return true;
}

} // namespace pikdraw::document
