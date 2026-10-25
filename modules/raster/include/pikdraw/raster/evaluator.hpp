#pragma once

#include "pikdraw/document/model/raster_edit_graph.hpp"
#include "pikdraw/raster/memory_budget.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace pikdraw::raster {

/// Encoded sRGB with straight alpha. 16-bit samples are little-endian.
enum class RasterPixelEncoding : uint8_t {
  SRgbRgba8StraightAlpha = 1,
  SRgbRgba16LEStraightAlpha = 2
};

struct RasterPixelView {
  std::span<const uint8_t> bytes;
  uint32_t width{0};
  uint32_t height{0};
  uint64_t row_stride{0};
  RasterPixelEncoding encoding{RasterPixelEncoding::SRgbRgba8StraightAlpha};
};

struct RasterPixelBuffer {
  uint32_t width{0};
  uint32_t height{0};
  uint64_t row_stride{0};
  RasterPixelEncoding encoding{RasterPixelEncoding::SRgbRgba8StraightAlpha};
  std::vector<uint8_t> bytes;
  std::shared_ptr<RasterMemoryBudget::Reservation> reservation;
};

enum class RasterEvaluationStatus : uint8_t {
  Success,
  Cancelled,
  InvalidSource,
  InvalidGraph,
  UnsupportedEncoding,
  InsufficientMemory,
  AllocationFailure
};

struct RasterEvaluationProgress {
  size_t completed_nodes{0};
  size_t total_nodes{0};
  uint32_t completed_rows{0};
  uint32_t total_rows{0};
};

struct RasterEvaluationOptions {
  std::function<bool()> cancelled;
  std::function<void(const RasterEvaluationProgress&)> progress;
};

struct RasterEvaluationResult {
  RasterEvaluationStatus status{RasterEvaluationStatus::InvalidSource};
  RasterPixelBuffer image;
  std::string diagnostic;

  [[nodiscard]] bool succeeded() const noexcept {
    return status == RasterEvaluationStatus::Success;
  }
};

/// Evaluates the supported non-destructive image graph in order. Each output
/// allocation is charged before creation; prior intermediates remain charged
/// until their replacement is complete. Cancellation never publishes a
/// partially evaluated frame.
class RasterEditEvaluator final {
 public:
  [[nodiscard]] static RasterEvaluationResult evaluate(
      const RasterPixelView& source,
      const document::RasterEditGraph& graph,
      RasterMemoryBudget& memory_budget,
      const RasterEvaluationOptions& options = {});

  /// Evaluates from ImageObject's packed RRGGBBAA words. The packed source is
  /// converted directly into one budgeted working frame without an extra
  /// interleaved staging copy.
  [[nodiscard]] static RasterEvaluationResult evaluate_packed_rgba8(
      std::span<const uint32_t> packed_rgba8,
      uint32_t width,
      uint32_t height,
      const document::RasterEditGraph& graph,
      RasterMemoryBudget& memory_budget,
      const RasterEvaluationOptions& options = {});
};

}  // namespace pikdraw::raster
