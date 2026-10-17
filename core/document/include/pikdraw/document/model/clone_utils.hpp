#pragma once

#include "pikdraw/document/model/object.hpp"
#include "pikdraw/document/model/editable_path.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include <unordered_map>
#include <vector>

namespace pikdraw::document {

/// Deep-clones an EditablePath, allocating new SubpathId, NodeId, and SegmentId
/// for all subpaths, nodes, and segments while preserving curve geometry and handles.
[[nodiscard]] EditablePath clone_editable_path(const EditablePath& source);

/// Clones a single standalone object (PathObject, TextObject, ImageObject),
/// generating a new unique ObjectId and shifting its transform.
/// If the object is a PathObject with an authoring_source, its EditablePath
/// is also deep-cloned with fresh path element IDs.
[[nodiscard]] ObjectVariant clone_object(
    const ObjectVariant& source,
    const geometry::Transform2D& shift = geometry::Transform2D::identity());

/// Result structure for cloning an object hierarchy (including groups).
struct ClonedHierarchyResult {
    /// All cloned objects (including group objects and their children) to be added to the document.
    std::vector<ObjectVariant> cloned_objects;
    /// The new IDs of the top-level roots that were requested to be cloned.
    std::vector<ObjectId> top_level_cloned_ids;
    /// Mapping from source ObjectId -> newly generated ObjectId.
    std::unordered_map<ObjectId, ObjectId> id_map;
};

/// Clones a collection of top-level object IDs and their complete recursive child hierarchies,
/// re-mapping all group children IDs and allocating unique ObjectIds and path element IDs.
[[nodiscard]] ClonedHierarchyResult clone_hierarchy(
    const std::vector<ObjectId>& root_ids,
    const DocumentSnapshot& snapshot,
    const geometry::Transform2D& shift = geometry::Transform2D::identity());

} // namespace pikdraw::document
