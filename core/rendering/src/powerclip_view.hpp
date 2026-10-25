#pragma once
#include "pikdraw/document/hierarchy/frame_geometry.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/rendering/camera.hpp"

namespace pikdraw::rendering::detail {
inline std::optional<document::PathObject> clip_frame_outline(
    const document::DocumentSnapshot& snapshot, document::ObjectId owner, const Camera2D& camera) {
    const auto* object = snapshot.objects().lookup_ref(owner);
    const auto* group = object ? std::get_if<document::GroupObject>(object) : nullptr;
    const auto* entry = snapshot.hierarchy().find(owner);
    if (!group || !group->clip || !entry) return {};
    const auto* mask = snapshot.objects().lookup_ref(group->clip->mask_object_id);
    auto path = mask ? document::frame_geometry(*mask) : std::nullopt;
    if (!path) return {};
    path->transform = entry->world * path->transform;
    path->has_fill = false; path->has_stroke = true;
    path->appearance = {}; path->stroke = color::RGBColor{.05, .65, .85, 1};
    path->stroke_width_mm = 1.0 / std::max(camera.zoom(), 1e-6);
    path->fill_gradient.reset();
    return path;
}
} // namespace pikdraw::rendering::detail
