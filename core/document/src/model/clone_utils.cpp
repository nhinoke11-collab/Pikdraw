#include "pikdraw/document/model/clone_utils.hpp"
#include <unordered_set>

namespace pikdraw::document {

EditablePath clone_editable_path(const EditablePath& source) {
    EditablePath result;
    result.winding_rule = source.winding_rule;
    result.subpaths.reserve(source.subpaths.size());

    for (const auto& subpath : source.subpaths) {
        EditableSubpath new_subpath;
        new_subpath.id = generate_path_element_id<SubpathId>();
        new_subpath.closed = subpath.closed;
        new_subpath.nodes.reserve(subpath.nodes.size());

        for (const auto& node : subpath.nodes) {
            PathNode new_node = node;
            new_node.id = generate_path_element_id<NodeId>();
            new_subpath.nodes.push_back(new_node);
        }

        new_subpath.segments.reserve(subpath.segments.size());
        for (const auto& segment : subpath.segments) {
            EditableSegment new_seg = segment;
            new_seg.id = generate_path_element_id<SegmentId>();
            new_subpath.segments.push_back(new_seg);
        }

        result.subpaths.push_back(std::move(new_subpath));
    }

    return result;
}

ObjectVariant clone_object(const ObjectVariant& source, const geometry::Transform2D& shift) {
    return std::visit([&shift](const auto& obj) -> ObjectVariant {
        using T = std::decay_t<decltype(obj)>;
        T clone = obj;
        clone.id = generate_object_id();
        clone.transform = shift * clone.transform;

        if constexpr (std::is_same_v<T, PathObject>) {
            if (clone.authoring_source) {
                auto new_authoring = clone_editable_path(*clone.authoring_source);
                clone.set_authoring_source(std::move(new_authoring));
            }
        }

        return clone;
    }, source);
}

namespace {

void clone_group_recursive(
    ObjectId group_id,
    const DocumentSnapshot& snapshot,
    const geometry::Transform2D& shift,
    ClonedHierarchyResult& result,
    std::unordered_set<ObjectId>& visited) {

    if (!visited.insert(group_id).second) return;

    const auto grp_opt = snapshot.objects().get(group_id);
    if (!grp_opt) return;

    const auto* grp = std::get_if<GroupObject>(&*grp_opt);
    if (!grp) return;

    GroupObject cloned_group = *grp;
    const ObjectId new_group_id = generate_object_id();
    cloned_group.id = new_group_id;
    cloned_group.transform = shift * cloned_group.transform;
    cloned_group.children_ids.clear();
    result.id_map[group_id] = new_group_id;

    for (const auto child_id : grp->children_ids) {
        const auto child_opt = snapshot.objects().get(child_id);
        if (!child_opt) continue;

        if (std::holds_alternative<GroupObject>(*child_opt)) {
            clone_group_recursive(child_id, snapshot, shift, result, visited);
            const auto it = result.id_map.find(child_id);
            if (it != result.id_map.end()) {
                cloned_group.children_ids.push_back(it->second);
            }
        } else {
            if (visited.insert(child_id).second) {
                ObjectVariant cloned_child = clone_object(*child_opt, shift);
                const ObjectId new_child_id = std::visit([](const auto& o) { return o.id; }, cloned_child);
                result.id_map[child_id] = new_child_id;
                cloned_group.children_ids.push_back(new_child_id);
                result.cloned_objects.push_back(std::move(cloned_child));
            }
        }
    }

    result.cloned_objects.push_back(std::move(cloned_group));
}

} // namespace

ClonedHierarchyResult clone_hierarchy(
    const std::vector<ObjectId>& root_ids,
    const DocumentSnapshot& snapshot,
    const geometry::Transform2D& shift) {

    ClonedHierarchyResult result;
    std::unordered_set<ObjectId> visited;

    for (const auto id : root_ids) {
        if (visited.contains(id)) continue;

        const auto obj_opt = snapshot.objects().get(id);
        if (!obj_opt) continue;

        if (std::holds_alternative<GroupObject>(*obj_opt)) {
            clone_group_recursive(id, snapshot, shift, result, visited);
            const auto it = result.id_map.find(id);
            if (it != result.id_map.end()) {
                result.top_level_cloned_ids.push_back(it->second);
            }
        } else {
            visited.insert(id);
            ObjectVariant cloned = clone_object(*obj_opt, shift);
            const ObjectId new_id = std::visit([](const auto& o) { return o.id; }, cloned);
            result.id_map[id] = new_id;
            result.top_level_cloned_ids.push_back(new_id);
            result.cloned_objects.push_back(std::move(cloned));
        }
    }

    return result;
}

} // namespace pikdraw::document
