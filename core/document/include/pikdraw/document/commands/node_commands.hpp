#pragma once

#include "pikdraw/document/commands/path_commands.hpp"
#include <utility>
#include <vector>

namespace pikdraw::document {

class MoveNodeHandleCommand final : public PathTopologyCommand {
public:
    MoveNodeHandleCommand(ObjectId object_id, SubpathId subpath_id, NodeId node_id,
                          bool is_incoming, geometry::Point2D new_handle_pos,
                          std::shared_ptr<PathTopologyMapping> mapping_result = {}) noexcept;
    [[nodiscard]] std::string_view name() const noexcept override { return "Move Node Handle"; }

protected:
    bool apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                    std::string& error) override;

private:
    SubpathId subpath_id_{};
    NodeId node_id_{};
    bool is_incoming_{false};
    geometry::Point2D new_handle_pos_{};
};

class SetNodeModeCommand final : public PathTopologyCommand {
public:
    SetNodeModeCommand(ObjectId object_id, SubpathId subpath_id, NodeId node_id,
                       PathNodeMode new_mode,
                       std::shared_ptr<PathTopologyMapping> mapping_result = {}) noexcept;
    [[nodiscard]] std::string_view name() const noexcept override { return "Set Node Mode"; }

protected:
    bool apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                    std::string& error) override;

private:
    SubpathId subpath_id_{};
    NodeId node_id_{};
    PathNodeMode new_mode_{PathNodeMode::Corner};
};

class BreakPathAtNodeCommand final : public PathTopologyCommand {
public:
    BreakPathAtNodeCommand(ObjectId object_id, SubpathId subpath_id, NodeId node_id,
                           std::shared_ptr<PathTopologyMapping> mapping_result = {}) noexcept;
    [[nodiscard]] std::string_view name() const noexcept override { return "Break Path at Node"; }

protected:
    bool apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                    std::string& error) override;

private:
    SubpathId subpath_id_{};
    NodeId node_id_{};
};

/// Scissors operation: split a source curve at a parameter when needed, then
/// open the path there in one topology mutation and one undo unit.
class ScissorsPathCommand final : public PathTopologyCommand {
public:
    ScissorsPathCommand(ObjectId object_id, SubpathId subpath_id, SegmentId segment_id,
                        double parameter, NodeId endpoint_node = {},
                        std::shared_ptr<PathTopologyMapping> mapping_result = {}) noexcept;
    [[nodiscard]] std::string_view name() const noexcept override { return "Scissors"; }

protected:
    bool apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                    std::string& error) override;

private:
    SubpathId subpath_id_{};
    SegmentId segment_id_{};
    double parameter_{0.5};
    NodeId endpoint_node_{};
};

struct SmartTrimBoundary {
    SegmentId segment_id{};
    double parameter{0.5};
};

/// Removes the selected intersection-bounded interval from one editable
/// subpath. Closed outline paths become open; filled paths are rejected by
/// the caller because opening a filled contour changes its fill semantics.
class SmartTrimPathCommand final : public PathTopologyCommand {
public:
    SmartTrimPathCommand(ObjectId object_id, SubpathId subpath_id,
                         SmartTrimBoundary start, SmartTrimBoundary end,
                         bool wraps_closed_subpath,
                         std::shared_ptr<PathTopologyMapping> mapping_result = {}) noexcept;
    [[nodiscard]] std::string_view name() const noexcept override { return "Smart Trim"; }

protected:
    bool apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                    std::string& error) override;

private:
    SubpathId subpath_id_{};
    SmartTrimBoundary start_{};
    SmartTrimBoundary end_{};
    bool wraps_closed_subpath_{false};
};

class ReverseSubpathCommand final : public PathTopologyCommand {
public:
    ReverseSubpathCommand(ObjectId object_id, SubpathId subpath_id,
                          std::shared_ptr<PathTopologyMapping> mapping_result = {}) noexcept;
    [[nodiscard]] std::string_view name() const noexcept override { return "Reverse Subpath"; }

protected:
    bool apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                    std::string& error) override;

private:
    SubpathId subpath_id_{};
};

class ConvertSegmentKindCommand final : public PathTopologyCommand {
public:
    ConvertSegmentKindCommand(ObjectId object_id, SubpathId subpath_id, SegmentId segment_id,
                              PathSegmentKind new_kind,
                              std::shared_ptr<PathTopologyMapping> mapping_result = {}) noexcept;
    [[nodiscard]] std::string_view name() const noexcept override { return "Convert Segment Kind"; }

protected:
    bool apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                    std::string& error) override;

private:
    SubpathId subpath_id_{};
    SegmentId segment_id_{};
    PathSegmentKind new_kind_{PathSegmentKind::Line};
};

class BatchMovePathNodesCommand final : public PathTopologyCommand {
public:
    BatchMovePathNodesCommand(ObjectId object_id,
                              std::vector<std::pair<SubpathId, NodeId>> target_nodes,
                              geometry::Point2D delta,
                              std::shared_ptr<PathTopologyMapping> mapping_result = {}) noexcept;
    [[nodiscard]] std::string_view name() const noexcept override { return "Batch Move Path Nodes"; }

protected:
    bool apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                    std::string& error) override;

private:
    std::vector<std::pair<SubpathId, NodeId>> target_nodes_;
    geometry::Point2D delta_{};
};

class BendSegmentCommand final : public PathTopologyCommand {
public:
    BendSegmentCommand(ObjectId object_id, SubpathId subpath_id, SegmentId segment_id,
                       geometry::Point2D outgoing_handle, geometry::Point2D incoming_handle,
                       std::shared_ptr<PathTopologyMapping> mapping_result = {}) noexcept;
    [[nodiscard]] std::string_view name() const noexcept override { return "Bend Segment"; }

protected:
    bool apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                    std::string& error) override;

private:
    SubpathId subpath_id_{};
    SegmentId segment_id_{};
    geometry::Point2D outgoing_handle_{};
    geometry::Point2D incoming_handle_{};
};

} // namespace pikdraw::document
