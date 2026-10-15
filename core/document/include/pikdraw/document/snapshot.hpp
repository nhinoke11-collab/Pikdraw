#pragma once

#include <memory>
#include "pikdraw/document/persistent_map.hpp"
#include <vector>
#include "pikdraw/document/types.hpp"
#include "pikdraw/document/object.hpp"
#include "pikdraw/geometry/rect.hpp"

namespace pikdraw::document {

constexpr size_t OBJECTS_PER_CHUNK = 64;
using ChunkId = uint32_t;

struct ObjectChunk {
    std::vector<std::shared_ptr<const DocumentObject>> objects;
};

using ChunkMap = PersistentMap<ChunkId, std::shared_ptr<const ObjectChunk>>;
using SpatialIndex = PersistentMap<ObjectId, pikdraw::geometry::Rect2D>;

// Truly immutable snapshot with chunk COW and matching revisioned spatial index
class DocumentSnapshot {
public:
    DocumentSnapshot(
        DocumentId doc_id,
        Epoch epoch,
        Revision revision,
        ChunkMap chunks,
        SpatialIndex spatial_index
    );

    [[nodiscard]] const DocumentId& doc_id() const { return doc_id_; }
    [[nodiscard]] Epoch epoch() const { return epoch_; }
    [[nodiscard]] Revision revision() const { return revision_; }
    [[nodiscard]] size_t object_count() const { return spatial_index_.size(); }

    [[nodiscard]] std::shared_ptr<const DocumentObject> get_object(ObjectId id) const;
    [[nodiscard]] std::vector<ObjectId> query_rect(const pikdraw::geometry::Rect2D& query_bounds) const;
    [[nodiscard]] const ChunkMap& chunks() const { return chunks_; }
    [[nodiscard]] const SpatialIndex& spatial_index() const {
        return spatial_index_;
    }

private:
    DocumentId doc_id_;
    Epoch epoch_{0};
    Revision revision_{0};
    ChunkMap chunks_;
    SpatialIndex spatial_index_;
};

} // namespace pikdraw::document
