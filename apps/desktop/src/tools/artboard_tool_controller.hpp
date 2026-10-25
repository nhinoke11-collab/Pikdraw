#pragma once

#include "pikdraw/document/model/document_page.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/geometry/primitives/point.hpp"

#include <optional>
#include <string>

namespace pikdraw::desktop::tools {

enum class ArtboardGestureKind { None, Create, Move, Resize };

struct ArtboardToolEdit {
    ArtboardGestureKind kind{ArtboardGestureKind::None};
    document::PageId page_id{document::kInvalidPageId};
    document::ArtboardId artboard_id{document::kInvalidArtboardId};
    std::string name;
    geometry::Rect2D bounds{};
};

class ArtboardToolController {
public:
    [[nodiscard]] bool begin_create(document::PageId page_id, geometry::Point2D anchor,
                                   std::string name);
    [[nodiscard]] bool begin_move(const document::Artboard& artboard,
                                  geometry::Point2D anchor);
    [[nodiscard]] bool begin_resize(const document::Artboard& artboard,
                                    geometry::Point2D anchor, int handle);
    void update(geometry::Point2D current) noexcept;
    [[nodiscard]] std::optional<ArtboardToolEdit> finish(double minimum_size_mm = 1.0) noexcept;
    void cancel() noexcept;

    [[nodiscard]] ArtboardGestureKind kind() const noexcept { return kind_; }
    [[nodiscard]] bool active() const noexcept { return kind_ != ArtboardGestureKind::None; }
    [[nodiscard]] geometry::Rect2D preview_bounds() const noexcept { return preview_; }
    [[nodiscard]] document::PageId page_id() const noexcept { return page_id_; }
    [[nodiscard]] document::ArtboardId artboard_id() const noexcept { return artboard_id_; }

private:
    ArtboardGestureKind kind_{ArtboardGestureKind::None};
    document::PageId page_id_{document::kInvalidPageId};
    document::ArtboardId artboard_id_{document::kInvalidArtboardId};
    std::string name_;
    geometry::Rect2D before_{};
    geometry::Rect2D preview_{};
    geometry::Point2D anchor_{};
    int handle_{-1};
};

} // namespace pikdraw::desktop::tools
