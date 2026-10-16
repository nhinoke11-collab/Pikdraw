#pragma once

#include "pikdraw/document/model/object_id.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"
#include "pikdraw/vector/precision/snap/snap_config.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pikdraw::vector::precision::snap {

struct SnapQueryStamp {
    std::string document_id;
    uint64_t document_epoch{0};
    uint64_t document_revision{0};
    uint64_t camera_generation{0};
    uint64_t config_generation{0};
    uint64_t capability_generation{0};
    std::string tool_context;
    uint64_t query_generation{0};
    friend bool operator==(const SnapQueryStamp&, const SnapQueryStamp&) = default;
};

struct SnapCameraTransform {
    geometry::Transform2D world_to_device_pixels;
    double dpi_scale_x{1.0};
    double dpi_scale_y{1.0};
    uint64_t generation{0};

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] geometry::Point2D world_to_dip(geometry::Point2D world) const noexcept;
    [[nodiscard]] geometry::Point2D world_vector_to_dip(geometry::Point2D vector) const noexcept;
};

struct SnapCapabilities {
    uint64_t generation{1};
    std::array<bool, kSnapOptionCount> enabled{};

    SnapCapabilities() noexcept;
    [[nodiscard]] bool supports(SnapOption option) const noexcept;
};

struct GridCellKey {
    int64_t x{0};
    int64_t y{0};
    friend bool operator==(const GridCellKey&, const GridCellKey&) = default;
};

struct SnapCandidateKey {
    SnapOption kind{SnapOption::Endpoint};
    std::optional<document::ObjectId> source_object_id;
    std::optional<uint64_t> source_node_or_segment_id;
    std::optional<GridCellKey> grid_cell;
    friend bool operator==(const SnapCandidateKey&, const SnapCandidateKey&) = default;
};

struct SnapCandidate {
    SnapCandidateKey key;
    double parameter_t{0.0};
    geometry::Point2D world_point;
    uint64_t source_revision{0};
    double distance_dip{0.0};
    int per_tool_priority{0};
    int configured_kind_priority{0};
    SnapQueryStamp stamp;
};

struct SnapAcquisitionState {
    SnapCandidateKey key;
    SnapQueryStamp stamp;
};

enum class SnapQueryStatus : uint8_t {
    Success,
    InvalidInput,
    StaleSource,
    StaleCamera,
    StaleConfig,
    StaleCapabilities,
    StaleToolContext,
    StaleQuery,
    MissingActiveLayer,
    WorkBudgetExceeded
};

struct SnapQueryContext {
    std::optional<size_t> active_layer_index;
    std::optional<document::ObjectId> current_source_object_id;
    std::optional<SnapAcquisitionState> previous_acquisition;
    // When supplied, rejects obsolete async query stamps before visiting geometry.
    std::optional<SnapQueryStamp> current_stamp;
    size_t max_entries_examined{100000};
    size_t max_results{8192};
};

struct SnapQueryStats {
    size_t index_entries{0};
    size_t kd_nodes_visited{0};
    size_t source_entries_examined{0};
    size_t distance_evaluations{0};
    size_t grid_cells_examined{0};
    std::array<size_t, kSnapOptionCount> kind_tree_queries{};
};

struct SnapQueryResult {
    SnapQueryStatus status{SnapQueryStatus::Success};
    SnapQueryStamp stamp;
    std::vector<SnapCandidate> candidates;
    std::optional<SnapCandidate> chosen;
    std::optional<SnapAcquisitionState> next_acquisition;
    SnapQueryStats stats;
    bool grid_density_limited{false};
};

struct SnapIndexBuildLimits {
    size_t max_entries{2000000};
};

enum class SnapIndexBuildStatus : uint8_t { Success, InvalidSource, EntryLimitExceeded };

class SnapSourceIndex;

struct SnapIndexBuildResult {
    SnapIndexBuildStatus status{SnapIndexBuildStatus::InvalidSource};
    std::shared_ptr<const SnapSourceIndex> index;
    size_t entry_count{0};
    std::string diagnostic;
};

class SnapSourceIndex {
public:
    struct Impl;

    [[nodiscard]] static SnapIndexBuildResult build(
        const document::DocumentSnapshot& source, SnapIndexBuildLimits limits = {});
    [[nodiscard]] uint64_t document_epoch() const noexcept;
    [[nodiscard]] uint64_t document_revision() const noexcept;
    [[nodiscard]] const std::string& document_id() const noexcept;
    [[nodiscard]] size_t entry_count() const noexcept;
    [[nodiscard]] SnapQueryResult query(
        const SnapQueryStamp& stamp, geometry::Point2D pointer_world,
        const SnapCameraTransform& camera, const SnapConfigSnapshot& config,
        const SnapCapabilities& capabilities, const SnapQueryContext& context = {}) const;

private:
    explicit SnapSourceIndex(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}
    std::shared_ptr<const Impl> impl_;
};

[[nodiscard]] bool snap_result_is_current(
    const SnapQueryStamp& result_stamp, const SnapQueryStamp& current_stamp) noexcept;

} // namespace pikdraw::vector::precision::snap
