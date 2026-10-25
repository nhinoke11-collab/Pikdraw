#include "artboard_tool_controller.hpp"

#include <algorithm>
#include <cmath>

namespace pikdraw::desktop::tools {
namespace {

geometry::Rect2D normalized_rect(geometry::Point2D a, geometry::Point2D b) noexcept {
    return {std::min(a.x, b.x), std::min(a.y, b.y),
            std::abs(a.x - b.x), std::abs(a.y - b.y)};
}

} // namespace

bool ArtboardToolController::begin_create(document::PageId page_id,
                                          geometry::Point2D anchor,
                                          std::string name) {
    if (active() || page_id == document::kInvalidPageId || name.empty() ||
        name.size() > 1024 || !std::isfinite(anchor.x) || !std::isfinite(anchor.y))
        return false;
    kind_ = ArtboardGestureKind::Create;
    page_id_ = page_id;
    name_ = std::move(name);
    anchor_ = anchor;
    preview_ = {};
    return true;
}

bool ArtboardToolController::begin_move(const document::Artboard& artboard,
                                        geometry::Point2D anchor) {
    if (active() || !artboard.is_valid_for(artboard.owner_page_id) ||
        !std::isfinite(anchor.x) || !std::isfinite(anchor.y)) return false;
    kind_ = ArtboardGestureKind::Move;
    page_id_ = artboard.owner_page_id;
    artboard_id_ = artboard.id;
    before_ = preview_ = {artboard.x_mm, artboard.y_mm,
                          artboard.width_mm, artboard.height_mm};
    anchor_ = anchor;
    return true;
}

bool ArtboardToolController::begin_resize(const document::Artboard& artboard,
                                          geometry::Point2D anchor, int handle) {
    if (handle < 0 || handle > 7 ||
        !begin_move(artboard, anchor)) return false;
    kind_ = ArtboardGestureKind::Resize;
    handle_ = handle;
    return true;
}

void ArtboardToolController::update(geometry::Point2D current) noexcept {
    if (!active() || !std::isfinite(current.x) || !std::isfinite(current.y)) return;
    if (kind_ == ArtboardGestureKind::Create) {
        preview_ = normalized_rect(anchor_, current);
        return;
    }
    if (kind_ == ArtboardGestureKind::Move) {
        preview_ = before_;
        preview_.x += current.x - anchor_.x;
        preview_.y += current.y - anchor_.y;
        return;
    }

    double left = before_.x;
    double right = before_.right();
    double bottom = before_.y;
    double top = before_.bottom();
    switch (handle_) {
    case 0: left = current.x; top = current.y; break; // top-left
    case 1: top = current.y; break;
    case 2: right = current.x; top = current.y; break;
    case 3: right = current.x; break;
    case 4: right = current.x; bottom = current.y; break;
    case 5: bottom = current.y; break;
    case 6: left = current.x; bottom = current.y; break;
    case 7: left = current.x; break;
    default: return;
    }
    preview_ = {std::min(left, right), std::min(bottom, top),
                std::abs(right - left), std::abs(top - bottom)};
}

std::optional<ArtboardToolEdit> ArtboardToolController::finish(
    double minimum_size_mm) noexcept {
    if (!active() || !std::isfinite(minimum_size_mm) || minimum_size_mm <= 0.0 ||
        !preview_.is_finite() || preview_.width < minimum_size_mm ||
        preview_.height < minimum_size_mm ||
        preview_.width > geometry::kMaxCanvasDimensionMm ||
        preview_.height > geometry::kMaxCanvasDimensionMm ||
        preview_.x < -geometry::kMaxCanvasDimensionMm ||
        preview_.y < -geometry::kMaxCanvasDimensionMm ||
        preview_.right() > geometry::kMaxCanvasDimensionMm ||
        preview_.bottom() > geometry::kMaxCanvasDimensionMm) {
        cancel();
        return std::nullopt;
    }
    ArtboardToolEdit result{kind_, page_id_, artboard_id_, name_, preview_};
    cancel();
    return result;
}

void ArtboardToolController::cancel() noexcept {
    kind_ = ArtboardGestureKind::None;
    page_id_ = document::kInvalidPageId;
    artboard_id_ = document::kInvalidArtboardId;
    name_.clear();
    before_ = {};
    preview_ = {};
    anchor_ = {};
    handle_ = -1;
}

} // namespace pikdraw::desktop::tools
