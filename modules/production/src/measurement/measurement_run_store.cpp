#include "pikdraw/production/measurement/measurement_run_store.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pikdraw::production::measurement {
namespace {
constexpr size_t kMaximumBytes = 64u * 1024u * 1024u;
constexpr size_t kMaximumPatches = 250000;
constexpr size_t kMaximumString = 256;
constexpr char kMagic[] = {'P', 'D', 'M', 'R'};
constexpr auto kCrcTable = [] {
    std::array<uint32_t, 256> table{};
    for (uint32_t index = 0; index < table.size(); ++index) {
        uint32_t value = index;
        for (int bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1u)));
        table[index] = value;
    }
    return table;
}();

bool valid_text(std::string_view value, size_t maximum = kMaximumString) {
    if (value.empty() || value.size() > maximum) return false;
    for (const unsigned char ch : value)
        if (ch == 0 || ch < 0x20 || ch == 0x7f) return false;
    return true;
}

bool valid_hash(std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
               (ch >= 'A' && ch <= 'F');
    });
}

uint32_t crc32(std::span<const uint8_t> bytes) {
    uint32_t crc = 0xffffffffu;
    for (const auto byte : bytes)
        crc = kCrcTable[(crc ^ byte) & 0xffu] ^ (crc >> 8);
    return ~crc;
}

struct Writer {
    std::vector<uint8_t> bytes;
    void u8(uint8_t value) { bytes.push_back(value); }
    void u16(uint16_t value) {
        u8(static_cast<uint8_t>(value)); u8(static_cast<uint8_t>(value >> 8));
    }
    void u32(uint32_t value) {
        for (int i = 0; i < 4; ++i) u8(static_cast<uint8_t>(value >> (i * 8)));
    }
    void f64(double value) { u64(std::bit_cast<uint64_t>(value)); }
    void u64(uint64_t value) {
        for (int i = 0; i < 8; ++i) u8(static_cast<uint8_t>(value >> (i * 8)));
    }
    bool string(std::string_view value) {
        if (value.size() > kMaximumString || value.size() > UINT16_MAX) return false;
        u16(static_cast<uint16_t>(value.size()));
        bytes.insert(bytes.end(), value.begin(), value.end());
        return bytes.size() <= kMaximumBytes;
    }
};

struct Reader {
    std::span<const uint8_t> bytes;
    size_t offset{0};
    bool take(size_t count, std::span<const uint8_t>& out) {
        if (count > bytes.size() - (std::min)(offset, bytes.size())) return false;
        out = bytes.subspan(offset, count); offset += count; return true;
    }
    bool u8(uint8_t& value) {
        std::span<const uint8_t> part;
        if (!take(1, part)) return false;
        value = part[0]; return true;
    }
    bool u16(uint16_t& value) {
        uint8_t a = 0, b = 0;
        if (!u8(a) || !u8(b)) return false;
        value = static_cast<uint16_t>(a | (static_cast<uint16_t>(b) << 8)); return true;
    }
    bool u32(uint32_t& value) {
        value = 0;
        for (int i = 0; i < 4; ++i) {
            uint8_t byte = 0; if (!u8(byte)) return false;
            value |= static_cast<uint32_t>(byte) << (i * 8);
        }
        return true;
    }
    bool u64(uint64_t& value) {
        value = 0;
        for (int i = 0; i < 8; ++i) {
            uint8_t byte = 0; if (!u8(byte)) return false;
            value |= static_cast<uint64_t>(byte) << (i * 8);
        }
        return true;
    }
    bool f64(double& value) {
        uint64_t bits = 0;
        if (!u64(bits)) return false;
        value = std::bit_cast<double>(bits); return std::isfinite(value);
    }
    bool string(std::string& value, size_t maximum = kMaximumString) {
        uint16_t length = 0; std::span<const uint8_t> part;
        if (!u16(length) || length > maximum || !take(length, part)) return false;
        value.assign(reinterpret_cast<const char*>(part.data()), part.size());
        return value.empty() || valid_text(value, maximum);
    }
};

bool valid_run(const MeasurementRun& run) {
    const auto& c = run.condition;
    if (run.schema_version != 1 || !valid_hash(c.chart_hash) ||
        !valid_hash(c.source_sha256) || !valid_hash(c.output_profile_sha256) ||
        !valid_text(c.print_route, 64) || !valid_text(c.instrument) ||
        !valid_text(c.illuminant, 32) || !valid_text(c.observer, 16) ||
        !valid_text(c.measurement_mode, 32) || !valid_text(c.backing, 64) ||
        !valid_text(c.measured_at, 64) || run.patches.empty() ||
        run.patches.size() > kMaximumPatches) return false;
    std::unordered_set<std::string_view> ids;
    ids.reserve(run.patches.size());
    for (const auto& p : run.patches) {
        if (!valid_text(p.sample_id) || !std::isfinite(p.lab.l) ||
            !std::isfinite(p.lab.a) || !std::isfinite(p.lab.b) || p.lab.l < 0.0 ||
            p.lab.l > 100.0 || p.lab.a < -200.0 || p.lab.a > 200.0 ||
            p.lab.b < -200.0 || p.lab.b > 200.0 || !ids.insert(p.sample_id).second)
            return false;
        if (p.xyz) for (const auto value : *p.xyz)
            if (!std::isfinite(value) || value < 0.0 || value > 10000.0) return false;
    }
    return true;
}

bool write_atomic(const std::filesystem::path& path, std::span<const uint8_t> bytes,
                  std::string* error) {
    if (path.empty() || !path.has_filename()) {
        if (error) *error = "Measurement run path must name a file.";
        return false;
    }
    auto temp = path;
    static std::atomic<uint64_t> sequence{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    temp += ".pikdraw-tmp-" + std::to_string(stamp) + "-" +
            std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
    std::error_code ec;
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file || !file.write(reinterpret_cast<const char*>(bytes.data()),
                                  static_cast<std::streamsize>(bytes.size())) || !file.flush()) {
            file.close(); std::filesystem::remove(temp, ec);
            if (error) *error = "Could not write the complete temporary measurement run.";
            return false;
        }
    }
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto code = GetLastError(); std::filesystem::remove(temp, ec);
        if (error) *error = "Could not atomically replace measurement run (Windows error " +
                            std::to_string(code) + ").";
        return false;
    }
#else
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        if (error) *error = "Could not atomically replace measurement run.";
        return false;
    }
#endif
    return true;
}
}  // namespace

std::vector<uint8_t> serialize_measurement_run(const MeasurementRun& run, std::string* error) {
    if (!valid_run(run)) {
        if (error) *error = "Measurement run is invalid or exceeds the supported schema limits.";
        return {};
    }
    Writer payload;
    const auto& c = run.condition;
    for (const auto* text : {&c.chart_hash, &c.source_sha256, &c.output_profile_sha256,
         &c.print_route, &c.instrument, &c.illuminant, &c.observer,
         &c.measurement_mode, &c.backing, &c.measured_at}) {
        if (!payload.string(*text)) goto too_large;
    }
    payload.u32(static_cast<uint32_t>(run.patches.size()));
    for (const auto& p : run.patches) {
        if (!payload.string(p.sample_id)) goto too_large;
        payload.f64(p.lab.l); payload.f64(p.lab.a); payload.f64(p.lab.b);
        payload.u8(p.xyz ? 1u : 0u);
        if (p.xyz) for (const auto value : *p.xyz) payload.f64(value);
        if (payload.bytes.size() > kMaximumBytes - 16) goto too_large;
    }
    {
        Writer out;
        out.bytes.insert(out.bytes.end(), std::begin(kMagic), std::end(kMagic));
        out.u16(1); out.u16(0); out.u32(static_cast<uint32_t>(payload.bytes.size()));
        out.bytes.insert(out.bytes.end(), payload.bytes.begin(), payload.bytes.end());
        out.u32(crc32(out.bytes));
        if (error) error->clear();
        return out.bytes;
    }
too_large:
    if (error) *error = "Measurement run exceeds the 64 MiB artifact limit.";
    return {};
}

MeasurementRunStoreResult deserialize_measurement_run(std::span<const uint8_t> bytes) {
    auto fail = [](std::string message) { return MeasurementRunStoreResult{std::nullopt, std::move(message)}; };
    if (bytes.size() < 16 || bytes.size() > kMaximumBytes) return fail("Measurement run size is invalid.");
    if (!std::equal(std::begin(kMagic), std::end(kMagic), bytes.begin()))
        return fail("Measurement run magic is invalid.");
    Reader header{bytes.subspan(4)};
    uint16_t version = 0, flags = 0; uint32_t payload_size = 0;
    if (!header.u16(version) || !header.u16(flags) || !header.u32(payload_size) ||
        version != 1 || flags != 0 || payload_size != bytes.size() - 16)
        return fail("Measurement run header is unsupported or inconsistent.");
    Reader crc_reader{bytes.subspan(bytes.size() - 4)}; uint32_t stored_crc = 0;
    if (!crc_reader.u32(stored_crc) || crc32(bytes.first(bytes.size() - 4)) != stored_crc)
        return fail("Measurement run checksum mismatch.");
    Reader in{bytes.subspan(12, payload_size)};
    MeasurementRun run;
    auto& c = run.condition;
    for (auto* text : {&c.chart_hash, &c.source_sha256, &c.output_profile_sha256,
         &c.print_route, &c.instrument, &c.illuminant, &c.observer,
         &c.measurement_mode, &c.backing, &c.measured_at})
        if (!in.string(*text)) return fail("Measurement run metadata is truncated or invalid.");
    uint32_t count = 0;
    if (!in.u32(count) || count == 0 || count > kMaximumPatches)
        return fail("Measurement run patch count is invalid.");
    run.patches.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        MeasuredPatch p; uint8_t has_xyz = 0;
        if (!in.string(p.sample_id) || !in.f64(p.lab.l) || !in.f64(p.lab.a) ||
            !in.f64(p.lab.b) || !in.u8(has_xyz) || has_xyz > 1)
            return fail("Measurement run patch is truncated or invalid.");
        if (has_xyz) {
            std::array<double, 3> xyz{};
            for (auto& value : xyz) if (!in.f64(value)) return fail("Measurement XYZ is invalid.");
            p.xyz = xyz;
        }
        run.patches.push_back(std::move(p));
    }
    if (in.offset != in.bytes.size() || !valid_run(run))
        return fail("Measurement run has trailing data, duplicate IDs, or invalid values.");
    return {std::move(run), {}};
}

bool save_measurement_run(const std::filesystem::path& path, const MeasurementRun& run,
                          std::string* error) {
    auto bytes = serialize_measurement_run(run, error);
    return !bytes.empty() && write_atomic(path, bytes, error);
}

MeasurementRunStoreResult load_measurement_run(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size < 16 || size > kMaximumBytes)
        return {std::nullopt, "Measurement run file is missing or outside size limits."};
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    std::ifstream file(path, std::ios::binary);
    if (!file || !file.read(reinterpret_cast<char*>(bytes.data()),
                             static_cast<std::streamsize>(bytes.size())) ||
        file.peek() != std::char_traits<char>::eof())
        return {std::nullopt, "Could not read the complete measurement run file."};
    return deserialize_measurement_run(bytes);
}
}  // namespace pikdraw::production::measurement
