#pragma once
#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/rendering/camera.hpp"

namespace pikdraw::desktop {
// One bounded source frame. Hover, handles and drag previews paint over it without
// rerasterizing unchanged source. A root/profile change always invalidates it.
struct CanvasFrameCache {
    document::SnapshotPtr snapshot;
    std::shared_ptr<const color::IccPreview> profile;
    geometry::Point2D center;
    double zoom{0};
    int width{0}, height{0};
    bool dark{true};
    std::vector<uint8_t> bgra;
    bool matches(const document::SnapshotPtr &root, const rendering::Camera2D &camera, int w, int h,
                 const std::shared_ptr<const color::IccPreview> &icc, bool is_dark) const {
        return snapshot == root && profile == icc && dark == is_dark && width == w && height == h &&
               zoom == camera.zoom() && center.x == camera.center().x && center.y == camera.center().y;
    }
};
} // namespace pikdraw::desktop
