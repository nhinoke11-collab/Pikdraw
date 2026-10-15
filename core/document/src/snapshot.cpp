#include "pikdraw/document/snapshot.hpp"

namespace pikdraw::document {

DocumentSnapshot::DocumentSnapshot(
    DocumentId doc_id,
    Epoch epoch,
    Revision revision,
    ChunkMap chunks,
    std::unordered_map<ObjectId, pikdraw::geometry::Rect2D> spatial_index
) : doc_id_(std::move(doc_id)),
    epoch_(epoch),
    revision_(revision),
    chunks_(std::move(chunks)),
    spatial_index_(std::move(spatial_index)) {}

std::shared_ptr<const DocumentObject> DocumentSnapshot::get_object(ObjectId id) const {
    const ChunkId cid = static_cast<ChunkId>(id / OBJECTS_PER_CHUNK);
    auto it = chunks_.find(cid);
    if (it == chunks_.end() || !it->second) {
        return nullptr;
    }
    for (const auto& obj : it->second->objects) {
        if (obj && obj->id == id) {
            return obj;
        }
    }
    return nullptr;
}

std::vector<ObjectId> DocumentSnapshot::query_rect(const pikdraw::geometry::Rect2D& query_bounds) const {
    std::vector<ObjectId> matches;
    for (const auto& [id, b] : spatial_index_) {
        if (b.intersects(query_bounds)) {
            matches.push_back(id);
        }
    }
    return matches;
}

} // namespace pikdraw::document
