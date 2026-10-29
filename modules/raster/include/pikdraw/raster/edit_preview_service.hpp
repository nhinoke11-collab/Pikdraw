#pragma once

#include "pikdraw/color/profiles/profile_resource.hpp"
#include "pikdraw/document/resources/memory_budget_policy.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace pikdraw::raster {

/// Evaluates edited image previews away from the UI thread and keeps bounded,
/// revision-keyed render images for the viewport.
class RasterEditPreviewService final {
 public:
  using Completion = std::function<void()>;

  /// Zero byte limits select the shared, live RAM/commit policy. Explicit
  /// nonzero limits remain available for deterministic callers and tests.
  explicit RasterEditPreviewService(
      uint64_t working_memory_bytes = 0,
      uint64_t cache_memory_bytes = 0,
      std::shared_ptr<const color::ProfileResource> preview_rgb_profile = {},
      std::shared_ptr<document::resources::MemoryBudgetPolicy> memory_policy =
          document::resources::process_memory_budget_policy());
  ~RasterEditPreviewService();

  RasterEditPreviewService(const RasterEditPreviewService&) = delete;
  RasterEditPreviewService& operator=(const RasterEditPreviewService&) = delete;

  /// Queues visible-snapshot images with enabled edit nodes. Duplicate and
  /// stale jobs are coalesced; completion runs on the worker thread.
  void request(const std::shared_ptr<const document::DocumentSnapshot>& snapshot,
               Completion completion = {});

  /// Returns a ready render image for the exact source/graph key, or null while
  /// evaluation is pending or unavailable.
  [[nodiscard]] std::shared_ptr<const document::ImageObject> find(
      const document::DocumentSnapshot& snapshot,
      const document::ImageObject& image) const;

  /// Returns the newest cached frame for the same immutable source when the
  /// current graph key is still pending. Geometry and presentation metadata
  /// are taken from the current snapshot image.
  [[nodiscard]] std::shared_ptr<const document::ImageObject> find_last_good(
      const document::DocumentSnapshot& snapshot,
      const document::ImageObject& image) const;

  [[nodiscard]] std::optional<std::string> take_diagnostic();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace pikdraw::raster
