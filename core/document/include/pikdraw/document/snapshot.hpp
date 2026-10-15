#pragma once

#include <memory>
#include <unordered_map>
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

using ChunkMap = std::unordered_map<ChunkId, std::shared_ptr<const ObjectChunk>>;

// Truly immutable snapshot with chunk COW and matching revisioned spatial index
class DocumentSnapshot {
public:
    DocumentSnapshot(
        DocumentId doc_id,
        Epoch epoch,
        Revision revision,
        ChunkMap chunks,
        std::unordered_map<ObjectId, pikdraw::geometry::Rect2D> spatial_index
    );

    [[nodiscard]] const DocumentId& doc_id() const { return doc_id_; }
    [[nodiscard]] Epoch epoch() const { return epoch_; }
    [[nodiscard]] Revision revision() const { return revision_; }
    [[nodiscard]] size_t object_count() const { return spatial_index_.size(); }

    [[nodiscard]] std::shared_ptr<const DocumentObject> get_object(ObjectId id) const;
    [[nodiscard]] std::vector<ObjectId> query_rect(const pikdraw::geometry::Rect2D& query_bounds) const;
    [[nodiscard]] const ChunkMap& chunks() const { return chunks_; }
    [[nodiscard]] const std::unordered_map<ObjectId, pikdraw::geometry::Rect2D>& spatial_index() const {
        return spatial_index_;
    }

private:
    DocumentId doc_id_;
    Epoch epoch_{0};
    Revision revision_{0};
    ChunkMap chunks_;
    std::unordered_map<ObjectId, pikdraw::geometry::Rect2D> spatial_index_;
};

} // namespace pikdraw::document
