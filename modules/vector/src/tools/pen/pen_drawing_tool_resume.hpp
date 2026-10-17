#pragma once

#include "pikdraw/document/document.hpp"

#include <algorithm>
#include <optional>
#include <string>

namespace pikdraw::vector::tools::pen::detail {

struct PenResumeSeed {
    size_t layer_index{0};
    document::PathNode endpoint;
    bool started_at_first_endpoint{false};
    geometry::Transform2D source_from_world{geometry::Transform2D::identity()};
    geometry::Transform2D world_from_source{geometry::Transform2D::identity()};
};

inline std::optional<PenResumeSeed> resolve_pen_resume_seed(
    const document::SnapshotPtr& base, document::ObjectId object_id,
    document::SubpathId subpath_id, document::NodeId endpoint_id,
    std::string& error) {
    if (!base || object_id == document::kInvalidObjectId ||
        !subpath_id.valid() || !endpoint_id.valid())
        error = "Resume needs a current editable path endpoint";
    if (!error.empty()) return std::nullopt;

    const auto object_value = base->get_object(object_id);
    if (!object_value || !std::holds_alternative<document::PathObject>(*object_value))
        error = "Resume target is not a path object";
    if (!error.empty()) return std::nullopt;
    const auto& object = std::get<document::PathObject>(*object_value);
    if (!object.visible || object.locked || !object.authoring_source)
        error = "Resume target is hidden, locked, or has no editable source";
    if (!error.empty()) return std::nullopt;

    std::optional<size_t> layer_index;
    const auto& layers = base->page().layers;
    for (size_t index = 0; index < layers.size(); ++index) {
        const auto& layer = layers[index];
        if (std::find(layer.object_ids.begin(), layer.object_ids.end(), object_id) ==
            layer.object_ids.end())
            continue;
        if (!layer.visible || layer.locked) {
            error = "Resume target belongs to a hidden or locked layer";
            return std::nullopt;
        }
        layer_index = index;
        break;
    }
    if (!layer_index) {
        error = "Resume target is not attached to a document layer";
        return std::nullopt;
    }

    const auto source_subpath = std::find_if(
        object.authoring_source->subpaths.begin(), object.authoring_source->subpaths.end(),
        [&](const document::EditableSubpath& candidate) {
            return candidate.id == subpath_id;
        });
    if (source_subpath == object.authoring_source->subpaths.end() ||
        source_subpath->closed || source_subpath->nodes.size() < 2) {
        error = "Resume requires an existing open subpath with two anchors";
        return std::nullopt;
    }

    const auto endpoint = std::find_if(source_subpath->nodes.begin(),
        source_subpath->nodes.end(), [&](const document::PathNode& node) {
            return node.id == endpoint_id;
        });
    if (endpoint == source_subpath->nodes.end()) {
        error = "Resume endpoint ID was not found";
        return std::nullopt;
    }
    const bool at_start = endpoint == source_subpath->nodes.begin();
    const bool at_end = endpoint + 1 == source_subpath->nodes.end();
    if (!at_start && !at_end) {
        error = "Resume is available only at the first or last anchor";
        return std::nullopt;
    }

    const auto inverse = object.transform.inverted();
    if (!inverse || !inverse->is_finite()) {
        error = "Resume is unavailable because the path transform is singular";
        return std::nullopt;
    }

    return PenResumeSeed{*layer_index, *endpoint, at_start, *inverse, object.transform};
}

} // namespace pikdraw::vector::tools::pen::detail
