#pragma once

#include "pikdraw/document/document.hpp"
#include "pikdraw/document/annotations/dimension_object.hpp"
#include "pikdraw/document/annotations/dimension_layout.hpp"
#include "pikdraw/document/annotations/dimension_style.hpp"
#include "pikdraw/document/commands/dimension_commands.hpp"
#include "pikdraw/geometry/primitives/point.hpp"

#include <optional>
#include <vector>

namespace pikdraw::desktop::tools {

enum class DimensionSessionState : uint8_t {
    Idle,
    PickFirst,
    PickSecond,
    Placement
};

enum class DimensionSnapType : uint8_t {
    None = 0,
    Node,
    Midpoint,
    Center,
    BoundingBoxCorner
};

class DimensionToolController {
public:
    explicit DimensionToolController(document::Document& doc);

    void activate(document::DimensionKind kind = document::DimensionKind::Aligned, bool smart_mode = false);
    void deactivate();
    [[nodiscard]] bool active() const noexcept { return active_; }

    [[nodiscard]] DimensionSessionState state() const noexcept { return state_; }
    [[nodiscard]] document::DimensionKind current_kind() const noexcept { return current_kind_; }
    [[nodiscard]] bool is_smart_mode() const noexcept { return smart_mode_; }
    [[nodiscard]] geometry::Point2D first_point() const noexcept { return first_point_; }
    [[nodiscard]] geometry::Point2D second_point() const noexcept { return second_point_; }

    [[nodiscard]] bool snap_active() const noexcept;
    [[nodiscard]] DimensionSnapType snap_type() const noexcept;
    [[nodiscard]] geometry::Point2D snap_point() const noexcept;
    [[nodiscard]] bool ortho_active() const noexcept;
    void update_snap(geometry::Point2D world_pos, double zoom, bool ortho_constraint = false);

    void set_linear_orientation(geometry::LinearOrientation orientation) noexcept;
    [[nodiscard]] geometry::LinearOrientation linear_orientation() const noexcept { return orientation_; }

    void set_current_style(document::DimensionStyle style) noexcept;
    [[nodiscard]] const document::DimensionStyle& current_style() const noexcept { return style_; }

    // Pointer gesture handling
    bool handle_pointer_press(
        geometry::Point2D world_pos,
        std::optional<document::ObjectId> hit_object = std::nullopt,
        std::optional<uint64_t> hit_element = std::nullopt);

    void handle_pointer_move(geometry::Point2D world_pos);
    bool handle_pointer_release(geometry::Point2D world_pos);

    // Escape cancellation
    bool handle_escape() noexcept;

    // Live preview
    [[nodiscard]] bool has_preview() const noexcept { return has_preview_; }
    [[nodiscard]] const document::DimensionLayout& preview_layout() const noexcept { return preview_layout_; }
    [[nodiscard]] const document::DimensionObject& draft_dimension() const noexcept { return draft_dim_; }

    // Commit draft to document
    bool commit();

private:
    void update_preview();

    document::Document& doc_;
    bool active_{false};
    bool smart_mode_{false};
    document::DimensionKind current_kind_{document::DimensionKind::Aligned};
    geometry::LinearOrientation orientation_{geometry::LinearOrientation::Aligned};
    document::DimensionStyle style_{document::DimensionStyle::make_default()};

    DimensionSessionState state_{DimensionSessionState::Idle};
    document::DimensionObject draft_dim_;
    document::DimensionLayout preview_layout_;
    bool has_preview_{false};

    geometry::Point2D first_point_{};
    geometry::Point2D second_point_{};
    std::vector<document::SourceReference> references_;

    bool snap_active_{false};
    DimensionSnapType snap_type_{DimensionSnapType::None};
    geometry::Point2D snap_point_{};
    document::ObjectId snapped_object_id_{document::kInvalidObjectId};
    std::optional<uint64_t> snapped_element_id_{std::nullopt};
    bool ortho_active_{false};
};

inline bool DimensionToolController::snap_active() const noexcept { return snap_active_; }
inline DimensionSnapType DimensionToolController::snap_type() const noexcept { return snap_type_; }
inline geometry::Point2D DimensionToolController::snap_point() const noexcept { return snap_point_; }
inline bool DimensionToolController::ortho_active() const noexcept { return ortho_active_; }

} // namespace pikdraw::desktop::tools
