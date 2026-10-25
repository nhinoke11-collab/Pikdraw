#include "pikdraw/interop/adapters/raster_worker_adapter.hpp"
#include "pikdraw/interop/adapters/raster_image_decoder.hpp"

#include <array>
#include <cassert>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace pikdraw::interop;

namespace {

void test_raster_chunk_span_serializer() {
    const std::array<uint8_t, 8> pixels{1, 2, 3, 4, 5, 6, 7, 8};
    const auto direct = SerializeRasterImageChunk(7, 4, 12, pixels);
    ImportRasterImageChunkRecord owned;
    owned.local_id = 7;
    owned.byte_offset = 4;
    owned.total_bytes = 12;
    owned.bytes.assign(pixels.begin(), pixels.end());
    const auto generic = SerializeImportRecord(ImportRecordValue{owned});
    assert(direct && generic && direct->type == ImportRecordType::RasterImageChunk &&
           direct->payload == generic->payload);

    const auto source = SerializeRasterSourceChunk(9, 0, 8, pixels);
    ImportRasterSourceChunkRecord source_owned;
    source_owned.local_id = 9;
    source_owned.total_bytes = 8;
    source_owned.bytes.assign(pixels.begin(), pixels.end());
    const auto source_generic = SerializeImportRecord(ImportRecordValue{source_owned});
    assert(source && source_generic &&
           source->type == ImportRecordType::RasterSourceChunk &&
           source->payload == source_generic->payload);
    constexpr uint64_t large_source_bytes = uint64_t{1} << 32;
    const auto wide_chunk = SerializeRasterSourceChunk(9, large_source_bytes - 8,
                                                        large_source_bytes, pixels);
    assert(wide_chunk && wide_chunk->type == ImportRecordType::RasterSourceChunk64);
    const auto restored_wide_chunk = DeserializeImportRecord(wide_chunk->type,
                                                             wide_chunk->payload);
    assert(restored_wide_chunk);
    const auto& wide_value = std::get<ImportRasterSourceChunkRecord>(*restored_wide_chunk);
    assert(wide_value.byte_offset == large_source_bytes - 8 &&
           wide_value.total_bytes == large_source_bytes && wide_value.bytes.size() == pixels.size());
    assert(!SerializeRasterImageChunk(0, 0, 8, pixels));
    assert(!SerializeRasterSourceChunk(9, 9, 8, pixels));

    ImportRasterImageRecord image;
    image.local_id = 1;
    image.source_id = "raster-image";
    image.name = "ICC bounds";
    image.media_type = "image/png";
    image.width_px = 1;
    image.height_px = 1;
    image.decoded_pixel_bytes = 4;
    image.source_bits_per_pixel = 32;
    image.icc_profile.resize(kMaxRasterIccProfileBytes, 0x5a);
    const auto image_record = SerializeImportRecord(ImportRecordValue{image});
    assert(image_record);
    const auto decoded_image = DeserializeImportRecord(image_record->type, image_record->payload);
    assert(decoded_image && std::get<ImportRasterImageRecord>(*decoded_image).icc_profile == image.icc_profile);
    image.icc_profile.push_back(0);
    assert(!SerializeImportRecord(ImportRecordValue{image}));
}

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

void append_tiff_entry(std::vector<uint8_t>& tiff, uint16_t tag,
                       uint16_t type, uint32_t count, uint32_t value) {
    append_le16(tiff, tag);
    append_le16(tiff, type);
    append_le32(tiff, count);
    append_le32(tiff, value);
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

std::vector<uint8_t> png16_fixture() {
    std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> header;
    append_be32(header, 1);
    append_be32(header, 1);
    header.insert(header.end(), {16, 6, 0, 0, 0});
    append_chunk(png, "IHDR", header);

    const std::array<uint8_t, 9> scanline{
        0, 0x12, 0x34, 0xab, 0xcd, 0x00, 0xff, 0xff, 0xff};
    std::vector<uint8_t> zlib{0x78, 0x01, 0x01};
    append_le16(zlib, static_cast<uint16_t>(scanline.size()));
    append_le16(zlib, static_cast<uint16_t>(~scanline.size()));
    zlib.insert(zlib.end(), scanline.begin(), scanline.end());
    uint32_t a = 1, b = 0;
    for (const uint8_t byte : scanline) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    append_be32(zlib, (b << 16) | a);
    append_chunk(png, "IDAT", zlib);
    append_chunk(png, "IEND", {});
    return png;
}

void test_raster_rgba16le_import_records() {
    const auto source = png16_fixture();
    std::array<uint8_t, 8> expected{0x34, 0x12, 0xcd, 0xab, 0xff, 0x00, 0xff, 0xff};
    std::array<uint8_t, 8> actual{};
    uint32_t next_offset = 0;
    bool saw_image = false;
    std::string diagnostic;
    const bool streamed = StreamRasterImportRecords(source, 0,
        [&](CanonicalImportRecord&& record) {
            const auto decoded = DeserializeImportRecord(record.type, record.payload);
            assert(decoded);
            if (const auto* image = std::get_if<ImportRasterImageRecord>(&*decoded)) {
                assert(image->width_px == 1 && image->height_px == 1);
                assert(image->decoded_bits_per_channel == 16);
                assert(image->decoded_pixel_bytes == expected.size());
                saw_image = true;
            } else if (const auto* chunk =
                           std::get_if<ImportRasterImageChunkRecord>(&*decoded)) {
                assert(chunk->byte_offset == next_offset &&
                       chunk->total_bytes == expected.size());
                std::copy(chunk->bytes.begin(), chunk->bytes.end(),
                    actual.begin() + chunk->byte_offset);
                next_offset += static_cast<uint32_t>(chunk->bytes.size());
            }
            return true;
        }, diagnostic, {}, true);
    assert(streamed && diagnostic.empty() && saw_image);
    assert(next_offset == expected.size() && actual == expected);
}

std::vector<uint8_t> multi_page_tiff_fixture(uint32_t page_count) {
    constexpr uint32_t ifd_bytes = 2 + 11 * 12 + 4;
    const uint32_t bits_offset = 8 + page_count * ifd_bytes;
    const uint32_t first_pixels = bits_offset + 6;
    std::vector<uint8_t> tiff{'I', 'I'};
    append_le16(tiff, 42);
    append_le32(tiff, 8);
    for (uint32_t page = 0; page < page_count; ++page) {
        append_le16(tiff, 11);
        append_tiff_entry(tiff, 256, 3, 1, 2);
        append_tiff_entry(tiff, 257, 3, 1, 1);
        append_tiff_entry(tiff, 258, 3, 3, bits_offset);
        append_tiff_entry(tiff, 259, 3, 1, 1);
        append_tiff_entry(tiff, 262, 3, 1, 2);
        append_tiff_entry(tiff, 274, 3, 1, 1);
        append_tiff_entry(tiff, 273, 4, 1, first_pixels + page * 6);
        append_tiff_entry(tiff, 277, 3, 1, 3);
        append_tiff_entry(tiff, 278, 4, 1, 1);
        append_tiff_entry(tiff, 279, 4, 1, 6);
        append_tiff_entry(tiff, 284, 3, 1, 1);
        const uint32_t next_ifd = page + 1 < page_count
            ? 8 + (page + 1) * ifd_bytes : 0;
        append_le32(tiff, next_ifd);
    }
    append_le16(tiff, 8);
    append_le16(tiff, 8);
    append_le16(tiff, 8);
    for (uint32_t page = 0; page < page_count; ++page)
        tiff.insert(tiff.end(), {static_cast<uint8_t>(page), 0, 0,
                                 0, static_cast<uint8_t>(page), 0});
    return tiff;
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

class VirtualBmpByteSource final : public RasterByteSource {
public:
    VirtualBmpByteSource(uint32_t width, uint32_t height) {
        const uint64_t stride = ((static_cast<uint64_t>(width) * 3 + 3) / 4) * 4;
        const uint64_t pixel_bytes = stride * height;
        size_bytes_ = 54 + pixel_bytes;
        header_.insert(header_.end(), {'B', 'M'});
        append_le32(header_, static_cast<uint32_t>(size_bytes_));
        append_le32(header_, 0);
        append_le32(header_, 54);
        append_le32(header_, 40);
        append_le32(header_, width);
        append_le32(header_, height);
        append_le16(header_, 1);
        append_le16(header_, 24);
        append_le32(header_, 0);
        append_le32(header_, static_cast<uint32_t>(pixel_bytes));
        append_le32(header_, 3780);
        append_le32(header_, 3780);
        append_le32(header_, 0);
        append_le32(header_, 0);
        assert(header_.size() == 54);
    }

    uint64_t size_bytes() const noexcept override { return size_bytes_; }

    bool read_at(uint64_t offset, std::span<uint8_t> destination,
                 size_t& bytes_read) const noexcept override {
        bytes_read = 0;
        if (offset > size_bytes_) return false;
        const size_t count = static_cast<size_t>((std::min)(
            static_cast<uint64_t>(destination.size()), size_bytes_ - offset));
        std::fill_n(destination.data(), count, uint8_t{0});
        if (offset < header_.size()) {
            const size_t header_offset = static_cast<size_t>(offset);
            const size_t header_count = (std::min)(count, header_.size() - header_offset);
            std::copy_n(header_.data() + header_offset, header_count, destination.data());
        }
        bytes_read = count;
        return true;
    }

private:
    std::vector<uint8_t> header_;
    uint64_t size_bytes_{0};
};

void test_raster_source_and_frame_above_old_fixed_limits() {
    constexpr uint32_t width = 7400;
    constexpr uint32_t height = 7400;
    constexpr uint64_t mib = 1024ull * 1024ull;
    VirtualBmpByteSource source(width, height);
    const uint64_t decoded_bytes = static_cast<uint64_t>(width) * height * 4;
    assert(source.size_bytes() > 128 * mib);
    assert(decoded_bytes > 192 * mib);
    assert(source.size_bytes() + decoded_bytes < 512 * mib);

    bool saw_image = false;
    uint64_t pixel_chunk_bytes = 0;
    uint64_t source_chunk_bytes = 0;
    std::string diagnostic;
    const bool streamed = StreamRasterImportRecords(source, 0,
        [&](CanonicalImportRecord&& record) {
            if (record.type == ImportRecordType::RasterImage) {
                const auto value = DeserializeImportRecord(record.type, record.payload);
                assert(value);
                const auto& image = std::get<ImportRasterImageRecord>(*value);
                assert(image.width_px == width && image.height_px == height);
                assert(image.decoded_pixel_bytes == decoded_bytes);
                assert(image.source_bytes_size == source.size_bytes());
                saw_image = true;
            } else if (record.type == ImportRecordType::RasterImageChunk) {
                const auto value = DeserializeImportRecord(record.type, record.payload);
                assert(value);
                pixel_chunk_bytes += std::get<ImportRasterImageChunkRecord>(*value).bytes.size();
            } else if (record.type == ImportRecordType::RasterSourceChunk) {
                const auto value = DeserializeImportRecord(record.type, record.payload);
                assert(value);
                source_chunk_bytes += std::get<ImportRasterSourceChunkRecord>(*value).bytes.size();
            }
            return true;
        }, diagnostic);
    assert(streamed && diagnostic.empty() && saw_image);
    assert(pixel_chunk_bytes == decoded_bytes);
    assert(source_chunk_bytes == source.size_bytes());
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
    png[16] = 0xFF; png[17] = 0xFF; png[18] = 0xFF; png[19] = 0xFF; // Exceeds the 32-bit raster record representation
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
    image.preview_pixel_color_basis = 2;
    const auto image_record = SerializeImportRecord(image);
    assert(image_record);
    const auto image_value = DeserializeImportRecord(
        image_record->type, image_record->payload);
    assert(image_value);
    const auto& restored_image = std::get<ImportRasterImageRecord>(*image_value);
    assert(restored_image.source_bits_per_pixel == 24);
    assert(restored_image.source_data_local_id == 1 &&
           restored_image.source_bytes_size == 3);
    assert(restored_image.preview_pixel_color_basis == 2);

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

    image.source_bytes_size = uint64_t{1} << 32;
    const auto wide_image_record = SerializeImportRecord(image);
    assert(wide_image_record && wide_image_record->type == ImportRecordType::RasterImage64);
    const auto wide_image_value = DeserializeImportRecord(wide_image_record->type,
                                                           wide_image_record->payload);
    assert(wide_image_value && std::get<ImportRasterImageRecord>(*wide_image_value).source_bytes_size ==
           image.source_bytes_size);
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

#ifdef _WIN32
void test_raster_stream_routes_every_staging_reservation_through_memory_admission() {
    const auto source = png_fixture();
    size_t admissions = 0;
    std::string diagnostic;
    const bool streamed = StreamRasterImportRecords(source, 0,
        [](CanonicalImportRecord&&) { return true; }, diagnostic,
        [&](uint64_t bytes, std::string&) {
            assert(bytes > 0);
            ++admissions;
            return true;
        });
    assert(streamed && diagnostic.empty());
    assert(admissions >= 3);

    diagnostic.clear();
    const bool rejected = StreamRasterImportRecords(source, 0,
        [](CanonicalImportRecord&&) { return true; }, diagnostic,
        [](uint64_t, std::string& message) {
            message = "system RAM pressure wait timed out after 5 minutes";
            return false;
        });
    assert(!rejected);
    assert(diagnostic == "system RAM pressure wait timed out after 5 minutes");
}
#endif

} // namespace

int main() {
    test_raster_chunk_span_serializer();
    test_raster_rgba16le_import_records();
#ifdef _WIN32
    test_raster_stream_routes_every_staging_reservation_through_memory_admission();
    test_raster_source_and_frame_above_old_fixed_limits();
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
    bool saw_source_bytes = false;
    bool saw_partial_report = false;
    uint32_t expected_offset = 0;
    std::array<uint8_t, 8> rgba{};
    std::vector<uint8_t> retained_source_bytes;
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
            assert(image->preview_pixel_color_basis == 1);
        } else if (const auto* chunk = std::get_if<ImportRasterImageChunkRecord>(&*decoded)) {
            saw_pixels = true;
            assert(chunk->byte_offset == expected_offset);
            assert(chunk->total_bytes == 8);
            std::copy(chunk->bytes.begin(), chunk->bytes.end(),
                      rgba.begin() + chunk->byte_offset);
            expected_offset += static_cast<uint32_t>(chunk->bytes.size());
        } else if (const auto* chunk = std::get_if<ImportRasterSourceChunkRecord>(&*decoded)) {
            saw_source_bytes = true;
            assert(chunk->byte_offset == retained_source_bytes.size());
            retained_source_bytes.insert(retained_source_bytes.end(),
                                         chunk->bytes.begin(), chunk->bytes.end());
        } else if (const auto* report = std::get_if<ImportFidelityReportRecord>(&*decoded)) {
            saw_partial_report = true;
            assert(report->coverage_profile == "raster-rgba8-v1");
            assert(report->coverage == ImportCoverage::Partial);
            assert(report->appearance == ImportAppearance::Partial);
            assert(report->editability == ImportEditability::ExplicitConversion);
            assert(report->production == ImportProduction::NotAssessed);
        }
    }
    assert(saw_layer && saw_image && saw_pixels && saw_source_bytes && saw_partial_report);
    assert(expected_offset == 8);
    assert(rgba[3] == 255 && rgba[7] == 128);
    assert(retained_source_bytes == source);

    assert(BuildRasterImportRecords(multi_page_tiff_fixture(2), records, diagnostic, 1));
    bool saw_second_tiff_page = false;
    for (const auto& record : records) {
        if (record.type != ImportRecordType::RasterImage) continue;
        const auto decoded = DeserializeImportRecord(record.type, record.payload);
        assert(decoded);
        const auto& image = std::get<ImportRasterImageRecord>(*decoded);
        assert(image.frame_count == 2 && image.frame_index == 1);
        saw_second_tiff_page = true;
    }
    assert(saw_second_tiff_page);

    const bool built_page_after_old_cap = BuildRasterImportRecords(
        multi_page_tiff_fixture(300), records, diagnostic, 299);
    if (!built_page_after_old_cap)
        std::fprintf(stderr, "300-frame TIFF fixture rejected: %s\n", diagnostic.c_str());
    assert(built_page_after_old_cap);
    bool saw_page_after_old_frame_cap = false;
    for (const auto& record : records) {
        if (record.type != ImportRecordType::RasterImage) continue;
        const auto decoded = DeserializeImportRecord(record.type, record.payload);
        assert(decoded);
        const auto& image = std::get<ImportRasterImageRecord>(*decoded);
        assert(image.frame_count == 300 && image.frame_index == 299);
        saw_page_after_old_frame_cap = true;
    }
    assert(saw_page_after_old_frame_cap);

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
