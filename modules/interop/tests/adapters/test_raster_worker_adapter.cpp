#include "pikdraw/interop/adapters/raster_worker_adapter.hpp"
#include "pikdraw/interop/adapters/raster_image_decoder.hpp"

#include <array>
#include <cassert>
#include <algorithm>
#include <cstdint>
#include <vector>

using namespace pikdraw::interop;

namespace {

void append_be32(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(static_cast<uint8_t>(value >> 24));
    bytes.push_back(static_cast<uint8_t>(value >> 16));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value));
}

uint32_t crc32(const std::vector<uint8_t>& bytes) {
    uint32_t crc = 0xFFFFFFFFu;
    for (const auto byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return ~crc;
}

void append_chunk(std::vector<uint8_t>& png, const char type[4],
                  const std::vector<uint8_t>& payload) {
    append_be32(png, static_cast<uint32_t>(payload.size()));
    const size_t crc_start = png.size();
    for (size_t i = 0; i < 4; ++i) png.push_back(static_cast<uint8_t>(type[i]));
    png.insert(png.end(), payload.begin(), payload.end());
    std::vector<uint8_t> crc_data(png.begin() + static_cast<std::ptrdiff_t>(crc_start), png.end());
    append_be32(png, crc32(crc_data));
}

std::vector<uint8_t> png_fixture() {
    std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> header;
    append_be32(header, 2);
    append_be32(header, 1);
    header.insert(header.end(), {8, 6, 0, 0, 0});
    append_chunk(png, "IHDR", header);

    const std::vector<uint8_t> scanline{0, 255, 0, 0, 255, 0, 255, 0, 128};
    std::vector<uint8_t> zlib{0x78, 0x01, 0x01};
    const auto length = static_cast<uint16_t>(scanline.size());
    zlib.push_back(static_cast<uint8_t>(length));
    zlib.push_back(static_cast<uint8_t>(length >> 8));
    const auto inverse = static_cast<uint16_t>(~length);
    zlib.push_back(static_cast<uint8_t>(inverse));
    zlib.push_back(static_cast<uint8_t>(inverse >> 8));
    zlib.insert(zlib.end(), scanline.begin(), scanline.end());
    uint32_t a = 1, b = 0;
    for (const auto byte : scanline) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    append_be32(zlib, (b << 16) | a);
    append_chunk(png, "IDAT", zlib);
    append_chunk(png, "IEND", {});
    return png;
}

std::vector<uint8_t> zlib_stored(std::vector<uint8_t> raw) {
    std::vector<uint8_t> zlib{0x78, 0x01};
    for (size_t offset = 0; offset < raw.size();) {
        const auto count = static_cast<uint16_t>((std::min)(size_t{65535}, raw.size() - offset));
        zlib.push_back(offset + count == raw.size() ? 1 : 0);
        zlib.push_back(static_cast<uint8_t>(count));
        zlib.push_back(static_cast<uint8_t>(count >> 8));
        const auto inverse = static_cast<uint16_t>(~count);
        zlib.push_back(static_cast<uint8_t>(inverse));
        zlib.push_back(static_cast<uint8_t>(inverse >> 8));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + count));
        offset += count;
    }
    uint32_t a = 1, b = 0;
    for (const auto byte : raw) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    append_be32(zlib, (b << 16) | a);
    return zlib;
}

std::vector<uint8_t> png_trns_fixture(uint8_t color_type,
                                      const std::vector<uint8_t>& scanline,
                                      const std::vector<uint8_t>& transparency,
                                      const std::vector<uint8_t>& palette = {}) {
    std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> header;
    append_be32(header, 2);
    append_be32(header, 1);
    header.insert(header.end(), {8, color_type, 0, 0, 0});
    append_chunk(png, "IHDR", header);
    if (!palette.empty()) append_chunk(png, "PLTE", palette);
    append_chunk(png, "tRNS", transparency);
    append_chunk(png, "IDAT", zlib_stored(scanline));
    append_chunk(png, "IEND", {});
    return png;
}

void test_raster_above_old_limit_and_over_budget() {
    std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> header;
    append_be32(header, 2049);
    append_be32(header, 2048);
    header.insert(header.end(), {8, 6, 0, 0, 0});
    append_chunk(png, "IHDR", header);
    std::vector<uint8_t> raw((1 + 2049u * 4) * 2048, 0);
    std::vector<uint8_t> zlib{0x78, 0x01};
    for (size_t offset = 0; offset < raw.size();) {
        const auto count = static_cast<uint16_t>((std::min)(size_t{32768}, raw.size() - offset));
        zlib.push_back(offset + count == raw.size() ? 1 : 0);
        zlib.push_back(static_cast<uint8_t>(count));
        zlib.push_back(static_cast<uint8_t>(count >> 8));
        zlib.push_back(static_cast<uint8_t>(~count));
        zlib.push_back(static_cast<uint8_t>((~count) >> 8));
        zlib.insert(zlib.end(), raw.begin() + offset, raw.begin() + offset + count);
        offset += count;
    }
    // All scanline bytes are zero, so Adler-32 is (length % 65521) << 16 | 1.
    append_be32(zlib, (static_cast<uint32_t>(raw.size() % 65521) << 16) | 1);
    append_chunk(png, "IDAT", zlib);
    append_chunk(png, "IEND", {});
    std::vector<CanonicalImportRecord> records;
    std::string diagnostic;
    assert(BuildRasterImportRecords(png, records, diagnostic));
    const auto image_record = DeserializeImportRecord(records[2].type, records[2].payload);
    assert(image_record);
    const auto& image = std::get<ImportRasterImageRecord>(*image_record);
    assert(image.width_px == 2049 && image.height_px == 2048);
    assert(image.decoded_pixel_bytes == 2049u * 2048u * 4);
    uint32_t offset = 0;
    for (const auto& record : records) {
        if (record.type != ImportRecordType::RasterImageChunk) continue;
        const auto value = DeserializeImportRecord(record.type, record.payload);
        assert(value);
        const auto& chunk = std::get<ImportRasterImageChunkRecord>(*value);
        assert(chunk.byte_offset == offset);
        offset += static_cast<uint32_t>(chunk.bytes.size());
    }
    assert(offset == image.decoded_pixel_bytes);
    png[16] = 0xFF; png[17] = 0xFF; png[18] = 0xFF; png[19] = 0xFF; // Dimensions exceed decoded pixel budget
    assert(!BuildRasterImportRecords(png, records, diagnostic));
    assert(records.empty() && !diagnostic.empty());
}

void test_24bit_source_record() {
    ImportRasterImageRecord image;
    image.local_id = 1;
    image.source_id = "rgb-24bit";
    image.name = "24-bit RGB source";
    image.media_type = "image/tiff";
    image.width_px = 1;
    image.height_px = 1;
    image.decoded_pixel_bytes = 4;
    image.source_bits_per_pixel = 24;
    image.width_mm = 10;
    image.height_mm = 10;
    image.has_placement_transform = true;
    image.source_data_local_id = 1;
    image.source_bytes_size = 3;
    const auto image_record = SerializeImportRecord(image);
    assert(image_record);
    const auto image_value = DeserializeImportRecord(
        image_record->type, image_record->payload);
    assert(image_value);
    const auto& restored_image = std::get<ImportRasterImageRecord>(*image_value);
    assert(restored_image.source_bits_per_pixel == 24);
    assert(restored_image.source_data_local_id == 1 &&
           restored_image.source_bytes_size == 3);

    ImportRasterSourceChunkRecord source_chunk;
    source_chunk.local_id = 1;
    source_chunk.total_bytes = 3;
    source_chunk.bytes = {0x11, 0x22, 0x33};
    const auto chunk_record = SerializeImportRecord(source_chunk);
    assert(chunk_record && chunk_record->type == ImportRecordType::RasterSourceChunk);
    const auto chunk_value = DeserializeImportRecord(
        chunk_record->type, chunk_record->payload);
    assert(chunk_value);
    assert(std::get<ImportRasterSourceChunkRecord>(*chunk_value).bytes ==
           source_chunk.bytes);
}

void test_png_transparency_chunks() {
    const auto rgb = png_trns_fixture(2, {0, 255, 0, 0, 0, 255, 0},
        {0, 255, 0, 0, 0, 0});
    auto decoded = RasterImageDecoder::decode(rgb);
    assert(decoded.success && decoded.image.source_bits_per_pixel == 24);
    assert(decoded.image.pixels_rgba[3] == 0 && decoded.image.pixels_rgba[7] == 255);

    const auto indexed = png_trns_fixture(3, {0, 0, 1}, {0, 128},
        {255, 0, 0, 0, 0, 255});
    decoded = RasterImageDecoder::decode(indexed);
    assert(decoded.success && decoded.image.source_bits_per_pixel == 8);
    assert(decoded.image.pixels_rgba[3] == 0 && decoded.image.pixels_rgba[7] == 128);

    const auto grayscale = png_trns_fixture(0, {0, 127, 200}, {0, 127});
    decoded = RasterImageDecoder::decode(grayscale);
    assert(decoded.success && decoded.image.pixels_rgba[3] == 0 &&
           decoded.image.pixels_rgba[7] == 255);
}

} // namespace

int main() {
#ifdef _WIN32
    test_raster_above_old_limit_and_over_budget();
    test_24bit_source_record();
    test_png_transparency_chunks();
    std::vector<CanonicalImportRecord> records;
    std::string diagnostic;
    const auto source = png_fixture();
    assert(BuildRasterImportRecords(source, records, diagnostic));
    assert(diagnostic.empty());
    assert(records.size() >= 5);
    assert(records.front().type == ImportRecordType::Page);

    bool saw_layer = false;
    bool saw_image = false;
    bool saw_pixels = false;
    bool saw_partial_report = false;
    uint32_t expected_offset = 0;
    std::array<uint8_t, 8> rgba{};
    for (const auto& record : records) {
        const auto decoded = DeserializeImportRecord(record.type, record.payload);
        assert(decoded.has_value());
        if (const auto* layer = std::get_if<ImportLayerRecord>(&*decoded)) {
            saw_layer = true;
            assert(layer->object_local_ids == std::vector<uint32_t>{1});
        } else if (const auto* image = std::get_if<ImportRasterImageRecord>(&*decoded)) {
            saw_image = true;
            assert(image->media_type == "image/png");
            assert(image->width_px == 2 && image->height_px == 1);
            assert(image->decoded_pixel_bytes == 8);
            assert(image->frame_count == 1 && image->exif_orientation == 1);
        } else if (const auto* chunk = std::get_if<ImportRasterImageChunkRecord>(&*decoded)) {
            saw_pixels = true;
            assert(chunk->byte_offset == expected_offset);
            assert(chunk->total_bytes == 8);
            std::copy(chunk->bytes.begin(), chunk->bytes.end(),
                      rgba.begin() + chunk->byte_offset);
            expected_offset += static_cast<uint32_t>(chunk->bytes.size());
        } else if (const auto* report = std::get_if<ImportFidelityReportRecord>(&*decoded)) {
            saw_partial_report = true;
            assert(report->coverage_profile == "raster-rgba8-v1");
            assert(report->coverage == ImportCoverage::Partial);
            assert(report->appearance == ImportAppearance::Partial);
            assert(report->editability == ImportEditability::ExplicitConversion);
            assert(report->production == ImportProduction::NotAssessed);
        }
    }
    assert(saw_layer && saw_image && saw_pixels && saw_partial_report);
    assert(expected_offset == 8);
    assert(rgba[3] == 255 && rgba[7] == 128);

    const std::vector<uint8_t> corrupt{0x89, 'P', 'N', 'G'};
    assert(!BuildRasterImportRecords(corrupt, records, diagnostic));
    assert(records.empty() && !diagnostic.empty());
#else
    std::vector<CanonicalImportRecord> records;
    std::string diagnostic;
    assert(!BuildRasterImportRecords(png_fixture(), records, diagnostic));
#endif
    return 0;
}
