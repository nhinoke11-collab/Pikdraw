#include "pikdraw/interop/adapters/raster_image_decoder.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <vector>

using namespace pikdraw::interop;

namespace {

void append_be32(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(static_cast<uint8_t>(value >> 24));
    bytes.push_back(static_cast<uint8_t>(value >> 16));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value));
}

void append_le16(std::vector<uint8_t>& bytes, uint16_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
}

void append_le32(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value >> 16));
    bytes.push_back(static_cast<uint8_t>(value >> 24));
}

uint32_t png_crc32(const std::vector<uint8_t>& bytes) {
    uint32_t crc = 0xFFFFFFFFu;
    for (const auto byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return ~crc;
}

void append_png_chunk(std::vector<uint8_t>& png, const char type[4],
                      const std::vector<uint8_t>& payload) {
    append_be32(png, static_cast<uint32_t>(payload.size()));
    const size_t crc_start = png.size();
    for (size_t i = 0; i < 4; ++i) png.push_back(static_cast<uint8_t>(type[i]));
    png.insert(png.end(), payload.begin(), payload.end());
    std::vector<uint8_t> crc_bytes(png.begin() + static_cast<std::ptrdiff_t>(crc_start),
                                   png.end());
    append_be32(png, png_crc32(crc_bytes));
}

std::vector<uint8_t> make_png_fixture() {
    std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> header;
    append_be32(header, 2);
    append_be32(header, 1);
    header.insert(header.end(), {8, 6, 0, 0, 0}); // RGBA8, deflate, no filter, no interlace
    append_png_chunk(png, "IHDR", header);

    const std::vector<uint8_t> scanline{0, 255, 0, 0, 128, 0, 255, 0, 255};
    std::vector<uint8_t> zlib{0x78, 0x01, 0x01};
    const auto length = static_cast<uint16_t>(scanline.size());
    append_le16(zlib, length);
    append_le16(zlib, static_cast<uint16_t>(~length));
    zlib.insert(zlib.end(), scanline.begin(), scanline.end());
    uint32_t a = 1, b = 0;
    for (const auto byte : scanline) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    append_be32(zlib, (b << 16) | a);
    append_png_chunk(png, "IDAT", zlib);
    append_png_chunk(png, "IEND", {});
    return png;
}

void append_tiff_entry(std::vector<uint8_t>& tiff, uint16_t tag,
                       uint16_t type, uint32_t count, uint32_t value) {
    append_le16(tiff, tag);
    append_le16(tiff, type);
    append_le32(tiff, count);
    append_le32(tiff, value);
}

std::vector<uint8_t> make_tiff_fixture(uint16_t orientation = 1) {
    constexpr uint32_t bits_offset = 146;
    constexpr uint32_t pixels_offset = 152;
    std::vector<uint8_t> tiff{'I', 'I'};
    append_le16(tiff, 42);
    append_le32(tiff, 8);
    append_le16(tiff, 11); // IFD entry count
    append_tiff_entry(tiff, 256, 3, 1, 2); // image width, SHORT
    append_tiff_entry(tiff, 257, 3, 1, 1); // image length, SHORT
    append_tiff_entry(tiff, 258, 3, 3, bits_offset); // bits per sample
    append_tiff_entry(tiff, 259, 3, 1, 1); // no compression
    append_tiff_entry(tiff, 262, 3, 1, 2); // RGB
    append_tiff_entry(tiff, 274, 3, 1, orientation); // EXIF orientation
    append_tiff_entry(tiff, 273, 4, 1, pixels_offset); // strip offset
    append_tiff_entry(tiff, 277, 3, 1, 3); // samples per pixel
    append_tiff_entry(tiff, 278, 4, 1, 1); // rows per strip
    append_tiff_entry(tiff, 279, 4, 1, 6); // strip byte count
    append_tiff_entry(tiff, 284, 3, 1, 1); // chunky planar layout
    append_le32(tiff, 0); // no next IFD
    append_le16(tiff, 8);
    append_le16(tiff, 8);
    append_le16(tiff, 8);
    tiff.insert(tiff.end(), {255, 0, 0, 0, 255, 0}); // red, green
    return tiff;
}

void verify_pixels(const RasterImageData& image, bool expect_alpha) {
    assert(image.width_px == 2 && image.height_px == 1);
    assert(image.pixels_rgba.size() == 8);
    assert(image.pixels_rgba[0] > 240 && image.pixels_rgba[1] < 8 &&
           image.pixels_rgba[2] < 8 &&
           (expect_alpha ? image.pixels_rgba[3] >= 127 &&
                               image.pixels_rgba[3] <= 129
                         : image.pixels_rgba[3] == 255));
    assert(image.pixels_rgba[4] < 8 && image.pixels_rgba[5] > 240 &&
           image.pixels_rgba[6] < 8 && image.pixels_rgba[7] == 255);
}

void verify_external_jpegs(int count, char** paths) {
    for (int index = 0; index < count; ++index) {
        std::ifstream input(std::filesystem::path(paths[index]), std::ios::binary);
        assert(input.good());
        const std::string raw{std::istreambuf_iterator<char>(input),
                              std::istreambuf_iterator<char>()};
        const std::vector<uint8_t> bytes(raw.begin(), raw.end());
        const auto decoded = RasterImageDecoder::decode(bytes);
        if (!decoded.success)
            std::cerr << "local JPEG decode failed: " << decoded.diagnostic_code
                      << " - " << decoded.message << '\n';
        assert(decoded.success && decoded.image.format == RasterImageFormat::Jpeg);
        assert(decoded.image.width_px > 0 && decoded.image.height_px > 0 &&
               !decoded.image.pixels_rgba.empty());
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<uint8_t> png_signature{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    const std::vector<uint8_t> jpeg_signature{0xFF, 0xD8, 0xFF};
    const std::vector<uint8_t> tiff_le{'I', 'I', 0x2A, 0x00};
    const std::vector<uint8_t> tiff_be{'M', 'M', 0x00, 0x2A};
    assert(RasterImageDecoder::sniff_format(png_signature) == RasterImageFormat::Png);
    assert(RasterImageDecoder::sniff_format(jpeg_signature) == RasterImageFormat::Jpeg);
    assert(RasterImageDecoder::sniff_format(tiff_le) == RasterImageFormat::Tiff);
    assert(RasterImageDecoder::sniff_format(tiff_be) == RasterImageFormat::Tiff);
    assert(RasterImageDecoder::sniff_format({}) == RasterImageFormat::Unknown);

    RasterImageDecodeOptions invalid_budget;
    invalid_budget.max_decoded_bytes = std::numeric_limits<uint32_t>::max() + 1ULL;
    const auto invalid = RasterImageDecoder::decode(png_signature, invalid_budget);
    assert(!invalid.success && invalid.diagnostic_code == "RASTER_INVALID_BUDGET");

#ifdef _WIN32
    const auto png = make_png_fixture();
    const auto decoded_png = RasterImageDecoder::decode(png);
    assert(decoded_png.success && decoded_png.image.format == RasterImageFormat::Png);
    verify_pixels(decoded_png.image, true);

    const auto tiff = make_tiff_fixture();
    const auto decoded_tiff = RasterImageDecoder::decode(tiff);
    assert(decoded_tiff.success && decoded_tiff.image.format == RasterImageFormat::Tiff);
    verify_pixels(decoded_tiff.image, false);
    const auto oriented_tiff = RasterImageDecoder::decode(make_tiff_fixture(6));
    assert(oriented_tiff.success && oriented_tiff.image.exif_orientation == 6 &&
           oriented_tiff.image.orientation_applied);
    assert(oriented_tiff.image.width_px == 1 && oriented_tiff.image.height_px == 2);
    RasterImageDecodeOptions keep_source_orientation;
    keep_source_orientation.apply_orientation = false;
    const auto orientation_choice = RasterImageDecoder::decode(
        make_tiff_fixture(6), keep_source_orientation);
    assert(!orientation_choice.success &&
           orientation_choice.diagnostic_code == "RASTER_ORIENTATION_REQUIRES_CHOICE");

    RasterImageDecodeOptions too_small;
    too_small.max_decoded_pixels = 1;
    const auto over_budget = RasterImageDecoder::decode(png, too_small);
    assert(!over_budget.success && over_budget.diagnostic_code == "RASTER_DECODED_BUDGET");
    RasterImageDecodeOptions missing_frame;
    missing_frame.frame_index = 1;
    const auto absent_frame = RasterImageDecoder::decode(png, missing_frame);
    assert(!absent_frame.success && absent_frame.diagnostic_code == "RASTER_FRAME_NOT_FOUND");
    const auto truncated = RasterImageDecoder::decode(png_signature);
    assert(!truncated.success);
    verify_external_jpegs(argc - 1, argv + 1);
#else
    const auto unavailable = RasterImageDecoder::decode(png_signature);
    assert(!unavailable.success &&
           unavailable.diagnostic_code == "RASTER_PLATFORM_UNAVAILABLE");
    (void)make_png_fixture;
    (void)make_tiff_fixture;
    (void)verify_pixels;
    (void)verify_external_jpegs;
#endif
    std::cout << "[PASS] bounded raster decoder signatures, pixels, frames and budgets\n";
    return 0;
}
