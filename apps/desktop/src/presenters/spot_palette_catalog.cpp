#include "pikdraw/desktop/presenters/spot_palette_catalog.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pikdraw::desktop {
namespace {
constexpr uint64_t kMaxAseBytes = 32ULL * 1024 * 1024;
constexpr uint32_t kMaxAseBlocks = 100'000;
constexpr size_t kMaxSpotSwatches = 20'000;
constexpr size_t kMaxNameUnits = 128;

class AseReader {
public:
    explicit AseReader(const std::vector<uint8_t>& data) : data_(data) {}
    size_t remaining() const noexcept { return data_.size() - position_; }
    size_t position() const noexcept { return position_; }
    bool skip(size_t count) noexcept {
        if (count > remaining()) return false;
        position_ += count;
        return true;
    }
    bool read_bytes(char* output, size_t count) noexcept {
        if (count > remaining()) return false;
        std::copy_n(reinterpret_cast<const char*>(data_.data() + position_), count, output);
        position_ += count;
        return true;
    }
    bool read_u16(uint16_t& value) noexcept {
        if (remaining() < 2) return false;
        value = static_cast<uint16_t>((data_[position_] << 8) | data_[position_ + 1]);
        position_ += 2;
        return true;
    }
    bool read_u32(uint32_t& value) noexcept {
        if (remaining() < 4) return false;
        value = (static_cast<uint32_t>(data_[position_]) << 24) |
                (static_cast<uint32_t>(data_[position_ + 1]) << 16) |
                (static_cast<uint32_t>(data_[position_ + 2]) << 8) |
                static_cast<uint32_t>(data_[position_ + 3]);
        position_ += 4;
        return true;
    }
    bool read_float(float& value) noexcept {
        uint32_t bits = 0;
        if (!read_u32(bits)) return false;
        value = std::bit_cast<float>(bits);
        return std::isfinite(value);
    }
    bool read_utf16(std::string& output) {
        uint16_t units = 0;
        if (!read_u16(units) || units == 0 || units > kMaxNameUnits + 1 ||
            static_cast<size_t>(units) * 2 > remaining())
            return false;
        std::vector<uint16_t> text;
        text.reserve(units);
        for (uint16_t i = 0; i < units; ++i) {
            uint16_t unit = 0;
            if (!read_u16(unit)) return false;
            text.push_back(unit);
        }
        if (!text.empty() && text.back() == 0) text.pop_back();
        if (text.empty() || text.size() > kMaxNameUnits) return false;
        output.clear();
        for (size_t i = 0; i < text.size(); ++i) {
            uint32_t codepoint = text[i];
            if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                if (i + 1 >= text.size() || text[i + 1] < 0xdc00 || text[i + 1] > 0xdfff)
                    return false;
                codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (text[++i] - 0xdc00);
            } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                return false;
            }
            append_utf8(output, codepoint);
        }
        return !output.empty();
    }

private:
    static void append_utf8(std::string& out, uint32_t value) {
        if (value <= 0x7f) out.push_back(static_cast<char>(value));
        else if (value <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | (value >> 6)));
            out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        } else if (value <= 0xffff) {
            out.push_back(static_cast<char>(0xe0 | (value >> 12)));
            out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (value >> 18)));
            out.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        }
    }

    const std::vector<uint8_t>& data_;
    size_t position_{0};
};

double lab_inverse(double value) {
    constexpr double epsilon = 216.0 / 24389.0;
    constexpr double kappa = 24389.0 / 27.0;
    const double cube = value * value * value;
    return cube > epsilon ? cube : (116.0 * value - 16.0) / kappa;
}

double srgb_gamma(double value) {
    value = std::clamp(value, 0.0, 1.0);
    return value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

color::RGBColor lab_to_srgb(float lightness, float a, float b) {
    const double fy = (static_cast<double>(lightness) + 16.0) / 116.0;
    const double x = 0.96422 * lab_inverse(fy + static_cast<double>(a) / 500.0);
    const double y = lab_inverse(fy);
    const double z = 0.82521 * lab_inverse(fy - static_cast<double>(b) / 200.0);
    return {srgb_gamma(3.1338561 * x - 1.6168667 * y - 0.4906146 * z),
            srgb_gamma(-0.9787684 * x + 1.9161415 * y + 0.0334540 * z),
            srgb_gamma(0.0719453 * x - 0.2289914 * y + 1.4052427 * z), 1.0};
}

std::string path_utf8(const std::filesystem::path& path) {
    const auto text = path.stem().u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

bool read_file(const std::filesystem::path& path, std::vector<uint8_t>& data,
               std::string* error) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size < 12 || size > kMaxAseBytes) {
        if (error) *error = ec ? "Cannot read ASE file size" : "ASE file size is outside the supported range";
        return false;
    }
    data.resize(static_cast<size_t>(size));
    std::ifstream input(path, std::ios::binary);
    if (!input || !input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) {
        if (error) *error = "Cannot read ASE file";
        return false;
    }
    return true;
}

bool supported_model(const char model[4], std::array<float, 4>& channels,
                     size_t& count) {
    if (std::memcmp(model, "RGB ", 4) == 0) count = 3;
    else if (std::memcmp(model, "CMYK", 4) == 0) count = 4;
    else if (std::memcmp(model, "LAB ", 4) == 0) count = 3;
    else if (std::memcmp(model, "Gray", 4) == 0) count = 1;
    else return false;
    for (size_t i = 0; i < count; ++i)
        if (!std::isfinite(channels[i])) return false;
    return true;
}

bool unit_values(const std::array<float, 4>& values, size_t count) {
    for (size_t i = 0; i < count; ++i)
        if (values[i] < 0.0f || values[i] > 1.0f) return false;
    return true;
}

} // namespace

std::filesystem::path spot_palette_directory() {
#ifdef _WIN32
    std::array<wchar_t, 32768> buffer{};
    const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(),
                                                 static_cast<DWORD>(buffer.size()));
    if (count == 0 || count >= buffer.size()) return {};
    return std::filesystem::path(buffer.data()) / L"PikDraw" / L"spot-palettes";
#else
    return {};
#endif
}

SpotPaletteLibrary sample_spot_palette() {
    return {"PikDraw Sample Spot Colors", {}, {
        {"PikDraw Sample Magenta", "PikDraw samples", {1.0, 0.0, 1.0, 1.0}},
        {"PikDraw Sample Cyan", "PikDraw samples", {0.0, 0.68, 0.92, 1.0}},
        {"PikDraw Orange", "PikDraw samples", {0.96, 0.34, 0.06, 1.0}},
        {"PikDraw Blue", "PikDraw samples", {0.0, 0.31, 0.64, 1.0}},
        {"PikDraw Warm Gray", "PikDraw samples", {0.53, 0.49, 0.45, 1.0}}}};
}

std::optional<SpotPaletteLibrary> load_ase_spot_palette(const std::filesystem::path& path,
                                                        std::string* error) {
    if (error) error->clear();
    std::vector<uint8_t> data;
    if (!read_file(path, data, error)) return std::nullopt;
    AseReader file(data);
    std::array<char, 4> signature{};
    uint16_t major = 0, minor = 0;
    uint32_t block_count = 0;
    if (!file.read_bytes(signature.data(), signature.size()) || signature != std::array<char, 4>{'A','S','E','F'} ||
        !file.read_u16(major) || !file.read_u16(minor) || !file.read_u32(block_count) ||
        major != 1 || block_count > kMaxAseBlocks) {
        if (error) *error = "Invalid or unsupported ASE header";
        return std::nullopt;
    }
    (void)minor;
    SpotPaletteLibrary library;
    library.name = path_utf8(path);
    library.source_path = path;
    std::vector<std::string> groups;
    for (uint32_t block_index = 0; block_index < block_count; ++block_index) {
        uint16_t type = 0;
        uint32_t length = 0;
        if (!file.read_u16(type) || !file.read_u32(length) || length > file.remaining()) {
            if (error) *error = "ASE block table is truncated or invalid";
            return std::nullopt;
        }
        const size_t end = file.position() + length;
        const size_t start = file.position();
        if (type == 0xc001) {
            std::string group;
            if (!file.read_utf16(group)) {
                if (error) *error = "ASE group name is invalid";
                return std::nullopt;
            }
            groups.push_back(std::move(group));
        } else if (type == 0xc002) {
            if (!groups.empty()) groups.pop_back();
        } else if (type == 0x0001) {
            std::string name;
            std::array<char, 4> model{};
            if (!file.read_utf16(name) || !file.read_bytes(model.data(), model.size())) {
                if (error) *error = "ASE swatch record is malformed";
                return std::nullopt;
            }
            std::array<float, 4> channels{};
            size_t count = 0;
            if (!supported_model(model.data(), channels, count)) {
                file.skip(end - file.position());
            } else {
                bool valid = true;
                for (size_t i = 0; i < count; ++i)
                    valid = file.read_float(channels[i]) && valid;
                uint16_t color_type = 0;
                valid = file.read_u16(color_type) && valid;
                if (!valid) {
                    if (error) *error = "ASE swatch contains invalid channel values";
                    return std::nullopt;
                }
                if (color_type == 1) {
                    SpotPaletteSwatch swatch;
                    swatch.name = std::move(name);
                    for (const auto& group : groups) {
                        if (!swatch.group.empty()) swatch.group += " / ";
                        swatch.group += group;
                    }
                    if (std::memcmp(model.data(), "RGB ", 4) == 0 && unit_values(channels, 3)) {
                        swatch.alternate_rgb = {channels[0], channels[1], channels[2], 1.0};
                    } else if (std::memcmp(model.data(), "CMYK", 4) == 0 && unit_values(channels, 4)) {
                        swatch.alternate_rgb = {(1.0 - channels[0]) * (1.0 - channels[3]),
                            (1.0 - channels[1]) * (1.0 - channels[3]),
                            (1.0 - channels[2]) * (1.0 - channels[3]), 1.0};
                    } else if (std::memcmp(model.data(), "LAB ", 4) == 0 &&
                               channels[0] >= 0.0f && channels[0] <= 100.0f &&
                               channels[1] >= -128.0f && channels[1] <= 127.0f &&
                               channels[2] >= -128.0f && channels[2] <= 127.0f) {
                        swatch.alternate_rgb = lab_to_srgb(channels[0], channels[1], channels[2]);
                    } else if (std::memcmp(model.data(), "Gray", 4) == 0 && unit_values(channels, 1)) {
                        swatch.alternate_rgb = {channels[0], channels[0], channels[0], 1.0};
                    } else {
                        if (error) *error = "ASE Spot swatch has out-of-range channel values";
                        return std::nullopt;
                    }
                    if (!swatch.alternate_rgb.is_valid()) {
                        if (error) *error = "ASE Spot swatch cannot be previewed";
                        return std::nullopt;
                    }
                    library.swatches.push_back(std::move(swatch));
                    if (library.swatches.size() > kMaxSpotSwatches) {
                        if (error) *error = "ASE library contains too many Spot swatches";
                        return std::nullopt;
                    }
                }
            }
        }
        if (file.position() > end) {
            if (error) *error = "ASE block length does not match its contents";
            return std::nullopt;
        }
        if (!file.skip(end - file.position())) {
            if (error) *error = "ASE block exceeds file bounds";
            return std::nullopt;
        }
        (void)start;
    }
    if (library.swatches.empty()) {
        if (error) *error = "ASE file contains no supported Spot swatches";
        return std::nullopt;
    }
    return library;
}

std::vector<SpotPaletteLibrary> load_spot_palette_libraries(
    const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> paths;
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) return {};
    for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        auto extension = it->path().extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
        if (extension == L".ase") paths.push_back(it->path());
        if (paths.size() >= 128) break;
    }
    std::sort(paths.begin(), paths.end());
    std::vector<SpotPaletteLibrary> libraries;
    libraries.reserve(paths.size());
    for (const auto& path : paths)
        if (auto library = load_ase_spot_palette(path)) libraries.push_back(std::move(*library));
    return libraries;
}

bool import_ase_spot_palette(const std::filesystem::path& source,
    const std::filesystem::path& directory, SpotPaletteLibrary& imported, std::string* error) {
    if (error) error->clear();
    auto parsed = load_ase_spot_palette(source, error);
    if (!parsed) return false;
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        if (error) *error = "Cannot create the local Spot palette folder";
        return false;
    }
    auto destination = directory / source.filename();
    for (uint32_t suffix = 2; std::filesystem::exists(destination, ec) && !ec; ++suffix) {
        if (suffix > 9999) {
            if (error) *error = "Too many imported palettes use this file name";
            return false;
        }
        destination = directory / (source.stem().wstring() + L" (" + std::to_wstring(suffix) + L")" +
                                   source.extension().wstring());
    }
    ec.clear();
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, ec);
    if (ec) {
        if (error) *error = "Cannot copy ASE library into the local Spot palette folder";
        return false;
    }
    imported = std::move(*parsed);
    imported.name = path_utf8(destination);
    imported.source_path = destination;
    return true;
}

} // namespace pikdraw::desktop
