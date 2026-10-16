#pragma once
#include "pikdraw/document/document.hpp"
#include <algorithm>
#include <cmath>
namespace pikdraw::vector {
inline bool finite_point(geometry::Point2D p) noexcept {
    return std::isfinite(p.x) && std::isfinite(p.y);
}
// Only layer-root path/text objects currently have complete tool semantics.
inline bool editable_target(const document::Page &page, const document::PersistentObjectMap &objects,
                            document::ObjectId id) {
    const auto *object = objects.lookup_ref(id);
    if (!object)
        return false;
    const bool editable = std::visit(
        [](const auto &o) {
            using T = std::decay_t<decltype(o)>;
            return !std::is_same_v<T, document::GroupObject> && o.visible && !o.locked;
        },
        *object);
    if (!editable)
        return false;
    for (const auto &layer : page.layers) {
        if (std::find(layer.object_ids.begin(), layer.object_ids.end(), id) != layer.object_ids.end())
            return layer.visible && !layer.locked;
    }
    return false;
}
inline bool editable_target(const document::DocumentSnapshot &snapshot, document::ObjectId id) {
    return editable_target(snapshot.page(), snapshot.objects(), id);
}
struct GestureStamp {
    std::string document_id;
    uint64_t epoch{0}, revision{0};
    explicit GestureStamp(const document::DocumentSnapshot &snapshot)
        : document_id(snapshot.document_id()), epoch(snapshot.epoch()), revision(snapshot.revision()) {}
    GestureStamp() = default;
    bool matches(const document::DocumentSnapshot &snapshot) const noexcept {
        return !document_id.empty() && document_id == snapshot.document_id() && epoch == snapshot.epoch() &&
               revision == snapshot.revision();
    }
};
} // namespace pikdraw::vector
