#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/commands/path_commands.hpp"
#include "pikdraw/document/document.hpp"
#include "path_command_lifecycle.hpp"
#include <algorithm>
#include <cmath>

namespace pikdraw::document {
using detail::add_mapping;
using detail::find_subpath;
using detail::identities_unique_for_add;
using detail::reverse_open_subpath;
using detail::segment_curve;
using detail::set_manual_handle;

MovePathNodeCommand::MovePathNodeCommand(ObjectId object_id, SubpathId subpath_id,
                                         NodeId node_id, geometry::Point2D new_anchor,
                                         std::shared_ptr<PathTopologyMapping> mapping_result) noexcept
    : PathTopologyCommand(object_id, std::move(mapping_result)), subpath_id_(subpath_id), node_id_(node_id),
      new_anchor_(new_anchor) {}

bool MovePathNodeCommand::apply_edit(EditablePath& path, PathTopologyMapping&,
                                    std::string& error) {
    if (!new_anchor_.is_finite()) {
        error = "Node anchor must be finite";
        return false;
    }
    auto* subpath = find_subpath(path, subpath_id_);
    if (!subpath) {
        error = "Subpath ID was not found";
        return false;
    }
    const auto found = std::find_if(subpath->nodes.begin(), subpath->nodes.end(),
        [&](const PathNode& node) { return node.id == node_id_; });
    if (found == subpath->nodes.end()) {
        error = "Node ID was not found";
        return false;
    }
    const auto delta = new_anchor_ - found->anchor;
    found->anchor = new_anchor_;
    if (found->has_incoming_handle) found->incoming_handle = found->incoming_handle + delta;
    if (found->has_outgoing_handle) found->outgoing_handle = found->outgoing_handle + delta;
    return true;
}

SplitPathSegmentCommand::SplitPathSegmentCommand(ObjectId object_id, SubpathId subpath_id,
                                                 SegmentId segment_id, double parameter,
                                                 std::shared_ptr<PathTopologyMapping> mapping_result) noexcept
    : PathTopologyCommand(object_id, std::move(mapping_result)), subpath_id_(subpath_id), segment_id_(segment_id),
      parameter_(parameter) {}

bool SplitPathSegmentCommand::apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                                         std::string& error) {
    if (!std::isfinite(parameter_) || parameter_ <= 0.0 || parameter_ >= 1.0) {
        error = "Segment split parameter must be strictly between zero and one";
        return false;
    }
    auto* subpath = find_subpath(path, subpath_id_);
    if (!subpath) {
        error = "Subpath ID was not found";
        return false;
    }
    const auto segment_it = std::find_if(subpath->segments.begin(), subpath->segments.end(),
        [&](const EditableSegment& segment) { return segment.id == segment_id_; });
    if (segment_it == subpath->segments.end()) {
        error = "Segment ID was not found";
        return false;
    }
    const size_t index = static_cast<size_t>(segment_it - subpath->segments.begin());
    geometry::CurveSplitResult split;
    try { split = geometry::split_curve(segment_curve(*subpath, index), parameter_); }
    catch (const std::exception& exception) { error = exception.what(); return false; }
    if (split.status != geometry::GeometryOperationStatus::Success || !split.first || !split.second) {
        error = "Geometry kernel could not split this source segment";
        return false;
    }
    const auto new_segment_id = generate_path_element_id<SegmentId>();
    const auto new_node_id = generate_path_element_id<NodeId>();
    if (!new_segment_id.valid() || !new_node_id.valid()) {
        error = "Path identity allocator exhausted";
        return false;
    }
    const auto middle = geometry::curve_point_at(segment_curve(*subpath, index), parameter_);
    EditableSegment first{segment_id_, PathSegmentKind::Line};
    EditableSegment second{new_segment_id, PathSegmentKind::Line};
    PathNode split_node{new_node_id, middle};

    auto assign_curve = [&](const geometry::CurveSegment& curve, EditableSegment& target,
                            bool is_first) -> bool {
        if (std::holds_alternative<geometry::LineSegment>(curve)) {
            target.kind = PathSegmentKind::Line;
        } else if (const auto* quad = std::get_if<geometry::QuadraticBezier>(&curve)) {
            target.kind = PathSegmentKind::QuadraticBezier;
            target.quadratic_control = quad->p1;
        } else if (const auto* cubic = std::get_if<geometry::CubicBezier>(&curve)) {
            target.kind = PathSegmentKind::CubicBezier;
            if (is_first) {
                set_manual_handle(subpath->nodes[index], false, cubic->p1);
                split_node.mode = PathNodeMode::Cusp;
                split_node.has_incoming_handle = true;
                split_node.incoming_handle = cubic->p2;
            } else {
                split_node.mode = PathNodeMode::Cusp;
                split_node.has_outgoing_handle = true;
                split_node.outgoing_handle = cubic->p1;
                const size_t end_index = (index + 1) % subpath->nodes.size();
                set_manual_handle(subpath->nodes[end_index], true, cubic->p2);
            }
        } else if (const auto* arc = std::get_if<geometry::EllipticArc>(&curve)) {
            target.kind = PathSegmentKind::EllipticArc;
            target.arc = *arc;
        }
        return true;
    };

    if (!assign_curve(*split.first, first, true) || !assign_curve(*split.second, second, false))
        return false;
    subpath->segments[index] = first;
    subpath->segments.insert(subpath->segments.begin() + static_cast<std::ptrdiff_t>(index + 1), second);
    if (subpath->closed && index + 1 == subpath->nodes.size())
        subpath->nodes.push_back(split_node);
    else
        subpath->nodes.insert(subpath->nodes.begin() + static_cast<std::ptrdiff_t>(index + 1), split_node);
    add_mapping(mapping, PathElementKind::Segment, segment_id_.value(), segment_id_.value(),
                IdMappingRelation::Preserved);
    add_mapping(mapping, PathElementKind::Segment, std::nullopt, new_segment_id.value(),
                IdMappingRelation::Created);
    add_mapping(mapping, PathElementKind::Node, std::nullopt, new_node_id.value(),
                IdMappingRelation::Created);
    return true;
}

DeletePathNodeCommand::DeletePathNodeCommand(ObjectId object_id, SubpathId subpath_id,
                                             NodeId node_id,
                                             std::shared_ptr<PathTopologyMapping> mapping_result) noexcept
    : PathTopologyCommand(object_id, std::move(mapping_result)), subpath_id_(subpath_id), node_id_(node_id) {}

bool DeletePathNodeCommand::apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                                       std::string& error) {
    auto* subpath = find_subpath(path, subpath_id_);
    if (!subpath) {
        error = "Subpath ID was not found";
        return false;
    }
    const auto node_it = std::find_if(subpath->nodes.begin(), subpath->nodes.end(),
        [&](const PathNode& node) { return node.id == node_id_; });
    if (node_it == subpath->nodes.end()) {
        error = "Node ID was not found";
        return false;
    }
    const size_t index = static_cast<size_t>(node_it - subpath->nodes.begin());
    const size_t count = subpath->nodes.size();
    if (count <= 1 || (subpath->closed && count <= 2)) {
        error = "Cannot delete this node without leaving a valid path; delete the path instead";
        return false;
    }
    const auto removed_node = node_it->id;
    if (subpath->closed) {
        const size_t previous_segment = (index + count - 1) % count;
        const size_t removed_segment = index;
        const auto preserved_segment = subpath->segments[previous_segment].id;
        const auto removed_segment_id = subpath->segments[removed_segment].id;
        subpath->segments[previous_segment] = EditableSegment{preserved_segment, PathSegmentKind::Line};
        subpath->segments.erase(subpath->segments.begin() + static_cast<std::ptrdiff_t>(removed_segment));
        subpath->nodes.erase(subpath->nodes.begin() + static_cast<std::ptrdiff_t>(index));
        add_mapping(mapping, PathElementKind::Segment, preserved_segment.value(),
                    preserved_segment.value(), IdMappingRelation::Preserved);
        add_mapping(mapping, PathElementKind::Segment, removed_segment_id.value(), std::nullopt,
                    IdMappingRelation::Removed);
    } else if (index == 0) {
        const auto removed_segment = subpath->segments.front().id;
        subpath->nodes.erase(subpath->nodes.begin());
        subpath->segments.erase(subpath->segments.begin());
        add_mapping(mapping, PathElementKind::Segment, removed_segment.value(), std::nullopt,
                    IdMappingRelation::Removed);
    } else if (index + 1 == count) {
        const auto removed_segment = subpath->segments.back().id;
        subpath->nodes.pop_back();
        subpath->segments.pop_back();
        add_mapping(mapping, PathElementKind::Segment, removed_segment.value(), std::nullopt,
                    IdMappingRelation::Removed);
    } else {
        const size_t previous_segment = index - 1;
        const auto preserved_segment = subpath->segments[previous_segment].id;
        const auto removed_segment = subpath->segments[index].id;
        subpath->segments[previous_segment] = EditableSegment{preserved_segment, PathSegmentKind::Line};
        subpath->segments.erase(subpath->segments.begin() + static_cast<std::ptrdiff_t>(index));
        subpath->nodes.erase(subpath->nodes.begin() + static_cast<std::ptrdiff_t>(index));
        add_mapping(mapping, PathElementKind::Segment, preserved_segment.value(),
                    preserved_segment.value(), IdMappingRelation::Preserved);
        add_mapping(mapping, PathElementKind::Segment, removed_segment.value(), std::nullopt,
                    IdMappingRelation::Removed);
    }
    add_mapping(mapping, PathElementKind::Node, removed_node.value(), std::nullopt,
                IdMappingRelation::Removed);
    return true;
}

JoinPathSubpathsCommand::JoinPathSubpathsCommand(
    ObjectId object_id, SubpathId first_subpath, NodeId first_endpoint,
    SubpathId second_subpath, NodeId second_endpoint, bool create_bridge,
    NodeId coincident_survivor, std::shared_ptr<PathTopologyMapping> mapping_result) noexcept
    : PathTopologyCommand(object_id, std::move(mapping_result)), first_subpath_(first_subpath),
      first_endpoint_(first_endpoint), second_subpath_(second_subpath),
      second_endpoint_(second_endpoint), create_bridge_(create_bridge),
      coincident_survivor_(coincident_survivor) {}

bool JoinPathSubpathsCommand::apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                                         std::string& error) {
    if (first_subpath_ == second_subpath_) {
        error = "Joining a subpath to itself is unsupported";
        return false;
    }
    auto* first = find_subpath(path, first_subpath_);
    auto* second = find_subpath(path, second_subpath_);
    if (!first || !second) {
        error = "One of the subpath IDs was not found";
        return false;
    }
    if (first->closed || second->closed) {
        error = "Only open subpaths can be joined";
        return false;
    }
    auto locate = [](const EditableSubpath& subpath, NodeId id) {
        return std::find_if(subpath.nodes.begin(), subpath.nodes.end(),
            [&](const PathNode& node) { return node.id == id; });
    };
    auto first_it = locate(*first, first_endpoint_);
    auto second_it = locate(*second, second_endpoint_);
    if (first_it == first->nodes.end() || second_it == second->nodes.end()) {
        error = "Join endpoint ID was not found";
        return false;
    }
    const size_t first_index = static_cast<size_t>(first_it - first->nodes.begin());
    const size_t second_index = static_cast<size_t>(second_it - second->nodes.begin());
    if (first_index == 0) reverse_open_subpath(*first);
    else if (first_index + 1 != first->nodes.size()) {
        error = "The first join node must be an open-subpath endpoint";
        return false;
    }
    if (second_index + 1 == second->nodes.size()) reverse_open_subpath(*second);
    else if (second_index != 0) {
        error = "The second join node must be an open-subpath endpoint";
        return false;
    }
    auto& a = first->nodes.back();
    auto& b = second->nodes.front();
    const bool coincident = a.anchor == b.anchor;
    if (coincident && (!coincident_survivor_.valid() ||
        (coincident_survivor_ != a.id && coincident_survivor_ != b.id))) {
        error = "Coincident endpoint merge requires an explicit surviving NodeId";
        return false;
    }
    if (!coincident && !create_bridge_) {
        error = "Separated endpoints require an explicit bridge segment";
        return false;
    }
    if (coincident) {
        PathNode merged = a;
        merged.id = coincident_survivor_;
        merged.mode = PathNodeMode::Cusp;
        merged.has_outgoing_handle = b.has_outgoing_handle;
        merged.outgoing_handle = b.outgoing_handle;
        if (coincident_survivor_ == a.id) {
            merged.has_incoming_handle = a.has_incoming_handle;
            merged.incoming_handle = a.incoming_handle;
            add_mapping(mapping, PathElementKind::Node, b.id.value(), a.id.value(),
                        IdMappingRelation::Merged);
        } else {
            merged.has_incoming_handle = a.has_incoming_handle;
            merged.incoming_handle = a.incoming_handle;
            add_mapping(mapping, PathElementKind::Node, a.id.value(), b.id.value(),
                        IdMappingRelation::Merged);
        }
        a = merged;
        second->nodes.erase(second->nodes.begin());
    } else {
        const auto bridge_id = generate_path_element_id<SegmentId>();
        if (!bridge_id.valid()) {
            error = "Path identity allocator exhausted";
            return false;
        }
        first->segments.push_back(EditableSegment{bridge_id, PathSegmentKind::Line});
        add_mapping(mapping, PathElementKind::Segment, std::nullopt, bridge_id.value(),
                    IdMappingRelation::Created);
    }
    first->nodes.insert(first->nodes.end(), second->nodes.begin(), second->nodes.end());
    first->segments.insert(first->segments.end(), second->segments.begin(), second->segments.end());
    add_mapping(mapping, PathElementKind::Subpath, second->id.value(), first->id.value(),
                IdMappingRelation::Merged);
    path.subpaths.erase(std::remove_if(path.subpaths.begin(), path.subpaths.end(),
        [&](const EditableSubpath& candidate) { return candidate.id == second_subpath_; }),
        path.subpaths.end());
    return true;
}

ClosePathSubpathCommand::ClosePathSubpathCommand(ObjectId object_id, SubpathId subpath_id,
                                                 std::optional<NodeId> merge_to_start,
                                                 std::shared_ptr<PathTopologyMapping> mapping_result) noexcept
    : PathTopologyCommand(object_id, std::move(mapping_result)), subpath_id_(subpath_id),
      merge_to_start_(merge_to_start) {}

bool ClosePathSubpathCommand::apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                                         std::string& error) {
    auto* subpath = find_subpath(path, subpath_id_);
    if (!subpath) {
        error = "Subpath ID was not found";
        return false;
    }
    if (subpath->closed || subpath->nodes.size() < 2) {
        error = "Close requires an open subpath with at least two nodes";
        return false;
    }
    auto& first = subpath->nodes.front();
    auto& last = subpath->nodes.back();
    if (merge_to_start_) {
        if (*merge_to_start_ != first.id || last.anchor != first.anchor || subpath->nodes.size() < 3) {
            error = "Closing onto a start node requires an exact coincident endpoint and distinct path nodes";
            return false;
        }
        const auto removed = last.id;
        first.has_incoming_handle = last.has_incoming_handle;
        first.incoming_handle = last.incoming_handle;
        first.mode = PathNodeMode::Cusp;
        subpath->nodes.pop_back();
        add_mapping(mapping, PathElementKind::Node, removed.value(), first.id.value(),
                    IdMappingRelation::Merged);
    } else {
        if (last.anchor == first.anchor) {
            error = "Zero-length closure requires an explicit target start node";
            return false;
        }
        const auto closing_id = generate_path_element_id<SegmentId>();
        if (!closing_id.valid()) {
            error = "Path identity allocator exhausted";
            return false;
        }
        subpath->segments.push_back(EditableSegment{closing_id, PathSegmentKind::Line});
        add_mapping(mapping, PathElementKind::Segment, std::nullopt, closing_id.value(),
                    IdMappingRelation::Created);
    }
    subpath->closed = true;
    return true;
}

// --- AddObjectCommand ---

AddObjectCommand::AddObjectCommand(ObjectVariant object, size_t layer_idx)
    : object_(std::move(object)), layer_idx_(layer_idx) {}

ObjectId AddObjectCommand::object_id() const noexcept {
    return std::visit([](const auto& obj) { return obj.id; }, object_);
}

CommandResult AddObjectCommand::execute(Document& doc) {
    Page page = doc.page();
    if (layer_idx_ >= page.layers.size()) {
        return CommandResult::fail("Invalid layer index for AddObjectCommand");
    }
    if (page.layers[layer_idx_].locked)
        return CommandResult::fail("Cannot add to a locked layer");

    ObjectId id = object_id();
    if (id == kInvalidObjectId) {
        return CommandResult::fail("Cannot add object with invalid ID");
    }

    if (doc.objects().contains(id)) {
        return CommandResult::fail("Object ID already exists");
    }
    if (!identities_unique_for_add(doc, object_))
        return CommandResult::fail("Object and editable path element IDs must be globally unique in the document");
    if (!std::visit([](const auto& obj) { return obj.transform.is_finite(); }, object_))
        return CommandResult::fail("Non-finite object transform");

    PersistentObjectMap map = doc.objects().set(id, object_);
    page.layers[layer_idx_].object_ids.push_back(id);
    doc.apply_mutation(std::move(page), std::move(map));
    reserve_object_id(id);
    if (const auto* path = std::get_if<PathObject>(&object_))
        if (path->authoring_source) {
            for (const auto& subpath : path->authoring_source->subpaths) {
                reserve_path_element_id(subpath.id);
                for (const auto& node : subpath.nodes) reserve_path_element_id(node.id);
                for (const auto& segment : subpath.segments) reserve_path_element_id(segment.id);
            }
        }
    executed_ = true;
    return CommandResult::ok();
}

CommandResult AddObjectCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted AddObjectCommand");
    }
    Page page = doc.page();
    if (layer_idx_ >= page.layers.size()) {
        return CommandResult::fail("Invalid layer index on undo");
    }

    ObjectId id = object_id();
    auto& ids = page.layers[layer_idx_].object_ids;
    for (auto it = ids.begin(); it != ids.end(); ++it) {
        if (*it == id) {
            ids.erase(it);
            break;
        }
    }
    PersistentObjectMap map = doc.objects().remove(id);
    doc.apply_mutation(std::move(page), std::move(map));
    executed_ = false;
    return CommandResult::ok();
}

// --- RemoveObjectCommand ---

RemoveObjectCommand::RemoveObjectCommand(ObjectId id)
    : id_(id) {}

CommandResult RemoveObjectCommand::execute(Document& doc) {
    auto opt = doc.objects().get(id_);
    if (!opt) {
        return CommandResult::fail("Object to remove not found in document");
    }
    removed_object_ = opt;

    Page page = doc.page();
    bool found_in_layer = false;
    for (size_t i = 0; i < page.layers.size(); ++i) {
        auto& ids = page.layers[i].object_ids;
        for (auto it = ids.begin(); it != ids.end(); ++it) {
            if (*it == id_) {
                object_idx_ = static_cast<size_t>(it - ids.begin());
                ids.erase(it);
                layer_idx_ = i;
                found_in_layer = true;
                break;
            }
        }
        if (found_in_layer) break;
    }

    PersistentObjectMap map = doc.objects().remove(id_);
    doc.apply_mutation(std::move(page), std::move(map));
    return CommandResult::ok();
}

CommandResult RemoveObjectCommand::undo(Document& doc) {
    if (!removed_object_) {
        return CommandResult::fail("No saved object to restore on undo");
    }
    Page page = doc.page();
    if (layer_idx_ >= page.layers.size()) {
        return CommandResult::fail("Invalid layer index restoring removed object");
    }

    PersistentObjectMap map = doc.objects().set(id_, *removed_object_);
    auto& ids = page.layers[layer_idx_].object_ids;
    if (object_idx_ > ids.size()) return CommandResult::fail("Invalid saved z-order index");
    ids.insert(ids.begin() + static_cast<std::ptrdiff_t>(object_idx_), id_);
    doc.apply_mutation(std::move(page), std::move(map));
    return CommandResult::ok();
}

// --- TransformObjectCommand ---

TransformObjectCommand::TransformObjectCommand(ObjectId id, geometry::Transform2D delta)
    : id_(id), delta_(delta) {}

CommandResult TransformObjectCommand::execute(Document& doc) {
    if (!delta_.is_finite()) return CommandResult::fail("Non-finite transform delta");
    auto opt = doc.objects().get(id_);
    if (!opt) {
        return CommandResult::fail("Object to transform not found in document");
    }

    ObjectVariant updated = *opt;
    std::visit([&](auto& obj) {
        previous_transform_ = obj.transform;
        obj.transform = delta_ * obj.transform;
    }, updated);

    if (!std::visit([](const auto& obj) { return obj.transform.is_finite(); }, updated))
        return CommandResult::fail("Transform overflow");

    PersistentObjectMap map = doc.objects().set(id_, std::move(updated));
    doc.apply_mutation(doc.page(), std::move(map));
    return CommandResult::ok();
}

CommandResult TransformObjectCommand::undo(Document& doc) {
    auto opt = doc.objects().get(id_);
    if (!opt) {
        return CommandResult::fail("Object to un-transform not found in document");
    }

    ObjectVariant updated = *opt;
    std::visit([&](auto& obj) {
        obj.transform = previous_transform_;
    }, updated);

    PersistentObjectMap map = doc.objects().set(id_, std::move(updated));
    doc.apply_mutation(doc.page(), std::move(map));
    return CommandResult::ok();
}

// --- SetAuthoringSourceCommand ---

SetAuthoringSourceCommand::SetAuthoringSourceCommand(
    ObjectId object_id, EditablePath source, std::optional<std::string> new_contour_spec,
    std::optional<geometry::Transform2D> new_transform) noexcept
    : object_id_(object_id), source_(std::move(source)), new_contour_spec_(std::move(new_contour_spec)),
      new_transform_(new_transform) {}

CommandResult SetAuthoringSourceCommand::execute(Document& doc) {
    auto opt = doc.objects().get(object_id_);
    if (!opt || !std::holds_alternative<PathObject>(*opt))
        return CommandResult::fail("Editable path object was not found");
    auto path_obj = std::get<PathObject>(*opt);
    if (path_obj.locked || !path_obj.visible)
        return CommandResult::fail("Cannot edit a locked or hidden path");
    before_source_ = path_obj.authoring_source;
    before_path_ = path_obj.path;
    before_contour_spec_ = path_obj.contour_spec;
    before_transform_ = path_obj.transform;
    try {
        path_obj.set_authoring_source(source_);
        if (new_contour_spec_.has_value()) {
            path_obj.contour_spec = *new_contour_spec_;
        }
        if (new_transform_.has_value()) {
            path_obj.transform = *new_transform_;
        }
    } catch (const std::exception& e) {
        return CommandResult::fail(e.what());
    }
    auto map = doc.objects().set(object_id_, ObjectVariant{std::move(path_obj)});
    doc.apply_mutation(doc.page(), std::move(map));
    executed_ = true;
    return CommandResult::ok();
}

CommandResult SetAuthoringSourceCommand::undo(Document& doc) {
    if (!executed_)
        return CommandResult::fail("Cannot undo unexecuted command");
    auto opt = doc.objects().get(object_id_);
    if (!opt || !std::holds_alternative<PathObject>(*opt))
        return CommandResult::fail("Editable path object was not found while undoing");
    auto path_obj = std::get<PathObject>(*opt);
    path_obj.authoring_source = before_source_;
    path_obj.path = before_path_;
    if (new_contour_spec_.has_value()) {
        path_obj.contour_spec = before_contour_spec_;
    }
    if (new_transform_.has_value()) {
        path_obj.transform = before_transform_;
    }
    auto map = doc.objects().set(object_id_, ObjectVariant{std::move(path_obj)});
    doc.apply_mutation(doc.page(), std::move(map));
    executed_ = false;
    return CommandResult::ok();
}

} // namespace pikdraw::document
