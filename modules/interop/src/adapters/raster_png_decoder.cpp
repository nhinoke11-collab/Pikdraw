#include "pikdraw/interop/adapters/raster_image_decoder.hpp"

#ifndef _WIN32
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <vector>
#include "tinf.h"

namespace pikdraw::interop {
namespace {
RasterImageDecodeResult failure(std::string code, std::string message) {
    RasterImageDecodeResult result;
    result.diagnostic_code = std::move(code);
    result.message = std::move(message);
    return result;
}

std::shared_ptr<raster::RasterMemoryBudget::Reservation> reserve_decode_memory(
    const RasterImageDecodeOptions& options, uint64_t bytes, bool& rejected) {
    if (!options.memory_budget) return {};
    try {
        auto reservation = options.memory_budget->try_reserve(bytes);
        rejected = !reservation;
        return reservation;
    } catch (...) {
        rejected = true;
        return {};
    }
}

uint32_t read_be32(const uint8_t* p) noexcept {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

uint16_t read_be16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) |
                                 static_cast<uint16_t>(p[1]));
}

uint8_t paeth_predictor(int a, int b, int c) noexcept {
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<uint8_t>(a);
    if (pb <= pc) return static_cast<uint8_t>(b);
    return static_cast<uint8_t>(c);
}


}

RasterImageDecodeResult decode_png_pure(
    std::span<const uint8_t> source,
    const RasterImageDecodeOptions& options) {
    if (options.frame_index != 0)
        return failure("RASTER_FRAME_NOT_FOUND", "The requested PNG frame does not exist.");
    if (source.size() < 8)
        return failure("RASTER_DECODE_FAILED", "Invalid PNG source.");

    uint32_t width = 0, height = 0;
    uint8_t bit_depth = 0, color_type = 0;
    bool has_ihdr = false;
    double dpi_x = 96.0, dpi_y = 96.0;
    std::vector<uint8_t> idat_bytes;
    std::vector<uint8_t> palette;
    std::vector<uint8_t> transparency;
    bool has_transparency = false;

    size_t offset = 8;
    while (offset + 12 <= source.size()) {
        const uint32_t chunk_len = read_be32(&source[offset]);
        const uint8_t* chunk_type = &source[offset + 4];
        if (offset + 12 + chunk_len > source.size()) break;
        const auto payload = source.subspan(offset + 8, chunk_len);
        offset += 12 + chunk_len;

        if (std::memcmp(chunk_type, "IHDR", 4) == 0) {
            if (payload.size() < 13) return failure("RASTER_DECODE_FAILED", "Malformed PNG IHDR.");
            width = read_be32(payload.data());
            height = read_be32(payload.data() + 4);
            bit_depth = payload[8];
            color_type = payload[9];
            const uint8_t compression = payload[10];
            const uint8_t filter_method = payload[11];
            const uint8_t interlace = payload[12];
            if (compression != 0 || filter_method != 0 || interlace != 0)
                return failure("RASTER_DECODE_FAILED", "Unsupported PNG compression/filter/interlace.");
            has_ihdr = true;
        } else if (std::memcmp(chunk_type, "IDAT", 4) == 0) {
            idat_bytes.insert(idat_bytes.end(), payload.begin(), payload.end());
        } else if (std::memcmp(chunk_type, "PLTE", 4) == 0) {
            palette.assign(payload.begin(), payload.end());
        } else if (std::memcmp(chunk_type, "tRNS", 4) == 0) {
            if (has_transparency)
                return failure("RASTER_DECODE_FAILED", "PNG contains duplicate tRNS chunks.");
            transparency.assign(payload.begin(), payload.end());
            has_transparency = true;
        } else if (std::memcmp(chunk_type, "pHYs", 4) == 0) {
            if (payload.size() >= 9 && payload[8] == 1) {
                const uint32_t ppux = read_be32(payload.data());
                const uint32_t ppuy = read_be32(payload.data() + 4);
                if (ppux > 0 && ppuy > 0) {
                    dpi_x = static_cast<double>(ppux) * 0.0254;
                    dpi_y = static_cast<double>(ppuy) * 0.0254;
                }
            }
        } else if (std::memcmp(chunk_type, "IEND", 4) == 0) {
            break;
        }
    }

    if (!has_ihdr || width == 0 || height == 0 || idat_bytes.empty())
        return failure("RASTER_DECODE_FAILED", "Missing IHDR or IDAT in PNG.");

    if (bit_depth != 8 && bit_depth != 16)
        return failure("RASTER_BIT_DEPTH_UNSUPPORTED", "Only 8-bit and 16-bit PNG channels are supported.");

    uint32_t channels = 0;
    switch (color_type) {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 3: channels = 1; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default:
        return failure("RASTER_PIXEL_CONVERSION", "Unsupported PNG color type.");
    }

    if (color_type == 3 && bit_depth != 8)
        return failure("RASTER_BIT_DEPTH_UNSUPPORTED", "Indexed PNG must be 8-bit.");
    if (color_type == 3 && palette.empty())
        return failure("RASTER_DECODE_FAILED", "Indexed PNG missing palette.");
    if (color_type == 3 && (palette.size() % 3 != 0 || palette.size() > 256 * 3))
        return failure("RASTER_DECODE_FAILED", "Indexed PNG palette has an invalid size.");
    if (has_transparency) {
        const uint16_t max_sample = bit_depth == 16 ? UINT16_MAX : UINT8_MAX;
        const bool valid_transparency =
            (color_type == 0 && transparency.size() == 2 &&
                read_be16(transparency.data()) <= max_sample) ||
            (color_type == 2 && transparency.size() == 6 &&
                read_be16(transparency.data()) <= max_sample &&
                read_be16(transparency.data() + 2) <= max_sample &&
                read_be16(transparency.data() + 4) <= max_sample) ||
            (color_type == 3 && !transparency.empty() &&
                transparency.size() <= palette.size() / 3);
        if (!valid_transparency)
            return failure("RASTER_DECODE_FAILED", "PNG tRNS transparency does not match its color type.");
    }

    const uint64_t pixel_count = static_cast<uint64_t>(width) * height;
    const bool preserve_16bit = options.preserve_16bit && bit_depth == 16;
    if (pixel_count > UINT64_MAX / (preserve_16bit ? 12u : 4u))
        return failure("RASTER_DECODED_BUDGET", "Decoded raster dimensions exceed budget.");
    const uint64_t preview_bytes = pixel_count * 4;
    const uint64_t master_bytes = preserve_16bit ? pixel_count * 8 : 0;
    if (pixel_count > options.max_decoded_pixels ||
        preview_bytes > options.max_decoded_bytes ||
        master_bytes > options.max_decoded_bytes - preview_bytes)
        return failure("RASTER_DECODED_BUDGET", "Decoded raster dimensions exceed budget.");
    const uint64_t output_bytes = preview_bytes + master_bytes;

    const size_t bytes_per_sample = (bit_depth == 16) ? 2 : 1;
    const size_t bytes_per_pixel = channels * bytes_per_sample;
    const size_t row_raw_bytes = 1 + static_cast<size_t>(width) * bytes_per_pixel;
    if (height != 0 && row_raw_bytes > std::numeric_limits<size_t>::max() / height)
        return failure("RASTER_DECODED_BUDGET", "PNG scanline storage exceeds the addressable budget.");
    const size_t total_raw_bytes = row_raw_bytes * height;
    if (total_raw_bytes > std::numeric_limits<unsigned int>::max())
        return failure("RASTER_DECODED_BUDGET", "PNG scanline storage exceeds the codec profile.");
    bool scratch_budget_rejected = false;
    auto scratch_reservation = reserve_decode_memory(
        options, total_raw_bytes, scratch_budget_rejected);
    if (scratch_budget_rejected)
        return failure("RASTER_MEMORY_BUDGET", "PNG scanline storage exceeds the shared raster memory budget.");
    std::vector<uint8_t> uncompressed(total_raw_bytes);
    unsigned int dest_len = static_cast<unsigned int>(total_raw_bytes);
    const int tinf_res = tinf_zlib_uncompress(uncompressed.data(), &dest_len,
                                              idat_bytes.data(), static_cast<unsigned int>(idat_bytes.size()));
    if (tinf_res != TINF_OK || dest_len != total_raw_bytes)
        return failure("RASTER_DECODE_FAILED", "Failed to decompress PNG IDAT stream.");

    for (uint32_t y = 0; y < height; ++y) {
        const size_t row_offset = y * row_raw_bytes;
        const uint8_t filter = uncompressed[row_offset];
        uint8_t* curr = &uncompressed[row_offset + 1];
        const uint8_t* prev = (y > 0) ? &uncompressed[(y - 1) * row_raw_bytes + 1] : nullptr;

        const size_t line_bytes = static_cast<size_t>(width) * bytes_per_pixel;
        switch (filter) {
        case 0:
            break;
        case 1:
            for (size_t i = 0; i < line_bytes; ++i) {
                const uint8_t a = (i >= bytes_per_pixel) ? curr[i - bytes_per_pixel] : 0;
                curr[i] = static_cast<uint8_t>(curr[i] + a);
            }
            break;
        case 2:
            if (prev) {
                for (size_t i = 0; i < line_bytes; ++i) {
                    curr[i] = static_cast<uint8_t>(curr[i] + prev[i]);
                }
            }
            break;
        case 3:
            for (size_t i = 0; i < line_bytes; ++i) {
                const uint8_t a = (i >= bytes_per_pixel) ? curr[i - bytes_per_pixel] : 0;
                const uint8_t b = prev ? prev[i] : 0;
                curr[i] = static_cast<uint8_t>(curr[i] + ((a + b) >> 1));
            }
            break;
        case 4:
            for (size_t i = 0; i < line_bytes; ++i) {
                const int a = (i >= bytes_per_pixel) ? curr[i - bytes_per_pixel] : 0;
                const int b = prev ? prev[i] : 0;
                const int c = (prev && i >= bytes_per_pixel) ? prev[i - bytes_per_pixel] : 0;
                curr[i] = static_cast<uint8_t>(curr[i] + paeth_predictor(a, b, c));
            }
            break;
        default:
            return failure("RASTER_DECODE_FAILED", "Corrupt PNG scanline filter.");
        }
    }

    RasterImageData image;
    image.format = RasterImageFormat::Png;
    image.width_px = width;
    image.height_px = height;
    image.frame_count = 1;
    image.frame_index = options.frame_index;
    image.source_bits_per_pixel = channels * bit_depth;
    image.decoded_bits_per_channel = preserve_16bit ? 16 : 8;
    image.exif_orientation = 1;
    image.orientation_applied = false;
    image.dpi_x = dpi_x;
    image.dpi_y = dpi_y;
    image.icc_profile_status = RasterIccProfileStatus::NotPresent;
    bool output_budget_rejected = false;
    image.pixel_memory_reservation = reserve_decode_memory(
        options, output_bytes, output_budget_rejected);
    if (output_budget_rejected)
        return failure("RASTER_MEMORY_BUDGET", "Decoded pixels exceed the shared raster memory budget.");
    image.pixels_rgba.resize(static_cast<size_t>(preview_bytes));
    if (preserve_16bit)
        image.pixels_rgba16le.resize(static_cast<size_t>(master_bytes));

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* src_row = &uncompressed[y * row_raw_bytes + 1];
        uint8_t* dst_row = &image.pixels_rgba[static_cast<size_t>(y) * width * 4];

        if (bit_depth == 16) {
            if (color_type == 6) {
                for (uint32_t x = 0; x < width; ++x) {
                    dst_row[x * 4 + 0] = src_row[x * 8 + 0];
                    dst_row[x * 4 + 1] = src_row[x * 8 + 2];
                    dst_row[x * 4 + 2] = src_row[x * 8 + 4];
                    dst_row[x * 4 + 3] = src_row[x * 8 + 6];
                }
            } else if (color_type == 2) {
                for (uint32_t x = 0; x < width; ++x) {
                    dst_row[x * 4 + 0] = src_row[x * 6 + 0];
                    dst_row[x * 4 + 1] = src_row[x * 6 + 2];
                    dst_row[x * 4 + 2] = src_row[x * 6 + 4];
                    dst_row[x * 4 + 3] = 255;
                }
            } else if (color_type == 0) {
                for (uint32_t x = 0; x < width; ++x) {
                    const uint8_t g = src_row[x * 2 + 0];
                    dst_row[x * 4 + 0] = g;
                    dst_row[x * 4 + 1] = g;
                    dst_row[x * 4 + 2] = g;
                    dst_row[x * 4 + 3] = 255;
                }
            } else if (color_type == 4) {
                for (uint32_t x = 0; x < width; ++x) {
                    const uint8_t g = src_row[x * 4 + 0];
                    const uint8_t a = src_row[x * 4 + 2];
                    dst_row[x * 4 + 0] = g;
                    dst_row[x * 4 + 1] = g;
                    dst_row[x * 4 + 2] = g;
                    dst_row[x * 4 + 3] = a;
                }
            }
            if (preserve_16bit) {
                uint8_t* dst16 = image.pixels_rgba16le.data() +
                    static_cast<size_t>(y) * width * 8;
                const auto write_sample = [dst16](size_t channel, uint16_t value) {
                    dst16[channel * 2] = static_cast<uint8_t>(value);
                    dst16[channel * 2 + 1] = static_cast<uint8_t>(value >> 8);
                };
                for (uint32_t x = 0; x < width; ++x) {
                    uint16_t red = 0, green = 0, blue = 0, alpha = UINT16_MAX;
                    if (color_type == 6) {
                        const size_t source = static_cast<size_t>(x) * 8;
                        red = read_be16(src_row + source);
                        green = read_be16(src_row + source + 2);
                        blue = read_be16(src_row + source + 4);
                        alpha = read_be16(src_row + source + 6);
                    } else if (color_type == 2) {
                        const size_t source = static_cast<size_t>(x) * 6;
                        red = read_be16(src_row + source);
                        green = read_be16(src_row + source + 2);
                        blue = read_be16(src_row + source + 4);
                        if (has_transparency && red == read_be16(transparency.data()) &&
                            green == read_be16(transparency.data() + 2) &&
                            blue == read_be16(transparency.data() + 4))
                            alpha = 0;
                    } else if (color_type == 0) {
                        const uint16_t gray = read_be16(src_row + static_cast<size_t>(x) * 2);
                        red = green = blue = gray;
                        if (has_transparency && gray == read_be16(transparency.data()))
                            alpha = 0;
                    } else if (color_type == 4) {
                        const size_t source = static_cast<size_t>(x) * 4;
                        red = green = blue = read_be16(src_row + source);
                        alpha = read_be16(src_row + source + 2);
                    }
                    const size_t output = static_cast<size_t>(x) * 4;
                    write_sample(output, red);
                    write_sample(output + 1, green);
                    write_sample(output + 2, blue);
                    write_sample(output + 3, alpha);
                    if (alpha == 0) dst_row[output + 3] = 0;
                }
            }
        } else {
            if (color_type == 6) {
                std::memcpy(dst_row, src_row, static_cast<size_t>(width) * 4);
            } else if (color_type == 2) {
                for (uint32_t x = 0; x < width; ++x) {
                    const bool transparent = has_transparency &&
                        src_row[x * 3 + 0] == read_be16(transparency.data()) &&
                        src_row[x * 3 + 1] == read_be16(transparency.data() + 2) &&
                        src_row[x * 3 + 2] == read_be16(transparency.data() + 4);
                    dst_row[x * 4 + 0] = src_row[x * 3 + 0];
                    dst_row[x * 4 + 1] = src_row[x * 3 + 1];
                    dst_row[x * 4 + 2] = src_row[x * 3 + 2];
                    dst_row[x * 4 + 3] = transparent ? 0 : 255;
                }
            } else if (color_type == 0) {
                for (uint32_t x = 0; x < width; ++x) {
                    const uint8_t g = src_row[x];
                    dst_row[x * 4 + 0] = g;
                    dst_row[x * 4 + 1] = g;
                    dst_row[x * 4 + 2] = g;
                    dst_row[x * 4 + 3] = has_transparency &&
                        g == read_be16(transparency.data()) ? 0 : 255;
                }
            } else if (color_type == 4) {
                for (uint32_t x = 0; x < width; ++x) {
                    const uint8_t g = src_row[x * 2 + 0];
                    const uint8_t a = src_row[x * 2 + 1];
                    dst_row[x * 4 + 0] = g;
                    dst_row[x * 4 + 1] = g;
                    dst_row[x * 4 + 2] = g;
                    dst_row[x * 4 + 3] = a;
                }
            } else if (color_type == 3) {
                for (uint32_t x = 0; x < width; ++x) {
                    const size_t idx = src_row[x];
                    if (idx * 3 + 2 >= palette.size())
                        return failure("RASTER_DECODE_FAILED", "Indexed PNG pixel refers to a missing palette entry.");
                    dst_row[x * 4 + 0] = palette[idx * 3 + 0];
                    dst_row[x * 4 + 1] = palette[idx * 3 + 1];
                    dst_row[x * 4 + 2] = palette[idx * 3 + 2];
                    dst_row[x * 4 + 3] = idx < transparency.size() ? transparency[idx] : 255;
                }
            }
        }
    }

    RasterImageDecodeResult result;
    result.success = true;
    result.image = std::move(image);
    return result;
}

} // namespace pikdraw::interop
#endif