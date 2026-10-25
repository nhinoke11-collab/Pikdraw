#pragma once

#include "pikdraw/production/print/test_sheet.hpp"
#include "pikdraw/raster/evaluator.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace pikdraw::production::print {

enum class TestSheetRenderStatus : uint8_t {
    Success,
    Cancelled,
    SourceMismatch,
    InvalidPlan,
    UnsupportedVariation,
    EvaluationFailed,
    MemoryLimit,
    AllocationFailure
};

struct TestSheetRenderProgress {
    uint32_t completed_elements{0};
    uint32_t total_elements{0};
    uint32_t completed_rows{0};
    uint32_t total_rows{0};
};

struct TestSheetRenderOptions {
    uint64_t memory_limit_bytes{512ull * 1024ull * 1024ull};
    std::function<bool(uint64_t)> wait_for_memory_headroom;
    std::function<bool()> cancelled;
    std::function<void(const TestSheetRenderProgress&)> progress;
};

struct TestSheetRaster {
    uint32_t width_px{0};
    uint32_t height_px{0};
    uint32_t ppi_x{0};
    uint32_t ppi_y{0};
    double one_hundred_mm_ruler_actual_mm{0.0};
    raster::RasterPixelBuffer image;
};

struct TestSheetRenderResult {
    TestSheetRenderStatus status{TestSheetRenderStatus::InvalidPlan};
    TestSheetRaster sheet;
    std::string diagnostic;
    [[nodiscard]] bool succeeded() const noexcept {
        return status == TestSheetRenderStatus::Success;
    }
};

/// Renders a frozen test-sheet plan in encoded sRGB. The source must be the
/// exact immutable revision named by the plan. Crops are evaluated at 1:1;
/// variation IDs are restricted to supported raster-graph controls.
[[nodiscard]] TestSheetRenderResult render_test_sheet(
    const PrintTestSheetPlan& plan, const raster::RasterPixelView& source,
    const TestSheetRenderOptions& options = {});

} // namespace pikdraw::production::print
