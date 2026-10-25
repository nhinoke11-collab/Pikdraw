#include "pikdraw/production/print/tac_preflight.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <vector>

namespace pikdraw::production::print {
namespace {
constexpr uint64_t kMaxBlockPixels = 8192;
constexpr double kMaxTacPercent = 400.0;

TacPreflightReport fail(TacPreflightStatus status, std::string message) {
    TacPreflightReport report;
    report.status = status;
    report.diagnostic = std::move(message);
    return report;
}

bool cancelled(const TacPreflightOptions& options) {
    try { return options.cancelled && options.cancelled(); }
    catch (...) { return true; }
}

uint16_t u16le(const uint8_t* value) {
    return static_cast<uint16_t>(value[0] | (static_cast<uint16_t>(value[1]) << 8));
}

uint16_t expand8(uint8_t value) {
    return static_cast<uint16_t>(value * 257u);
}

} // namespace

TacPreflightReport analyze_raster_tac(
    const raster::RasterPixelView& pixels,
    std::shared_ptr<const color::ProfileResource> source_rgb_profile,
    std::shared_ptr<const color::ProfileResource> destination_cmyk_profile,
    const TacPreflightOptions& options) {
    if (!source_rgb_profile || !destination_cmyk_profile)
        return fail(TacPreflightStatus::InvalidInput,
            "TAC preflight requires explicit source and destination ICC profiles.");
    if (source_rgb_profile->identity().color_space != color::ColorSpace::Rgb ||
        destination_cmyk_profile->identity().color_space != color::ColorSpace::Cmyk)
        return fail(TacPreflightStatus::UnsupportedProfile,
            "TAC preflight currently supports an RGB source and process-CMYK destination profile only.");
    if (options.limit_percent < 100.0 || options.limit_percent > kMaxTacPercent ||
        !std::isfinite(options.limit_percent) || options.block_pixels == 0 ||
        options.block_pixels > kMaxBlockPixels || pixels.width == 0 || pixels.height == 0)
        return fail(TacPreflightStatus::InvalidInput,
            "TAC preflight requires a bounded RGB source, a CMYK destination profile, and an explicit 100-400% limit.");
    if (pixels.encoding != raster::RasterPixelEncoding::SRgbRgba8StraightAlpha &&
        pixels.encoding != raster::RasterPixelEncoding::SRgbRgba16LEStraightAlpha)
        return fail(TacPreflightStatus::InvalidInput,
                    "Unsupported source pixel encoding for TAC analysis.");
    const uint64_t bytes_per_pixel = pixels.encoding ==
        raster::RasterPixelEncoding::SRgbRgba16LEStraightAlpha ? 8u : 4u;
    if (pixels.width > std::numeric_limits<uint64_t>::max() / bytes_per_pixel ||
        pixels.row_stride < static_cast<uint64_t>(pixels.width) * bytes_per_pixel ||
        pixels.height > std::numeric_limits<uint64_t>::max() / pixels.row_stride ||
        pixels.bytes.size() < static_cast<uint64_t>(pixels.height) * pixels.row_stride)
        return fail(TacPreflightStatus::InvalidInput,
                    "Source dimensions and row-strided buffer size are inconsistent.");
    const uint64_t total_pixels = static_cast<uint64_t>(pixels.width) * pixels.height;
    if (pixels.width != 0 && total_pixels / pixels.width != pixels.height)
        return fail(TacPreflightStatus::InvalidInput, "Source pixel count overflows.");
    const auto transform = color::ProfileColorTransform::create(
        source_rgb_profile, destination_cmyk_profile,
        {options.intent, false});
    if (!transform)
        return fail(TacPreflightStatus::TransformUnavailable,
                    "The selected RGB-to-CMYK ICC transform is unavailable: " + transform.diagnostic);

    const uint64_t block_pixels = options.block_pixels;
    const uint64_t source_pixel_size = 8;
    const uint64_t destination_pixel_size = 10;
    if (block_pixels > std::numeric_limits<size_t>::max() / source_pixel_size ||
        block_pixels > std::numeric_limits<size_t>::max() / destination_pixel_size)
        return fail(TacPreflightStatus::AllocationFailure,
                    "Requested TAC block size cannot be represented in memory.");

    TacPreflightReport report;
    report.status = TacPreflightStatus::Success;
    report.source_profile_sha256 = source_rgb_profile->identity().checksum_sha256;
    report.output_profile_sha256 = destination_cmyk_profile->identity().checksum_sha256;
    std::vector<uint64_t> tac_histogram;
    std::vector<uint8_t> source_rgba16le;
    std::vector<uint8_t> destination_cmyka16le;
    try {
        tac_histogram.assign(40001, 0);
        source_rgba16le.resize(static_cast<size_t>(block_pixels * source_pixel_size));
        destination_cmyka16le.resize(static_cast<size_t>(block_pixels * destination_pixel_size));
    } catch (...) {
        return fail(TacPreflightStatus::AllocationFailure,
                    "Could not allocate the bounded TAC conversion block.");
    }

    long double tac_sum = 0.0L;
    uint64_t next_progress = 0;
    for (uint64_t start = 0; start < total_pixels;) {
        if (cancelled(options))
            return fail(TacPreflightStatus::Cancelled,
                        "TAC preflight was cancelled before publishing a partial report.");
        const uint64_t count = (std::min)(block_pixels, total_pixels - start);
        for (uint64_t index = 0; index < count; ++index) {
            const uint64_t linear = start + index;
            const uint32_t x = static_cast<uint32_t>(linear % pixels.width);
            const uint32_t y = static_cast<uint32_t>(linear / pixels.width);
            const uint64_t in_offset = static_cast<uint64_t>(y) * pixels.row_stride +
                                       static_cast<uint64_t>(x) * bytes_per_pixel;
            uint16_t rgba[4]{};
            if (pixels.encoding == raster::RasterPixelEncoding::SRgbRgba16LEStraightAlpha) {
                for (size_t c = 0; c < 4; ++c)
                    rgba[c] = u16le(pixels.bytes.data() + in_offset + c * 2);
            } else {
                for (size_t c = 0; c < 4; ++c)
                    rgba[c] = expand8(pixels.bytes[static_cast<size_t>(in_offset) + c]);
            }
            for (size_t c = 0; c < 4; ++c) {
                const uint64_t out = index * source_pixel_size + c * 2;
                source_rgba16le[out] = static_cast<uint8_t>(rgba[c]);
                source_rgba16le[out + 1] = static_cast<uint8_t>(rgba[c] >> 8);
            }
        }
        const auto source_span = std::span<const uint8_t>(source_rgba16le.data(),
            static_cast<size_t>(count * source_pixel_size));
        const auto destination_span = std::span<uint8_t>(destination_cmyka16le.data(),
            static_cast<size_t>(count * destination_pixel_size));
        color::IccTransformExecutionOptions execution;
        execution.cancelled = [&options] { return cancelled(options); };
        execution.progress = [&](uint64_t completed, uint64_t total) {
            if (options.progress) {
                try { options.progress(start + completed, total_pixels); } catch (...) { }
            }
            if (completed == total && total != 0) next_progress = start + completed;
        };
        if (!transform.transform->convert_process16le(source_span, destination_span,
                                                       true, true, execution)) {
            return fail(cancelled(options) ? TacPreflightStatus::Cancelled
                                           : TacPreflightStatus::ConversionFailed,
                cancelled(options) ? "TAC preflight was cancelled during ICC conversion."
                                   : "ICC RGB-to-CMYK transform failed; no partial report was published.");
        }
        for (uint64_t index = 0; index < count; ++index) {
            const uint64_t offset = index * destination_pixel_size;
            const uint16_t alpha = u16le(destination_cmyka16le.data() + offset + 8);
            uint32_t sum = 0;
            for (size_t channel = 0; channel < 4; ++channel) {
                uint32_t ink = u16le(destination_cmyka16le.data() + offset + channel * 2);
                ink = (ink * static_cast<uint32_t>(alpha) + 32767u) / 65535u;
                sum += ink;
            }
            const double tac = static_cast<double>(sum) * 100.0 / 65535.0;
            const size_t bin = static_cast<size_t>(std::lround(
                std::clamp(tac, 0.0, 400.0) * 100.0));
            ++tac_histogram[bin];
            tac_sum += tac;
            report.maximum_tac_percent = (std::max)(report.maximum_tac_percent, tac);
            if (tac > options.limit_percent) ++report.pixels_over_limit;
        }
        report.pixels_analyzed += count;
        start += count;
        if (options.progress && next_progress < start) {
            try { options.progress(start, total_pixels); } catch (...) { }
        }
    }
    if (report.pixels_analyzed == 0)
        return fail(TacPreflightStatus::InvalidInput, "TAC preflight did not analyze any pixels.");
    report.mean_tac_percent = static_cast<double>(tac_sum / report.pixels_analyzed);
    report.over_limit_coverage_percent = 100.0 * report.pixels_over_limit /
                                         report.pixels_analyzed;
    const uint64_t p95_rank = report.pixels_analyzed - report.pixels_analyzed / 20;
    uint64_t cumulative = 0;
    for (size_t bin = 0; bin < tac_histogram.size(); ++bin) {
        cumulative += tac_histogram[bin];
        if (cumulative >= p95_rank) {
            report.p95_tac_percent = static_cast<double>(bin) / 100.0;
            break;
        }
    }
    report.exact_pixels_over_limit = report.pixels_over_limit;
    report.diagnostic = "Full-resolution process-CMYK TAC computed under the selected ICC profile and intent; spot/white inks and independent output plates are not included.";
    return report;
}

} // namespace pikdraw::production::print
