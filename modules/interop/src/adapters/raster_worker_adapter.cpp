#include "pikdraw/interop/adapters/raster_worker_adapter.hpp"

#include "pikdraw/interop/adapters/raster_image_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace pikdraw::interop {
namespace {

constexpr uint32_t kMaxRasterPixelBytes = 32u * 1024u * 1024u;
constexpr uint32_t kRasterChunkBytes = 256u * 1024u;

const char* media_type_for(RasterImageFormat format) noexcept {
    switch (format) {
    case RasterImageFormat::Png: return "image/png";
    case RasterImageFormat::Jpeg: return "image/jpeg";
    case RasterImageFormat::Tiff: return "image/tiff";
    case RasterImageFormat::Unknown: break;
    }
    return "";
}

} // namespace

bool BuildRasterImportRecords(std::span<const uint8_t> source,
                              std::vector<CanonicalImportRecord>& records,
                              std::string& diagnostic) {
    records.clear();
    diagnostic.clear();
    try {
        RasterImageDecodeOptions options;
        options.max_source_bytes = 64ULL * 1024ULL * 1024ULL;
        options.max_decoded_pixels = 8ULL * 1024ULL * 1024ULL;
        options.max_decoded_bytes = kMaxRasterPixelBytes;
        options.max_icc_profile_bytes = 256ULL * 1024ULL;
        options.max_frames = 1;
        options.frame_index = 0;
        const auto decoded = RasterImageDecoder::decode(source, options);
        if (!decoded.success) {
            diagnostic = decoded.message.empty()
                ? "The raster source is outside the bounded PNG/JPEG/TIFF profile."
                : decoded.message;
            return false;
        }
        if (decoded.image.frame_count != 1) {
            diagnostic = "Multi-frame raster import requires an explicit frame selection.";
            return false;
        }
        if (decoded.image.has_embedded_icc_profile &&
            decoded.image.icc_profile_status != RasterIccProfileStatus::Preserved) {
            diagnostic = "The embedded ICC profile exceeds the raster import budget or could not be retained.";
            return false;
        }
        const auto* media_type = media_type_for(decoded.image.format);
        if (!media_type || decoded.image.pixels_rgba.empty() ||
            decoded.image.pixels_rgba.size() > kMaxRasterPixelBytes ||
            decoded.image.pixels_rgba.size() > UINT32_MAX ||
            !std::isfinite(decoded.image.dpi_x) || !std::isfinite(decoded.image.dpi_y) ||
            decoded.image.dpi_x <= 0 || decoded.image.dpi_y <= 0) {
            diagnostic = "The decoded raster dimensions, resolution or format are invalid.";
            return false;
        }

        const double page_width_mm =
            static_cast<double>(decoded.image.width_px) * 25.4 / decoded.image.dpi_x;
        const double page_height_mm =
            static_cast<double>(decoded.image.height_px) * 25.4 / decoded.image.dpi_y;
        ImportRasterImageRecord image;
        image.local_id = 1;
        image.source_id = "raster-image";
        image.name = "Imported raster";
        image.media_type = media_type;
        image.width_px = decoded.image.width_px;
        image.height_px = decoded.image.height_px;
        image.decoded_pixel_bytes = static_cast<uint32_t>(decoded.image.pixels_rgba.size());
        image.source_bits_per_pixel = decoded.image.source_bits_per_pixel;
        image.frame_index = decoded.image.frame_index;
        image.frame_count = decoded.image.frame_count;
        image.exif_orientation = decoded.image.exif_orientation;
        image.orientation_applied = decoded.image.orientation_applied;
        image.dpi_x = decoded.image.dpi_x;
        image.dpi_y = decoded.image.dpi_y;
        image.dpi_from_source =
            decoded.image.dpi_x != 96.0 || decoded.image.dpi_y != 96.0;
        image.icc_profile = decoded.image.embedded_icc_profile;

        const auto add_record = [&](const ImportRecordValue& value) {
            auto encoded = SerializeImportRecord(value);
            if (!encoded) return false;
            records.push_back(std::move(*encoded));
            return records.size() <= 144;
        };
        if (!add_record(ImportPageRecord{page_width_mm, page_height_mm}))
            throw std::runtime_error("Raster page dimensions exceed the import profile.");
        ImportLayerRecord layer;
        layer.local_id = 1;
        layer.source_id = "raster-layer";
        layer.name = "Raster image";
        layer.object_local_ids.push_back(1);
        if (!add_record(layer) || !add_record(image))
            throw std::runtime_error("Raster metadata exceeds the bounded import record profile.");

        const auto pixels = std::span<const uint8_t>(decoded.image.pixels_rgba);
        for (size_t offset = 0; offset < pixels.size(); offset += kRasterChunkBytes) {
            const size_t length = (std::min)(static_cast<size_t>(kRasterChunkBytes),
                                              pixels.size() - offset);
            ImportRasterImageChunkRecord chunk;
            chunk.local_id = 1;
            chunk.byte_offset = static_cast<uint32_t>(offset);
            chunk.total_bytes = image.decoded_pixel_bytes;
            chunk.bytes.assign(pixels.begin() + static_cast<std::ptrdiff_t>(offset),
                               pixels.begin() + static_cast<std::ptrdiff_t>(offset + length));
            if (!add_record(chunk))
                throw std::runtime_error("Raster pixel chunks exceed the bounded import record profile.");
        }

        ImportFidelityIssueRecord issue;
        issue.severity = ImportFidelitySeverity::Warning;
        issue.source_locator = image.source_id;
        issue.feature_code = "raster_rgba8_conversion";
        issue.diagnostic_code = "appearance_and_editability_limited";
        if (!add_record(issue))
            throw std::runtime_error("Raster fidelity issue exceeds the import record profile.");
        ImportFidelityReportRecord report;
        report.coverage_profile = "raster-rgba8-v1";
        report.parse_outcome = ImportParseOutcome::CompleteWithDiagnostics;
        report.coverage = ImportCoverage::Partial;
        report.appearance = ImportAppearance::Partial;
        report.editability = ImportEditability::ExplicitConversion;
        report.production = ImportProduction::NotAssessed;
        report.issue_count = 1;
        if (!add_record(report))
            throw std::runtime_error("Raster fidelity report exceeds the import record profile.");
        return true;
    } catch (const std::exception& error) {
        records.clear();
        diagnostic = error.what();
        return false;
    } catch (...) {
        records.clear();
        diagnostic = "Raster import record construction failed.";
        return false;
    }
}

} // namespace pikdraw::interop
