#pragma once
#include "pikdraw/document/document.hpp"
#include <algorithm>
#include <cmath>
namespace pikdraw::vector {
inline bool finite_point(geometry::Point2D p) noexcept {
    return std::isfinite(p.x) && std::isfinite(p.y);
}
inline bool editable_target(const document::Page& page,const document::PersistentObjectMap& objects,
                            document::ObjectId id) {
    try { return document::SceneHierarchy(page,objects).editable(id); }
    catch(...) {return false;}
}
inline bool editable_target(const document::DocumentSnapshot &snapshot, document::ObjectId id) {
    return !snapshot.is_content_locked() && snapshot.hierarchy().editable(id);
}
struct GestureStamp {
    std::string document_id;
    uint64_t epoch{0}, revision{0}, page_id{0};
    explicit GestureStamp(const document::DocumentSnapshot &snapshot)
        : document_id(snapshot.document_id()), epoch(snapshot.epoch()), revision(snapshot.revision()), page_id(snapshot.active_page_id()) {}
    GestureStamp() = default;
    bool matches(const document::DocumentSnapshot &snapshot) const noexcept {
        return !document_id.empty() && document_id == snapshot.document_id() && epoch == snapshot.epoch() &&
               revision == snapshot.revision() && page_id == snapshot.active_page_id();
    }
};
} // namespace pikdraw::vector
