#pragma once

#include "pikdraw/document/model/object.hpp"
#include "pikdraw/document/snapshots/persistent_map.hpp"
#include "pikdraw/document/model/structure.hpp"
#include <memory>
#include <vector>

namespace pikdraw::document {

/// Immutable Document Snapshot consumed by concurrent readers (Renderer, Exporters)
class DocumentSnapshot {
public:
    DocumentSnapshot(uint64_t epoch, uint64_t revision, Page page, PersistentObjectMap objects,
                     std::string document_id = {})
        : epoch_(epoch), revision_(revision), page_(std::move(page)), objects_(std::move(objects)),
          document_id_(std::move(document_id)) {
        validate_document_structure(page_, objects_);
    }

    [[nodiscard]] const std::string& document_id() const noexcept { return document_id_; }

    [[nodiscard]] uint64_t epoch() const noexcept { return epoch_; }
    [[nodiscard]] uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] const Page& page() const noexcept { return page_; }
    [[nodiscard]] const PersistentObjectMap& objects() const noexcept { return objects_; }

    [[nodiscard]] size_t object_count() const noexcept { return objects_.size(); }
    [[nodiscard]] bool contains_object(ObjectId id) const { return objects_.contains(id); }
    [[nodiscard]] std::optional<ObjectVariant> get_object(ObjectId id) const { return objects_.get(id); }

    [[nodiscard]] geometry::Rect2D bounds() const {
        return geometry::Rect2D{0.0, 0.0, page_.width_mm, page_.height_mm};
    }

private:
    uint64_t epoch_{1};
    uint64_t revision_{0};
    Page page_;
    PersistentObjectMap objects_;
    std::string document_id_;
};

using SnapshotPtr = std::shared_ptr<const DocumentSnapshot>;

} // namespace pikdraw::document
