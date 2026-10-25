#include "pikdraw/desktop/shell/raster_bitmap_export.hpp"

#include "pikdraw/color/profiles/profile_resource.hpp"
#include "pikdraw/color/preview/profile_color_transform.hpp"
#include "pikdraw/interop/writers/raster_image_writer.hpp"
#include "pikdraw/raster/edit_geometry.hpp"
#include "pikdraw/raster/evaluator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace pikdraw::desktop {
namespace {

constexpr uint64_t kScratchReserveBytes = 16ULL * 1024ULL * 1024ULL;

RasterBitmapExportResult fail(std::string diagnostic) {
    RasterBitmapExportResult result;
    result.diagnostic = std::move(diagnostic);
    return result;
}

bool validate_or_reserve(uint64_t bytes,
                         const RasterBitmapExportOptions& options) {
    return !options.wait_for_memory_headroom ||
        options.wait_for_memory_headroom(bytes);
}

uint16_t read_u16le(std::span<const uint8_t> bytes, size_t offset) noexcept {
    return static_cast<uint16_t>(bytes[offset] |
        (static_cast<uint16_t>(bytes[offset + 1]) << 8));
}

void write_u16le(std::span<uint8_t> bytes, size_t offset,
                 uint16_t value) noexcept {
    bytes[offset] = static_cast<uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

double srgb_to_linear(double value) noexcept {
    return value <= 0.04045 ? value / 12.92
        : std::pow((value + 0.055) / 1.055, 2.4);
}

double linear_to_srgb(double value) noexcept {
    return value <= 0.0031308 ? value * 12.92
        : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

std::shared_ptr<const color::ProfileResource> output_profile_for(
    const document::ImageObject& image,
    const RasterBitmapExportOptions& options,
    std::string& diagnostic) {
    if (image.source.preview_pixel_color_basis ==
            document::RasterPixelColorBasis::EmbeddedProfileUnapplied) {
        if (image.source.icc_profile.empty() ||
            image.source.icc_profile.size() > document::RasterSourceMetadata::kMaxIccProfileBytes) {
            diagnostic = "The image's source ICC profile is missing or exceeds the preserved profile limit.";
            return {};
        }
        auto profile = color::ProfileResource::create(
            "Embedded raster source profile", color::ColorSpace::Rgb,
            image.source.icc_profile);
        if (!profile) {
            diagnostic = "The image's source ICC profile is not a valid RGB profile.";
            return {};
        }
        return std::move(profile.resource);
    }
    if (image.source.preview_pixel_color_basis ==
            document::RasterPixelColorBasis::LegacyUnknown) {
        diagnostic = "The image's pixel color basis is unknown; resolve its source profile before export.";
        return {};
    }
    if (options.srgb_profile.empty() || options.srgb_profile.size() > 1024u * 1024u) {
        diagnostic = "A valid embedded sRGB profile is required for raster master export.";
        return {};
    }
    auto profile = color::ProfileResource::create(
        "sRGB output profile", color::ColorSpace::Rgb,
        std::vector<uint8_t>(options.srgb_profile.begin(), options.srgb_profile.end()));
    if (!profile) {
        diagnostic = "The configured sRGB profile is invalid.";
        return {};
    }
    return std::move(profile.resource);
}

} // namespace

RasterBitmapExportResult export_raster_bitmap_master_tiff(
    const document::ImageObject& image, std::wstring_view output_path,
    const RasterBitmapExportOptions& options) {
    if (output_path.empty() || image.width_px <= 0 || image.height_px <= 0)
        return fail("The output path or image dimensions are invalid.");
    if (options.cancelled && options.cancelled())
        return fail("Raster master export was cancelled.");

    std::string profile_error;
    const auto profile = output_profile_for(image, options, profile_error);
    if (!profile) return fail(std::move(profile_error));

    const auto pixels = image.rgba_pixels();
    const auto pixels16 = image.rgba16le_pixels();
    const uint64_t pixel_count = static_cast<uint64_t>(image.width_px) *
                                 static_cast<uint64_t>(image.height_px);
    const bool high_precision = !pixels16.empty();
    const uint64_t bytes_per_pixel = high_precision ? 8u : 4u;
    if (pixel_count > SIZE_MAX / bytes_per_pixel ||
        pixels.size() != pixel_count ||
        (high_precision && pixels16.size() != pixel_count * bytes_per_pixel))
        return fail("The image dimensions do not match its RGBA pixel buffer.");

    const uint64_t source_bytes = pixel_count * bytes_per_pixel;
    if (source_bytes > UINT64_MAX - kScratchReserveBytes ||
        !validate_or_reserve(source_bytes + kScratchReserveBytes, options))
        return fail("Raster master export was cancelled or timed out waiting for system memory.");

    std::vector<uint8_t> rgba8;
    std::span<const uint8_t> source_samples;
    if (high_precision) {
        source_samples = pixels16;
    } else {
        try {
            rgba8.resize(static_cast<size_t>(source_bytes));
        } catch (...) {
            return fail("Could not allocate the 8-bit raster export buffer.");
        }
        for (size_t index = 0; index < pixels.size(); ++index) {
            const uint32_t packed = pixels[index];
            const size_t offset = index * 4;
            rgba8[offset] = static_cast<uint8_t>(packed >> 24);
            rgba8[offset + 1] = static_cast<uint8_t>(packed >> 16);
            rgba8[offset + 2] = static_cast<uint8_t>(packed >> 8);
            rgba8[offset + 3] = static_cast<uint8_t>(packed);
        }
        source_samples = rgba8;
    }

    const bool has_enabled_nodes = std::any_of(
        image.edit_graph.nodes.begin(), image.edit_graph.nodes.end(),
        [](const document::RasterEditNode& node) { return node.enabled; });
    if (has_enabled_nodes && raster::raster_edit_requires_srgb_processing(image.edit_graph) &&
        image.source.preview_pixel_color_basis != document::RasterPixelColorBasis::AssumedSRgb &&
        image.source.preview_pixel_color_basis != document::RasterPixelColorBasis::ConvertedToSRgb)
        return fail("Color edits require source pixels resolved to sRGB before export.");

    uint32_t output_width = static_cast<uint32_t>(image.width_px);
    uint32_t output_height = static_cast<uint32_t>(image.height_px);
    std::vector<uint8_t> evaluated_bytes;
    std::span<const uint8_t> output_samples = source_samples;
    if (has_enabled_nodes) {
        std::unique_ptr<raster::RasterMemoryBudget> budget;
        try {
            budget = std::make_unique<raster::RasterMemoryBudget>(
                std::numeric_limits<uint64_t>::max(),
                [&](uint64_t requested_bytes) {
                    const uint64_t reserved = budget ? budget->reserved_bytes() : 0;
                    if (requested_bytes > UINT64_MAX - reserved ||
                        requested_bytes + reserved > UINT64_MAX - kScratchReserveBytes)
                        return false;
                    return validate_or_reserve(
                        requested_bytes + reserved + kScratchReserveBytes, options);
                });
        } catch (...) {
            return fail("Could not create the raster edit memory budget.");
        }
        const raster::RasterPixelEncoding encoding = high_precision
            ? raster::RasterPixelEncoding::SRgbRgba16LEStraightAlpha
            : raster::RasterPixelEncoding::SRgbRgba8StraightAlpha;
        raster::RasterPixelView source_view{
            source_samples, static_cast<uint32_t>(image.width_px),
            static_cast<uint32_t>(image.height_px),
            static_cast<uint64_t>(image.width_px) * bytes_per_pixel, encoding};
        raster::RasterEvaluationOptions evaluation_options;
        evaluation_options.cancelled = options.cancelled;
        auto evaluated = raster::RasterEditEvaluator::evaluate(
            source_view, image.edit_graph, *budget, evaluation_options);
        if (!evaluated.succeeded())
            return fail(evaluated.diagnostic.empty()
                ? "The raster edit graph could not be evaluated for export."
                : std::move(evaluated.diagnostic));
        if (evaluated.image.width == 0 || evaluated.image.height == 0 ||
            evaluated.image.encoding != encoding)
            return fail("The raster evaluator returned an unsupported export buffer.");
        output_width = evaluated.image.width;
        output_height = evaluated.image.height;
        evaluated_bytes = std::move(evaluated.image.bytes);
        output_samples = evaluated_bytes;
    }

    if (options.cancelled && options.cancelled())
        return fail("Raster master export was cancelled.");
    double dpi_x = image.source.dpi_x;
    double dpi_y = image.source.dpi_y;
    if (!std::isfinite(dpi_x) || dpi_x <= 0.0) dpi_x = 96.0;
    if (!std::isfinite(dpi_y) || dpi_y <= 0.0) dpi_y = 96.0;
    if (!options.cmyk_output_profile.empty()) {
        if (options.cmyk_output_profile.size() > 1024u * 1024u)
            return fail("The destination CMYK ICC profile exceeds the output profile limit.");
        auto destination = color::ProfileResource::create(
            "Raster CMYK output profile", color::ColorSpace::Cmyk,
            std::vector<uint8_t>(options.cmyk_output_profile.begin(),
                                 options.cmyk_output_profile.end()));
        if (!destination)
            return fail("The destination profile is not a valid CMYK ICC resource.");
        auto transform_result = color::ProfileColorTransform::create(
            profile, destination.resource,
            {options.cmyk_rendering_intent, false});
        if (!transform_result)
            return fail(transform_result.diagnostic.empty()
                ? "The ICC engine could not create the requested RGB-to-CMYK transform."
                : std::move(transform_result.diagnostic));

        if (options.srgb_profile.empty() || options.srgb_profile.size() > 1024u * 1024u)
            return fail("A valid RGB working profile is required to flatten transparency for CMYK output.");
        auto working_profile = color::ProfileResource::create(
            "Raster RGB working profile", color::ColorSpace::Rgb,
            std::vector<uint8_t>(options.srgb_profile.begin(),
                                 options.srgb_profile.end()));
        if (!working_profile)
            return fail("The RGB working profile for CMYK output is invalid.");

        const uint64_t output_pixel_count =
            static_cast<uint64_t>(output_width) * output_height;
        if (output_pixel_count > SIZE_MAX / 30u || output_pixel_count > UINT64_MAX / 30u)
            return fail("CMYK16 output dimensions exceed the addressable buffer range.");
        const uint64_t cmyk_bytes = output_pixel_count * 8u;
        const uint64_t transform_bytes = output_pixel_count * (high_precision ? 14u : 22u);
        constexpr uint64_t transform_scratch = 2ULL * 1024ULL * 1024ULL;
        if (transform_bytes > UINT64_MAX - cmyk_bytes - transform_scratch ||
            !validate_or_reserve(transform_bytes + cmyk_bytes + transform_scratch, options))
            return fail("CMYK output was cancelled or could not reserve transform memory.");

        std::vector<uint8_t> expanded_rgba16le;
        std::vector<uint8_t> converted_srgb16le;
        std::vector<uint8_t> flattened_rgb16le;
        std::vector<uint8_t> cmyk16le;
        std::span<const uint8_t> source_rgba16le = output_samples;
        try {
            if (high_precision) {
                if (output_samples.size() != output_pixel_count * 8u)
                    return fail("The evaluated RGBA16 buffer size is invalid.");
            } else {
                if (output_samples.size() != output_pixel_count * 4u)
                    return fail("The evaluated RGBA8 buffer size is invalid.");
                expanded_rgba16le.resize(static_cast<size_t>(output_pixel_count * 8u));
                for (size_t index = 0; index < output_samples.size(); ++index) {
                    const uint16_t expanded = static_cast<uint16_t>(output_samples[index] * 257u);
                    expanded_rgba16le[index * 2] = static_cast<uint8_t>(expanded & 0xffu);
                    expanded_rgba16le[index * 2 + 1] = static_cast<uint8_t>(expanded >> 8);
                }
                source_rgba16le = expanded_rgba16le;
            }
            cmyk16le.resize(static_cast<size_t>(cmyk_bytes));
        } catch (...) {
            return fail("Could not allocate the CMYK conversion buffers.");
        }
        color::IccTransformExecutionOptions execution;
        execution.cancelled = options.cancelled;

        std::span<const uint8_t> working_rgba16le = source_rgba16le;
        const auto& source_profile_bytes = profile->bytes();
        const auto& working_profile_bytes = working_profile.resource->bytes();
        const bool already_in_working_rgb =
            source_profile_bytes.size() == working_profile_bytes.size() &&
            std::equal(source_profile_bytes.begin(), source_profile_bytes.end(),
                       working_profile_bytes.begin());
        if (!already_in_working_rgb) {
            auto source_to_working = color::ProfileColorTransform::create(
                profile, working_profile.resource,
                {color::IccRenderingIntent::RelativeColorimetric, false});
            if (!source_to_working)
                return fail(source_to_working.diagnostic.empty()
                    ? "The ICC engine could not resolve source pixels into the RGB working profile."
                    : std::move(source_to_working.diagnostic));
            try {
                converted_srgb16le.resize(source_rgba16le.size());
            } catch (...) {
                return fail("Could not allocate the RGB working conversion buffer.");
            }
            if (!source_to_working.transform->convert_process16le(
                    source_rgba16le, converted_srgb16le, true, true, execution))
                return fail(options.cancelled && options.cancelled()
                    ? "CMYK output conversion was cancelled."
                    : "The ICC engine could not resolve source pixels into the RGB working profile.");
            working_rgba16le = converted_srgb16le;
        }

        try {
            flattened_rgb16le.resize(static_cast<size_t>(output_pixel_count * 6u));
        } catch (...) {
            return fail("Could not allocate the flattened RGB16 output buffer.");
        }
        for (uint64_t pixel = 0; pixel < output_pixel_count; ++pixel) {
            if ((pixel & 8191u) == 0u && options.cancelled && options.cancelled())
                return fail("CMYK output conversion was cancelled.");
            const size_t source_offset = static_cast<size_t>(pixel * 8u);
            const size_t destination_offset = static_cast<size_t>(pixel * 6u);
            const double alpha = read_u16le(working_rgba16le, source_offset + 6u) / 65535.0;
            for (size_t channel = 0; channel < 3; ++channel) {
                const uint16_t source = read_u16le(working_rgba16le,
                                                    source_offset + channel * 2u);
                uint16_t flattened = 65535u;
                if (alpha >= 1.0) {
                    flattened = source;
                } else if (alpha > 0.0) {
                    const double linear = srgb_to_linear(source / 65535.0);
                    const double composite = linear * alpha + (1.0 - alpha);
                    flattened = static_cast<uint16_t>(std::lround(
                        std::clamp(linear_to_srgb(composite), 0.0, 1.0) * 65535.0));
                }
                write_u16le(flattened_rgb16le, destination_offset + channel * 2u,
                            flattened);
            }
        }
        if (!transform_result.transform->convert_process16le(
                flattened_rgb16le, cmyk16le, false, false, execution))
            return fail(options.cancelled && options.cancelled()
                ? "CMYK output conversion was cancelled."
                : "The ICC engine failed to convert the flattened raster master to CMYK16.");
        if (options.cancelled && options.cancelled())
            return fail("CMYK output conversion was cancelled.");

        interop::RasterCmykImageWriteOptions cmyk_write_options;
        cmyk_write_options.dpi_x = dpi_x;
        cmyk_write_options.dpi_y = dpi_y;
        cmyk_write_options.embedded_icc_profile = destination.resource->bytes();
        cmyk_write_options.allow_replace_existing = options.allow_replace_existing;
        cmyk_write_options.wait_for_memory_headroom = options.wait_for_memory_headroom;
        const auto written = interop::WriteRasterImageCmykFile16(
            output_path, output_width, output_height, cmyk16le, cmyk_write_options);
        if (!written.success) return fail(written.diagnostic);

        RasterBitmapExportResult result;
        result.success = true;
        result.width_px = output_width;
        result.height_px = output_height;
        result.bits_per_channel = 16;
        result.cmyk_output = true;
        return result;
    }

    interop::RasterImageWriteOptions write_options;
    write_options.format = interop::RasterImageOutputFormat::Tiff;
    write_options.dpi_x = dpi_x;
    write_options.dpi_y = dpi_y;
    write_options.embedded_icc_profile = profile->bytes();
    write_options.allow_replace_existing = options.allow_replace_existing;
    write_options.wait_for_memory_headroom = options.wait_for_memory_headroom;
    const auto written = high_precision
        ? interop::WriteRasterImageFile16(
            output_path, output_width, output_height, output_samples, write_options)
        : interop::WriteRasterImageFile(
            output_path, output_width, output_height, output_samples, write_options);
    if (!written.success) return fail(written.diagnostic);

    RasterBitmapExportResult result;
    result.success = true;
    result.width_px = output_width;
    result.height_px = output_height;
    result.bits_per_channel = high_precision ? 16 : 8;
    return result;
}

} // namespace pikdraw::desktop
