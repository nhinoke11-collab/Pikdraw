#include "pikdraw/interop/adapters/raster_image_decoder.hpp"
#include "pikdraw/color/preview/profile_color_transform.hpp"
#include "pikdraw/color/profiles/profile_resource.hpp"

#include <cassert>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#endif

using namespace pikdraw::interop;

namespace {

class MemoryRasterSource final : public RasterByteSource {
public:
    explicit MemoryRasterSource(std::span<const uint8_t> bytes) : bytes_(bytes) {}
    uint64_t size_bytes() const noexcept override { return logical_size_; }
    bool read_at(uint64_t offset, std::span<uint8_t> destination,
                 size_t& bytes_read) const noexcept override {
        bytes_read = 0;
        if (offset > bytes_.size()) return false;
        const size_t count = (std::min)(destination.size(), bytes_.size() - static_cast<size_t>(offset));
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset), count, destination.begin());
        bytes_read = count;
        return true;
    }
    std::span<const uint8_t> bytes_;
    uint64_t logical_size_{bytes_.size()};
};

#ifdef _WIN32
class SparseRasterFile final : public RasterByteSource {
public:
    explicit SparseRasterFile(std::filesystem::path path) : path_(std::move(path)) {
        handle_ = CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, nullptr);
        LARGE_INTEGER size{};
        if (handle_ == INVALID_HANDLE_VALUE || !GetFileSizeEx(handle_, &size) ||
            size.QuadPart < 0) {
            close();
            return;
        }
        size_ = static_cast<uint64_t>(size.QuadPart);
    }
    ~SparseRasterFile() override { close(); }
    uint64_t size_bytes() const noexcept override { return size_; }
    bool valid() const noexcept { return handle_ != INVALID_HANDLE_VALUE; }

    bool read_at(uint64_t offset, std::span<uint8_t> destination,
                 size_t& bytes_read) const noexcept override {
        bytes_read = 0;
        if (!valid() || offset > size_) return false;
        const size_t requested = static_cast<size_t>((std::min)(
            static_cast<uint64_t>(destination.size()), size_ - offset));
        if (offset > static_cast<uint64_t>(std::numeric_limits<LONGLONG>::max()))
            return false;
        std::lock_guard lock(mutex_);
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(handle_, position, nullptr, FILE_BEGIN)) return false;
        while (bytes_read < requested) {
            const DWORD count = static_cast<DWORD>((std::min)(
                requested - bytes_read, static_cast<size_t>(MAXDWORD)));
            DWORD received = 0;
            if (!ReadFile(handle_, destination.data() + bytes_read, count,
                          &received, nullptr) || received == 0)
                return false;
            bytes_read += received;
        }
        return true;
    }

private:
    void close() noexcept {
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    std::filesystem::path path_;
    HANDLE handle_{INVALID_HANDLE_VALUE};
    uint64_t size_{0};
    mutable std::mutex mutex_;
};

class SparseRasterFixture final {
public:
    explicit SparseRasterFixture(std::span<const uint8_t> prefix) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("pikdraw-sparse-raster-" + std::to_string(stamp) + ".png");
        HANDLE file = CreateFileW(path_.c_str(), GENERIC_WRITE | GENERIC_READ, 0,
            nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        DWORD returned = 0;
        const bool sparse = DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0,
            nullptr, 0, &returned, nullptr) != FALSE;
        DWORD written = 0;
        const bool prefix_written = sparse && prefix.size() <= MAXDWORD &&
            WriteFile(file, prefix.data(), static_cast<DWORD>(prefix.size()),
                      &written, nullptr) != FALSE && written == prefix.size();
        LARGE_INTEGER end{};
        end.QuadPart = static_cast<LONGLONG>(0x100000000ull + 4096ull);
        const bool length_set = prefix_written &&
            SetFilePointerEx(file, end, nullptr, FILE_BEGIN) != FALSE &&
            SetEndOfFile(file) != FALSE;
        CloseHandle(file);
        valid_ = length_set;
        if (!valid_) {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
        }
    }
    ~SparseRasterFixture() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    bool valid() const noexcept { return valid_; }
    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
    bool valid_{false};
};

class SparseBigTiffFixture final {
public:
    explicit SparseBigTiffFixture(bool include_second_ifd = false) {
        constexpr uint64_t ifd_offset = 0x100000000ull;
        constexpr uint64_t entry_count = 12;
        constexpr uint64_t ifd_bytes = 8 + entry_count * 20 + 8;
        constexpr uint64_t pixel_offset = ifd_offset + ifd_bytes;
        constexpr uint64_t second_ifd_offset = pixel_offset + 4;
        constexpr uint64_t second_pixel_offset = second_ifd_offset + ifd_bytes;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("pikdraw-sparse-bigtiff-" + std::to_string(stamp) + ".tif");
        HANDLE file = CreateFileW(path_.c_str(), GENERIC_WRITE | GENERIC_READ, 0,
            nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        DWORD returned = 0;
        bool ok = DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0,
            nullptr, 0, &returned, nullptr) != FALSE;
        const auto write_at = [&](uint64_t offset, std::span<const uint8_t> bytes) {
            LARGE_INTEGER position{};
            position.QuadPart = static_cast<LONGLONG>(offset);
            if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) return false;
            size_t written_total = 0;
            while (written_total < bytes.size()) {
                const DWORD requested = static_cast<DWORD>((std::min)(
                    bytes.size() - written_total, static_cast<size_t>(MAXDWORD)));
                DWORD written = 0;
                if (!WriteFile(file, bytes.data() + written_total, requested,
                               &written, nullptr) || written == 0)
                    return false;
                written_total += written;
            }
            return true;
        };
        const auto put16 = [](std::vector<uint8_t>& out, uint16_t value) {
            out.push_back(static_cast<uint8_t>(value));
            out.push_back(static_cast<uint8_t>(value >> 8));
        };
        const auto put64 = [](std::vector<uint8_t>& out, uint64_t value) {
            for (unsigned i = 0; i < 8; ++i)
                out.push_back(static_cast<uint8_t>(value >> (i * 8)));
        };
        const auto entry = [&](std::vector<uint8_t>& out, uint16_t tag,
                               uint16_t type, uint64_t count, uint64_t value) {
            put16(out, tag);
            put16(out, type);
            put64(out, count);
            put64(out, value);
        };

        std::vector<uint8_t> header{'I', 'I'};
        put16(header, 43); // BigTIFF marker.
        put16(header, 8);  // 64-bit offset size.
        put16(header, 0);
        put64(header, ifd_offset);
        std::vector<uint8_t> ifd;
        put64(ifd, entry_count);
        entry(ifd, 256, 4, 1, 1);                // ImageWidth
        entry(ifd, 257, 4, 1, 1);                // ImageLength
        entry(ifd, 258, 3, 4, 0x0008000800080008ull); // BitsPerSample
        entry(ifd, 259, 3, 1, 1);                // Compression: none
        entry(ifd, 262, 3, 1, 2);                // Photometric: RGB
        entry(ifd, 273, 16, 1, pixel_offset);    // StripOffsets: LONG8
        entry(ifd, 274, 3, 1, 1);                // Orientation
        entry(ifd, 277, 3, 1, 4);                // SamplesPerPixel
        entry(ifd, 278, 4, 1, 1);                // RowsPerStrip
        entry(ifd, 279, 16, 1, 4);               // StripByteCounts: LONG8
        entry(ifd, 284, 3, 1, 1);                // PlanarConfiguration
        entry(ifd, 338, 3, 1, 2);                // ExtraSamples: unassociated alpha
        put64(ifd, include_second_ifd ? second_ifd_offset : 0);
        auto second_ifd = ifd;
        if (include_second_ifd) {
            std::fill(second_ifd.end() - 8, second_ifd.end(), 0);
            for (unsigned i = 0; i < 8; ++i)
                second_ifd[120 + i] = static_cast<uint8_t>(second_pixel_offset >> (i * 8));
        }
        const std::array<uint8_t, 4> pixel{17, 83, 191, 255};
        ok = ok && ifd.size() == ifd_bytes && write_at(0, header) &&
            write_at(ifd_offset, ifd) && write_at(pixel_offset, pixel);
        if (ok && include_second_ifd)
            ok = write_at(second_ifd_offset, second_ifd) &&
                 write_at(second_pixel_offset, pixel);
        LARGE_INTEGER end{};
        end.QuadPart = static_cast<LONGLONG>((include_second_ifd
            ? second_pixel_offset : pixel_offset) + pixel.size());
        ok = ok && SetFilePointerEx(file, end, nullptr, FILE_BEGIN) != FALSE &&
            SetEndOfFile(file) != FALSE;
        CloseHandle(file);
        valid_ = ok;
        if (!valid_) {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
        }
    }

    ~SparseBigTiffFixture() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    bool valid() const noexcept { return valid_; }
    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
    bool valid_{false};
};
#endif

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

std::vector<uint8_t> zlib_store(std::span<const uint8_t> bytes) {
    std::vector<uint8_t> zlib{0x78, 0x01};
    size_t offset = 0;
    do {
        const size_t count = (std::min)(bytes.size() - offset, size_t{65535});
        const bool final_block = offset + count == bytes.size();
        zlib.push_back(final_block ? 0x01 : 0x00);
        const auto length = static_cast<uint16_t>(count);
        append_le16(zlib, length);
        append_le16(zlib, static_cast<uint16_t>(~length));
        zlib.insert(zlib.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                    bytes.begin() + static_cast<std::ptrdiff_t>(offset + count));
        offset += count;
        if (bytes.empty()) break;
    } while (offset < bytes.size());
    uint32_t a = 1, b = 0;
    for (const auto byte : bytes) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    append_be32(zlib, (b << 16) | a);
    return zlib;
}

std::vector<uint8_t> make_png_fixture(std::span<const uint8_t> icc_profile = {}) {
    std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> header;
    append_be32(header, 2);
    append_be32(header, 1);
    header.insert(header.end(), {8, 6, 0, 0, 0}); // RGBA8, deflate, no filter, no interlace
    append_png_chunk(png, "IHDR", header);

    if (!icc_profile.empty()) {
        std::vector<uint8_t> iccp{'s', 'R', 'G', 'B', 0, 0};
        const auto compressed_profile = zlib_store(icc_profile);
        iccp.insert(iccp.end(), compressed_profile.begin(), compressed_profile.end());
        append_png_chunk(png, "iCCP", iccp);
    }

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

std::vector<uint8_t> make_tall_png_fixture() {
    constexpr uint32_t width = 2;
    constexpr uint32_t height = 130;
    std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> header;
    append_be32(header, width);
    append_be32(header, height);
    header.insert(header.end(), {8, 6, 0, 0, 0});
    append_png_chunk(png, "IHDR", header);
    std::vector<uint8_t> raw;
    raw.reserve((1 + width * 4) * height);
    for (uint32_t row = 0; row < height; ++row) {
        raw.insert(raw.end(), {0, 255, static_cast<uint8_t>(row), 0, 255,
                               0, 255, 0, 255});
    }
    append_png_chunk(png, "IDAT", zlib_store(raw));
    append_png_chunk(png, "IEND", {});
    return png;
}

#ifdef _WIN32
std::vector<uint8_t> read_system_profile(const wchar_t* filename) {
    std::array<wchar_t, MAX_PATH> directory{};
    const UINT length = GetWindowsDirectoryW(directory.data(),
                                              static_cast<UINT>(directory.size()));
    if (length == 0 || length >= directory.size()) return {};
    std::filesystem::path path(directory.data());
    path /= L"System32/spool/drivers/color";
    path /= filename;
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    const std::string raw{std::istreambuf_iterator<char>(input),
                          std::istreambuf_iterator<char>()};
    return {raw.begin(), raw.end()};
}

std::vector<uint8_t> read_system_srgb_profile() {
    return read_system_profile(L"sRGB Color Space Profile.icm");
}

std::optional<pikdraw::color::IccRenderingIntent> profile_rendering_intent(
    std::span<const uint8_t> profile) {
    if (profile.size() < 68) return std::nullopt;
    const uint32_t intent = (static_cast<uint32_t>(profile[64]) << 24) |
        (static_cast<uint32_t>(profile[65]) << 16) |
        (static_cast<uint32_t>(profile[66]) << 8) |
        static_cast<uint32_t>(profile[67]);
    if (intent > static_cast<uint32_t>(pikdraw::color::IccRenderingIntent::AbsoluteColorimetric))
        return std::nullopt;
    return static_cast<pikdraw::color::IccRenderingIntent>(intent);
}
#endif

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

std::vector<uint8_t> make_cmyk_icc_tiff_fixture(
    std::span<const uint8_t> icc_profile) {
    constexpr uint16_t entry_count = 12;
    constexpr uint32_t ifd_bytes = 2 + entry_count * 12 + 4;
    constexpr uint32_t bits_offset = 8 + ifd_bytes;
    const uint32_t profile_offset = bits_offset + 8;
    const uint64_t pixel_offset64 = static_cast<uint64_t>(profile_offset) +
        icc_profile.size();
    assert(!icc_profile.empty() && icc_profile.size() <= UINT32_MAX &&
           pixel_offset64 <= UINT32_MAX);
    const auto pixel_offset = static_cast<uint32_t>(pixel_offset64);

    std::vector<uint8_t> tiff{'I', 'I'};
    append_le16(tiff, 42);
    append_le32(tiff, 8);
    append_le16(tiff, entry_count);
    append_tiff_entry(tiff, 256, 3, 1, 3); // width
    append_tiff_entry(tiff, 257, 3, 1, 1); // height
    append_tiff_entry(tiff, 258, 3, 4, bits_offset); // four 8-bit channels
    append_tiff_entry(tiff, 259, 3, 1, 1); // uncompressed
    append_tiff_entry(tiff, 262, 3, 1, 5); // separated CMYK
    append_tiff_entry(tiff, 273, 4, 1, pixel_offset); // strip offset
    append_tiff_entry(tiff, 277, 3, 1, 4); // samples per pixel
    append_tiff_entry(tiff, 278, 4, 1, 1); // rows per strip
    append_tiff_entry(tiff, 279, 4, 1, 12); // strip byte count
    append_tiff_entry(tiff, 284, 3, 1, 1); // chunky planar layout
    append_tiff_entry(tiff, 332, 3, 1, 1); // CMYK ink set
    append_tiff_entry(tiff, 34675, 7,
                      static_cast<uint32_t>(icc_profile.size()), profile_offset);
    append_le32(tiff, 0); // no next IFD
    for (int channel = 0; channel < 4; ++channel) append_le16(tiff, 8);
    tiff.insert(tiff.end(), icc_profile.begin(), icc_profile.end());
    tiff.insert(tiff.end(), {0, 0, 0, 0, 0, 0, 0, 128,
                             0, 0, 0, 255}); // paper white, midtone, solid black
    return tiff;
}

std::vector<uint8_t> make_two_page_tiff_fixture() {
    constexpr uint32_t ifd_bytes = 2 + 11 * 12 + 4;
    constexpr uint32_t second_ifd_offset = 8 + ifd_bytes;
    constexpr uint32_t bits_offset = second_ifd_offset + ifd_bytes;
    constexpr uint32_t first_pixels_offset = bits_offset + 6;
    constexpr uint32_t second_pixels_offset = first_pixels_offset + 6;
    std::vector<uint8_t> tiff{'I', 'I'};
    append_le16(tiff, 42);
    append_le32(tiff, 8);
    const auto append_ifd = [&](uint32_t pixel_offset, uint32_t next_ifd) {
        append_le16(tiff, 11);
        append_tiff_entry(tiff, 256, 3, 1, 2);
        append_tiff_entry(tiff, 257, 3, 1, 1);
        append_tiff_entry(tiff, 258, 3, 3, bits_offset);
        append_tiff_entry(tiff, 259, 3, 1, 1);
        append_tiff_entry(tiff, 262, 3, 1, 2);
        append_tiff_entry(tiff, 274, 3, 1, 1);
        append_tiff_entry(tiff, 273, 4, 1, pixel_offset);
        append_tiff_entry(tiff, 277, 3, 1, 3);
        append_tiff_entry(tiff, 278, 4, 1, 1);
        append_tiff_entry(tiff, 279, 4, 1, 6);
        append_tiff_entry(tiff, 284, 3, 1, 1);
        append_le32(tiff, next_ifd);
    };
    append_ifd(first_pixels_offset, second_ifd_offset);
    append_ifd(second_pixels_offset, 0);
    append_le16(tiff, 8);
    append_le16(tiff, 8);
    append_le16(tiff, 8);
    tiff.insert(tiff.end(), {255, 0, 0, 0, 255, 0});
    tiff.insert(tiff.end(), {0, 0, 255, 255, 255, 255});
    return tiff;
}

std::vector<uint8_t> make_alpha_tiff_fixture(bool associated) {
    constexpr uint32_t bits_offset = 8 + 2 + 12 * 12 + 4;
    constexpr uint32_t pixels_offset = bits_offset + 8;
    std::vector<uint8_t> tiff{'I', 'I'};
    append_le16(tiff, 42); append_le32(tiff, 8); append_le16(tiff, 12);
    append_tiff_entry(tiff, 256, 3, 1, 2);
    append_tiff_entry(tiff, 257, 3, 1, 1);
    append_tiff_entry(tiff, 258, 3, 4, bits_offset);
    append_tiff_entry(tiff, 259, 3, 1, 1);
    append_tiff_entry(tiff, 262, 3, 1, 2);
    append_tiff_entry(tiff, 273, 4, 1, pixels_offset);
    append_tiff_entry(tiff, 274, 3, 1, 1);
    append_tiff_entry(tiff, 277, 3, 1, 4);
    append_tiff_entry(tiff, 278, 4, 1, 1);
    append_tiff_entry(tiff, 279, 4, 1, 8);
    append_tiff_entry(tiff, 284, 3, 1, 1);
    append_tiff_entry(tiff, 338, 3, 1, associated ? 1 : 2);
    append_le32(tiff, 0);
    for (int channel = 0; channel < 4; ++channel) append_le16(tiff, 8);
    if (associated) tiff.insert(tiff.end(), {110, 81, 19, 128, 14, 26, 38, 64});
    else tiff.insert(tiff.end(), {220, 162, 38, 128, 56, 104, 152, 64});
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
    assert(decoded_png.image.pixel_color_basis == RasterPixelColorBasis::AssumedSRgb);
    verify_pixels(decoded_png.image, true);
    auto output_budget = std::make_shared<pikdraw::raster::RasterMemoryBudget>(png.size() + 8);
    {
        RasterImageDecodeOptions budgeted_options;
        budgeted_options.memory_budget = output_budget;
        const auto budgeted_decode = RasterImageDecoder::decode(png, budgeted_options);
        assert(budgeted_decode.success && budgeted_decode.image.pixels_rgba.size() == 8);
        assert(output_budget->reserved_bytes() == 8);
    }
    assert(output_budget->reserved_bytes() == 0);

    const auto tall_png = RasterImageDecoder::decode(make_tall_png_fixture());
    assert(tall_png.success && tall_png.image.height_px == 130);
    assert(tall_png.image.pixels_rgba[(63 * 2) * 4 + 1] == 63);
    assert(tall_png.image.pixels_rgba[(64 * 2) * 4 + 1] == 64);
    assert(tall_png.image.pixels_rgba[(129 * 2) * 4 + 1] == 129);

    const auto streamed_fixture = make_tall_png_fixture();
    MemoryRasterSource random_source(streamed_fixture);
    auto row_budget = std::make_shared<pikdraw::raster::RasterMemoryBudget>(1024);
    RasterImageDecodeOptions row_options;
    row_options.memory_budget = row_budget;
    std::array<uint8_t, 8> signature{};
    assert(RasterImageDecoder::read_exact(random_source, 0, signature));
    assert(RasterImageDecoder::sniff_format(signature) == RasterImageFormat::Png);
    assert(!RasterImageDecoder::read_exact(random_source,
        std::numeric_limits<uint64_t>::max(), signature));
    uint32_t rows_seen = 0;
    const auto row_result = RasterImageDecoder::decode_rows(random_source, row_options,
        [&rows_seen, &tall_png](uint32_t y, uint32_t width,
                                std::span<const uint8_t> rgba) {
            assert(y == rows_seen && width == 2 && rgba.size() == 8);
            const auto expected = std::span<const uint8_t>(tall_png.image.pixels_rgba)
                .subspan(static_cast<size_t>(y) * rgba.size(), rgba.size());
            assert(std::equal(rgba.begin(), rgba.end(), expected.begin(), expected.end()));
            ++rows_seen;
            return true;
        });
    assert(row_result.success && rows_seen == 130 && row_result.image.pixels_rgba.empty());
    assert(row_budget->reserved_bytes() == 0);
    uint32_t tile_rows_seen = 0;
    const auto tile_result = RasterImageDecoder::decode_region_rows(
        random_source, row_options, RasterImageRegion{1, 60, 1, 3},
        [&tile_rows_seen, &tall_png](const RasterImageData& metadata, uint32_t y,
                                     uint32_t width, std::span<const uint8_t> rgba) {
            assert(metadata.width_px == 2 && metadata.height_px == 130);
            assert(y == 60 + tile_rows_seen && width == 1 && rgba.size() == 4);
            const auto expected = std::span<const uint8_t>(tall_png.image.pixels_rgba)
                .subspan(static_cast<size_t>(y) * 8 + 4, 4);
            assert(std::equal(rgba.begin(), rgba.end(), expected.begin(), expected.end()));
            ++tile_rows_seen;
            return true;
        });
    assert(tile_result.success && tile_rows_seen == 3 &&
           tile_result.image.pixels_rgba.empty());
    assert(row_budget->reserved_bytes() == 0);
    auto tiny_row_budget = std::make_shared<pikdraw::raster::RasterMemoryBudget>(1);
    RasterImageDecodeOptions tiny_row_options;
    tiny_row_options.memory_budget = tiny_row_budget;
    uint32_t tiny_budget_callbacks = 0;
    const auto tiny_budget_result = RasterImageDecoder::decode_rows(
        random_source, tiny_row_options,
        [&tiny_budget_callbacks](uint32_t, uint32_t, std::span<const uint8_t>) {
            ++tiny_budget_callbacks;
            return true;
        });
    assert(!tiny_budget_result.success && tiny_budget_callbacks == 0 &&
           tiny_row_budget->reserved_bytes() == 0);
    assert(tiny_budget_result.diagnostic_code == "RASTER_SCRATCH_BUDGET" ||
           tiny_budget_result.diagnostic_code == "RASTER_MEMORY_BUDGET");
    uint32_t rows_before_cancel = 0;
    const auto cancelled_rows = RasterImageDecoder::decode_rows(random_source, {},
        [&rows_before_cancel](uint32_t, uint32_t, std::span<const uint8_t>) {
            ++rows_before_cancel;
            return false;
        });
    assert(!cancelled_rows.success && cancelled_rows.diagnostic_code == "RASTER_CANCELLED");
    assert(rows_before_cancel == 1);
#ifdef _WIN32
    SparseRasterFixture sparse_fixture(streamed_fixture);
    assert(sparse_fixture.valid());
    SparseRasterFile sparse_source(sparse_fixture.path());
    assert(sparse_source.valid() && sparse_source.size_bytes() > 0x100000000ull);
    std::array<uint8_t, 8> sparse_tail{};
    assert(RasterImageDecoder::read_exact(sparse_source,
        0x100000000ull + 32, sparse_tail));
    assert(std::all_of(sparse_tail.begin(), sparse_tail.end(),
                       [](uint8_t byte) { return byte == 0; }));
    uint32_t sparse_rows = 0;
    const auto sparse_result = RasterImageDecoder::decode_rows(sparse_source, {},
        [&sparse_rows, &tall_png](uint32_t y, uint32_t width,
                                 std::span<const uint8_t> rgba) {
            assert(y == sparse_rows && width == 2 && rgba.size() == 8);
            const auto expected = std::span<const uint8_t>(tall_png.image.pixels_rgba)
                .subspan(static_cast<size_t>(y) * rgba.size(), rgba.size());
            assert(std::equal(rgba.begin(), rgba.end(), expected.begin(), expected.end()));
            ++sparse_rows;
            return true;
        });
    assert(sparse_result.success && sparse_rows == 130 &&
           sparse_result.image.pixels_rgba.empty());

    SparseBigTiffFixture big_tiff_fixture;
    assert(big_tiff_fixture.valid());
    SparseRasterFile big_tiff_source(big_tiff_fixture.path());
    assert(big_tiff_source.valid() && big_tiff_source.size_bytes() > 0x100000000ull);
    uint32_t big_tiff_rows = 0;
    const auto big_tiff_result = RasterImageDecoder::decode_rows(
        big_tiff_source, {},
        [&big_tiff_rows](uint32_t y, uint32_t width,
                         std::span<const uint8_t> rgba) {
            assert(y == big_tiff_rows && width == 1 && rgba.size() == 4);
            assert(rgba[0] == 17 && rgba[1] == 83 && rgba[2] == 191 && rgba[3] == 255);
            ++big_tiff_rows;
            return true;
        });
    if (!big_tiff_result.success)
        std::cerr << "BigTIFF decode failed: " << big_tiff_result.diagnostic_code
                  << ": " << big_tiff_result.message << '\n';
    assert(big_tiff_result.success && big_tiff_rows == 1 &&
           big_tiff_result.image.frame_count == 1 &&
           big_tiff_result.image.pixels_rgba.empty());

    SparseBigTiffFixture multipage_big_tiff_fixture(true);
    assert(multipage_big_tiff_fixture.valid());
    SparseRasterFile multipage_big_tiff_source(multipage_big_tiff_fixture.path());
    uint32_t multipage_big_tiff_rows = 0;
    const auto multipage_big_tiff_result = RasterImageDecoder::decode_rows(
        multipage_big_tiff_source, {},
        [&multipage_big_tiff_rows](uint32_t, uint32_t,
                                  std::span<const uint8_t>) {
            ++multipage_big_tiff_rows;
            return true;
        });
    assert(!multipage_big_tiff_result.success &&
           multipage_big_tiff_result.diagnostic_code == "RASTER_MULTIFRAME_UNSUPPORTED" &&
           multipage_big_tiff_rows == 0);
#endif
    MemoryRasterSource short_source(streamed_fixture);
    short_source.logical_size_ = streamed_fixture.size() + 1;
    assert(!RasterImageDecoder::read_exact(short_source, streamed_fixture.size(), signature));

    const auto srgb_profile = read_system_srgb_profile();
    if (srgb_profile.empty()) {
        std::cout << "[NOT_MEASURED] System sRGB ICC profile is unavailable for raster transform coverage\n";
    } else {
        const auto tagged_png = RasterImageDecoder::decode(make_png_fixture(srgb_profile));
        assert(tagged_png.success && tagged_png.image.has_embedded_icc_profile);
        assert(tagged_png.image.icc_profile_status == RasterIccProfileStatus::Preserved);
        assert(tagged_png.image.embedded_icc_profile == srgb_profile);
        assert(tagged_png.image.pixel_color_basis == RasterPixelColorBasis::ConvertedToSRgb);
        verify_pixels(tagged_png.image, true);
    }

    const auto swop_profile = read_system_profile(L"RSWOP.icm");
    if (swop_profile.empty() || srgb_profile.empty()) {
        std::cout << "[NOT_MEASURED] Installed SWOP/sRGB profile pair unavailable for CMYK raster conversion\n";
    } else {
        const auto cmyk_tiff = make_cmyk_icc_tiff_fixture(swop_profile);
        const auto decoded_cmyk = RasterImageDecoder::decode(cmyk_tiff);
        assert(decoded_cmyk.success && decoded_cmyk.image.format == RasterImageFormat::Tiff);
        MemoryRasterSource cmyk_stream(cmyk_tiff);
        std::vector<uint8_t> streamed_cmyk_pixels(decoded_cmyk.image.pixels_rgba.size());
        uint32_t cmyk_rows = 0;
        const auto streamed_cmyk = RasterImageDecoder::decode_rows(
            cmyk_stream, {}, [&streamed_cmyk_pixels, &cmyk_rows](
                uint32_t row, uint32_t width, std::span<const uint8_t> rgba) {
                assert(row == cmyk_rows && width == 3 && rgba.size() == 12);
                std::copy(rgba.begin(), rgba.end(),
                          streamed_cmyk_pixels.begin() + static_cast<size_t>(row) * rgba.size());
                ++cmyk_rows;
                return true;
            });
        assert(streamed_cmyk.success && cmyk_rows == 1 &&
               streamed_cmyk.image.pixel_color_basis == RasterPixelColorBasis::ConvertedToSRgb &&
               streamed_cmyk_pixels == decoded_cmyk.image.pixels_rgba);
        assert(decoded_cmyk.image.has_embedded_icc_profile &&
               decoded_cmyk.image.icc_profile_status == RasterIccProfileStatus::Preserved &&
               decoded_cmyk.image.embedded_icc_profile == swop_profile);
        assert(decoded_cmyk.image.pixel_color_basis == RasterPixelColorBasis::ConvertedToSRgb);
        assert(decoded_cmyk.image.width_px == 3 && decoded_cmyk.image.height_px == 1 &&
               decoded_cmyk.image.pixels_rgba.size() == 12);
        assert(decoded_cmyk.image.pixels_rgba[3] == 255 &&
               decoded_cmyk.image.pixels_rgba[7] == 255 &&
               decoded_cmyk.image.pixels_rgba[11] == 255);
        assert(decoded_cmyk.image.pixels_rgba[0] >= 250 &&
               decoded_cmyk.image.pixels_rgba[1] >= 250 &&
               decoded_cmyk.image.pixels_rgba[2] >= 250 &&
               decoded_cmyk.image.pixels_rgba[3] == 255);
        assert(decoded_cmyk.image.pixels_rgba[8] <= 32 &&
               decoded_cmyk.image.pixels_rgba[9] <= 32 &&
               decoded_cmyk.image.pixels_rgba[10] <= 32 &&
               decoded_cmyk.image.pixels_rgba[11] == 255);

        auto source_resource = pikdraw::color::ProfileResource::create(
            "Test SWOP CMYK", pikdraw::color::ColorSpace::Cmyk, swop_profile);
        auto destination_resource = pikdraw::color::ProfileResource::create(
            "Test system sRGB", pikdraw::color::ColorSpace::Rgb, srgb_profile);
        assert(source_resource && destination_resource);
        const auto preferred_intent = profile_rendering_intent(swop_profile);
        assert(preferred_intent);
        const auto cmyk_to_srgb = pikdraw::color::ProfileColorTransform::create(
            source_resource.resource, destination_resource.resource,
            {*preferred_intent, false});
        assert(cmyk_to_srgb);
        constexpr std::array<double, 3> cmy{0.0, 0.0, 0.0};
        const std::array<double, 3> black_levels{0.0, 128.0 / 255.0, 1.0};
        for (size_t sample = 0; sample < black_levels.size(); ++sample) {
            pikdraw::color::ColorValue converted;
            const pikdraw::color::ColorValue source_value{
                pikdraw::color::CMYKColor{cmy[0], cmy[1], cmy[2],
                                          black_levels[sample], 1.0}};
            assert(cmyk_to_srgb.transform->convert(source_value, converted));
            const auto* rgb = std::get_if<pikdraw::color::RGBColor>(&converted.payload());
            assert(rgb);
            const std::array<double, 3> reference{rgb->r, rgb->g, rgb->b};
            for (size_t channel = 0; channel < reference.size(); ++channel) {
                const auto expected = static_cast<int>(std::lround(
                    std::clamp(reference[channel], 0.0, 1.0) * 255.0));
                const auto actual = static_cast<int>(decoded_cmyk.image.pixels_rgba[
                    sample * 4 + channel]);
                assert(std::abs(actual - expected) <= 5);
            }
        }
        assert(cmyk_to_srgb.transform->failure_count() == 0);
        std::cout << "[PASS] RSWOP CMYK TIFF ICC -> sRGB pixels match separate document transform API within 5/255\n";
    }

    const auto tiff = make_tiff_fixture();
    const auto decoded_tiff = RasterImageDecoder::decode(tiff);
    assert(decoded_tiff.success && decoded_tiff.image.format == RasterImageFormat::Tiff);
    verify_pixels(decoded_tiff.image, false);
    for (bool associated : {false, true}) {
        const auto alpha_tiff = RasterImageDecoder::decode(make_alpha_tiff_fixture(associated));
        assert(alpha_tiff.success);
        const std::vector<uint8_t> expected{220, 162, 38, 128, 56, 104, 152, 64};
        assert(alpha_tiff.image.pixels_rgba.size() == expected.size());
        for (size_t channel = 0; channel < expected.size(); ++channel)
            assert(std::abs(int(alpha_tiff.image.pixels_rgba[channel]) - expected[channel]) <=
                   (channel % 4 == 3 ? 0 : 2));
    }
    RasterImageDecodeOptions second_page_options;
    second_page_options.frame_index = 1;
    const auto second_page = RasterImageDecoder::decode(
        make_two_page_tiff_fixture(), second_page_options);
    assert(second_page.success && second_page.image.frame_count == 2 &&
           second_page.image.frame_index == 1);
    assert(second_page.image.pixels_rgba[0] < 8 &&
           second_page.image.pixels_rgba[2] > 240);
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
