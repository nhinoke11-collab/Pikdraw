#include "pikdraw/desktop/workflows/document_workflow.hpp"

#include "pikdraw/document/commands/import_content_command.hpp"
#include "pikdraw/interop/adapters/raster_image_decoder.hpp"
#include "pikdraw/interop/adapters/svg_worker_adapter.hpp"
#include "pikdraw/raster/asset_store.hpp"
#include "pikdraw/raster/memory_budget.hpp"
#include "pikdraw/workers/job_object.hpp"
#include "pikdraw/workers/process_launcher.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <limits>
#include <type_traits>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pikdraw::desktop {
namespace {

constexpr uint64_t kMaxImportResponseBytes = std::numeric_limits<uint64_t>::max();
constexpr uint64_t kMaxImportCanonicalBytes = std::numeric_limits<uint64_t>::max();
constexpr uint64_t kMaxImportRecords = pikdraw::interop::IPC_MAX_OBJECTS;
constexpr uint32_t kMaxImportChunks = pikdraw::interop::IPC_MAX_OBJECTS;
constexpr uint32_t kMaxImportFrames = pikdraw::interop::IPC_MAX_OBJECTS + 1;
constexpr uint32_t kMaxRichImportChunks = pikdraw::interop::IPC_MAX_OBJECTS;
constexpr uint32_t kMaxRichImportFrames = pikdraw::interop::IPC_MAX_OBJECTS + 1;

enum class DetectedImportFormat { Svg, Dxf, Png, Jpeg, Tiff, Cdr, Ai, Pdf, Eps, Bmp };

struct SourceSignature {
    DetectedImportFormat format{DetectedImportFormat::Svg};
    bool recognized{false};
    bool dos_eps_wrapper{false};
};

bool ReadPrefix(HANDLE source, std::array<uint8_t, 4096>& prefix, size_t& count) {
    LARGE_INTEGER beginning{};
    if (!SetFilePointerEx(source, beginning, nullptr, FILE_BEGIN)) return false;
    DWORD read = 0;
    if (!ReadFile(source, prefix.data(), static_cast<DWORD>(prefix.size()), &read, nullptr)) return false;
    count = read;
    return SetFilePointerEx(source, beginning, nullptr, FILE_BEGIN) != FALSE;
}

SourceSignature DetectSignature(HANDLE source) {
    std::array<uint8_t, 4096> bytes{};
    size_t count = 0;
    if (!ReadPrefix(source, bytes, count)) return {};
    const auto starts_with = [&](std::initializer_list<uint8_t> signature) {
        return count >= signature.size() && std::equal(signature.begin(), signature.end(), bytes.begin());
    };
    if (starts_with({0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A}))
        return {DetectedImportFormat::Png, true};
    if (starts_with({0xFF, 0xD8, 0xFF}))
        return {DetectedImportFormat::Jpeg, true};
    if (starts_with({'I', 'I', 0x2A, 0x00}) || starts_with({'M', 'M', 0x00, 0x2A}))
        return {DetectedImportFormat::Tiff, true};
    if (starts_with({'B', 'M'}))
        return {DetectedImportFormat::Bmp, true};
    if (starts_with({'P', 'K', 0x03, 0x04}) ||
        (count >= 12 && starts_with({'R', 'I', 'F', 'F'}) &&
         bytes[8] == 'C' && bytes[9] == 'D' && bytes[10] == 'R'))
        return {DetectedImportFormat::Cdr, true};
    if (starts_with({0xC5, 0xD0, 0xD3, 0xC6}))
        return {DetectedImportFormat::Eps, true, true};
    if (starts_with({'%', 'P', 'D', 'F', '-'})) {
        std::string_view pdf_text(reinterpret_cast<const char*>(bytes.data()), count);
        if (pdf_text.find("Illustrator") != std::string_view::npos ||
            pdf_text.find("AIPrivateData") != std::string_view::npos ||
            pdf_text.find("xmpGImg") != std::string_view::npos) {
            return {DetectedImportFormat::Ai, true};
        }
        return {DetectedImportFormat::Pdf, true};
    }
    std::string_view text(reinterpret_cast<const char*>(bytes.data()), count);
    if (text.starts_with("%!PS-Adobe-") && text.find("EPSF-") != std::string_view::npos)
        return {DetectedImportFormat::Eps, true};
    if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
    const auto svg = text.find("<svg");
    if (svg != std::string_view::npos && svg < 2048) return {DetectedImportFormat::Svg, true};
    const auto section = text.find("SECTION");
    const auto entities = text.find("ENTITIES");
    if (section != std::string_view::npos && entities != std::string_view::npos &&
        section < 2048 && entities < 4096 && text.find('\0') == std::string_view::npos)
        return {DetectedImportFormat::Dxf, true};
    return {};
}

bool ReadOriginalSource(HANDLE source, uint64_t expected_size,
                        std::vector<uint8_t>& bytes,
                        pikdraw::workers::SystemMemoryPressureWaiter& memory_waiter,
                        std::stop_token cancellation) {
    if (source == INVALID_HANDLE_VALUE || expected_size == 0 ||
        expected_size > SIZE_MAX ||
        memory_waiter.WaitForHeadroom(static_cast<size_t>(expected_size), cancellation) !=
            pikdraw::workers::SystemMemoryWaitResult::Ready)
        return false;
    LARGE_INTEGER beginning{};
    if (!SetFilePointerEx(source, beginning, nullptr, FILE_BEGIN)) return false;
    try {
        bytes.resize(static_cast<size_t>(expected_size));
    } catch (...) {
        return false;
    }
    size_t offset = 0;
    while (offset < bytes.size()) {
        if (memory_waiter.WaitForHeadroom(1, cancellation) !=
                pikdraw::workers::SystemMemoryWaitResult::Ready) return false;
        const DWORD requested = static_cast<DWORD>(
            (std::min)(bytes.size() - offset, static_cast<size_t>(1024 * 1024)));
        DWORD received = 0;
        if (!ReadFile(source, bytes.data() + offset, requested, &received, nullptr) ||
            received == 0)
            return false;
        offset += received;
    }
    uint8_t extra = 0;
    DWORD received = 0;
    const bool exact = ReadFile(source, &extra, 1, &received, nullptr) && received == 0;
    LARGE_INTEGER zero{};
    return exact && SetFilePointerEx(source, zero, nullptr, FILE_BEGIN) != FALSE;
}

std::string SourceDigestHex(const pikdraw::workers::SourceDigest& digest) {
    constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.reserve(digest.size() * 2);
    for (const uint8_t byte : digest) {
        result.push_back(kHex[byte >> 4]);
        result.push_back(kHex[byte & 0x0f]);
    }
    return result;
}

bool IsRasterExtension(const std::wstring& extension) noexcept {
    return extension == L".png" || extension == L".jpg" || extension == L".jpeg" ||
           extension == L".tif" || extension == L".tiff" || extension == L".bmp";
}

std::wstring FindGhostscriptExecutable() {
    std::vector<std::wstring> roots;
    for (const wchar_t* variable : {L"ProgramFiles", L"SystemDrive"}) {
        std::wstring value(32768, L'\0');
        const DWORD length = GetEnvironmentVariableW(variable, value.data(),
            static_cast<DWORD>(value.size()));
        if (length == 0 || length >= value.size()) continue;
        value.resize(length);
        if (std::wstring_view(variable) == L"SystemDrive") {
            roots.push_back(value + L"\\Program Files");
            roots.push_back(value + L"\\Program Files (x86)");
        } else {
            roots.push_back(std::move(value));
        }
    }
    for (const auto& root : roots) {
        WIN32_FIND_DATAW entry{};
        const std::wstring pattern = root + L"\\gs\\gs*";
        HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
        if (search == INVALID_HANDLE_VALUE) continue;
        std::vector<std::wstring> versions;
        do {
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 &&
                std::wstring_view(entry.cFileName).starts_with(L"gs"))
                versions.emplace_back(entry.cFileName);
        } while (FindNextFileW(search, &entry));
        FindClose(search);
        std::sort(versions.begin(), versions.end(), std::greater<>());
        for (const auto& version : versions) {
            const std::wstring candidate = root + L"\\gs\\" + version +
                L"\\bin\\gswin64c.exe";
            const DWORD attributes = GetFileAttributesW(candidate.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0)
                return candidate;
        }
    }
    return {};
}

class TemporaryImportDirectory final {
public:
    explicit TemporaryImportDirectory(std::wstring path) : path_(std::move(path)) {}
    TemporaryImportDirectory(const TemporaryImportDirectory&) = delete;
    TemporaryImportDirectory& operator=(const TemporaryImportDirectory&) = delete;
    TemporaryImportDirectory(TemporaryImportDirectory&& other) noexcept
        : path_(std::move(other.path_)) { other.path_.clear(); }
    TemporaryImportDirectory& operator=(TemporaryImportDirectory&&) = delete;
    ~TemporaryImportDirectory() {
        if (path_.empty()) return;
        std::error_code ignored;
        std::filesystem::remove_all(std::filesystem::path(path_), ignored);
    }

    [[nodiscard]] static std::optional<TemporaryImportDirectory> Create() {
        std::array<wchar_t, 32768> root{};
        const DWORD root_length = GetTempPathW(
            static_cast<DWORD>(root.size()), root.data());
        if (root_length == 0 || root_length >= root.size()) return std::nullopt;
        std::array<wchar_t, 32768> candidate{};
        if (!GetTempFileNameW(root.data(), L"pke", 0, candidate.data())) return std::nullopt;
        if (!DeleteFileW(candidate.data()) || !CreateDirectoryW(candidate.data(), nullptr)) {
            DeleteFileW(candidate.data());
            return std::nullopt;
        }
        return TemporaryImportDirectory(std::wstring(candidate.data()));
    }

    [[nodiscard]] const std::wstring& path() const noexcept { return path_; }

private:
    std::wstring path_;
};

std::optional<pikdraw::workers::SourceSnapshot> ConvertDosEpsToPdf(
    const pikdraw::workers::SourceSnapshot& source, std::string& diagnostic,
    pikdraw::workers::SystemMemoryPressureWaiter& memory_waiter,
    std::stop_token cancellation) {
    constexpr size_t kDosEpsHeaderBytes = 30;
    constexpr std::array<uint8_t, 4> kDosEpsMagic{0xC5, 0xD0, 0xD3, 0xC6};
    std::array<uint8_t, kDosEpsHeaderBytes> header{};
    LARGE_INTEGER beginning{};
    DWORD header_bytes = 0;
    if (!SetFilePointerEx(source.ReadHandle(), beginning, nullptr, FILE_BEGIN) ||
        !ReadFile(source.ReadHandle(), header.data(), static_cast<DWORD>(header.size()),
                  &header_bytes, nullptr) || header_bytes != header.size() ||
        !std::equal(kDosEpsMagic.begin(), kDosEpsMagic.end(), header.begin())) {
        diagnostic = "DOS EPS source could not be read from its verified snapshot";
        return std::nullopt;
    }
    const auto read_u32 = [&](size_t offset) {
        return static_cast<uint32_t>(header[offset]) |
            (static_cast<uint32_t>(header[offset + 1]) << 8) |
            (static_cast<uint32_t>(header[offset + 2]) << 16) |
            (static_cast<uint32_t>(header[offset + 3]) << 24);
    };
    const uint32_t postscript_offset = read_u32(4);
    const uint32_t postscript_length = read_u32(8);
    if (postscript_offset < kDosEpsHeaderBytes || postscript_length == 0 ||
        static_cast<uint64_t>(postscript_offset) > source.SizeBytes() ||
        static_cast<uint64_t>(postscript_length) > source.SizeBytes() - postscript_offset) {
        diagnostic = "DOS EPS PostScript section offsets are invalid";
        return std::nullopt;
    }
    if (memory_waiter.WaitForHeadroom(postscript_length, cancellation) !=
        pikdraw::workers::SystemMemoryWaitResult::Ready) {
        diagnostic = cancellation.stop_requested() ? "DOS EPS import was cancelled"
            : "System RAM pressure wait timed out after 5 minutes";
        return std::nullopt;
    }
    std::vector<uint8_t> postscript_bytes;
    try { postscript_bytes.resize(postscript_length); }
    catch (...) { diagnostic = "DOS EPS PostScript section does not fit available system RAM"; return std::nullopt; }
    LARGE_INTEGER postscript_start{};
    postscript_start.QuadPart = postscript_offset;
    if (!SetFilePointerEx(source.ReadHandle(), postscript_start, nullptr, FILE_BEGIN)) {
        diagnostic = "DOS EPS PostScript section could not be staged";
        return std::nullopt;
    }
    size_t postscript_cursor = 0;
    while (postscript_cursor < postscript_bytes.size()) {
        if (memory_waiter.WaitForHeadroom(1, cancellation) !=
                pikdraw::workers::SystemMemoryWaitResult::Ready) {
            diagnostic = cancellation.stop_requested() ? "DOS EPS import was cancelled"
                : "System RAM pressure wait timed out after 5 minutes";
            return std::nullopt;
        }
        const DWORD requested = static_cast<DWORD>((std::min)(
            postscript_bytes.size() - postscript_cursor, size_t{1024 * 1024}));
        DWORD received = 0;
        if (!ReadFile(source.ReadHandle(), postscript_bytes.data() + postscript_cursor,
                      requested, &received, nullptr) || received == 0) {
            diagnostic = "DOS EPS PostScript section ended before its declared length";
            return std::nullopt;
        }
        postscript_cursor += received;
    }

    const std::wstring executable = FindGhostscriptExecutable();
    if (executable.empty()) {
        diagnostic = "DOS EPS uses PostScript features outside the native subset and Ghostscript is unavailable";
        return std::nullopt;
    }
    auto temporary_directory = TemporaryImportDirectory::Create();
    if (!temporary_directory) {
        diagnostic = "A private temporary directory for the bounded Ghostscript conversion could not be created";
        return std::nullopt;
    }
    const std::filesystem::path pdf_path = std::filesystem::path(temporary_directory->path()) /
                                           L"converted.pdf";
    pikdraw::workers::WorkerLaunchConfig config;
    config.worker_executable_path = executable;
    config.working_directory = std::filesystem::path(executable).parent_path()
        .parent_path().wstring();
    config.appcontainer_profile_name = L"PikDrawEpsConverter";
    config.use_appcontainer = true;
    config.grant_worker_directory_access = true;
    config.writable_directories.push_back(temporary_directory->path());
    config.temporary_directory = temporary_directory->path();
    if (memory_waiter.WaitForHeadroom(1, cancellation) !=
            pikdraw::workers::SystemMemoryWaitResult::Ready) {
        diagnostic = cancellation.stop_requested() ? "DOS EPS import was cancelled"
            : "System RAM pressure wait timed out after 5 minutes";
        return std::nullopt;
    }
    config.job_limits.max_memory_bytes = 0;
    config.arguments = {L"-q", L"-dSAFER", L"-dLockSafetyParams", L"-dBATCH",
        L"-dNOPAUSE", L"-dEPSCrop", L"-sstdout=%stderr",
        L"-sDEVICE=pdfwrite", L"-dCompatibilityLevel=1.7",
        L"-sOutputFile=" + pdf_path.wstring(), L"-f", L"-"};
    pikdraw::workers::WorkerProcessHost converter;
    if (!converter.Launch(config)) {
        diagnostic = "Windows could not start Ghostscript inside the restricted EPS converter container";
        return std::nullopt;
    }
    const auto postscript = std::span<const uint8_t>(postscript_bytes);
    std::vector<uint8_t> standard_output;
    if (!converter.TransactRawProcessBytes(postscript, std::numeric_limits<uint64_t>::max(),
            std::chrono::seconds(180), standard_output, cancellation, &memory_waiter)) {
        diagnostic = "Ghostscript EPS conversion failed in its restricted process: ";
        diagnostic += converter.LastTransportFailure();
        if (converter.LastWorkerExitCode() != STILL_ACTIVE) {
            char exit_status[40]{};
            std::snprintf(exit_status, sizeof(exit_status), " (exit status 0x%08lx)",
                static_cast<unsigned long>(converter.LastWorkerExitCode()));
            diagnostic += exit_status;
        }
        return std::nullopt;
    }
    const std::wstring pdf_path_wide = pdf_path.wstring();
    HANDLE output = CreateFileW(pdf_path_wide.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        diagnostic = "Ghostscript did not create its PDF output in the private temporary directory";
        return std::nullopt;
    }
    LARGE_INTEGER output_size{};
    std::array<uint8_t, 1024> output_prefix{};
    DWORD prefix_bytes = 0;
    const bool memory_available = memory_waiter.WaitForHeadroom(1, cancellation) ==
        pikdraw::workers::SystemMemoryWaitResult::Ready;
    const bool readable = GetFileSizeEx(output, &output_size) != FALSE &&
        output_size.QuadPart >= 8 &&
        memory_available &&
        ReadFile(output, output_prefix.data(), static_cast<DWORD>(output_prefix.size()),
                 &prefix_bytes, nullptr) != FALSE;
    CloseHandle(output);
    constexpr std::string_view kPdfHeader = "%PDF-";
    const auto pdf_header = readable ? std::search(output_prefix.begin(),
        output_prefix.begin() + prefix_bytes, kPdfHeader.begin(), kPdfHeader.end())
        : output_prefix.end();
    if (!readable || output_size.QuadPart == 0 || pdf_header == output_prefix.end()) {
        diagnostic = "Ghostscript output is not a readable PDF with a valid leading header";
        return std::nullopt;
    }
    if (memory_waiter.WaitForHeadroom(1, cancellation) !=
            pikdraw::workers::SystemMemoryWaitResult::Ready) {
        diagnostic = cancellation.stop_requested() ? "DOS EPS import was cancelled"
            : "System RAM pressure wait timed out after 5 minutes";
        return std::nullopt;
    }
    auto snapshot = pikdraw::workers::SourceSnapshot::Create(
        pdf_path_wide, std::numeric_limits<uint64_t>::max());
    if (!snapshot || !snapshot->IsValid()) {
        diagnostic = "The verified Ghostscript PDF output could not be staged for validation";
        return std::nullopt;
    }
    return snapshot;
}

const char* FormatName(DetectedImportFormat format) noexcept {
    switch (format) {
    case DetectedImportFormat::Svg: return "SVG";
    case DetectedImportFormat::Dxf: return "DXF";
    case DetectedImportFormat::Png: return "PNG";
    case DetectedImportFormat::Jpeg: return "JPEG";
    case DetectedImportFormat::Tiff: return "TIFF";
    case DetectedImportFormat::Cdr: return "CorelDRAW";
    case DetectedImportFormat::Ai: return "AI";
    case DetectedImportFormat::Pdf: return "PDF";
    case DetectedImportFormat::Eps: return "EPS";
    case DetectedImportFormat::Bmp: return "BMP";
    }
    return "Unknown";
}

bool IsRaster(DetectedImportFormat format) noexcept {
    return format == DetectedImportFormat::Png ||
           format == DetectedImportFormat::Jpeg ||
           format == DetectedImportFormat::Tiff ||
           format == DetectedImportFormat::Bmp;
}

const wchar_t* ExpectedExtension(DetectedImportFormat format) noexcept {
    switch (format) {
    case DetectedImportFormat::Svg: return L".svg";
    case DetectedImportFormat::Dxf: return L".dxf";
    case DetectedImportFormat::Png: return L".png";
    case DetectedImportFormat::Jpeg: return L".jpg";
    case DetectedImportFormat::Tiff: return L".tif";
    case DetectedImportFormat::Bmp: return L".bmp";
    case DetectedImportFormat::Cdr: return L".cdr";
    case DetectedImportFormat::Ai: return L".ai";
    case DetectedImportFormat::Pdf: return L".pdf";
    case DetectedImportFormat::Eps: return L".eps";
    }
    return L"";
}

bool ExtensionMatches(DetectedImportFormat format, std::wstring_view extension) noexcept {
    if (extension == ExpectedExtension(format)) return true;
    return (format == DetectedImportFormat::Jpeg && extension == L".jpeg") ||
           (format == DetectedImportFormat::Tiff && extension == L".tiff") ||
           (format == DetectedImportFormat::Bmp && extension == L".bmp") ||
           (format == DetectedImportFormat::Cdr && extension == L".cdr") ||
           (format == DetectedImportFormat::Eps && extension == L".eps");
}

std::wstring WorkerExecutablePath() {
    std::wstring module(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (!length || length >= module.size()) return {};
    module.resize(length);
    return (std::filesystem::path(module).parent_path() / L"workers" / L"spike_worker.exe").wstring();
}

class CanonicalImportSpool final {
public:
    CanonicalImportSpool() : file_(std::tmpfile()) {}
    CanonicalImportSpool(const CanonicalImportSpool&) = delete;
    CanonicalImportSpool& operator=(const CanonicalImportSpool&) = delete;
    ~CanonicalImportSpool() { if (file_) std::fclose(file_); }

    [[nodiscard]] bool is_open() const noexcept { return file_ != nullptr; }
    [[nodiscard]] uint64_t byte_count() const noexcept { return byte_count_; }
    [[nodiscard]] bool pressure_limited() const noexcept { return pressure_limited_; }

    bool append(std::span<const uint8_t> bytes) noexcept {
        if (!file_ || bytes.size() > UINT64_MAX - byte_count_) return false;
        if (!bytes.empty() && std::fwrite(bytes.data(), 1, bytes.size(), file_) != bytes.size())
            return false;
        byte_count_ += bytes.size();
        return true;
    }

    bool rewind_for_read() noexcept {
        if (!file_ || std::fflush(file_) != 0) return false;
        std::rewind(file_);
        return std::ferror(file_) == 0;
    }

    bool read_record(uint64_t& consumed, uint16_t& type,
                     std::vector<uint8_t>& payload, bool& end,
                     pikdraw::workers::SystemMemoryPressureWaiter& memory_waiter,
                     std::stop_token cancellation) noexcept {
        end = false;
        payload.clear();
        if (!file_ || consumed > byte_count_) return false;
        if (consumed == byte_count_) {
            end = true;
            return true;
        }
        std::array<uint8_t, 8> header{};
        if (byte_count_ - consumed < header.size() ||
            std::fread(header.data(), 1, header.size(), file_) != header.size())
            return false;
        type = static_cast<uint16_t>(header[0]) |
            static_cast<uint16_t>(static_cast<uint16_t>(header[1]) << 8);
        const uint16_t flags = static_cast<uint16_t>(header[2]) |
            static_cast<uint16_t>(static_cast<uint16_t>(header[3]) << 8);
        const uint32_t payload_size = static_cast<uint32_t>(header[4]) |
            (static_cast<uint32_t>(header[5]) << 8) |
            (static_cast<uint32_t>(header[6]) << 16) |
            (static_cast<uint32_t>(header[7]) << 24);
        if (type < 1 || type > static_cast<uint16_t>(
                pikdraw::interop::ImportRecordType::RasterImage64) ||
            flags != 0 || payload_size > byte_count_ - consumed - header.size())
            return false;
        if (memory_waiter.WaitForHeadroom(payload_size, cancellation) !=
            pikdraw::workers::SystemMemoryWaitResult::Ready) {
            pressure_limited_ = true;
            return false;
        }
        try {
            payload.resize(payload_size);
        } catch (...) {
            return false;
        }
        if (payload_size != 0 &&
            std::fread(payload.data(), 1, payload_size, file_) != payload_size) {
            payload.clear();
            return false;
        }
        consumed += header.size() + payload_size;
        return true;
    }

private:
    std::FILE* file_{nullptr};
    uint64_t byte_count_{0};
    bool pressure_limited_{false};
};

bool AppendValidatedRecords(const pikdraw::interop::IpcV2Frame& frame,
                            CanonicalImportSpool& canonical_spool,
                            size_t& record_count,
                            uint64_t& canonical_bytes,
                            uint64_t max_canonical_bytes) {
    using namespace pikdraw::interop;
    if (static_cast<MessageType>(frame.header.message_type) != MessageType::V2ProposalChunk)
        return false;
    const auto chunk = DeserializeV2ProposalChunkPayload(frame.payload);
    if (!chunk || canonical_bytes > max_canonical_bytes ||
        chunk->canonical_record_bytes.size() > max_canonical_bytes - canonical_bytes)
        return false;
    if (!canonical_spool.append(chunk->canonical_record_bytes)) return false;
    canonical_bytes += chunk->canonical_record_bytes.size();
    record_count += chunk->record_count;
    return record_count <= kMaxImportRecords;
}

std::optional<color::ColorValue> DecodeColor(const pikdraw::interop::ImportColorRecord& source) {
    using namespace pikdraw::interop;
    color::ColorValue result;
    switch (source.model) {
    case ImportColorModel::Rgb:
        result = color::RGBColor{source.components[0], source.components[1],
                                 source.components[2], source.components[3]};
        break;
    case ImportColorModel::Cmyk:
        result = color::CMYKColor{source.components[0], source.components[1],
                                  source.components[2], source.components[3], source.components[4]};
        break;
    case ImportColorModel::Spot:
        result = color::SpotColor{source.spot_name, source.components[0],
            color::RGBColor{source.alternate_rgb[0], source.alternate_rgb[1],
                            source.alternate_rgb[2], source.alternate_rgb[3]},
            (source.overprint_flags & 1) != 0};
        break;
    case ImportColorModel::Gray:
        result = color::GrayscaleColor{source.components[0], source.components[1]};
        break;
    default:
        return std::nullopt;
    }
    result.set_overprint({(source.overprint_flags & 1) != 0,
                          (source.overprint_flags & 2) != 0});
    return result.is_valid() ? std::optional<color::ColorValue>{std::move(result)} : std::nullopt;
}

bool DecodeCanonicalProposal(CanonicalImportSpool& canonical_spool,
                             ImportedContentProposal& output,
                             std::string& diagnostic,
                             pikdraw::workers::SystemMemoryPressureWaiter& memory_waiter,
                             std::stop_token cancellation,
                             const std::function<void(uint64_t, uint64_t)>& source_progress) {
    using namespace pikdraw::interop;
    const auto fail = [&diagnostic](std::string reason) {
        diagnostic = std::move(reason);
        return false;
    };
    const auto read_u32 = [](std::span<const uint8_t> payload, size_t index) {
        const size_t at = index * sizeof(uint32_t);
        return static_cast<uint32_t>(payload[at]) |
            (static_cast<uint32_t>(payload[at + 1]) << 8) |
            (static_cast<uint32_t>(payload[at + 2]) << 16) |
            (static_cast<uint32_t>(payload[at + 3]) << 24);
    };
    const auto read_u64 = [](std::span<const uint8_t> payload, size_t offset) {
        uint64_t value = 0;
        for (size_t i = 0; i < sizeof(uint64_t); ++i)
            value |= static_cast<uint64_t>(payload[offset + i]) << (i * 8);
        return value;
    };
    std::unordered_map<uint32_t, document::ObjectId> ids;
    std::unordered_map<uint32_t, size_t> page_indices;
    size_t object_count = 0;
    if (!canonical_spool.rewind_for_read())
        return fail("canonical record spool could not be rewound");
    uint64_t consumed_bytes = 0;
    for (;;) {
        if (memory_waiter.WaitForHeadroom(1, cancellation) !=
            pikdraw::workers::SystemMemoryWaitResult::Ready)
            return fail(cancellation.stop_requested() ? "import cancelled while waiting for RAM"
                : "system RAM pressure wait timed out after 5 minutes while scanning records");
        uint16_t raw_type = 0;
        std::vector<uint8_t> record_payload;
        bool end = false;
        if (!canonical_spool.read_record(consumed_bytes, raw_type, record_payload, end,
                                          memory_waiter, cancellation))
            return fail(canonical_spool.pressure_limited()
                ? cancellation.stop_requested()
                    ? "import cancelled while waiting for RAM"
                    : "system RAM pressure wait timed out after 5 minutes while scanning import records"
                : "canonical record spool contains an invalid record");
        if (end) break;
        if (raw_type == static_cast<uint16_t>(ImportRecordType::Path) ||
            raw_type == static_cast<uint16_t>(ImportRecordType::Text) ||
            raw_type == static_cast<uint16_t>(ImportRecordType::Group) ||
            raw_type == static_cast<uint16_t>(ImportRecordType::RasterImage) ||
            raw_type == static_cast<uint16_t>(ImportRecordType::RasterImage64)) {
            const std::span<const uint8_t> payload(record_payload);
            if (payload.size() < sizeof(uint32_t))
                return fail("object record has no local ID");
            ++object_count;
            const uint32_t local_id = read_u32(payload, 0);
            const document::ObjectId id = document::generate_object_id();
            if (local_id == 0 || id == document::kInvalidObjectId ||
                !ids.emplace(local_id, id).second)
                return fail("object record has a zero or duplicate local ID");
        }
    }
    struct RasterAssembly {
        std::vector<uint32_t> pixels;
        std::vector<uint8_t> pixels_rgba16le;
        std::shared_ptr<pikdraw::raster::RasterMemoryBudget::Reservation> memory_reservation;
        uint32_t next_offset{0};
        uint32_t total_bytes{0};
        uint32_t width_px{0};
        uint32_t height_px{0};
        uint8_t decoded_bits_per_channel{8};
        size_t placement_count{0};
        std::shared_ptr<const std::vector<uint32_t>> decoded_pixels;
        std::shared_ptr<const std::vector<uint8_t>> decoded_pixels_rgba16le;
    };
    struct PendingImagePixels {
        size_t object_index{0};
        uint32_t data_local_id{0};
    };
    struct SourceAssembly {
        std::vector<uint8_t> bytes;
        std::shared_ptr<pikdraw::raster::RasterMemoryBudget::Reservation> memory_reservation;
        uint64_t next_offset{0};
        uint64_t total_bytes{0};
        std::string media_type;
        std::shared_ptr<const document::RasterSourceProvider> provider;
        std::optional<document::RasterSourceDescriptor> descriptor;
    };
    struct PendingImageSource {
        size_t object_index{0};
        uint32_t data_local_id{0};
    };
    std::unordered_map<uint32_t, RasterAssembly> raster_assemblies;
    // Re-measure available system headroom before each large proposal allocation.
    // This keeps admission tied to current pressure instead of a machine-size cap.
    pikdraw::raster::RasterMemoryBudget raster_proposal_memory_budget(0);
    const auto reserve_raster_memory = [&](uint64_t bytes,
            pikdraw::workers::SystemMemoryWaitResult& wait_result) {
        for (;;) {
            wait_result = memory_waiter.WaitForHeadroom(
                static_cast<size_t>(bytes), cancellation);
            if (wait_result != pikdraw::workers::SystemMemoryWaitResult::Ready)
                return std::shared_ptr<pikdraw::raster::RasterMemoryBudget::Reservation>{};
            const size_t headroom = pikdraw::workers::SystemMemoryHeadroomBeforePressureLimit();
            auto reservation = raster_proposal_memory_budget.try_reserve_with_headroom(
                bytes, static_cast<uint64_t>(headroom));
            if (reservation || headroom >= bytes) return reservation;
            // RAM changed after the waiter's probe; wait again using the same
            // cumulative allowance instead of rejecting this import early.
        }
    };
    std::vector<PendingImagePixels> pending_image_pixels;
    std::unordered_map<uint32_t, SourceAssembly> source_assemblies;
    std::shared_ptr<pikdraw::raster::TemporaryRasterAssetSession> source_store_session;
    std::vector<PendingImageSource> pending_image_sources;
    raster_assemblies.reserve(16);
    pending_image_pixels.reserve(16);
    source_assemblies.reserve(16);
    pending_image_sources.reserve(16);
    output.objects.reserve(object_count);
    if (!canonical_spool.rewind_for_read())
        return fail("canonical record spool could not be rewound for decoding");
    consumed_bytes = 0;
    for (;;) {
        if (memory_waiter.WaitForHeadroom(1, cancellation) !=
            pikdraw::workers::SystemMemoryWaitResult::Ready)
            return fail(cancellation.stop_requested() ? "import cancelled while waiting for RAM"
                : "system RAM pressure wait timed out after 5 minutes while decoding records");
        uint16_t raw_type = 0;
        std::vector<uint8_t> record_payload;
        bool end = false;
        if (!canonical_spool.read_record(consumed_bytes, raw_type, record_payload, end,
                                          memory_waiter, cancellation))
            return fail(canonical_spool.pressure_limited()
                ? cancellation.stop_requested()
                    ? "import cancelled while waiting for RAM"
                    : "system RAM pressure wait timed out after 5 minutes while decoding import records"
                : "canonical record spool failed while decoding");
        if (end) break;
        const std::span<const uint8_t> payload(record_payload);
        if (raw_type == static_cast<uint16_t>(ImportRecordType::RasterImageChunk)) {
            if (payload.size() < 16) return fail("raster chunk header is truncated");
            const uint32_t local_id = read_u32(payload, 0);
            const uint32_t byte_offset = read_u32(payload, 1);
            const uint32_t total_bytes = read_u32(payload, 2);
            const uint32_t chunk_bytes = read_u32(payload, 3);
            if (local_id == 0 || total_bytes == 0 || chunk_bytes == 0 ||
                chunk_bytes > pikdraw::interop::kMaxRasterChunkPayloadBytes ||
                payload.size() != 16ull + chunk_bytes ||
                byte_offset > total_bytes || chunk_bytes > total_bytes - byte_offset)
                return fail("raster chunk has an invalid byte range");
            const auto found = raster_assemblies.find(local_id);
            if (found == raster_assemblies.end() || found->second.next_offset != byte_offset ||
                found->second.total_bytes != total_bytes)
                return fail("raster chunk is out of sequence or has no image metadata");
            if (found->second.decoded_bits_per_channel == 16) {
                if (found->second.pixels_rgba16le.size() != total_bytes)
                    return fail("16-bit raster chunk has no matching image allocation");
                std::copy_n(payload.begin() + 16, chunk_bytes,
                    found->second.pixels_rgba16le.begin() + byte_offset);
            } else {
                for (size_t byte = 0; byte < chunk_bytes; ++byte) {
                    const size_t absolute = static_cast<size_t>(byte_offset) + byte;
                    const size_t destination = absolute / 4;
                    const unsigned shift = static_cast<unsigned>((3 - (absolute % 4)) * 8);
                    const uint32_t mask = static_cast<uint32_t>(0xffu << shift);
                    found->second.pixels[destination] =
                        (found->second.pixels[destination] & ~mask) |
                        (static_cast<uint32_t>(payload[16 + byte]) << shift);
                }
            }
            found->second.next_offset += chunk_bytes;
            continue;
        }
        if (raw_type == static_cast<uint16_t>(ImportRecordType::RasterSourceChunk) ||
            raw_type == static_cast<uint16_t>(ImportRecordType::RasterSourceChunk64)) {
            const bool wide = raw_type == static_cast<uint16_t>(ImportRecordType::RasterSourceChunk64);
            const size_t header_bytes = wide ? 24 : 16;
            if (payload.size() < header_bytes) return fail("raster source chunk header is truncated");
            const uint32_t local_id = read_u32(payload, 0);
            const uint64_t byte_offset = wide ? read_u64(payload, 4) : read_u32(payload, 1);
            const uint64_t total_bytes = wide ? read_u64(payload, 12) : read_u32(payload, 2);
            const uint32_t chunk_bytes = read_u32(payload, wide ? 5 : 3);
            if (local_id == 0 || total_bytes == 0 || chunk_bytes == 0 ||
                chunk_bytes > pikdraw::interop::kMaxRasterChunkPayloadBytes ||
                payload.size() != header_bytes + static_cast<size_t>(chunk_bytes) ||
                byte_offset > total_bytes || chunk_bytes > total_bytes - byte_offset)
                return fail("raster source chunk has an invalid byte range");
            const auto found = source_assemblies.find(local_id);
            if (found == source_assemblies.end() || found->second.next_offset != byte_offset ||
                found->second.total_bytes != total_bytes)
                return fail("raster source chunk is out of sequence or has no image metadata");
            if (memory_waiter.WaitForHeadroom(1, cancellation) !=
                pikdraw::workers::SystemMemoryWaitResult::Ready)
                return fail(cancellation.stop_requested() ? "import cancelled while waiting for RAM"
                    : "system RAM pressure wait timed out after 5 minutes while retaining raster source bytes");
            try {
                found->second.bytes.insert(found->second.bytes.end(),
                                           payload.begin() + static_cast<std::ptrdiff_t>(header_bytes),
                                           payload.end());
            } catch (...) {
                return fail("raster source bytes could not be retained");
            }
            found->second.next_offset += chunk_bytes;
            continue;
        }
        auto decoded_value = DeserializeImportRecord(static_cast<ImportRecordType>(raw_type), payload);
        if (!decoded_value) return fail("canonical record failed semantic decoding");
        if (const auto* image = std::get_if<ImportRasterImageRecord>(&*decoded_value)) {
            const uint64_t bytes_per_pixel = image->decoded_bits_per_channel == 16 ? 8 : 4;
            const uint64_t pixel_count = static_cast<uint64_t>(image->width_px) * image->height_px;
            if (image->frame_count == 0 || image->frame_index >= image->frame_count ||
                (image->decoded_bits_per_channel != 8 &&
                 image->decoded_bits_per_channel != 16) ||
                pixel_count == 0 || pixel_count > UINT64_MAX / bytes_per_pixel ||
                image->decoded_pixel_bytes != pixel_count * bytes_per_pixel)
                return fail("raster image metadata has invalid decoded pixel dimensions");
            const uint32_t data_local_id = image->pixel_data_local_id == 0
                ? image->local_id : image->pixel_data_local_id;
            auto found = raster_assemblies.find(data_local_id);
            if (found == raster_assemblies.end()) {
                if (data_local_id != image->local_id)
                    return fail("shared raster data refers to a missing owner image");
                RasterAssembly assembly;
                uint64_t reservation_bytes = image->decoded_pixel_bytes;
                if (image->decoded_bits_per_channel == 16) {
                    if (pixel_count > (UINT64_MAX - reservation_bytes) / 4)
                        return fail("16-bit raster preview reservation overflows");
                    reservation_bytes += pixel_count * 4;
                }
                auto wait_result = pikdraw::workers::SystemMemoryWaitResult::Ready;
                assembly.memory_reservation = reserve_raster_memory(
                    reservation_bytes, wait_result);
                if (wait_result != pikdraw::workers::SystemMemoryWaitResult::Ready)
                    return fail(cancellation.stop_requested() ? "import cancelled while waiting for RAM"
                        : "system RAM pressure wait timed out after 5 minutes while reserving raster pixels");
                if (!assembly.memory_reservation)
                    return fail("resource_limit: raster pixel reservation failed with current system RAM headroom");
                try {
                    if (image->decoded_bits_per_channel == 16)
                        assembly.pixels_rgba16le.resize(image->decoded_pixel_bytes);
                    else
                        assembly.pixels.resize(image->decoded_pixel_bytes / sizeof(uint32_t));
                } catch (...) {
                    return fail("raster proposal pixel staging allocation failed after system RAM admission");
                }
                assembly.total_bytes = image->decoded_pixel_bytes;
                assembly.width_px = image->width_px;
                assembly.height_px = image->height_px;
                assembly.decoded_bits_per_channel = image->decoded_bits_per_channel;
                found = raster_assemblies.emplace(data_local_id, std::move(assembly)).first;
            } else if (found->second.width_px != image->width_px ||
                       found->second.height_px != image->height_px ||
                       found->second.total_bytes != image->decoded_pixel_bytes ||
                       found->second.decoded_bits_per_channel != image->decoded_bits_per_channel) {
                return fail("shared raster placements disagree on pixel dimensions");
            }
            if (image->source_bytes_size != 0) {
                auto source_found = source_assemblies.find(image->source_data_local_id);
                if (image->source_data_local_id == image->local_id) {
                    if (source_found != source_assemblies.end())
                        return fail("raster source bytes have duplicate ownership metadata");
                    SourceAssembly assembly;
                    assembly.total_bytes = image->source_bytes_size;
                    assembly.media_type = image->media_type;
                    auto wait_result = pikdraw::workers::SystemMemoryWaitResult::Ready;
                    assembly.memory_reservation = reserve_raster_memory(
                        assembly.total_bytes, wait_result);
                    if (wait_result != pikdraw::workers::SystemMemoryWaitResult::Ready)
                        return fail(cancellation.stop_requested() ? "import cancelled while waiting for RAM"
                            : "system RAM pressure wait timed out after 5 minutes while reserving raster source bytes");
                    if (!assembly.memory_reservation)
                        return fail("resource_limit: raster source reservation failed with current system RAM headroom");
                    try {
                        assembly.bytes.reserve(assembly.total_bytes);
                        source_found = source_assemblies.emplace(
                            image->local_id, std::move(assembly)).first;
                    } catch (...) {
                        return fail("raster proposal source staging allocation failed after system RAM admission");
                    }
                } else if (source_found == source_assemblies.end() ||
                           source_found->second.total_bytes != image->source_bytes_size ||
                           source_found->second.media_type != image->media_type ||
                           source_found->second.next_offset != image->source_bytes_size) {
                    return fail("raster source placement refers to missing or incomplete source bytes");
                }
            }
            ++found->second.placement_count;
            document::ImageObject converted;
            converted.id = ids.at(image->local_id);
            converted.name = image->name;
            converted.width_px = static_cast<int32_t>(image->width_px);
            converted.height_px = static_cast<int32_t>(image->height_px);
            if (image->has_placement_transform) {
                converted.bounds_rect = {0.0, 0.0, image->width_mm, image->height_mm};
                const auto& matrix = image->placement_transform;
                converted.transform = {matrix[0], matrix[1], matrix[2], matrix[3],
                                       matrix[4], matrix[5]};
            } else {
                converted.bounds_rect = image->width_mm > 0.0 && image->height_mm > 0.0
                    ? geometry::Rect2D{image->x_mm, image->y_mm, image->width_mm, image->height_mm}
                    : geometry::Rect2D{0.0, 0.0,
                        static_cast<double>(image->width_px) * 25.4 / image->dpi_x,
                        static_cast<double>(image->height_px) * 25.4 / image->dpi_y};
            }
            converted.source.media_type = image->media_type;
            converted.source.dpi_x = image->dpi_x;
            converted.source.dpi_y = image->dpi_y;
            converted.source.dpi_from_source = image->dpi_from_source;
            converted.source.frame_index = image->frame_index;
            converted.source.frame_count = image->frame_count;
            converted.source.source_bits_per_pixel = image->source_bits_per_pixel;
            converted.source.exif_orientation = image->exif_orientation;
            converted.source.orientation_applied = image->orientation_applied;
            converted.source.icc_profile = image->icc_profile;
            switch (image->preview_pixel_color_basis) {
            case 1:
                converted.source.preview_pixel_color_basis =
                    document::RasterPixelColorBasis::AssumedSRgb;
                break;
            case 2:
                converted.source.preview_pixel_color_basis =
                    document::RasterPixelColorBasis::ConvertedToSRgb;
                break;
            case 3:
                converted.source.preview_pixel_color_basis =
                    document::RasterPixelColorBasis::EmbeddedProfileUnapplied;
                break;
            default:
                converted.source.preview_pixel_color_basis =
                    document::RasterPixelColorBasis::LegacyUnknown;
                break;
            }
            const size_t object_index = output.objects.size();
            output.objects.emplace_back(std::move(converted));
            pending_image_pixels.push_back({object_index, data_local_id});
            if (image->source_bytes_size != 0)
                pending_image_sources.push_back({object_index, image->source_data_local_id});
        } else if (const auto* page = std::get_if<ImportPageRecord>(&*decoded_value)) {
            if (output.source_page_width_mm <= 0.0 || output.source_page_height_mm <= 0.0) {
                output.source_page_width_mm = page->width_mm;
                output.source_page_height_mm = page->height_mm;
            }
            if (page->has_manifest_metadata) {
                if (page->local_id != output.pages.size() + 1 ||
                    !page_indices.emplace(page->local_id, output.pages.size()).second)
                    return fail("page manifest contains a duplicate or out-of-order local ID");
                document::ImportedPageContent imported;
                imported.name = page->name;
                imported.layout = document::Page::make_default();
                imported.layout.width_mm = page->width_mm;
                imported.layout.height_mm = page->height_mm;
                imported.layout.layers.clear();
                if (page->rotation_degrees > 270 || page->rotation_degrees % 90 != 0)
                    return fail("page manifest contains an invalid PDF rotation");
                imported.source_page_index = page->source_page_index;
                imported.source_page_rotation_quarters_clockwise = static_cast<uint8_t>(
                    page->rotation_degrees / 90);
                output.pages.push_back(std::move(imported));
            }
        } else if (const auto* layer = std::get_if<ImportLayerRecord>(&*decoded_value)) {
            document::Layer converted{layer->name, layer->visible, layer->locked, {}};
            converted.object_ids.reserve(layer->object_local_ids.size());
            for (const uint32_t id : layer->object_local_ids) {
                const auto found = ids.find(id);
                if (found == ids.end()) return fail("layer refers to a missing object");
                converted.object_ids.push_back(found->second);
            }
            if (!output.pages.empty()) {
                const auto page = page_indices.find(layer->page_local_id);
                if (page == page_indices.end() || page->second >= output.pages.size())
                    return fail("layer refers to a page outside the imported manifest");
                output.pages[page->second].layout.layers.push_back(std::move(converted));
            } else {
                output.layers.push_back(std::move(converted));
            }
        } else if (const auto* group = std::get_if<ImportGroupRecord>(&*decoded_value)) {
            document::GroupObject converted;
            converted.id = ids.at(group->local_id);
            converted.name = group->name;
            converted.children_ids.reserve(group->child_local_ids.size());
            for (const uint32_t id : group->child_local_ids) converted.children_ids.push_back(ids.at(id));
            if (group->clip_mask_local_id != 0) {
                document::ClipProperties clip{};
                clip.mask_object_id = ids.at(group->clip_mask_local_id);
                clip.lock_contents_to_frame = false;
                clip.preserve_frame_appearance = false;
                clip.fit_mode = document::ClipContentFitMode::CurrentPosition;
                clip.is_empty_frame = false;
                converted.clip = clip;
            }
            output.objects.emplace_back(std::move(converted));
        } else if (const auto* path = std::get_if<ImportPathRecord>(&*decoded_value)) {
            auto fill = DecodeColor(path->fill);
            auto stroke = DecodeColor(path->stroke);
            if (!fill || !stroke || path->winding_rule > 1)
                return fail("path has invalid color or winding data");
            document::PathObject converted;
            converted.id = ids.at(path->local_id);
            converted.name = path->name;
            converted.has_fill = path->has_fill;
            converted.has_stroke = path->has_stroke;
            converted.fill = std::move(*fill);
            converted.stroke = std::move(*stroke);
            converted.stroke_width_mm = path->stroke_width_mm;
            converted.appearance.object_opacity = path->object_opacity;
            converted.appearance.fill_opacity = path->fill_opacity;
            converted.appearance.stroke_opacity = path->stroke_opacity;
            converted.path.set_winding_rule(static_cast<geometry::WindingRule>(path->winding_rule));
            for (const auto& element : path->elements) {
                const auto& v = element.values;
                switch (element.verb) {
                case ImportPathVerb::Move: converted.path.move_to(v[0], v[1]); break;
                case ImportPathVerb::Line: converted.path.line_to(v[0], v[1]); break;
                case ImportPathVerb::Quad: converted.path.quad_to(v[2], v[3], v[0], v[1]); break;
                case ImportPathVerb::Cubic: converted.path.cubic_to(v[2], v[3], v[4], v[5], v[0], v[1]); break;
                case ImportPathVerb::Arc: converted.path.arc_to(v[6], v[7], v[8],
                    (element.flags & 1) != 0, (element.flags & 2) != 0, v[0], v[1]); break;
                case ImportPathVerb::Close: converted.path.close(); break;
                default: return fail("path contains an unsupported command");
                }
            }
            if (converted.path.empty()) return fail("path decoded to empty geometry");
            output.objects.emplace_back(std::move(converted));
        } else if (const auto* text = std::get_if<ImportTextRecord>(&*decoded_value)) {
            auto fill = DecodeColor(text->fill);
            if (!fill) return fail("text has invalid fill color");
            document::TextObject converted;
            converted.id = ids.at(text->local_id);
            converted.name = text->name;
            converted.text = text->utf8_text;
            converted.font.family_name = text->font_family;
            converted.font.postscript_name = text->font_postscript_name;
            converted.font.weight = text->font_weight;
            converted.font.italic = text->italic;
            converted.font.size_pt = text->font_size_pt;
            converted.position = {text->x_mm, text->y_mm};
            converted.transform = geometry::Transform2D::rotation_degrees(
                text->rotation_degrees);
            converted.fill = std::move(*fill);
            converted.appearance.object_opacity = text->object_opacity;
            converted.appearance.fill_opacity = text->fill_opacity;
            converted.appearance.stroke_opacity = text->stroke_opacity;
            output.objects.emplace_back(std::move(converted));
        } else if (const auto* report = std::get_if<ImportFidelityReportRecord>(&*decoded_value)) {
            output.profile_id = report->coverage_profile;
            output.coverage_status = report->coverage == ImportCoverage::Partial ? "partial" : "unknown";
            output.appearance_status = report->appearance == ImportAppearance::Partial ? "partial" : "unknown";
            output.editability_status = report->editability == ImportEditability::NativeEditable
                ? "native_editable" : "explicit_conversion";
            output.production_status = report->production == ImportProduction::Blocked
                ? "blocked" : "not_assessed";
        } else if (const auto* issue = std::get_if<ImportFidelityIssueRecord>(&*decoded_value)) {
            ImportIssueSummary::Severity severity = ImportIssueSummary::Severity::Warning;
            switch (issue->severity) {
            case ImportFidelitySeverity::Info:
                severity = ImportIssueSummary::Severity::Info;
                break;
            case ImportFidelitySeverity::Warning:
                severity = ImportIssueSummary::Severity::Warning;
                break;
            case ImportFidelitySeverity::Unsupported:
                severity = ImportIssueSummary::Severity::Unsupported;
                break;
            }
            output.issues.push_back({severity, issue->source_locator,
                                     issue->feature_code, issue->diagnostic_code});
        }
    }
    if (memory_waiter.WaitForHeadroom(1, cancellation) !=
        pikdraw::workers::SystemMemoryWaitResult::Ready)
        return fail(cancellation.stop_requested() ? "import cancelled while waiting for RAM"
            : "system RAM pressure wait timed out after 5 minutes while finalizing raster images");
    for (auto& [id, decoded] : raster_assemblies) {
        (void)id;
        if (decoded.next_offset != decoded.total_bytes ||
            (decoded.decoded_bits_per_channel == 16
                ? decoded.pixels_rgba16le.size() != decoded.total_bytes
                : decoded.pixels.size() != static_cast<size_t>(decoded.width_px) * decoded.height_px))
            return fail("raster image chunks are incomplete or inconsistent");
        // Even one placement is copied by commands, snapshots and Undo. Keep
        // immutable pixels shared so a transform never copies the full image.
        try {
            if (decoded.decoded_bits_per_channel == 16) {
                auto preview = std::make_shared<std::vector<uint32_t>>(
                    static_cast<size_t>(decoded.width_px) * decoded.height_px);
                for (size_t pixel = 0; pixel < preview->size(); ++pixel) {
                    const size_t source = pixel * 8;
                    (*preview)[pixel] =
                        (static_cast<uint32_t>(decoded.pixels_rgba16le[source + 1]) << 24) |
                        (static_cast<uint32_t>(decoded.pixels_rgba16le[source + 3]) << 16) |
                        (static_cast<uint32_t>(decoded.pixels_rgba16le[source + 5]) << 8) |
                         static_cast<uint32_t>(decoded.pixels_rgba16le[source + 7]);
                }
                decoded.decoded_pixels = std::move(preview);
                decoded.decoded_pixels_rgba16le =
                    std::make_shared<const std::vector<uint8_t>>(
                        std::move(decoded.pixels_rgba16le));
            } else {
                decoded.decoded_pixels = std::make_shared<const std::vector<uint32_t>>(
                    std::move(decoded.pixels));
            }
        } catch (...) {
            return fail("shared raster preview could not be finalized");
        }
    }
    for (const auto& pending : pending_image_pixels) {
        if (pending.object_index >= output.objects.size())
            return fail("raster placement points outside the object list");
        auto* image = std::get_if<document::ImageObject>(&output.objects[pending.object_index]);
        const auto assembly = raster_assemblies.find(pending.data_local_id);
        if (!image || assembly == raster_assemblies.end() ||
            (assembly->second.decoded_bits_per_channel == 16
                ? (!assembly->second.decoded_pixels_rgba16le ||
                   assembly->second.decoded_pixels_rgba16le->size() !=
                       static_cast<size_t>(image->width_px) * image->height_px * 8)
                : (assembly->second.decoded_pixels
                ? assembly->second.decoded_pixels->size() : assembly->second.pixels.size()) !=
                static_cast<size_t>(image->width_px) * image->height_px))
            return fail("raster placement pixels do not match its dimensions");
        if (assembly->second.decoded_pixels) {
            image->shared_pixels_rgba = assembly->second.decoded_pixels;
            image->shared_pixels_rgba16le = assembly->second.decoded_pixels_rgba16le;
        } else {
            image->pixels_rgba = std::move(assembly->second.pixels);
        }
    }
    if (!source_assemblies.empty()) {
        std::string store_error;
        source_store_session = pikdraw::raster::TemporaryRasterAssetSession::create(
            &store_error);
        if (!source_store_session)
            return fail("temporary raster source store could not be created: " +
                        store_error);
    }
    uint64_t total_source_bytes = 0;
    for (const auto& [id, source] : source_assemblies) {
        (void)id;
        if (source.total_bytes > std::numeric_limits<uint64_t>::max() - total_source_bytes)
            return fail("raster source byte total exceeds the progress counter range");
        total_source_bytes += source.total_bytes;
    }
    uint64_t source_bytes_completed = 0;
    for (auto& [id, source] : source_assemblies) {
        (void)id;
        if (source.next_offset != source.total_bytes || source.bytes.size() != source.total_bytes)
            return fail("raster original source bytes are incomplete");
        std::string store_error;
        const auto provider = source_store_session->store_embedded(
            source.bytes, source.media_type,
            [&cancellation] { return cancellation.stop_requested(); },
            [&](const pikdraw::raster::RasterAssetProgress& value) {
                if (source_progress)
                    source_progress(source_bytes_completed + value.bytes_copied,
                                    total_source_bytes);
            },
            &store_error);
        if (!provider)
            return fail("raster source could not be staged in the temporary disk store: " +
                        store_error);
        source_bytes_completed += source.total_bytes;
        source.descriptor = provider->descriptor();
        source.provider = std::move(provider);
        // The immutable provider is now the source of truth for encoded bytes.
        // Release the assembly and its RAM reservation before document commit.
        std::vector<uint8_t>().swap(source.bytes);
        source.memory_reservation.reset();
    }
    for (const auto& pending : pending_image_sources) {
        if (pending.object_index >= output.objects.size())
            return fail("raster source placement points outside the object list");
        auto* image = std::get_if<document::ImageObject>(&output.objects[pending.object_index]);
        const auto source = source_assemblies.find(pending.data_local_id);
        if (!image || source == source_assemblies.end() ||
            !source->second.descriptor || !source->second.provider)
            return fail("raster source placement has no completed source asset");
        image->source.asset_provider = source->second.provider;
        image->asset_source = source->second.descriptor;
    }
    if (!output.pages.empty()) {
        std::unordered_map<document::ObjectId, const document::ObjectVariant*> objects_by_id;
        objects_by_id.reserve(output.objects.size());
        for (const auto& object : output.objects) {
            const auto id = std::visit([](const auto& value) { return value.id; }, object);
            if (!objects_by_id.emplace(id, &object).second)
                return fail("page-set proposal contains duplicate native object IDs");
        }
        std::unordered_map<document::ObjectId, size_t> object_page;
        object_page.reserve(output.objects.size());
        for (size_t page_index = 0; page_index < output.pages.size(); ++page_index) {
            auto& page = output.pages[page_index];
            if (page.name.empty() || page.layout.layers.empty())
                return fail("page manifest contains an unnamed page or a page without a layer");
            std::vector<document::ObjectId> pending;
            for (const auto& layer : page.layout.layers)
                pending.insert(pending.end(), layer.object_ids.begin(), layer.object_ids.end());
            while (!pending.empty()) {
                const auto id = pending.back();
                pending.pop_back();
                if (!object_page.emplace(id, page_index).second)
                    return fail("page-set object hierarchy has duplicate or cross-page ownership");
                const auto found = objects_by_id.find(id);
                if (found == objects_by_id.end())
                    return fail("page-set layer refers to an object outside the proposal");
                if (const auto* group = std::get_if<document::GroupObject>(found->second))
                    pending.insert(pending.end(), group->children_ids.begin(),
                                   group->children_ids.end());
            }
        }
        if (object_page.size() != output.objects.size())
            return fail("page-set proposal contains an object outside all page layer hierarchies");
        for (auto& object : output.objects) {
            const auto id = std::visit([](const auto& value) { return value.id; }, object);
            output.pages[object_page.at(id)].objects.push_back(std::move(object));
        }
        output.objects.clear();
        for (const auto& page : output.pages)
            output.layers.insert(output.layers.end(), page.layout.layers.begin(),
                                 page.layout.layers.end());
    }
    if (output.layers.empty()) return fail("proposal has no layers");
    const size_t materialized_object_count = output.pages.empty()
        ? output.objects.size()
        : [&output] {
            size_t count = 0;
            for (const auto& page : output.pages) count += page.objects.size();
            return count;
        }();
    if (materialized_object_count != ids.size())
        return fail("proposal object count does not match its ID table");
    if (output.source_page_width_mm <= 0 || output.source_page_height_mm <= 0)
        return fail("proposal has no positive page dimensions");
    if (output.profile_id.empty()) return fail("proposal has no fidelity profile ID");
    return true;
}

document::ImportFidelityReport make_document_import_report(
    const ImportedContentProposal& proposal, uint32_t source_page_index = 0) {
    document::ImportFidelityReport report;
    report.format_name = proposal.format_name;
    report.profile_id = proposal.profile_id;
    report.source_bytes = proposal.source_bytes;
    report.source_page_width_mm = proposal.source_page_width_mm;
    report.source_page_height_mm = proposal.source_page_height_mm;
    report.source_page_index = source_page_index;
    if (proposal.coverage_status == "partial")
        report.coverage = document::ImportReportCoverage::Partial;
    else if (proposal.coverage_status == "complete")
        report.coverage = document::ImportReportCoverage::Complete;
    if (proposal.appearance_status == "partial")
        report.appearance = document::ImportReportAppearance::Partial;
    else if (proposal.appearance_status == "verified")
        report.appearance = document::ImportReportAppearance::Verified;
    if (proposal.editability_status == "native_editable" ||
        proposal.editability_status == "native_editable_after_conversion")
        report.editability = document::ImportReportEditability::Editable;
    else if (proposal.editability_status == "explicit_conversion")
        report.editability = document::ImportReportEditability::ExplicitConversion;
    if (proposal.production_status == "blocked")
        report.production = document::ImportReportProduction::Blocked;
    else if (proposal.production_status == "assessed")
        report.production = document::ImportReportProduction::Assessed;
    report.issues.reserve(proposal.issues.size());
    for (const auto& issue : proposal.issues) {
        document::ImportReportIssue converted;
        switch (issue.severity) {
        case ImportIssueSummary::Severity::Info:
            converted.severity = document::ImportReportIssueSeverity::Info;
            break;
        case ImportIssueSummary::Severity::Warning:
            converted.severity = document::ImportReportIssueSeverity::Warning;
            break;
        case ImportIssueSummary::Severity::Unsupported:
            converted.severity = document::ImportReportIssueSeverity::Unsupported;
            break;
        }
        converted.source_locator = issue.source_locator;
        converted.feature_code = issue.feature_code;
        converted.diagnostic_code = issue.diagnostic_code;
        report.issues.push_back(std::move(converted));
    }
    return report;
}

} // namespace

document::AsyncProposalMetadata DocumentWorkflow::begin_import_job(uint64_t job_id) {
    return root_.document().begin_job(job_id);
}

void DocumentWorkflow::cancel_import_job(uint64_t job_id) {
    root_.document().cancel_job(job_id);
}

document::ProposalStatus DocumentWorkflow::commit_import_proposal(
    document::AsyncProposalMetadata metadata, ImportedContentProposal proposal,
    bool resize_page_to_source) {
    auto fidelity_report = make_document_import_report(proposal);
    if (!fidelity_report.is_valid())
        return document::ProposalStatus::ExecutionFailed;
    if (proposal.content_mode == document::DocumentContentMode::PdfPrintLocked) {
        const auto& asset = proposal.pdf_source_asset;
        if (!asset || !asset->bytes || asset->bytes->empty() ||
            asset->page_count == 0 || proposal.pages.size() != asset->page_count ||
            !proposal.objects.empty())
            return document::ProposalStatus::ExecutionFailed;
        std::vector<document::DocumentPage> pages;
        pages.reserve(proposal.pages.size());
        for (size_t index = 0; index < proposal.pages.size(); ++index) {
            auto& imported = proposal.pages[index];
            if (imported.objects.size() != 1 || imported.source_page_index != index ||
                imported.source_page_rotation_quarters_clockwise > 3)
                return document::ProposalStatus::ExecutionFailed;
            auto* preview = std::get_if<document::ImageObject>(
                &imported.objects.front());
            if (!preview || preview->name != "PDF source screen preview" ||
                preview->width_px <= 0 || preview->height_px <= 0 ||
                static_cast<uint64_t>(preview->width_px) * preview->height_px !=
                    preview->rgba_pixels().size())
                return document::ProposalStatus::ExecutionFailed;
            std::shared_ptr<const std::vector<uint32_t>> preview_pixels =
                preview->shared_pixels_rgba;
            if (!preview_pixels) {
                try {
                    preview_pixels = std::make_shared<const std::vector<uint32_t>>(
                        std::move(preview->pixels_rgba));
                } catch (...) {
                    return document::ProposalStatus::ExecutionFailed;
                }
            }
            document::PdfSourcePageReference reference{
                asset, imported.source_page_index,
                imported.source_page_rotation_quarters_clockwise,
                static_cast<uint32_t>(preview->width_px),
                static_cast<uint32_t>(preview->height_px),
                std::move(preview_pixels)};
            imported.objects.clear();
            for (auto& layer : imported.layout.layers)
                layer.object_ids.clear();
            auto document_page = document::make_document_page(
                imported.name.empty() ? "PDF Page " + std::to_string(index + 1) : imported.name,
                std::move(imported.layout), {}, std::move(reference));
            fidelity_report.source_page_index = imported.source_page_index;
            document_page.import_fidelity_reports.push_back(fidelity_report);
            pages.push_back(std::move(document_page));
        }
        const auto current = root_.document().current_snapshot();
        if (!current) return document::ProposalStatus::ExecutionFailed;
        const auto active_page_id = pages.front().id;
        document::SnapshotPtr snapshot;
        try {
            snapshot = std::make_shared<const document::DocumentSnapshot>(
                0, 0, std::move(pages), active_page_id,
                current->document_id(), document::DocumentContentMode::PdfPrintLocked);
        } catch (...) {
            return document::ProposalStatus::ExecutionFailed;
        }
        const auto status = root_.document().replace_from_proposal(
            std::move(metadata), std::move(snapshot));
        if (status == document::ProposalStatus::Accepted) {
            root_.vector_controller().on_escape();
            root_.vector_controller().selection().clear();
            sync_status_bar();
        }
        return status;
    }
    document::AsyncProposal staged;
    staged.metadata = std::move(metadata);
    staged.description = "Import " + proposal.format_name;
    if (!proposal.pages.empty()) {
        for (auto& page : proposal.pages) {
            page.import_fidelity_report = fidelity_report;
            page.import_fidelity_report.source_page_index = page.source_page_index;
        }
        staged.commands.push_back(std::make_unique<document::ImportPagesCommand>(
            std::move(proposal.pages), staged.metadata.target_page_id,
            resize_page_to_source));
        const auto status = root_.document().validate_and_commit_proposal(std::move(staged));
        if (status == document::ProposalStatus::Accepted) sync_status_bar();
        return status;
    }
    const std::optional<std::pair<double, double>> page_size = resize_page_to_source
        ? std::optional<std::pair<double, double>>{{proposal.source_page_width_mm,
                                                    proposal.source_page_height_mm}}
        : std::nullopt;
    staged.commands.push_back(std::make_unique<document::ImportContentCommand>(
        std::move(proposal.layers), std::move(proposal.objects), page_size,
        staged.metadata.target_page_id, std::move(fidelity_report)));
    const auto status = root_.document().validate_and_commit_proposal(std::move(staged));
    if (status == document::ProposalStatus::Accepted) sync_status_bar();
    return status;
}

ImportWorkerResult run_import_worker_job(
    const std::wstring& source_path, ImportFormat format,
    const document::AsyncProposalMetadata& job, std::stop_token cancellation,
    const std::function<void(ImportProgress)>& on_progress,
    PdfOpenIntent pdf_open_intent,
    uint32_t raster_frame_index) {
    ImportWorkerResult result;
    std::optional<ImportProgressStage> last_progress_stage;
    std::optional<ImportProgressStage> stage_before_memory_wait;
    auto last_progress_time = std::chrono::steady_clock::time_point::min();
    const auto progress = [&](ImportProgress value) {
        if (!on_progress) return;
        const auto now = std::chrono::steady_clock::now();
        // Large vector files can produce thousands of chunks. Keep progress
        // messages from starving paint/input; stage changes still arrive at once.
        if (last_progress_stage == value.stage && value.stage == ImportProgressStage::Reading &&
            now - last_progress_time < std::chrono::milliseconds(50) &&
            !(value.source_bytes_total > 0 &&
              value.source_bytes_copied >= value.source_bytes_total)) return;
        last_progress_stage = value.stage;
        last_progress_time = now;
        on_progress(value);
    };
#ifndef _WIN32
    (void)source_path; (void)format; (void)job; (void)cancellation; (void)progress;
    (void)pdf_open_intent; (void)raster_frame_index;
    result.error_message = "The isolated vector import worker is available only in the Windows desktop build.";
    return result;
#else
    using namespace pikdraw::interop;
    using namespace pikdraw::workers;
    SystemMemoryPressureWaiter memory_waiter(
        kSystemMemoryPressureWaitLimit,
        [&](bool waiting) {
            if (waiting) {
                stage_before_memory_wait = last_progress_stage;
                progress({ImportProgressStage::WaitingForMemory, 0});
            } else if (stage_before_memory_wait) {
                const auto restore = *stage_before_memory_wait;
                stage_before_memory_wait.reset();
                progress({restore, 0});
            }
        });
    try {
        if (cancellation.stop_requested()) { result.cancelled = true; return result; }
        progress({ImportProgressStage::Snapshotting, 0});
        if (memory_waiter.WaitForHeadroom(1, cancellation) != SystemMemoryWaitResult::Ready) {
            result.cancelled = cancellation.stop_requested();
            if (!result.cancelled)
                result.error_message = "Import timed out after waiting 5 minutes for system RAM to recover.";
            return result;
        }
        auto extension = std::filesystem::path(source_path).extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        const uint64_t source_limit = IsRasterExtension(extension)
            ? kRasterRecordByteLimit : std::numeric_limits<uint64_t>::max();
        SourceSnapshotFailure snapshot_failure{};
        auto source = IsRasterExtension(extension)
            ? SourceSnapshot::PinReadOnly(source_path, source_limit, &snapshot_failure)
            : SourceSnapshot::Create(source_path, source_limit, &snapshot_failure);
        if (!source || !source->IsValid()) {
            result.error_message = snapshot_failure == SourceSnapshotFailure::SourceTooLarge
                ? (IsRasterExtension(extension)
                    ? "The raster source exceeds the 32-bit raster-source record representation."
                    : "The selected file exceeds the addressable size for this build.")
                : "The selected file could not be staged safely.";
            return result;
        }
        auto signature = DetectSignature(source->ReadHandle());
        // A verified PDF container gets its representation tag from the suffix:
        // .ai means PDF-compatible AI, while .pdf remains PDF even if authored by Illustrator.
        if ((signature.format == DetectedImportFormat::Ai ||
             signature.format == DetectedImportFormat::Pdf) && extension == L".ai") {
            signature.format = DetectedImportFormat::Ai;
        } else if ((signature.format == DetectedImportFormat::Ai ||
                    signature.format == DetectedImportFormat::Pdf) && extension == L".pdf") {
            signature.format = DetectedImportFormat::Pdf;
        }
        if (!signature.recognized ||
            (format == ImportFormat::Dxf) !=
                (signature.format == DetectedImportFormat::Dxf)) {
            result.error_message = "The file extension and source signature do not match a supported import format.";
            return result;
        }
        if (!ExtensionMatches(signature.format, extension)) {
            result.error_message = "The selected filename extension does not match its detected source format.";
            return result;
        }
        if (pdf_open_intent == PdfOpenIntent::LockedSourceForPrint &&
            signature.format != DetectedImportFormat::Pdf) {
            result.error_message = "The locked source-for-print option currently accepts PDF files only.";
            return result;
        }
        const uint64_t original_source_bytes = source->SizeBytes();
        if (IsRaster(signature.format) && original_source_bytes > kRasterRecordByteLimit) {
            result.error_message = "The raster source exceeds the 32-bit raster-source record representation.";
            return result;
        }
        bool converted_dos_eps = false;
        if (signature.dos_eps_wrapper) {
            std::string diagnostic;
            auto converted_source = ConvertDosEpsToPdf(*source, diagnostic, memory_waiter,
                                                       cancellation);
            if (!converted_source || !converted_source->IsValid()) {
                if (cancellation.stop_requested()) { result.cancelled = true; return result; }
                result.error_message = "The DOS EPS file could not be converted in its restricted interpreter: ";
                result.error_message += diagnostic;
                return result;
            }
            source = std::move(converted_source);
            signature = {DetectedImportFormat::Pdf, true, false};
            converted_dos_eps = true;
        }
        switch (signature.format) {
        default:
            break;
        }
        if (cancellation.stop_requested()) { result.cancelled = true; return result; }

        const std::wstring worker_path = WorkerExecutablePath();
        if (worker_path.empty() || !std::filesystem::exists(worker_path)) {
            result.error_message = "The isolated import worker is missing from the application package.";
            return result;
        }
        progress({ImportProgressStage::StartingWorker, 0});
        WorkerLaunchConfig config;
        config.worker_executable_path = worker_path;
        config.working_directory = std::filesystem::path(worker_path).parent_path().wstring();
        config.appcontainer_profile_name = L"PikDrawVectorImport";
        config.use_appcontainer = true;
        if (memory_waiter.WaitForHeadroom(1, cancellation) != SystemMemoryWaitResult::Ready) {
            result.cancelled = cancellation.stop_requested();
            if (!result.cancelled)
                result.error_message = "Import timed out after waiting 5 minutes for system RAM to recover.";
            return result;
        }
        // Import admission is governed by the shared 95% pressure waiter, not
        // by a fixed per-worker memory quota; allocation waits now use the
        // shared 95% physical-RAM pressure gate and resume when headroom returns.
        config.job_limits.max_memory_bytes = 0;
        config.source_snapshot = &*source;
        WorkerProcessHost host;
        if (!host.Launch(config)) {
            result.error_message = "Windows could not start the isolated import worker.";
            return result;
        }
        uint32_t required_features = IPC_V2_FEATURE_SOURCE_HANDLE |
                                     IPC_V2_FEATURE_CHECKED_PROPOSAL_STREAM;
        if (IsRaster(signature.format))
            required_features |= IPC_V2_FEATURE_RASTER_RGBA16LE;
        if (signature.format == DetectedImportFormat::Cdr)
            required_features |= IPC_V2_FEATURE_RASTER_SOURCE_ASSETS;
        if (signature.format == DetectedImportFormat::Tiff)
            required_features |= IPC_V2_FEATURE_SOURCE_PAGE_SELECTION;
        if (!host.NegotiateV2(required_features, std::chrono::seconds(3),
                              &memory_waiter, cancellation)) {
            if (cancellation.stop_requested()) { result.cancelled = true; return result; }
            if (memory_waiter.remaining_wait().count() == 0) {
                result.error_message = "Import timed out after waiting 5 minutes for system RAM to recover.";
                return result;
            }
            result.error_message = "The isolated import worker did not negotiate the required protocol.";
            return result;
        }
        if (cancellation.stop_requested()) { result.cancelled = true; return result; }

        V2ImportBegin begin;
        begin.job.document_id = job.document_id;
        begin.job.epoch = job.epoch;
        begin.job.base_revision = job.base_revision;
        begin.job.generation = job.job_generation;
        begin.job.source_token = source->Token();
        begin.source_bytes = source->SizeBytes();
        begin.source_sha256 = source->Sha256();
        begin.format_id = FormatName(signature.format);
        if (signature.format == DetectedImportFormat::Tiff)
            begin.source_page_index = raster_frame_index;
        begin.profile_id = IsRaster(signature.format) ? "raster-rgba8-v1"
            : signature.format == DetectedImportFormat::Svg ? "svg-hierarchy-v3"
            : signature.format == DetectedImportFormat::Pdf
                ? (pdf_open_intent == PdfOpenIntent::LockedSourceForPrint
                    ? "pdf-print-source-v1" : "pdf-page-set-v3")
            : signature.format == DetectedImportFormat::Ai ? "ai-pdf-compatible-page-set-v3"
            : signature.format == DetectedImportFormat::Eps ? "eps-path-v1"
            : signature.format == DetectedImportFormat::Cdr ? "cdr-libcdr-svg-v2"
            : "dxf-entities-v1";
        const auto request = SerializeV2ImportBegin(begin, 1);
        const bool cdr_import = signature.format == DetectedImportFormat::Cdr;
        const bool rich_import = cdr_import ||
            signature.format == DetectedImportFormat::Pdf ||
            signature.format == DetectedImportFormat::Ai ||
            signature.format == DetectedImportFormat::Eps ||
            IsRaster(signature.format);
        const uint64_t max_response_bytes = rich_import ? std::numeric_limits<uint64_t>::max()
                                                       : kMaxImportResponseBytes;
        const uint64_t max_canonical_bytes = rich_import ? std::numeric_limits<uint64_t>::max()
                                                        : kMaxImportCanonicalBytes;
        const uint32_t max_chunks = rich_import ? kMaxRichImportChunks : kMaxImportChunks;
        const uint32_t max_frames = rich_import ? kMaxRichImportFrames : kMaxImportFrames;
        const V2ProposalStreamLimits limits{max_response_bytes, max_canonical_bytes,
            kMaxImportRecords, max_chunks, max_frames};
        CanonicalImportSpool canonical_spool;
        if (!canonical_spool.is_open()) {
            result.error_message = "The temporary disk spool for the import proposal could not be created.";
            return result;
        }
        size_t record_count = 0;
        uint64_t canonical_bytes = 0;
        std::string stream_consumer_failure;
        progress({ImportProgressStage::Reading, 0});
        const auto import_timeout = rich_import ? std::chrono::seconds(120) : std::chrono::seconds(30);
        const bool complete = host.TransactV2ProposalStream(request, import_timeout, limits,
            [&](const IpcV2Frame& frame) {
                if (cancellation.stop_requested()) return false;
                if (static_cast<MessageType>(frame.header.message_type) == MessageType::V2ImportEnd)
                    return true;
                if (!AppendValidatedRecords(frame, canonical_spool, record_count,
                                            canonical_bytes, max_canonical_bytes)) {
                    stream_consumer_failure = "A validated import chunk could not be written to the temporary proposal spool.";
                    return false;
                }
                progress({ImportProgressStage::Reading, record_count});
                return true;
            }, cancellation, &memory_waiter);
        if (!complete) {
            if (cancellation.stop_requested() || std::string_view(host.LastTransportFailure()) == "v2 import cancelled") {
                result.cancelled = true;
            } else if (std::string_view(host.LastTransportFailure()) ==
                       "system RAM pressure wait timed out after 5 minutes") {
                result.error_message = "Import timed out after waiting 5 minutes for system RAM to recover. No partial objects were committed.";
            } else if (std::string_view(host.LastTransportFailure()) == "worker returned v2 error response") {
                if (IsRaster(signature.format))
                    result.error_message = "The raster worker could not decode this source within codec, 32-bit record, or available memory limits. Check that the file is valid, select a smaller TIFF page, or free RAM and retry.";
                else if (signature.format == DetectedImportFormat::Svg)
                    result.error_message = "This file is malformed or uses features outside the supported static SVG subset.";
                else if (signature.format == DetectedImportFormat::Pdf || signature.format == DetectedImportFormat::Ai)
                    result.error_message = "This PDF/AI file is malformed or contains pages, effects, images, or objects outside the editable single-page path/text subset.";
                else if (signature.format == DetectedImportFormat::Eps)
                    result.error_message = "This EPS file is malformed or uses PostScript operators outside the safe editable path subset.";
                else if (signature.format == DetectedImportFormat::Cdr)
                    result.error_message = "This CDR file is malformed, uses a version outside the libcdr profile, or contains vector features outside the editable path/text subset.";
                else
                    result.error_message = "This file is malformed or uses entities outside the supported 2D DXF profile; source units are required.";
            } else {
                const std::string_view transport_failure = host.LastTransportFailure();
                result.error_message = stream_consumer_failure.empty()
                    ? "The isolated import worker rejected an incomplete or invalid proposal"
                    : stream_consumer_failure;
                if (!transport_failure.empty() && transport_failure != "none") {
                    if (stream_consumer_failure.empty()) result.error_message += ": ";
                    else result.error_message += " (";
                    result.error_message.append(transport_failure);
                    if (!stream_consumer_failure.empty()) result.error_message += ")";
                }
                result.error_message += ".";
                if (cdr_import && host.LastWorkerExitCode() != STILL_ACTIVE) {
                    char exit_status[40]{};
                    std::snprintf(exit_status, sizeof(exit_status),
                        " Worker exit status: 0x%08lx.",
                        static_cast<unsigned long>(host.LastWorkerExitCode()));
                    result.error_message += exit_status;
                }
            }
            return result;
        }
        if (cancellation.stop_requested()) { result.cancelled = true; return result; }
        progress({ImportProgressStage::Validating, record_count});
        result.proposal.format_name = FormatName(signature.format);
        result.proposal.source_bytes = source->SizeBytes();
        if (memory_waiter.WaitForHeadroom(1, cancellation) != SystemMemoryWaitResult::Ready) {
            result.cancelled = cancellation.stop_requested();
            if (!result.cancelled)
                result.error_message = "Import timed out after waiting 5 minutes for system RAM to recover. No partial objects were committed.";
            return result;
        }
        std::string proposal_diagnostic;
        if (!DecodeCanonicalProposal(canonical_spool, result.proposal, proposal_diagnostic,
                                     memory_waiter, cancellation,
                [&](uint64_t copied, uint64_t total) {
                    progress({ImportProgressStage::Reading, record_count, copied, total});
                })) {
            if (cancellation.stop_requested()) { result.cancelled = true; return result; }
            result.error_message = "The verified import proposal could not be mapped into native document objects: " +
                proposal_diagnostic;
            return result;
        }
        if (converted_dos_eps) {
            result.proposal.format_name = "EPS";
            result.proposal.profile_id = "eps-path-v1";
            result.proposal.source_bytes = original_source_bytes;
            result.proposal.coverage_status = "partial";
            result.proposal.appearance_status = "partial";
            result.proposal.editability_status = "native_editable_after_conversion";
            result.proposal.production_status = "not_assessed";
            if (!result.proposal.layers.empty())
                result.proposal.layers.front().name = "EPS Artwork";
            result.proposal.issues.push_back({
                ImportIssueSummary::Severity::Warning, "eps-source",
                "postscript_conversion", "dos-eps-postscript-interpreted-in-restricted-worker-and-normalized-to-pdf"});
        }
        if (pdf_open_intent == PdfOpenIntent::LockedSourceForPrint) {
            if (result.proposal.pages.empty() || !result.proposal.objects.empty() ||
                result.proposal.pages.size() > std::numeric_limits<uint32_t>::max()) {
                result.error_message = "The PDF source manifest is empty or contains editable objects.";
                return result;
            }
            for (size_t index = 0; index < result.proposal.pages.size(); ++index) {
                if (result.proposal.pages[index].source_page_index != index) {
                    result.error_message = "The PDF source manifest does not contain every page in source order.";
                    return result;
                }
            }
            std::vector<uint8_t> original_bytes;
            if (!ReadOriginalSource(source->ReadHandle(), source->SizeBytes(), original_bytes,
                                    memory_waiter, cancellation)) {
                result.cancelled = cancellation.stop_requested();
                if (!result.cancelled)
                    result.error_message = memory_waiter.remaining_wait().count() == 0
                        ? "Import timed out after waiting 5 minutes for system RAM to recover."
                        : "The staged PDF source could not be retained in the document.";
                return result;
            }
            auto asset = std::make_shared<document::PdfSourceAsset>();
            asset->sha256_hex = SourceDigestHex(source->Sha256());
            asset->page_count = static_cast<uint32_t>(result.proposal.pages.size());
            asset->bytes = std::make_shared<const std::vector<uint8_t>>(std::move(original_bytes));
            result.proposal.content_mode = document::DocumentContentMode::PdfPrintLocked;
            result.proposal.pdf_source_asset = std::move(asset);
        }
        result.success = true;
        progress({ImportProgressStage::Complete, record_count});
        return result;
    } catch (...) {
        if (cancellation.stop_requested()) result.cancelled = true;
        else result.error_message = "The import job failed while staging or validating the source.";
        return result;
    }
#endif
}

} // namespace pikdraw::desktop
