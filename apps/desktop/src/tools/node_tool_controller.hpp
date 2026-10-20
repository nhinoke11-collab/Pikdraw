#pragma once

#include "pikdraw/document/document.hpp"
#include "pikdraw/vector/tools/node/node_tool.hpp"
#include <memory>
#include <optional>
#include <vector>

namespace pikdraw::desktop::tools {

class NodeToolController {
public:
    explicit NodeToolController(document::Document& document) noexcept
        : document_(document), tool_(std::make_unique<vector::tools::node::NodeTool>(document)) {}

    NodeToolController(const NodeToolController&) = delete;
    NodeToolController& operator=(const NodeToolController&) = delete;

    bool activate(document::ObjectId target_id = document::kInvalidObjectId);
    void deactivate();
    [[nodiscard]] bool active() const noexcept { return is_active_; }
    [[nodiscard]] bool is_active() const noexcept { return is_active_; }

    bool pointer_down(geometry::Point2D world, bool shift, double tol_mm = 2.0, double zoom = 1.0);
    bool pointer_move(geometry::Point2D world);
    bool pointer_up(geometry::Point2D world);
    void cancel();

    // Operations
    bool delete_selected();
    bool add_node_at_midpoint();
    bool break_selected();
    bool reverse_selected();
    bool set_selected_mode(document::PathNodeMode mode);
    bool convert_selected_segment(document::PathSegmentKind kind);
    bool join_selected(bool bridge = true);
    bool close_selected();
    bool move_selected_delta(geometry::Point2D delta);
    bool set_selected_node_position(geometry::Point2D new_pos);

    // Queries
    [[nodiscard]] size_t selected_node_count() const noexcept;
    [[nodiscard]] std::optional<geometry::Point2D> single_selected_node_position() const;
    [[nodiscard]] std::optional<document::PathNodeMode> selected_nodes_mode() const;
    [[nodiscard]] bool is_marquee() const noexcept;
    [[nodiscard]] geometry::Rect2D marquee_rect() const noexcept;
    [[nodiscard]] std::vector<vector::tools::node::NodeHandleRef> visible_handles() const;
    [[nodiscard]] document::ObjectId target_object() const noexcept;
    [[nodiscard]] bool is_node_selected(document::SubpathId sub_id, document::NodeId node_id) const noexcept {
        return tool_->is_node_selected(sub_id, node_id);
    }
    [[nodiscard]] bool is_rounding_corners() const noexcept { return tool_->is_rounding_corners(); }
    [[nodiscard]] double current_corner_radius() const noexcept { return tool_->current_corner_radius(); }
    void set_corner_type(vector::RectCornerType type) noexcept { tool_->set_corner_type(type); }
    [[nodiscard]] vector::RectCornerType corner_type() const noexcept { return tool_->corner_type(); }
    [[nodiscard]] std::vector<vector::tools::node::CornerWidget> corner_widgets(double zoom = 1.0) const {
        return tool_->corner_widgets(zoom);
    }

    [[nodiscard]] vector::tools::node::NodeTool& tool() noexcept { return *tool_; }
    [[nodiscard]] const vector::tools::node::NodeTool& tool() const noexcept { return *tool_; }

private:
    document::Document& document_;
    std::unique_ptr<vector::tools::node::NodeTool> tool_;
    bool is_active_{false};
};

} // namespace pikdraw::desktop::tools
