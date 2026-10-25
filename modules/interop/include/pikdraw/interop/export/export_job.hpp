#pragma once

#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/interop/export/export_options.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pikdraw::interop {

enum class ExportScope {
    Selection,
    ActivePage,
    Artboard,
    PageRange,
    AllPages,
    AllArtwork
};

enum class ExportBoundsPolicy {
    Page,
    VisibleArtwork,
    Selection,
    Artboard,
    ExplicitRectangle
};

enum class ExportTargetMode {
    SingleFile,
    PerPageBatch,
    PerAssetBatch
};

enum class ExportJobError {
    None,
    MissingSnapshot,
    InvalidJobIdentity,
    InvalidAdapterProfile,
    InvalidOptionsSchema,
    InvalidScope,
    InvalidPage,
    InvalidArtboard,
    InvalidSelection,
    InvalidBounds,
    InvalidOutputTargets
};

struct ExportJobRequest {
    std::string job_id;
    std::string adapter_id;
    std::string output_version;
    std::string profile_id;
    std::shared_ptr<const ExportOptionsSnapshot> options;
    ExportScope scope{ExportScope::ActivePage};
    ExportBoundsPolicy bounds_policy{ExportBoundsPolicy::Page};
    ExportTargetMode target_mode{ExportTargetMode::SingleFile};
    std::vector<document::PageId> page_ids;
    std::vector<document::ObjectId> selected_object_ids;
    std::optional<document::ArtboardId> artboard_id;
    std::optional<geometry::Rect2D> explicit_bounds;
    bool clip_to_page{true};
    std::vector<std::string> output_paths;
    uint64_t cancellation_generation{0};
};

struct ExportJobPage {
    document::PageId page_id{document::kInvalidPageId};
    std::vector<document::ObjectId> object_ids;
};

struct ExportOutputTarget {
    std::string path;
    std::vector<document::PageId> page_ids;
    std::vector<document::ObjectId> object_ids;
    std::optional<document::ArtboardId> artboard_id;
    std::optional<geometry::Rect2D> bounds;
};

class ExportJob;
struct ExportJobBuildResult;
[[nodiscard]] ExportJobBuildResult build_export_job(
    document::SnapshotPtr snapshot, ExportJobRequest request);

/// A resolved, immutable view of one export request and its source revision.
class ExportJob final {
public:
    [[nodiscard]] const document::SnapshotPtr& snapshot() const noexcept { return snapshot_; }
    [[nodiscard]] const std::string& job_id() const noexcept { return request_.job_id; }
    [[nodiscard]] const std::string& adapter_id() const noexcept { return request_.adapter_id; }
    [[nodiscard]] const std::string& output_version() const noexcept { return request_.output_version; }
    [[nodiscard]] const std::string& profile_id() const noexcept { return request_.profile_id; }
    [[nodiscard]] uint32_t options_schema_version() const noexcept {
        return request_.options ? request_.options->schema_version() : 0;
    }
    [[nodiscard]] const std::shared_ptr<const ExportOptionsSnapshot>& options() const noexcept {
        return request_.options;
    }
    [[nodiscard]] ExportScope scope() const noexcept { return request_.scope; }
    [[nodiscard]] ExportBoundsPolicy bounds_policy() const noexcept {
        return request_.bounds_policy;
    }
    [[nodiscard]] ExportTargetMode target_mode() const noexcept { return request_.target_mode; }
    [[nodiscard]] const std::vector<ExportJobPage>& pages() const noexcept { return pages_; }
    [[nodiscard]] const std::vector<ExportOutputTarget>& output_targets() const noexcept {
        return output_targets_;
    }
    [[nodiscard]] const std::vector<document::ObjectId>& selected_object_ids() const noexcept {
        return request_.selected_object_ids;
    }
    [[nodiscard]] const std::optional<document::ArtboardId>& artboard_id() const noexcept {
        return request_.artboard_id;
    }
    [[nodiscard]] const std::optional<geometry::Rect2D>& explicit_bounds() const noexcept {
        return request_.explicit_bounds;
    }
    [[nodiscard]] bool clip_to_page() const noexcept { return request_.clip_to_page; }
    [[nodiscard]] const std::vector<std::string>& output_paths() const noexcept {
        return request_.output_paths;
    }
    [[nodiscard]] uint64_t cancellation_generation() const noexcept {
        return request_.cancellation_generation;
    }
    [[nodiscard]] uint64_t snapshot_revision() const noexcept { return snapshot_->revision(); }
    [[nodiscard]] const std::string& document_id() const noexcept {
        return snapshot_->document_id();
    }

private:
    friend struct ExportJobBuildResult;
    friend ExportJobBuildResult build_export_job(
        document::SnapshotPtr, ExportJobRequest);

    ExportJob(document::SnapshotPtr snapshot, ExportJobRequest request,
              std::vector<ExportJobPage> pages,
              std::vector<ExportOutputTarget> output_targets)
        : snapshot_(std::move(snapshot)), request_(std::move(request)),
          pages_(std::move(pages)), output_targets_(std::move(output_targets)) {}

    document::SnapshotPtr snapshot_;
    ExportJobRequest request_;
    std::vector<ExportJobPage> pages_;
    std::vector<ExportOutputTarget> output_targets_;
};

struct ExportJobBuildResult {
    std::shared_ptr<const ExportJob> job;
    ExportJobError error{ExportJobError::None};
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(job); }
};

/// Capture the exact source snapshot and resolve page/object scope before a writer runs.
[[nodiscard]] ExportJobBuildResult build_export_job(
    document::SnapshotPtr snapshot, ExportJobRequest request);

} // namespace pikdraw::interop
