#pragma once

#include "pikdraw/document/commands/node_commands.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/vector/tools/node/corner_rounding.hpp"
#include <optional>
#include <utility>
#include <vector>

namespace pikdraw::vector::tools::node {

struct NodeHandleRef {
    document::SubpathId subpath_id{};
    document::NodeId node_id{};
    bool is_incoming{false};
    geometry::Point2D position{};
};

enum class NodeDragMode {
    None,
    MoveNode,
    MoveHandle,
    RoundCorner,
    Marquee,
    BendSegment
};

struct ActiveSegmentDrag {
    document::SubpathId subpath_id{};
    document::SegmentId segment_id{};
    size_t start_node_index{0};
    size_t end_node_index{0};
    document::NodeId start_node_id{};
    document::NodeId end_node_id{};
    geometry::Point2D p0_world{};
    geometry::Point2D p3_world{};
    geometry::Point2D original_c1_world{};
    geometry::Point2D original_c2_world{};
    bool was_cubic{false};
    double click_t{0.5};
};

class NodeTool {
public:
    explicit NodeTool(document::Document& document) noexcept
        : document_(document) {}

    void set_target_object(document::ObjectId object_id);
    [[nodiscard]] document::ObjectId target_object() const noexcept { return target_object_id_; }

    // Pointer gesture handling
    bool pointer_down(geometry::Point2D world_pos, bool shift, double tolerance_mm = 2.0, double zoom = 1.0);
    bool pointer_move(geometry::Point2D world_pos);
    bool pointer_up(geometry::Point2D world_pos);
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

    // Selection query
    void select_all();
    void clear_selection();
    [[nodiscard]] size_t selected_node_count() const noexcept { return selected_nodes_.size(); }
    [[nodiscard]] const std::vector<std::pair<document::SubpathId, document::NodeId>>& selected_nodes() const noexcept {
        return selected_nodes_;
    }
    [[nodiscard]] bool is_node_selected(document::SubpathId sub_id, document::NodeId node_id) const noexcept;
    [[nodiscard]] std::optional<geometry::Point2D> single_selected_node_position() const;
    [[nodiscard]] std::optional<document::PathNodeMode> selected_nodes_mode() const;

    // Overlay visual queries
    [[nodiscard]] bool is_marquee() const noexcept { return drag_mode_ == NodeDragMode::Marquee; }
    [[nodiscard]] geometry::Rect2D marquee_rect() const noexcept;
    [[nodiscard]] std::vector<NodeHandleRef> visible_handles() const;
    [[nodiscard]] NodeDragMode drag_mode() const noexcept { return drag_mode_; }
    [[nodiscard]] bool is_dragging() const noexcept { return drag_mode_ != NodeDragMode::None; }
    [[nodiscard]] bool is_rounding_corners() const noexcept { return drag_mode_ == NodeDragMode::RoundCorner; }
    [[nodiscard]] double current_corner_radius() const noexcept { return current_corner_radius_; }
    [[nodiscard]] geometry::Point2D drag_delta() const noexcept {
        return (drag_mode_ != NodeDragMode::None) ? (current_drag_world_ - drag_start_world_) : geometry::Point2D{0.0, 0.0};
    }
    [[nodiscard]] std::optional<CornerWidget> active_corner_widget() const noexcept { return active_corner_widget_; }
    [[nodiscard]] std::vector<CornerWidget> corner_widgets(double zoom = 1.0) const;
    [[nodiscard]] std::optional<document::EditablePath> preview_rounded_path() const;
    [[nodiscard]] std::optional<ActiveSegmentDrag> active_segment_drag() const noexcept { return active_segment_drag_; }
    [[nodiscard]] std::pair<geometry::Point2D, geometry::Point2D> current_segment_bend_handles() const;

    // Hit testing and geometry queries
    [[nodiscard]] const document::EditablePath* get_editable_path() const;
    [[nodiscard]] geometry::Transform2D target_transform() const;
    [[nodiscard]] geometry::Transform2D target_inverse_transform() const;
    [[nodiscard]] std::optional<NodeHandleRef> hit_test_handle(geometry::Point2D world_pos, double tol) const;
    [[nodiscard]] std::optional<std::pair<document::SubpathId, document::NodeId>> hit_test_node(
        geometry::Point2D world_pos, double tol) const;
    [[nodiscard]] std::optional<std::pair<document::SubpathId, document::SegmentId>> hit_test_segment(
        geometry::Point2D world_pos, double tol, double* out_t = nullptr) const;
    [[nodiscard]] std::optional<CornerWidget> hit_test_corner_widget(
        geometry::Point2D world_pos, double tol, double zoom = 1.0) const;

private:
    document::Document& document_;
    document::ObjectId target_object_id_{document::kInvalidObjectId};

    std::vector<std::pair<document::SubpathId, document::NodeId>> selected_nodes_;
    std::optional<std::pair<document::SubpathId, document::SegmentId>> selected_segment_;
    std::optional<NodeHandleRef> active_handle_;
    std::optional<std::pair<document::SubpathId, document::NodeId>> clicked_node_;

    NodeDragMode drag_mode_{NodeDragMode::None};
    bool has_dragged_{false};
    bool multi_select_click_{false};
    geometry::Point2D drag_start_world_{};
    geometry::Point2D current_drag_world_{};
    geometry::Point2D initial_handle_pos_{};
    geometry::Point2D initial_handle_local_pos_{};
    std::vector<std::pair<std::pair<document::SubpathId, document::NodeId>, geometry::Point2D>> initial_node_anchors_;

    // Live Corner Rounding
    std::optional<CornerWidget> active_corner_widget_;
    std::optional<document::EditablePath> baseline_path_;
    double current_corner_radius_{0.0};

    // Direct Segment Bending
    std::optional<ActiveSegmentDrag> active_segment_drag_;
};

} // namespace pikdraw::vector::tools::node
