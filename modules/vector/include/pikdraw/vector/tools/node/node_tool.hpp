#pragma once

#include "pikdraw/document/commands/node_commands.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
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
    Marquee
};

class NodeTool {
public:
    explicit NodeTool(document::Document& document) noexcept
        : document_(document) {}

    void set_target_object(document::ObjectId object_id);
    [[nodiscard]] document::ObjectId target_object() const noexcept { return target_object_id_; }

    // Pointer gesture handling
    bool pointer_down(geometry::Point2D world_pos, bool shift, double tolerance_mm = 2.0);
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

private:
    [[nodiscard]] const document::EditablePath* get_editable_path() const;
    [[nodiscard]] std::optional<NodeHandleRef> hit_test_handle(geometry::Point2D world_pos, double tol) const;
    [[nodiscard]] std::optional<std::pair<document::SubpathId, document::NodeId>> hit_test_node(
        geometry::Point2D world_pos, double tol) const;
    [[nodiscard]] std::optional<std::pair<document::SubpathId, document::SegmentId>> hit_test_segment(
        geometry::Point2D world_pos, double tol, double* out_t = nullptr) const;

    document::Document& document_;
    document::ObjectId target_object_id_{document::kInvalidObjectId};

    std::vector<std::pair<document::SubpathId, document::NodeId>> selected_nodes_;
    std::optional<std::pair<document::SubpathId, document::SegmentId>> selected_segment_;
    std::optional<NodeHandleRef> active_handle_;

    NodeDragMode drag_mode_{NodeDragMode::None};
    geometry::Point2D drag_start_world_{};
    geometry::Point2D current_drag_world_{};
    geometry::Point2D initial_handle_pos_{};
};

} // namespace pikdraw::vector::tools::node
