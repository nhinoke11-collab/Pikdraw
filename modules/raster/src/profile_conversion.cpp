#include "pikdraw/raster/profile_conversion.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <span>
#include <utility>

namespace pikdraw::raster {
namespace {

constexpr uint64_t kPixelsPerBatch = 16384;

RasterProfileConversionResult failure(RasterProfileConversionStatus status,
                                       std::string diagnostic) {
  RasterProfileConversionResult result;
  result.status = status;
  result.diagnostic = std::move(diagnostic);
  return result;
}

uint16_t read_u16le(std::span<const uint8_t> bytes, size_t offset) noexcept {
  return static_cast<uint16_t>(bytes[offset]) |
      static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

uint8_t to_u8(uint16_t sample) noexcept {
  return static_cast<uint8_t>((static_cast<uint32_t>(sample) * 255u + 32767u) /
                              65535u);
}

}  // namespace

RasterProfileConversionResult convert_image_profile_to_rgb(
    const document::ImageObject& image,
    std::shared_ptr<const color::ProfileResource> destination,
    const RasterProfileConversionOptions& options) {
  try {
  if (image.width_px <= 0 || image.height_px <= 0 ||
      image.source.preview_pixel_color_basis !=
          document::RasterPixelColorBasis::EmbeddedProfileUnapplied ||
      image.source.icc_profile.empty() ||
      image.source.icc_profile.size() >
          document::RasterSourceMetadata::kMaxIccProfileBytes) {
    return failure(RasterProfileConversionStatus::InvalidSource,
        "The image must have valid decoded pixels and an unresolved embedded ICC profile.");
  }
  if (!destination || destination->identity().color_space != color::ColorSpace::Rgb) {
    return failure(RasterProfileConversionStatus::InvalidProfile,
        "The destination must be a validated RGB ICC profile.");
  }

  const uint64_t width = static_cast<uint32_t>(image.width_px);
  const uint64_t height = static_cast<uint32_t>(image.height_px);
  if (width > std::numeric_limits<uint64_t>::max() / height)
    return failure(RasterProfileConversionStatus::InvalidSource,
        "The image pixel dimensions overflow the supported range.");
  const uint64_t pixel_count = width * height;
  if (pixel_count > std::numeric_limits<size_t>::max() / sizeof(uint32_t))
    return failure(RasterProfileConversionStatus::InvalidSource,
        "The image pixel count exceeds the addressable buffer range.");
  const auto source8 = image.rgba_pixels();
  if (source8.size() != static_cast<size_t>(pixel_count))
    return failure(RasterProfileConversionStatus::InvalidSource,
        "The image RGBA8 preview does not match its dimensions.");

  const auto source16 = image.rgba16le_pixels();
  const bool high_precision = !source16.empty();
  if (high_precision && pixel_count > std::numeric_limits<size_t>::max() / 8u)
    return failure(RasterProfileConversionStatus::InvalidSource,
        "The image RGBA16 master exceeds the addressable buffer range.");
  if (high_precision && source16.size() != static_cast<size_t>(pixel_count) * 8u)
    return failure(RasterProfileConversionStatus::InvalidSource,
        "The image RGBA16 master does not match its dimensions.");

  auto source_profile = color::ProfileResource::create(
      "Embedded raster RGB", color::ColorSpace::Rgb, image.source.icc_profile);
  if (!source_profile)
    return failure(RasterProfileConversionStatus::InvalidProfile,
        "The embedded ICC profile is not a valid RGB profile.");

  auto transformed = color::ProfileColorTransform::create(
      source_profile.resource, destination, options.transform);
  const bool identical_profile = source_profile.resource->identity() ==
      destination->identity();
  if (!transformed && !identical_profile) {
    return failure(RasterProfileConversionStatus::TransformUnavailable,
        transformed.diagnostic.empty()
            ? "The requested ICC transform is unavailable."
            : std::move(transformed.diagnostic));
  }

    auto output8 = std::make_shared<std::vector<uint32_t>>(
        static_cast<size_t>(pixel_count));
    std::shared_ptr<std::vector<uint8_t>> output16;

    if (identical_profile) {
      if (high_precision)
        output16 = std::make_shared<std::vector<uint8_t>>(
            static_cast<size_t>(pixel_count) * 8u);
      for (uint64_t first = 0; first < pixel_count;) {
        if (options.cancelled && options.cancelled())
          return failure(RasterProfileConversionStatus::Cancelled,
                         "Raster profile conversion was cancelled.");
        const uint64_t count = (std::min)(kPixelsPerBatch, pixel_count - first);
        const auto begin = static_cast<size_t>(first);
        const auto end = static_cast<size_t>(first + count);
        std::copy(source8.begin() + static_cast<std::ptrdiff_t>(begin),
                  source8.begin() + static_cast<std::ptrdiff_t>(end),
                  output8->begin() + static_cast<std::ptrdiff_t>(begin));
        if (high_precision) {
          const size_t byte_begin = begin * 8u;
          const size_t byte_end = end * 8u;
          std::copy(source16.begin() + static_cast<std::ptrdiff_t>(byte_begin),
                    source16.begin() + static_cast<std::ptrdiff_t>(byte_end),
                    output16->begin() + static_cast<std::ptrdiff_t>(byte_begin));
        }
        first += count;
        if (options.progress) options.progress(first, pixel_count);
      }
    } else if (high_precision) {
      output16 = std::make_shared<std::vector<uint8_t>>(
          static_cast<size_t>(pixel_count) * 8u);
      for (uint64_t first = 0; first < pixel_count;) {
        if (options.cancelled && options.cancelled())
          return failure(RasterProfileConversionStatus::Cancelled,
                         "Raster profile conversion was cancelled.");
        const uint64_t count = (std::min)(kPixelsPerBatch, pixel_count - first);
        const auto byte_offset = static_cast<size_t>(first) * 8u;
        const auto byte_count = static_cast<size_t>(count) * 8u;
        color::IccTransformExecutionOptions execution;
        execution.cancelled = options.cancelled;
        if (!transformed.transform->convert_process16le(
                source16.subspan(byte_offset, byte_count),
                std::span<uint8_t>(*output16).subspan(byte_offset, byte_count),
                true, true, execution)) {
          if (options.cancelled && options.cancelled())
            return failure(RasterProfileConversionStatus::Cancelled,
                           "Raster profile conversion was cancelled.");
          return failure(RasterProfileConversionStatus::TransformUnavailable,
                         "The ICC engine failed to convert raster samples.");
        }
        first += count;
        if (options.progress) options.progress(first, pixel_count);
      }
      for (size_t pixel = 0; pixel < output8->size(); ++pixel) {
        const size_t offset = pixel * 8u;
        const auto channel = [&](size_t index) {
          return to_u8(read_u16le(*output16, offset + index * 2u));
        };
        (*output8)[pixel] = (static_cast<uint32_t>(channel(0)) << 24) |
            (static_cast<uint32_t>(channel(1)) << 16) |
            (static_cast<uint32_t>(channel(2)) << 8) | channel(3);
      }
    } else {
      const auto transform = transformed.transform;
      const auto batch_capacity = static_cast<size_t>(kPixelsPerBatch) * 4u;
      std::vector<uint8_t> input(batch_capacity);
      std::vector<uint8_t> converted(batch_capacity);
      for (uint64_t first = 0; first < pixel_count;) {
        if (options.cancelled && options.cancelled())
          return failure(RasterProfileConversionStatus::Cancelled,
                         "Raster profile conversion was cancelled.");
        const uint64_t count = (std::min)(kPixelsPerBatch, pixel_count - first);
        const size_t byte_count = static_cast<size_t>(count) * 4u;
        for (uint64_t index = 0; index < count; ++index) {
          const uint32_t packed = source8[static_cast<size_t>(first + index)];
          const auto offset = static_cast<size_t>(index) * 4u;
          input[offset] = static_cast<uint8_t>(packed >> 24);
          input[offset + 1] = static_cast<uint8_t>(packed >> 16);
          input[offset + 2] = static_cast<uint8_t>(packed >> 8);
          input[offset + 3] = static_cast<uint8_t>(packed);
        }
        if (!transform->convert_rgb8(
                std::span<const uint8_t>(input).first(byte_count),
                std::span<uint8_t>(converted).first(byte_count))) {
          return failure(RasterProfileConversionStatus::TransformUnavailable,
                         "The ICC engine failed to convert raster samples.");
        }
        for (uint64_t index = 0; index < count; ++index) {
          const auto offset = static_cast<size_t>(index) * 4u;
          (*output8)[static_cast<size_t>(first + index)] =
              (static_cast<uint32_t>(converted[offset]) << 24) |
              (static_cast<uint32_t>(converted[offset + 1]) << 16) |
              (static_cast<uint32_t>(converted[offset + 2]) << 8) |
              converted[offset + 3];
        }
        first += count;
        if (options.progress) options.progress(first, pixel_count);
      }
    }

    RasterProfileConversionResult result;
    result.status = RasterProfileConversionStatus::Success;
    result.rgba8_preview = std::move(output8);
    result.rgba16le_master = std::move(output16);
    return result;
  } catch (const std::bad_alloc&) {
    return failure(RasterProfileConversionStatus::AllocationFailure,
                   "Memory allocation failed while converting raster pixels.");
  } catch (...) {
    return failure(RasterProfileConversionStatus::TransformUnavailable,
                   "The ICC engine failed during raster conversion.");
  }
}

}  // namespace pikdraw::raster
