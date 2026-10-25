#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include "pikdraw/interop/adapters/svg_worker_adapter.hpp"
#include "pikdraw/interop/adapters/raster_image_decoder.hpp"
#include "pikdraw/workers/appcontainer.hpp"
#include "pikdraw/workers/process_launcher.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <windows.h>
#include <securityappcontainer.h>
#include <stdexcept>
#include <string_view>
#include <utility>

using namespace pikdraw::interop;
using namespace pikdraw::workers;
namespace fs = std::filesystem;

namespace {

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
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

uint32_t crc32(std::span<const uint8_t> bytes) {
    uint32_t crc = 0xFFFFFFFFu;
    for (const auto byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return ~crc;
}

void append_png_chunk(std::vector<uint8_t>& png, const char type[4],
                      std::span<const uint8_t> payload) {
    append_be32(png, static_cast<uint32_t>(payload.size()));
    const size_t crc_start = png.size();
    for (size_t index = 0; index < 4; ++index)
        png.push_back(static_cast<uint8_t>(type[index]));
    png.insert(png.end(), payload.begin(), payload.end());
    append_be32(png, crc32(std::span<const uint8_t>(png).subspan(crc_start)));
}

std::vector<uint8_t> make_png(uint32_t width = 2, uint32_t height = 1) {
    std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> header;
    append_be32(header, width);
    append_be32(header, height);
    header.insert(header.end(), {8, 6, 0, 0, 0});
    append_png_chunk(png, "IHDR", header);

    std::vector<uint8_t> scanline;
    if (width == 2 && height == 1) scanline = {0, 255, 0, 0, 255, 0, 255, 0, 128};
    else scanline.resize((1 + static_cast<size_t>(width) * 4) * height, 0);
    std::vector<uint8_t> zlib{0x78, 0x01};
    size_t offset = 0;
    while (offset < scanline.size()) {
        const auto length = static_cast<uint16_t>((std::min)(size_t{32768}, scanline.size() - offset));
        zlib.push_back(offset + length == scanline.size() ? 1 : 0);
        zlib.push_back(static_cast<uint8_t>(length));
        zlib.push_back(static_cast<uint8_t>(length >> 8));
        const auto inverse = static_cast<uint16_t>(~length);
        zlib.push_back(static_cast<uint8_t>(inverse));
        zlib.push_back(static_cast<uint8_t>(inverse >> 8));
        zlib.insert(zlib.end(), scanline.begin() + offset, scanline.begin() + offset + length);
        offset += length;
    }
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

std::vector<uint8_t> make_two_page_tiff() {
    constexpr uint32_t ifd_bytes = 2 + 11 * 12 + 4;
    constexpr uint32_t second_ifd = 8 + ifd_bytes;
    constexpr uint32_t bits_offset = second_ifd + ifd_bytes;
    constexpr uint32_t first_pixels = bits_offset + 6;
    constexpr uint32_t second_pixels = first_pixels + 6;
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
    append_ifd(first_pixels, second_ifd);
    append_ifd(second_pixels, 0);
    append_le16(tiff, 8);
    append_le16(tiff, 8);
    append_le16(tiff, 8);
    tiff.insert(tiff.end(), {255, 0, 0, 0, 255, 0});
    tiff.insert(tiff.end(), {0, 0, 255, 255, 255, 255});
    return tiff;
}

std::vector<uint8_t> read_system_swop_profile() {
    std::array<wchar_t, MAX_PATH> directory{};
    const UINT length = GetWindowsDirectoryW(directory.data(),
                                              static_cast<UINT>(directory.size()));
    if (length == 0 || length >= directory.size()) return {};
    fs::path path(directory.data());
    path /= L"System32/spool/drivers/color/RSWOP.icm";
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    const std::string raw{std::istreambuf_iterator<char>(input),
                          std::istreambuf_iterator<char>()};
    return {raw.begin(), raw.end()};
}

std::vector<uint8_t> make_cmyk_icc_tiff(std::span<const uint8_t> profile) {
    constexpr uint16_t entry_count = 12;
    constexpr uint32_t ifd_bytes = 2 + entry_count * 12 + 4;
    constexpr uint32_t bits_offset = 8 + ifd_bytes;
    const auto profile_offset = bits_offset + 8;
    const uint64_t pixel_offset64 = static_cast<uint64_t>(profile_offset) + profile.size();
    Require(!profile.empty() && profile.size() <= UINT32_MAX &&
            pixel_offset64 <= UINT32_MAX, "SWOP profile does not fit the TIFF fixture");
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
    append_tiff_entry(tiff, 34675, 7, static_cast<uint32_t>(profile.size()), profile_offset);
    append_le32(tiff, 0); // no next IFD
    for (int channel = 0; channel < 4; ++channel) append_le16(tiff, 8);
    tiff.insert(tiff.end(), profile.begin(), profile.end());
    tiff.insert(tiff.end(), {0, 0, 0, 0, 0, 0, 0, 128,
                             0, 0, 0, 255}); // white, mid-K, solid K
    return tiff;
}

struct TemporarySource final {
    fs::path path;
    ~TemporarySource() {
        std::error_code ignored;
        if (!path.empty()) fs::remove(path, ignored);
    }

    bool Write(std::span<const uint8_t> bytes) {
        path = fs::temp_directory_path() /
            ("pikdraw-raster-worker-" + std::to_string(GetCurrentProcessId()) + "-" +
             std::to_string(GetTickCount64()) + ".png");
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(file);
    }
};

struct TemporaryWorkerPackage final {
    fs::path root;
    fs::path executable;
    ~TemporaryWorkerPackage() {
        std::error_code ignored;
        if (!root.empty()) fs::remove_all(root, ignored);
    }

    bool Create() {
        root = fs::current_path() /
            ("pikdraw-raster-worker-bin-" + std::to_string(GetCurrentProcessId()) + "-" +
             std::to_string(GetTickCount64()));
        if (!fs::create_directory(root)) return false;
        wchar_t mod[32768]{};
        GetModuleFileNameW(nullptr, mod, static_cast<DWORD>(std::size(mod)));
        const auto self_dir = fs::path(mod).parent_path();
        auto find_file = [&](const std::string& name) -> fs::path {
            if (fs::exists(fs::current_path() / name)) return fs::current_path() / name;
            if (fs::exists(self_dir / name)) return self_dir / name;
            if (fs::exists(self_dir / "workers" / name)) return self_dir / "workers" / name;
            if (fs::exists(fs::current_path() / "platform" / "workers" / name))
                return fs::current_path() / "platform" / "workers" / name;
            return {};
        };
        auto source = find_file("spike_worker_test.exe");
        if (source.empty()) source = find_file("spike_worker.exe");
        const auto pdfium = find_file("pdfium.dll");
        executable = root / "spike_worker.exe";
        return !source.empty() && !pdfium.empty() &&
            fs::copy_file(source, executable) && fs::copy_file(pdfium, root / "pdfium.dll");
    }
};

std::vector<uint8_t> make_vector_pdf() {
    const std::string stream = "1 0 0 rg\n10 10 40 20 re\nf\nBT\n/F1 12 Tf\n20 60 Td\n(Hello) Tj\nET\n";
    const std::vector<std::string> objects{
        "<< /Type /Catalog /Pages 2 0 R /Creator (Adobe Illustrator 2025) >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] "
        "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
        "<< /Length " + std::to_string(stream.size()) + ">>\nstream\n" + stream + "endstream",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"
    };
    std::string pdf = "%PDF-1.4\n";
    std::vector<size_t> offsets(objects.size() + 1, 0);
    for (size_t index = 0; index < objects.size(); ++index) {
        offsets[index + 1] = pdf.size();
        pdf += std::to_string(index + 1) + " 0 obj\n" + objects[index] + "\nendobj\n";
    }
    const size_t xref_offset = pdf.size();
    pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (size_t index = 1; index < offsets.size(); ++index) {
        char line[32]{};
        std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offsets[index]);
        pdf += line;
    }
    pdf += "trailer\n<< /Size " + std::to_string(objects.size() + 1) +
        " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref_offset) + "\n%%EOF\n";
    return {pdf.begin(), pdf.end()};
}

struct Capture final {
    size_t callbacks{0};
    size_t chunks{0};
    bool ended{false};
    bool records_valid{true};
    std::vector<ImportRecordValue> records;
};

bool capture_frame(const IpcV2Frame& frame, Capture& capture) {
    ++capture.callbacks;
    const auto type = static_cast<MessageType>(frame.header.message_type);
    const auto packet = SerializeV2Frame(type, frame.header.sequence_id, frame.payload);
    if (type == MessageType::V2ProposalChunk) {
        const auto chunk = DeserializeV2ProposalChunk(packet);
        if (!chunk) return capture.records_valid = false;
        ++capture.chunks;
        size_t offset = 0;
        for (uint32_t index = 0; index < chunk->record_count; ++index) {
            if (chunk->canonical_record_bytes.size() - offset < 8)
                return capture.records_valid = false;
            const uint16_t raw_type = static_cast<uint16_t>(chunk->canonical_record_bytes[offset]) |
                static_cast<uint16_t>(static_cast<uint16_t>(chunk->canonical_record_bytes[offset + 1]) << 8);
            const uint16_t flags = static_cast<uint16_t>(chunk->canonical_record_bytes[offset + 2]) |
                static_cast<uint16_t>(static_cast<uint16_t>(chunk->canonical_record_bytes[offset + 3]) << 8);
            const uint32_t size = static_cast<uint32_t>(chunk->canonical_record_bytes[offset + 4]) |
                (static_cast<uint32_t>(chunk->canonical_record_bytes[offset + 5]) << 8) |
                (static_cast<uint32_t>(chunk->canonical_record_bytes[offset + 6]) << 16) |
                (static_cast<uint32_t>(chunk->canonical_record_bytes[offset + 7]) << 24);
            offset += 8;
            if (flags != 0 || raw_type < 1 ||
                raw_type > static_cast<uint16_t>(ImportRecordType::RasterImage64) ||
                size > chunk->canonical_record_bytes.size() - offset)
                return capture.records_valid = false;
            auto record = DeserializeImportRecord(static_cast<ImportRecordType>(raw_type),
                std::span<const uint8_t>(chunk->canonical_record_bytes).subspan(offset, size));
            if (!record) return capture.records_valid = false;
            capture.records.push_back(std::move(*record));
            offset += size;
        }
        return offset == chunk->canonical_record_bytes.size();
    }
    if (type == MessageType::V2ImportEnd) {
        const auto end = DeserializeV2ImportEnd(packet);
        if (!end || !end->fidelity_report_complete)
            return capture.records_valid = false;
        capture.ended = true;
        return true;
    }
    return capture.records_valid = false;
}

std::optional<SourceSnapshot> snapshot_for(const fs::path& path) {
    return SourceSnapshot::Create(path.wstring(), 64ULL * 1024ULL * 1024ULL);
}

struct RunResult final {
    bool complete{false};
    std::string failure;
    Capture capture;
};

RunResult run_import(const SourceSnapshot& source, std::string format_id = "PNG",
                     std::string profile_id = "raster-rgba8-v1",
                     std::optional<uint32_t> source_page_index = std::nullopt) {
    TemporaryWorkerPackage package;
    Require(package.Create(), "could not stage the production worker for AppContainer execution");
    const auto profile_name = L"PikDraw.Raster.Import.Tests." +
        std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
    PSID raw_sid = nullptr;
    Require(SUCCEEDED(DeriveAppContainerSidFromAppContainerName(profile_name.c_str(), &raw_sid)) && raw_sid,
            "could not derive a unique raster worker AppContainer SID");
    ScopedSid sid(raw_sid);
    Require(AppContainerProfileGuard::GrantDirectoryAccess(package.root.wstring(), sid.Get()),
            "could not grant the AppContainer execute access to its owned worker directory");
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(package.root, ec)) {
        if (entry.is_regular_file()) {
            AppContainerProfileGuard::GrantDirectoryAccess(entry.path().wstring(), sid.Get());
        }
    }

    WorkerLaunchConfig config;
    config.worker_executable_path = package.executable.wstring();
    config.working_directory = package.root.wstring();
    config.appcontainer_profile_name = profile_name;
    config.use_appcontainer = true;
    config.source_snapshot = &source;
    config.default_timeout = std::chrono::seconds(5);

    WorkerProcessHost host;
    const bool launched = host.Launch(config);
    const DWORD launch_error = GetLastError();
    if (!launched)
        throw std::runtime_error("could not launch the production worker in AppContainer (Win32 " +
                                 std::to_string(launch_error) + ")");
    Require(host.NegotiateV2(IPC_V2_SUPPORTED_FEATURES, std::chrono::seconds(3)),
            "test worker did not negotiate protocol v2");

    V2ImportBegin begin;
    begin.job.document_id = "raster-worker-import-test";
    begin.job.epoch = 3;
    begin.job.base_revision = 5;
    begin.job.generation = 7;
    begin.job.source_token = source.Token();
    begin.source_bytes = source.SizeBytes();
    begin.source_sha256 = source.Sha256();
    begin.format_id = std::move(format_id);
    begin.profile_id = std::move(profile_id);
    begin.source_page_index = source_page_index;
    const auto request = SerializeV2ImportBegin(begin, 1);
    RunResult result;
    // No aggregate byte quota: encoded source and decoded pixels are both
    // present in the proposal. Keep only format/record structural bounds.
    const V2ProposalStreamLimits limits{
        std::numeric_limits<uint64_t>::max(),
        std::numeric_limits<uint64_t>::max(),
        IPC_MAX_OBJECTS,
        IPC_MAX_OBJECTS,
        IPC_MAX_OBJECTS + 1};
    result.complete = host.TransactV2ProposalStream(request, std::chrono::seconds(5), limits,
        [&](const IpcV2Frame& frame) { return capture_frame(frame, result.capture); });
    result.failure = host.LastTransportFailure();
    return result;
}

void test_png_worker_stream() {
    TemporarySource file;
    const auto bytes = make_png();
    Require(file.Write(bytes), "could not write PNG test input");
    auto source = snapshot_for(file.path);
    Require(source && source->IsValid(), "could not snapshot PNG test input");
    const auto result = run_import(*source);
    Require(result.complete, ("worker did not complete the bounded raster stream: " +
                              result.failure).c_str());
    Require(result.capture.records_valid && result.capture.ended,
            "worker returned malformed raster records or no end record");
    Require(result.capture.chunks == 1,
            "small raster proposal did not fit in one transport frame");
    Require(std::holds_alternative<ImportPageRecord>(result.capture.records[0]),
            "raster proposal did not begin with page metadata");
    Require(std::holds_alternative<ImportLayerRecord>(result.capture.records[1]),
            "raster proposal has no image layer");
    const auto& image = std::get<ImportRasterImageRecord>(result.capture.records[2]);
    Require(image.media_type == "image/png" && image.width_px == 2 && image.height_px == 1 &&
            image.decoded_pixel_bytes == 8 && image.frame_count == 1 &&
            image.source_data_local_id == image.local_id &&
            image.source_bytes_size == bytes.size(),
            "raster metadata was altered or lost in the worker stream");
    const ImportRasterImageChunkRecord* pixels = nullptr;
    const ImportRasterSourceChunkRecord* original_source = nullptr;
    const ImportFidelityIssueRecord* issue = nullptr;
    const ImportFidelityReportRecord* report = nullptr;
    for (const auto& record : result.capture.records) {
        if (const auto* value = std::get_if<ImportRasterImageChunkRecord>(&record)) pixels = value;
        else if (const auto* value = std::get_if<ImportRasterSourceChunkRecord>(&record)) original_source = value;
        else if (const auto* value = std::get_if<ImportFidelityIssueRecord>(&record)) issue = value;
        else if (const auto* value = std::get_if<ImportFidelityReportRecord>(&record)) report = value;
    }
    Require(pixels && pixels->local_id == image.local_id && pixels->byte_offset == 0 &&
            pixels->total_bytes == 8 && pixels->bytes.size() == 8,
            "raster pixels were not streamed in order");
    Require(original_source && original_source->local_id == image.local_id &&
            original_source->byte_offset == 0 &&
            original_source->total_bytes == bytes.size() && original_source->bytes == bytes,
            "encoded raster source was not streamed and preserved");
    Require(issue != nullptr, "raster conversion warning is missing");
    Require(report != nullptr, "raster conversion report is missing");
    Require(report->coverage_profile == "raster-rgba8-v1" &&
            report->editability == ImportEditability::ExplicitConversion &&
            report->production == ImportProduction::NotAssessed,
            "raster conversion fidelity status is missing");
}

void test_cmyk_icc_tiff_worker_stream() {
    const auto profile = read_system_swop_profile();
    if (profile.empty()) {
        std::cout << "[NOT_MEASURED] Installed SWOP profile unavailable for worker ICC stream check\n";
        return;
    }
    const auto bytes = make_cmyk_icc_tiff(profile);
    const auto decoded = RasterImageDecoder::decode(bytes);
    Require(decoded.success && decoded.image.pixel_color_basis ==
                RasterPixelColorBasis::ConvertedToSRgb,
            "direct decoder did not apply the embedded SWOP profile");

    TemporarySource file;
    Require(file.Write(bytes), "could not write CMYK TIFF profile test input");
    auto source = snapshot_for(file.path);
    Require(source && source->IsValid(), "could not snapshot CMYK TIFF profile input");
    const auto result = run_import(*source, "TIFF", "raster-rgba8-v1");
    Require(result.complete && result.capture.records_valid && result.capture.ended,
            "worker did not complete the CMYK TIFF profile stream");

    const ImportRasterImageRecord* image = nullptr;
    const ImportFidelityReportRecord* report = nullptr;
    for (const auto& record : result.capture.records) {
        if (const auto* value = std::get_if<ImportRasterImageRecord>(&record)) image = value;
        if (const auto* value = std::get_if<ImportFidelityReportRecord>(&record)) report = value;
    }
    Require(image != nullptr, "worker omitted the CMYK raster image record");
    if (image->icc_profile != profile || image->preview_pixel_color_basis != 2 ||
        image->source_bytes_size != bytes.size()) {
        std::cerr << "CMYK ICC IPC diagnostic: profile=" << image->icc_profile.size() << '/'
                  << profile.size() << " basis="
                  << static_cast<int>(image->preview_pixel_color_basis)
                  << " source=" << image->source_bytes_size << '/' << bytes.size() << '\n';
    }
    Require(image->width_px == decoded.image.width_px &&
            image->height_px == decoded.image.height_px,
            "worker IPC changed CMYK raster dimensions");
    Require(image->icc_profile == profile &&
            image->preview_pixel_color_basis == 2 && // IPC basis: ICC converted to sRGB.
            image->source_bytes_size == bytes.size(),
            "worker IPC changed ICC source, converted basis or source length");
    Require(report && report->coverage_profile == "raster-rgba8-v1",
            "worker omitted the raster fidelity report");

    std::vector<uint8_t> streamed_pixels(decoded.image.pixels_rgba.size());
    std::vector<uint8_t> streamed_source(bytes.size());
    uint32_t pixel_offset = 0;
    uint32_t source_offset = 0;
    for (const auto& record : result.capture.records) {
        if (const auto* pixels = std::get_if<ImportRasterImageChunkRecord>(&record)) {
            Require(pixels->local_id == image->local_id &&
                    pixels->byte_offset == pixel_offset &&
                    pixels->total_bytes == decoded.image.pixels_rgba.size() &&
                    pixels->bytes.size() <= streamed_pixels.size() - pixel_offset,
                    "worker ICC-transformed pixel chunks are incomplete or out of order");
            std::copy(pixels->bytes.begin(), pixels->bytes.end(),
                      streamed_pixels.begin() + pixel_offset);
            pixel_offset += static_cast<uint32_t>(pixels->bytes.size());
        } else if (const auto* source_chunk =
                       std::get_if<ImportRasterSourceChunkRecord>(&record)) {
            Require(source_chunk->local_id == image->local_id &&
                    source_chunk->byte_offset == source_offset &&
                    source_chunk->total_bytes == bytes.size() &&
                    source_chunk->bytes.size() <= streamed_source.size() - source_offset,
                    "worker ICC source chunks are incomplete or out of order");
            std::copy(source_chunk->bytes.begin(), source_chunk->bytes.end(),
                      streamed_source.begin() + source_offset);
            source_offset += static_cast<uint32_t>(source_chunk->bytes.size());
        }
    }
    Require(pixel_offset == streamed_pixels.size() &&
            streamed_pixels == decoded.image.pixels_rgba,
            "worker IPC changed ICC-converted pixel values");
    Require(source_offset == bytes.size() && streamed_source == bytes,
            "worker IPC did not preserve the exact encoded CMYK source");
}

void test_raster_stream_exceeds_single_frame() {
    TemporarySource file;
    const auto bytes = make_png(600, 600);
    Require(file.Write(bytes), "could not write multi-frame PNG source");
    auto source = snapshot_for(file.path);
    Require(source && source->IsValid(), "could not snapshot multi-frame PNG source");
    const auto result = run_import(*source);
    Require(result.complete && result.capture.ended && result.capture.records_valid,
            "raster larger than a single IPC frame was rejected");
    Require(result.capture.chunks > 1, "large raster did not span proposal frames");
    uint32_t expected_offset = 0;
    for (const auto& record : result.capture.records) {
        if (const auto* pixels = std::get_if<ImportRasterImageChunkRecord>(&record)) {
            Require(pixels->byte_offset == expected_offset, "large raster pixels have a gap");
            expected_offset += static_cast<uint32_t>(pixels->bytes.size());
        }
    }
    Require(expected_offset == 600u * 600u * 4u, "large raster pixels were truncated");
}

void test_tiff_selected_page_stream() {
    TemporarySource file;
    const auto bytes = make_two_page_tiff();
    Require(file.Write(bytes), "could not write multi-page TIFF source");
    auto source = snapshot_for(file.path);
    Require(source && source->IsValid(), "could not snapshot multi-page TIFF source");
    const auto result = run_import(*source, "TIFF", "raster-rgba8-v1", 1);
    Require(result.complete && result.capture.ended && result.capture.records_valid,
            "worker rejected selected TIFF frame 2");
    const ImportRasterImageRecord* image = nullptr;
    const ImportRasterImageChunkRecord* pixels = nullptr;
    for (const auto& record : result.capture.records) {
        if (const auto* value = std::get_if<ImportRasterImageRecord>(&record)) image = value;
        if (const auto* value = std::get_if<ImportRasterImageChunkRecord>(&record)) pixels = value;
    }
    Require(image && image->frame_count == 2 && image->frame_index == 1,
            "selected TIFF frame metadata was not retained");
    Require(pixels && pixels->bytes.size() >= 4 && pixels->bytes[0] < 8 &&
            pixels->bytes[2] > 240,
            "worker did not decode pixels from the selected TIFF page");
}

void test_malformed_png_rejected_without_partial_proposal() {
    TemporarySource file;
    const std::array<uint8_t, 8> bytes{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    Require(file.Write(bytes), "could not write malformed PNG input");
    auto source = snapshot_for(file.path);
    Require(source && source->IsValid(), "could not snapshot malformed PNG input");
    const auto result = run_import(*source);
    Require(!result.complete && result.capture.callbacks == 0 &&
            result.failure == "worker returned v2 error response",
            "malformed PNG was accepted or leaked a partial proposal");
}

void test_pdf_and_pdf_compatible_ai_stream_editable_path_text() {
    TemporarySource file;
    const auto bytes = make_vector_pdf();
    Require(file.Write(bytes), "could not write PDF vector source");
    auto source = snapshot_for(file.path);
    Require(source && source->IsValid(), "could not snapshot PDF vector source");
    for (const auto& [format, profile] : std::array<std::pair<std::string_view, std::string_view>, 2>{{
             {"PDF", "pdf-page-vector-v2"}, {"AI", "ai-pdf-compatible-vector-v2"}}}) {
        const auto result = run_import(*source, std::string(format), std::string(profile));
        Require(result.complete && result.capture.ended && result.capture.records_valid,
                "worker did not complete an editable PDF/AI path-text proposal");
        bool saw_path = false, saw_text = false, saw_image = false, saw_report = false, saw_ai_warning = false;
        for (const auto& record : result.capture.records) {
            saw_path |= std::holds_alternative<ImportPathRecord>(record);
            saw_text |= std::holds_alternative<ImportTextRecord>(record);
            saw_image |= std::holds_alternative<ImportRasterImageRecord>(record);
            if (const auto* issue = std::get_if<ImportFidelityIssueRecord>(&record))
                saw_ai_warning |= issue->feature_code == "ai-authoring-data-not-read";
            if (const auto* report = std::get_if<ImportFidelityReportRecord>(&record)) {
                saw_report = report->coverage == ImportCoverage::Partial &&
                    report->editability == ImportEditability::NativeEditable;
                Require(report->coverage_profile == profile, "worker changed the PDF/AI coverage profile");
            }
        }
        Require(saw_path && saw_text && !saw_image && saw_report,
                "worker did not produce native path/text without a thumbnail raster");
        Require(format == "AI" ? saw_ai_warning : !saw_ai_warning,
                "AI private-data loss warning does not match the selected source representation");
    }
}

void test_preview_only_vector_profiles_are_rejected_without_proposals() {
    TemporarySource file;
    const auto bytes = make_png();
    Require(file.Write(bytes), "could not write source for unsupported-profile checks");
    auto source = snapshot_for(file.path);
    Require(source && source->IsValid(), "could not snapshot unsupported-profile source");

    const std::array<std::pair<std::string_view, std::string_view>, 4> profiles{{
        {"CDR", "cdr-preview-v1"},
        {"AI", "pdf-ai-preview-v1"},
        {"PDF", "pdf-ai-preview-v1"},
        {"EPS", "eps-preview-v1"}
    }};
    for (const auto& [format, profile] : profiles) {
        const auto result = run_import(*source, std::string(format), std::string(profile));
        Require(!result.complete && result.capture.callbacks == 0 &&
                result.capture.records.empty() &&
                (result.failure == "worker returned v2 error response" ||
                 result.failure == "unsupported format import profile"),
                (std::string("legacy preview profile was not rejected for ") + std::string(format) +
                 ": " + result.failure).c_str());
    }
}

} // namespace

int main() {
    try {
        test_png_worker_stream();
        test_cmyk_icc_tiff_worker_stream();
        test_tiff_selected_page_stream();
        test_raster_stream_exceeds_single_frame();
        test_malformed_png_rejected_without_partial_proposal();
        test_pdf_and_pdf_compatible_ai_stream_editable_path_text();
        test_preview_only_vector_profiles_are_rejected_without_proposals();
        std::cout << "Raster import and unsupported vector-profile worker tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Raster worker import tests failed: " << error.what() << '\n';
        return 1;
    }
}
