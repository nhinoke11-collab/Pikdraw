#pragma once

#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/document/resources/memory_budget_policy.hpp"
#include "pikdraw/raster/analysis/histogram.hpp"

#include <functional>
#include <memory>

namespace pikdraw::raster {

enum class RasterHistogramMode : uint8_t {
  Source = 0,
  Edited = 1
};

/// Computes one selected image's source-code histogram off the UI thread.
/// Results are keyed to the immutable document snapshot, source storage, and
/// requested source/edited mode.
class RasterHistogramService final {
 public:
  using Completion = std::function<void()>;

  explicit RasterHistogramService(
      std::shared_ptr<document::resources::MemoryBudgetPolicy> memory_policy =
          document::resources::process_memory_budget_policy());
  ~RasterHistogramService();

  RasterHistogramService(const RasterHistogramService&) = delete;
  RasterHistogramService& operator=(const RasterHistogramService&) = delete;

  void request(document::SnapshotPtr snapshot,
               document::ObjectId image_id,
               Completion completion = {},
               RasterHistogramMode mode = RasterHistogramMode::Source);
  void cancel() noexcept;

  [[nodiscard]] std::shared_ptr<const RasterHistogramResult> find(
      const document::DocumentSnapshot& snapshot,
      document::ObjectId image_id,
      RasterHistogramMode mode = RasterHistogramMode::Source) const;
  [[nodiscard]] bool is_processing(
      const document::DocumentSnapshot& snapshot,
      document::ObjectId image_id,
      RasterHistogramMode mode = RasterHistogramMode::Source) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace pikdraw::raster
