#pragma once

#include "pikdraw/color/preview/profile_color_transform.hpp"
#include "pikdraw/raster/evaluator.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace pikdraw::production::print {

enum class TacPreflightStatus : uint8_t {
    Success,
    Cancelled,
    InvalidInput,
    UnsupportedProfile,
    TransformUnavailable,
    ConversionFailed,
    AllocationFailure
};

struct TacPreflightOptions {
    double limit_percent{0.0};
    color::IccRenderingIntent intent{color::IccRenderingIntent::RelativeColorimetric};
    uint64_t block_pixels{8192};
    std::function<bool()> cancelled;
    std::function<void(uint64_t completed_pixels, uint64_t total_pixels)> progress;
};

struct TacPreflightReport {
    TacPreflightStatus status{TacPreflightStatus::InvalidInput};
    uint64_t pixels_analyzed{0};
    uint64_t pixels_over_limit{0};
    double over_limit_coverage_percent{0.0};
    double mean_tac_percent{0.0};
    double p95_tac_percent{0.0};
    double maximum_tac_percent{0.0};
    uint64_t exact_pixels_over_limit{0};
    std::string source_profile_sha256;
    std::string output_profile_sha256;
    std::string diagnostic;
    [[nodiscard]] bool succeeded() const noexcept {
        return status == TacPreflightStatus::Success;
    }
};

/// Full-resolution per-pixel TAC analysis through the selected RGB->CMYK ICC
/// transform. Alpha is interpreted as process-ink coverage on white paper.
/// This does not generate independent output plates or evaluate spot inks.
[[nodiscard]] TacPreflightReport analyze_raster_tac(
    const raster::RasterPixelView& pixels,
    std::shared_ptr<const color::ProfileResource> source_rgb_profile,
    std::shared_ptr<const color::ProfileResource> destination_cmyk_profile,
    const TacPreflightOptions& options);

} // namespace pikdraw::production::print
