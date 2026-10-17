#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/model/editable_path.hpp"
#include <optional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pikdraw::document {

enum class PathElementKind { Subpath, Node, Segment };
enum class IdMappingRelation { Preserved, Created, Removed, Merged };

struct PathElementIdMapping {
    PathElementKind element{PathElementKind::Node};
    std::optional<uint64_t> before;
    std::optional<uint64_t> after;
    IdMappingRelation relation{IdMappingRelation::Preserved};
};

struct PathTopologyMapping {
    std::vector<PathElementIdMapping> elements;
};

class PathTopologyCommand : public DocumentCommand {
public:
    [[nodiscard]] std::string_view name() const noexcept override = 0;
    CommandResult execute(Document& doc) final;
    CommandResult undo(Document& doc) final;
    [[nodiscard]] const PathTopologyMapping& id_mapping() const noexcept { return mapping_; }

protected:
    explicit PathTopologyCommand(ObjectId object_id,
                                 std::shared_ptr<PathTopologyMapping> mapping_result = {}) noexcept
        : object_id_(object_id), mapping_result_(std::move(mapping_result)) {}
    virtual bool apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                            std::string& error) = 0;

private:
    ObjectId object_id_{kInvalidObjectId};
    std::shared_ptr<const EditablePath> before_source_;
    std::shared_ptr<const EditablePath> after_source_;
    geometry::Path2D before_adapter_;
    geometry::Path2D after_adapter_;
    PathTopologyMapping mapping_;
    std::shared_ptr<PathTopologyMapping> mapping_result_;
};

class MovePathNodeCommand final : public PathTopologyCommand {
public:
    MovePathNodeCommand(ObjectId object_id, SubpathId subpath_id, NodeId node_id,
                        geometry::Point2D new_anchor,
                        std::shared_ptr<PathTopologyMapping> mapping_result = {}) noexcept;
    [[nodiscard]] std::string_view name() const noexcept override { return "Move Path Node"; }

protected:
    bool apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                    std::string& error) override;

private:
    SubpathId subpath_id_{};
    NodeId node_id_{};
    geometry::Point2D new_anchor_{};
};

class SplitPathSegmentCommand final : public PathTopologyCommand {
public:
    SplitPathSegmentCommand(ObjectId object_id, SubpathId subpath_id, SegmentId segment_id,
                            double parameter,
                            std::shared_ptr<PathTopologyMapping> mapping_result = {}) noexcept;
    [[nodiscard]] std::string_view name() const noexcept override { return "Split Path Segment"; }

protected:
    bool apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                    std::string& error) override;

private:
    SubpathId subpath_id_{};
    SegmentId segment_id_{};
    double parameter_{0.5};
};

class DeletePathNodeCommand final : public PathTopologyCommand {
public:
    DeletePathNodeCommand(ObjectId object_id, SubpathId subpath_id, NodeId node_id,
                          std::shared_ptr<PathTopologyMapping> mapping_result = {}) noexcept;
    [[nodiscard]] std::string_view name() const noexcept override { return "Delete Path Node"; }

protected:
    bool apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                    std::string& error) override;

private:
    SubpathId subpath_id_{};
    NodeId node_id_{};
};

class JoinPathSubpathsCommand final : public PathTopologyCommand {
public:
    JoinPathSubpathsCommand(ObjectId object_id, SubpathId first_subpath,
                            NodeId first_endpoint, SubpathId second_subpath,
                            NodeId second_endpoint, bool create_bridge,
                            NodeId coincident_survivor = {},
                            std::shared_ptr<PathTopologyMapping> mapping_result = {}) noexcept;
    [[nodiscard]] std::string_view name() const noexcept override { return "Join Path Subpaths"; }

protected:
    bool apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                    std::string& error) override;

private:
    SubpathId first_subpath_{};
    NodeId first_endpoint_{};
    SubpathId second_subpath_{};
    NodeId second_endpoint_{};
    bool create_bridge_{false};
    NodeId coincident_survivor_{};
};

class ClosePathSubpathCommand final : public PathTopologyCommand {
public:
    ClosePathSubpathCommand(ObjectId object_id, SubpathId subpath_id,
                            std::optional<NodeId> merge_to_start = std::nullopt,
                            std::shared_ptr<PathTopologyMapping> mapping_result = {}) noexcept;
    [[nodiscard]] std::string_view name() const noexcept override { return "Close Path Subpath"; }

protected:
    bool apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                    std::string& error) override;

private:
    SubpathId subpath_id_{};
    std::optional<NodeId> merge_to_start_;
};

/// Appends newly drawn nodes to one of an open subpath's endpoints.
/// When extending from its first endpoint, the existing subpath traversal is
/// reversed first so its source geometry remains unchanged and IDs are kept.
/// Extension nodes are supplied in drawing order and must have invalid IDs;
/// this command allocates their stable IDs atomically on first execution.
class ExtendPathEndpointCommand final : public PathTopologyCommand {
public:
    ExtendPathEndpointCommand(ObjectId object_id, SubpathId subpath_id,
                              NodeId endpoint_id,
                              std::vector<PathNode> extension_nodes,
                              std::vector<PathSegmentKind> segment_kinds,
                              std::shared_ptr<PathTopologyMapping> mapping_result = {}) noexcept;
    [[nodiscard]] std::string_view name() const noexcept override {
        return "Extend Path Endpoint";
    }

protected:
    bool apply_edit(EditablePath& path, PathTopologyMapping& mapping,
                    std::string& error) override;

private:
    SubpathId subpath_id_{};
    NodeId endpoint_id_{};
    std::vector<PathNode> extension_nodes_;
    std::vector<PathSegmentKind> segment_kinds_;
};

} // namespace pikdraw::document
