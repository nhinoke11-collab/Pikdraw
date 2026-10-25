#include "pikdraw/interop/adapters/raster_worker_adapter.hpp"

#include "pikdraw/interop/adapters/raster_image_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace pikdraw::interop {
namespace {

constexpr size_t kRasterChunkBytes = kMaxRasterChunkPayloadBytes;

class SpanRasterByteSource final : public RasterByteSource {
public:
    explicit SpanRasterByteSource(std::span<const uint8_t> bytes) : bytes_(bytes) {}

    uint64_t size_bytes() const noexcept override { return bytes_.size(); }

    bool read_at(uint64_t offset, std::span<uint8_t> destination,
                 size_t& bytes_read) const noexcept override {
        bytes_read = 0;
        if (offset > bytes_.size()) return false;
        const size_t count = static_cast<size_t>((std::min)(
            static_cast<uint64_t>(destination.size()),
            static_cast<uint64_t>(bytes_.size()) - offset));
        if (count != 0)
            std::copy_n(bytes_.data() + static_cast<size_t>(offset), count,
                        destination.data());
        bytes_read = count;
        return true;
    }

private:
    std::span<const uint8_t> bytes_;
};

const char* media_type_for(RasterImageFormat format) noexcept {
    switch (format) {
    case RasterImageFormat::Png: return "image/png";
    case RasterImageFormat::Jpeg: return "image/jpeg";
    case RasterImageFormat::Tiff: return "image/tiff";
    case RasterImageFormat::Bmp: return "image/bmp";
    case RasterImageFormat::Unknown: break;
    }
    return "";
}

} // namespace

bool StreamRasterImportRecords(std::span<const uint8_t> source,
                               uint32_t frame_index,
                               const RasterImportRecordSink& sink,
                               std::string& diagnostic,
                               const RasterMemoryAdmissionCallback& memory_admission,
                               bool preserve_16bit) {
    const SpanRasterByteSource byte_source(source);
    return StreamRasterImportRecords(
        byte_source, frame_index, sink, diagnostic, memory_admission, preserve_16bit);
}

bool StreamRasterImportRecords(const RasterByteSource& source,
                               uint32_t frame_index,
                               const RasterImportRecordSink& sink,
                               std::string& diagnostic,
                               const RasterMemoryAdmissionCallback& memory_admission,
                               bool preserve_16bit) {
    diagnostic.clear();
    if (!sink) {
        diagnostic = "Raster proposal record sink is unavailable.";
        return false;
    }
    if (source.size_bytes() > kRasterRecordByteLimit) {
        diagnostic = "Raster source exceeds the 32-bit raster-source record representation.";
        return false;
    }
    try {
        auto memory_budget = std::make_shared<raster::RasterMemoryBudget>(
            std::numeric_limits<uint64_t>::max(),
            [&](uint64_t bytes) {
                if (!memory_admission) return true;
                return memory_admission(bytes, diagnostic);
            });
        RasterImageDecodeOptions options;
        options.max_source_bytes = kRasterRecordByteLimit;
        options.max_decoded_pixels = kRasterRecordByteLimit / 4;
        options.max_decoded_bytes = kRasterRecordByteLimit;
        options.max_icc_profile_bytes = kMaxRasterIccProfileBytes;
        options.max_frames = UINT32_MAX;
        options.frame_index = frame_index;
        options.apply_orientation = true;
        options.preserve_16bit = preserve_16bit;
        options.memory_budget = memory_budget;
        size_t record_count = 0;
        const auto add_record = [&](const ImportRecordValue& value) {
            auto encoded = SerializeImportRecord(value);
            if (!encoded || record_count >= IPC_MAX_OBJECTS || !sink(std::move(*encoded)))
                return false;
            ++record_count;
            return true;
        };

        ImportRasterImageRecord image;
        RasterIccProfileStatus icc_status = RasterIccProfileStatus::Unavailable;
        bool embedded_icc_needs_report = false;
        bool embedded_icc_pixels_unmanaged = false;
        bool metadata_emitted = false;
        uint32_t expected_row = 0;
        uint64_t total_pixel_bytes = 0;
        uint64_t next_pixel_offset = 0;
        auto pixel_chunk_reservation = memory_budget->try_reserve(kRasterChunkBytes);
        if (!pixel_chunk_reservation) {
            if (diagnostic.empty())
                diagnostic = "Raster IPC chunk could not be admitted or allocated.";
            return false;
        }
        std::vector<uint8_t> pixel_chunk;
        pixel_chunk.reserve(kRasterChunkBytes);
        const auto flush_pixel_chunk = [&]() {
            if (pixel_chunk.empty()) return true;
            auto serialized_chunk_reservation = memory_budget->try_reserve(pixel_chunk.size());
            if (!serialized_chunk_reservation) {
                if (diagnostic.empty())
                    diagnostic = "Serialized raster IPC chunk could not be admitted or allocated.";
                return false;
            }
            if (next_pixel_offset > UINT32_MAX || total_pixel_bytes > UINT32_MAX ||
                next_pixel_offset > total_pixel_bytes ||
                pixel_chunk.size() > total_pixel_bytes - next_pixel_offset) {
                diagnostic = "Raster pixel stream exceeds the current 32-bit record profile.";
                return false;
            }
            auto chunk = SerializeRasterImageChunk(1,
                static_cast<uint32_t>(next_pixel_offset),
                static_cast<uint32_t>(total_pixel_bytes), pixel_chunk);
            if (!chunk || record_count >= IPC_MAX_OBJECTS || !sink(std::move(*chunk))) {
                diagnostic = "Raster pixel chunks exceed the bounded import record profile.";
                return false;
            }
            ++record_count;
            next_pixel_offset += pixel_chunk.size();
            pixel_chunk.clear();
            return true;
        };

        const auto decoded = RasterImageDecoder::decode_rows(source, options,
            RasterImageRowCallback([&](const RasterImageData& metadata,
                                       uint32_t row_y, uint32_t width,
                                       std::span<const uint8_t> rgba) {
                const uint64_t bytes_per_pixel = 4ull *
                    (metadata.decoded_bits_per_channel / 8u);
                if (row_y != expected_row || row_y >= metadata.height_px ||
                    width != metadata.width_px ||
                    (metadata.decoded_bits_per_channel != 8 &&
                     metadata.decoded_bits_per_channel != 16) ||
                    rgba.size() != static_cast<uint64_t>(width) * bytes_per_pixel) {
                    diagnostic = "Raster decoder returned a row with invalid order or layout.";
                    return false;
                }
                if (!metadata_emitted) {
                    const auto* media_type = media_type_for(metadata.format);
                    uint64_t pixel_count = 0;
                    if (!media_type || metadata.width_px == 0 || metadata.height_px == 0 ||
                        !std::isfinite(metadata.dpi_x) || !std::isfinite(metadata.dpi_y) ||
                        metadata.dpi_x <= 0 || metadata.dpi_y <= 0 ||
                        metadata.width_px > UINT32_MAX / metadata.height_px) {
                        diagnostic = "The decoded raster dimensions, resolution or format are invalid.";
                        return false;
                    }
                    pixel_count = static_cast<uint64_t>(metadata.width_px) * metadata.height_px;
                    if (metadata.width_px > static_cast<uint32_t>(INT32_MAX) ||
                        metadata.height_px > static_cast<uint32_t>(INT32_MAX)) {
                        diagnostic = "Decoded raster dimensions exceed the native image dimension representation.";
                        return false;
                    }
                    if (pixel_count > kRasterRecordByteLimit / bytes_per_pixel) {
                        diagnostic = "Decoded raster dimensions exceed the 32-bit raster pixel record representation.";
                        return false;
                    }
                    total_pixel_bytes = pixel_count * bytes_per_pixel;
                    if (total_pixel_bytes == 0 || total_pixel_bytes > kRasterRecordByteLimit) {
                        diagnostic = "Decoded raster pixels exceed the 32-bit raster pixel record representation.";
                        return false;
                    }
                    image.local_id = 1;
                    image.source_id = "raster-image";
                    image.name = "Imported raster";
                    image.media_type = media_type;
                    image.source_data_local_id = 1;
                    image.source_bytes_size = source.size_bytes();
                    image.width_px = metadata.width_px;
                    image.height_px = metadata.height_px;
                    image.decoded_pixel_bytes = static_cast<uint32_t>(total_pixel_bytes);
                    image.decoded_bits_per_channel = metadata.decoded_bits_per_channel;
                    image.source_bits_per_pixel = metadata.source_bits_per_pixel;
                    image.frame_index = metadata.frame_index;
                    image.frame_count = metadata.frame_count;
                    image.exif_orientation = metadata.exif_orientation;
                    image.orientation_applied = metadata.orientation_applied;
                    image.dpi_x = metadata.dpi_x;
                    image.dpi_y = metadata.dpi_y;
                    image.dpi_from_source = metadata.dpi_x != 96.0 || metadata.dpi_y != 96.0;
                    switch (metadata.pixel_color_basis) {
                    case RasterPixelColorBasis::AssumedSRgb: image.preview_pixel_color_basis = 1; break;
                    case RasterPixelColorBasis::ConvertedToSRgb: image.preview_pixel_color_basis = 2; break;
                    case RasterPixelColorBasis::EmbeddedProfileUnapplied: image.preview_pixel_color_basis = 3; break;
                    }
                    icc_status = metadata.icc_profile_status;
                    const bool embedded_icc_not_preserved = metadata.has_embedded_icc_profile &&
                        (metadata.icc_profile_status != RasterIccProfileStatus::Preserved ||
                         metadata.embedded_icc_profile.size() > kMaxRasterIccProfileBytes);
                    embedded_icc_pixels_unmanaged = metadata.has_embedded_icc_profile &&
                        metadata.pixel_color_basis == RasterPixelColorBasis::EmbeddedProfileUnapplied;
                    embedded_icc_needs_report = embedded_icc_not_preserved || embedded_icc_pixels_unmanaged;
                    if (metadata.has_embedded_icc_profile &&
                        metadata.icc_profile_status == RasterIccProfileStatus::Preserved &&
                        metadata.embedded_icc_profile.size() <= kMaxRasterIccProfileBytes)
                        image.icc_profile = metadata.embedded_icc_profile;

                    const double page_width_mm =
                        static_cast<double>(metadata.width_px) * 25.4 / metadata.dpi_x;
                    const double page_height_mm =
                        static_cast<double>(metadata.height_px) * 25.4 / metadata.dpi_y;
                    if (!std::isfinite(page_width_mm) || !std::isfinite(page_height_mm) ||
                        page_width_mm <= 0 || page_height_mm <= 0 ||
                        !add_record(ImportPageRecord{page_width_mm, page_height_mm})) {
                        diagnostic = "Raster page dimensions exceed the import record profile.";
                        return false;
                    }
                    ImportLayerRecord layer;
                    layer.local_id = 1;
                    layer.source_id = "raster-layer";
                    layer.name = "Raster image";
                    layer.object_local_ids.push_back(1);
                    if (!add_record(layer) || !add_record(image)) {
                        diagnostic = "Raster metadata exceeds the bounded import record profile.";
                        return false;
                    }
                    metadata_emitted = true;
                }

                size_t row_offset = 0;
                while (row_offset < rgba.size()) {
                    const size_t available = kRasterChunkBytes - pixel_chunk.size();
                    const size_t count = (std::min)(available, rgba.size() - row_offset);
                    pixel_chunk.insert(pixel_chunk.end(),
                        rgba.begin() + static_cast<std::ptrdiff_t>(row_offset),
                        rgba.begin() + static_cast<std::ptrdiff_t>(row_offset + count));
                    row_offset += count;
                    if (pixel_chunk.size() == kRasterChunkBytes && !flush_pixel_chunk())
                        return false;
                }
                ++expected_row;
                return true;
            }));
        if (!decoded.success) {
            if (diagnostic.empty()) {
                diagnostic = decoded.message.empty()
                    ? "The raster source is outside the bounded PNG/JPEG/TIFF/BMP profile."
                    : decoded.message;
            }
            return false;
        }
        if (!metadata_emitted || expected_row != decoded.image.height_px ||
            next_pixel_offset + pixel_chunk.size() != total_pixel_bytes) {
            diagnostic = "The raster decoder did not deliver the complete selected frame.";
            return false;
        }
        if (!flush_pixel_chunk()) return false;

        uint64_t source_offset = 0;
        while (source_offset < source.size_bytes()) {
            const size_t count = static_cast<size_t>((std::min)(
                static_cast<uint64_t>(kRasterChunkBytes), source.size_bytes() - source_offset));
            pixel_chunk.resize(count);
            size_t bytes_read = 0;
            if (!source.read_at(source_offset, pixel_chunk, bytes_read) || bytes_read != count) {
                diagnostic = "Raster source bytes could not be streamed into source asset records.";
                return false;
            }
            auto serialized_source_reservation = memory_budget->try_reserve(count);
            if (!serialized_source_reservation) {
                if (diagnostic.empty())
                    diagnostic = "Serialized raster source chunk could not be admitted or allocated.";
                return false;
            }
            if (source_offset > source.size_bytes() ||
                count > source.size_bytes() - source_offset) {
                diagnostic = "Raster source stream range is invalid.";
                return false;
            }
            auto chunk = SerializeRasterSourceChunk(1,
                source_offset, source.size_bytes(), pixel_chunk);
            if (!chunk || record_count >= IPC_MAX_OBJECTS || !sink(std::move(*chunk))) {
                diagnostic = "Raster source chunks exceed the bounded import record profile.";
                return false;
            }
            ++record_count;
            source_offset += count;
            pixel_chunk.clear();
        }

        ImportFidelityIssueRecord issue;
        issue.severity = ImportFidelitySeverity::Warning;
        issue.source_locator = image.source_id;
        issue.feature_code = image.decoded_bits_per_channel == 16
            ? "raster_rgba16le_master_preserved" : "raster_rgba8_conversion";
        issue.diagnostic_code = "appearance_and_editability_limited";
        if (!add_record(issue))
            throw std::runtime_error("Raster fidelity issue exceeds the import record profile.");
        if (embedded_icc_needs_report) {
            ImportFidelityIssueRecord icc_issue;
            icc_issue.severity = ImportFidelitySeverity::Warning;
            icc_issue.source_locator = image.source_id;
            icc_issue.feature_code = "raster_embedded_icc_profile";
            switch (icc_status) {
            case RasterIccProfileStatus::ExceedsBudget:
                icc_issue.diagnostic_code = "embedded_profile_exceeds_ipc_budget";
                break;
            case RasterIccProfileStatus::MultipleContextsUnsupported:
                icc_issue.diagnostic_code = "multiple_embedded_profiles_unsupported";
                break;
            case RasterIccProfileStatus::NotPresent:
                icc_issue.diagnostic_code = "embedded_profile_not_available";
                break;
            case RasterIccProfileStatus::Preserved:
                icc_issue.diagnostic_code = embedded_icc_pixels_unmanaged
                    ? "embedded_profile_pixels_not_converted_to_srgb"
                    : "embedded_profile_exceeds_ipc_budget";
                break;
            case RasterIccProfileStatus::Unavailable:
                icc_issue.diagnostic_code = "embedded_profile_unavailable";
                break;
            }
            if (!add_record(icc_issue))
                throw std::runtime_error("Raster ICC fidelity issue exceeds the import record profile.");
        }
        ImportFidelityReportRecord report;
        report.coverage_profile = "raster-rgba8-v1";
        report.parse_outcome = ImportParseOutcome::CompleteWithDiagnostics;
        report.coverage = ImportCoverage::Partial;
        report.appearance = ImportAppearance::Partial;
        report.editability = ImportEditability::ExplicitConversion;
        report.production = ImportProduction::NotAssessed;
        report.issue_count = embedded_icc_needs_report ? 2 : 1;
        if (!add_record(report))
            throw std::runtime_error("Raster fidelity report exceeds the import record profile.");
        return true;
    } catch (const std::exception& error) {
        diagnostic = error.what();
        return false;
    } catch (...) {
        diagnostic = "Raster import record construction failed.";
        return false;
    }
}

bool BuildRasterImportRecords(std::span<const uint8_t> source,
                              std::vector<CanonicalImportRecord>& records,
                              std::string& diagnostic,
                              uint32_t frame_index) {
    records.clear();
    const bool built = StreamRasterImportRecords(source, frame_index,
        [&](CanonicalImportRecord&& record) {
            records.push_back(std::move(record));
            return records.size() <= IPC_MAX_OBJECTS;
        }, diagnostic);
    if (!built) records.clear();
    return built;
}

} // namespace pikdraw::interop
