#include "atomic_export_file.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <istream>
#include <limits>
#include <ostream>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#endif

namespace pikdraw::interop::export_detail {

#ifdef _WIN32
namespace {

std::filesystem::path utf8_path(const std::string& value) {
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char byte : value) encoded.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(std::move(encoded));
}

bool cancellation_requested(const AtomicExportOptions& options, uint64_t generation,
                            bool& callback_failed) {
    if (!options.is_cancelled) return false;
    try {
        return options.is_cancelled(generation);
    } catch (...) {
        callback_failed = true;
        return false;
    }
}

void report_progress(const AtomicExportOptions& options, size_t target_index,
                     size_t target_count, uint64_t bytes_written, uint64_t byte_limit) {
    if (!options.on_progress) return;
    try {
        options.on_progress(target_index, target_count, bytes_written, byte_limit);
    } catch (...) {
        // UI progress cannot interrupt or invalidate export work.
    }
}

AtomicFileResult failed(std::string code, std::string message,
                        AtomicFileOutcome outcome = AtomicFileOutcome::Failed) {
    return {outcome, 0, {}, std::move(code), std::move(message)};
}

class StagedFile final {
public:
    explicit StagedFile(HANDLE handle) : handle_(handle) {}
    StagedFile(const StagedFile&) = delete;
    StagedFile& operator=(const StagedFile&) = delete;
    ~StagedFile() {
        if (handle_ == INVALID_HANDLE_VALUE) return;
        if (!committed_) {
            FILE_DISPOSITION_INFO disposition{TRUE};
            (void)SetFileInformationByHandle(handle_, FileDispositionInfo,
                                             &disposition, sizeof(disposition));
        }
        CloseHandle(handle_);
    }

    [[nodiscard]] HANDLE handle() const noexcept { return handle_; }
    void mark_committed() noexcept { committed_ = true; }

private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
    bool committed_{false};
};

std::unique_ptr<StagedFile> create_staged_file(const std::filesystem::path& directory,
                                               std::string& diagnostic) {
    static std::atomic<uint64_t> next_id{1};
    for (uint32_t attempt = 0; attempt < 256; ++attempt) {
        const auto id = next_id.fetch_add(1, std::memory_order_relaxed);
        const auto name = L".pikdraw-export-" + std::to_wstring(GetCurrentProcessId()) +
                          L"-" + std::to_wstring(id) + L".tmp";
        const auto path = directory / name;
        HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
                                    FILE_SHARE_READ, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                                    nullptr);
        if (handle != INVALID_HANDLE_VALUE)
            return std::make_unique<StagedFile>(handle);
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) {
            diagnostic = "A temporary output file could not be created.";
            return nullptr;
        }
    }
    diagnostic = "A unique temporary output file could not be allocated.";
    return nullptr;
}

class HandleStreamBuffer final : public std::streambuf {
public:
    HandleStreamBuffer(HANDLE handle, uint64_t byte_limit,
                       std::function<bool()> is_cancelled,
                       std::function<void(uint64_t)> report_progress)
        : handle_(handle), byte_limit_(byte_limit),
          is_cancelled_(std::move(is_cancelled)),
          report_progress_(std::move(report_progress)) {}
    [[nodiscard]] bool limit_exceeded() const noexcept { return limit_exceeded_; }
    [[nodiscard]] bool cancelled() const noexcept { return cancelled_; }
    [[nodiscard]] bool cancellation_check_failed() const noexcept {
        return cancellation_check_failed_;
    }

protected:
    std::streamsize xsputn(const char* source, std::streamsize count) override {
        if (count <= 0) return 0;
        const auto requested = static_cast<uint64_t>(count);
        const auto remaining = written_bytes_ < byte_limit_ ? byte_limit_ - written_bytes_ : 0;
        const auto allowed = std::min(requested, remaining);
        std::streamsize written_total = 0;
        while (static_cast<uint64_t>(written_total) < allowed) {
            if (is_cancelled_) {
                try {
                    if (is_cancelled_()) {
                        cancelled_ = true;
                        break;
                    }
                } catch (...) {
                    cancellation_check_failed_ = true;
                    break;
                }
            }
            constexpr uint64_t kWriteChunkBytes = 64 * 1024;
            const DWORD chunk = static_cast<DWORD>(
                std::min<uint64_t>(allowed - static_cast<uint64_t>(written_total),
                                   kWriteChunkBytes));
            DWORD written = 0;
            if (!WriteFile(handle_, source + written_total, chunk, &written, nullptr) ||
                written == 0) break;
            written_total += static_cast<std::streamsize>(written);
            written_bytes_ += written;
            if (report_progress_) {
                try {
                    report_progress_(written_bytes_);
                } catch (...) {
                    // Progress observers cannot invalidate bytes already written.
                }
            }
        }
        if (allowed < requested) limit_exceeded_ = true;
        return written_total;
    }

    int_type overflow(int_type character) override {
        if (traits_type::eq_int_type(character, traits_type::eof()))
            return traits_type::not_eof(character);
        const char value = traits_type::to_char_type(character);
        return xsputn(&value, 1) == 1 ? character : traits_type::eof();
    }

    int sync() override { return FlushFileBuffers(handle_) ? 0 : -1; }

private:
    HANDLE handle_;
    uint64_t byte_limit_{0};
    uint64_t written_bytes_{0};
    bool limit_exceeded_{false};
    std::function<bool()> is_cancelled_;
    std::function<void(uint64_t)> report_progress_;
    bool cancelled_{false};
    bool cancellation_check_failed_{false};
};

class HandleInputBuffer final : public std::streambuf {
public:
    explicit HandleInputBuffer(HANDLE handle) : handle_(handle) {
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(handle_, zero, nullptr, FILE_BEGIN)) failed_ = true;
        setg(buffer_.data(), buffer_.data(), buffer_.data());
    }
    [[nodiscard]] bool failed() const noexcept { return failed_; }

protected:
    int_type underflow() override {
        if (gptr() < egptr()) return traits_type::to_int_type(*gptr());
        DWORD read = 0;
        if (!ReadFile(handle_, buffer_.data(), static_cast<DWORD>(buffer_.size()), &read, nullptr)) {
            failed_ = true;
            return traits_type::eof();
        }
        if (read == 0) return traits_type::eof();
        setg(buffer_.data(), buffer_.data(), buffer_.data() + read);
        return traits_type::to_int_type(*gptr());
    }

    pos_type seekoff(off_type offset, std::ios_base::seekdir direction,
                     std::ios_base::openmode mode) override {
        if ((mode & std::ios_base::in) == 0) {
            failed_ = true;
            return pos_type(off_type(-1));
        }
        LARGE_INTEGER current{};
        if (!SetFilePointerEx(handle_, LARGE_INTEGER{}, &current, FILE_CURRENT)) {
            failed_ = true;
            return pos_type(off_type(-1));
        }
        current.QuadPart -= static_cast<LONGLONG>(egptr() - gptr());
        LONGLONG destination = 0;
        if (direction == std::ios_base::beg) {
            destination = static_cast<LONGLONG>(offset);
        } else if (direction == std::ios_base::cur) {
            if ((offset > 0 && current.QuadPart > std::numeric_limits<LONGLONG>::max() - offset) ||
                (offset < 0 && current.QuadPart < std::numeric_limits<LONGLONG>::min() - offset)) {
                failed_ = true;
                return pos_type(off_type(-1));
            }
            destination = current.QuadPart + static_cast<LONGLONG>(offset);
        } else if (direction == std::ios_base::end) {
            LARGE_INTEGER size{};
            if (!GetFileSizeEx(handle_, &size) ||
                (offset > 0 && size.QuadPart > std::numeric_limits<LONGLONG>::max() - offset) ||
                (offset < 0 && size.QuadPart < std::numeric_limits<LONGLONG>::min() - offset)) {
                failed_ = true;
                return pos_type(off_type(-1));
            }
            destination = size.QuadPart + static_cast<LONGLONG>(offset);
        } else {
            failed_ = true;
            return pos_type(off_type(-1));
        }
        if (destination < 0) {
            failed_ = true;
            return pos_type(off_type(-1));
        }
        LARGE_INTEGER position{};
        position.QuadPart = destination;
        if (!SetFilePointerEx(handle_, position, nullptr, FILE_BEGIN)) {
            failed_ = true;
            return pos_type(off_type(-1));
        }
        setg(buffer_.data(), buffer_.data(), buffer_.data());
        return pos_type(static_cast<off_type>(destination));
    }

    pos_type seekpos(pos_type position, std::ios_base::openmode mode) override {
        return seekoff(static_cast<off_type>(position), std::ios_base::beg, mode);
    }

private:
    HANDLE handle_;
    std::array<char, 64 * 1024> buffer_{};
    bool failed_{false};
};

bool hash_staged_file(HANDLE handle, uint64_t& byte_size, std::string& sha256_hex) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle, &size) || size.QuadPart <= 0) return false;
    byte_size = static_cast<uint64_t>(size.QuadPart);

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<uint8_t> hash_object;
    bool ok = false;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                                    nullptr, 0))) return false;
    ULONG object_bytes = 0;
    ULONG result_bytes = 0;
    if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                          reinterpret_cast<PUCHAR>(&object_bytes),
                                          sizeof(object_bytes), &result_bytes, 0)) ||
        object_bytes == 0) goto cleanup;
    hash_object.resize(object_bytes);
    if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, hash_object.data(), object_bytes,
                                         nullptr, 0, 0))) goto cleanup;
    {
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN)) goto cleanup;
        std::vector<uint8_t> bytes(1024 * 1024);
        for (;;) {
            DWORD read = 0;
            if (!ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr))
                goto cleanup;
            if (read == 0) break;
            if (!BCRYPT_SUCCESS(BCryptHashData(hash, bytes.data(), read, 0))) goto cleanup;
        }
    }
    {
        std::vector<uint8_t> digest(32);
        if (!BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(),
                                             static_cast<ULONG>(digest.size()), 0))) goto cleanup;
        static constexpr char hex[] = "0123456789abcdef";
        sha256_hex.clear();
        sha256_hex.reserve(digest.size() * 2);
        for (const auto byte : digest) {
            sha256_hex.push_back(hex[byte >> 4]);
            sha256_hex.push_back(hex[byte & 0x0f]);
        }
        ok = true;
    }

cleanup:
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}

bool atomically_rename(HANDLE staged_handle, const std::filesystem::path& target,
                       bool replace_existing) {
    const auto wide_target = target.wstring();
    if (wide_target.size() > (std::numeric_limits<DWORD>::max() / sizeof(wchar_t))) return false;
    const size_t name_bytes = wide_target.size() * sizeof(wchar_t);
    const size_t allocation = sizeof(FILE_RENAME_INFO) + name_bytes;
    std::vector<uint8_t> storage(allocation);
    auto* info = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    info->ReplaceIfExists = replace_existing ? TRUE : FALSE;
    info->RootDirectory = nullptr;
    info->FileNameLength = static_cast<DWORD>(name_bytes);
    std::memcpy(info->FileName, wide_target.data(), name_bytes);
    return SetFileInformationByHandle(staged_handle, FileRenameInfo, info,
                                      static_cast<DWORD>(storage.size())) != FALSE;
}

} // namespace

bool same_path_case_insensitive(const std::filesystem::path& left,
                                const std::filesystem::path& right) {
    const auto left_text = left.lexically_normal().wstring();
    const auto right_text = right.lexically_normal().wstring();
    if (left_text.size() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        right_text.size() > static_cast<size_t>(std::numeric_limits<int>::max())) return false;
    return CompareStringOrdinal(left_text.data(), static_cast<int>(left_text.size()),
                                right_text.data(), static_cast<int>(right_text.size()),
                                TRUE) == CSTR_EQUAL;
}

bool output_extension_allowed(const std::string& target,
                              const std::vector<std::string>& allowed_extensions) {
    const auto actual = utf8_path(target).extension().wstring();
    for (const auto& extension : allowed_extensions) {
        const auto configured = utf8_path(extension).wstring();
        if (actual.size() <= static_cast<size_t>(std::numeric_limits<int>::max()) &&
            configured.size() <= static_cast<size_t>(std::numeric_limits<int>::max()) &&
            CompareStringOrdinal(actual.data(), static_cast<int>(actual.size()),
                                 configured.data(), static_cast<int>(configured.size()),
                                 TRUE) == CSTR_EQUAL) return true;
    }
    return false;
}

bool canonicalize_output_path(const std::string& utf8, std::filesystem::path& output,
                              std::string& diagnostic) {
    std::error_code error;
    auto requested = utf8_path(utf8);
    if (requested.empty() || requested.filename().empty()) {
        diagnostic = "The output target must name a file.";
        return false;
    }
    if (requested.is_relative()) requested = std::filesystem::absolute(requested, error);
    if (error) {
        diagnostic = "The output location could not be resolved.";
        return false;
    }
    const auto parent = requested.parent_path();
    if (parent.empty() || !std::filesystem::exists(parent, error) || error ||
        !std::filesystem::is_directory(parent, error) || error) {
        diagnostic = "The output folder does not exist or is unavailable.";
        return false;
    }
    const auto canonical_parent = std::filesystem::weakly_canonical(parent, error);
    if (error) {
        diagnostic = "The output folder could not be resolved.";
        return false;
    }
    output = canonical_parent / requested.filename();
    wchar_t volume_path[MAX_PATH]{};
    if (!GetVolumePathNameW(canonical_parent.c_str(), volume_path, MAX_PATH) ||
        GetDriveTypeW(volume_path) != DRIVE_FIXED) {
        diagnostic = "Atomic export is enabled only for a local fixed volume.";
        return false;
    }

    const bool exists = std::filesystem::exists(output, error);
    if (error) {
        diagnostic = "The output target could not be inspected.";
        return false;
    }
    if (exists && std::filesystem::is_directory(output, error)) {
        diagnostic = "The output target is a folder, not a file.";
        return false;
    }
    if (error) {
        diagnostic = "The output target could not be inspected.";
        return false;
    }
    return true;
}

bool is_protected_source(const std::filesystem::path& output,
                         const std::vector<std::string>& protected_sources,
                         std::string& diagnostic) {
    std::error_code error;
    const auto canonical_output = std::filesystem::weakly_canonical(output, error);
    if (error) {
        diagnostic = "The output target could not be resolved for source protection.";
        return true;
    }
    for (const auto& source_text : protected_sources) {
        if (source_text.empty()) {
            diagnostic = "A protected source path is empty.";
            return true;
        }
        auto source = utf8_path(source_text);
        if (source.is_relative()) source = std::filesystem::absolute(source, error);
        if (error) {
            diagnostic = "A protected source path could not be resolved.";
            return true;
        }
        const auto canonical_source = std::filesystem::weakly_canonical(source, error);
        if (error) {
            diagnostic = "A protected source path could not be resolved.";
            return true;
        }
        if (same_path_case_insensitive(canonical_output, canonical_source)) {
            diagnostic = "The output target matches a protected source file.";
            return true;
        }
        const bool output_exists = std::filesystem::exists(output, error);
        if (error) {
            diagnostic = "The output target could not be inspected.";
            return true;
        }
        const bool source_exists = std::filesystem::exists(source, error);
        if (error) {
            diagnostic = "A protected source path could not be inspected.";
            return true;
        }
        if (output_exists && source_exists && std::filesystem::equivalent(output, source, error)) {
            if (error) {
                diagnostic = "The output target could not be compared with a protected source.";
                return true;
            }
            diagnostic = "The output target refers to a protected source file.";
            return true;
        }
        if (error) {
            diagnostic = "The output target could not be compared with a protected source.";
            return true;
        }
    }
    return false;
}

AtomicFileResult write_atomic_output(const ExportOutputTarget& target,
                                     const std::filesystem::path& output_path,
                                     const AtomicExportOptions& options,
                                     uint64_t cancellation_generation,
                                     size_t target_index, size_t target_count) {
    std::string diagnostic;
    auto staged = create_staged_file(output_path.parent_path(), diagnostic);
    if (!staged)
        return failed("EXPORT_TEMP_CREATE_FAILED",
                      diagnostic.empty() ? "A staging file could not be created." : diagnostic);

    report_progress(options, target_index, target_count, 0,
                    options.max_output_bytes_per_file);
    bool cancellation_check_failed_during_write = false;
    auto cancel_during_write = [&] {
        bool callback_failed = false;
        const bool requested = cancellation_requested(options, cancellation_generation,
                                                      callback_failed);
        cancellation_check_failed_during_write |= callback_failed;
        return requested || callback_failed;
    };
    auto progress_during_write = [&](uint64_t bytes_written) {
        report_progress(options, target_index, target_count, bytes_written,
                        options.max_output_bytes_per_file);
    };
    HandleStreamBuffer buffer(staged->handle(), options.max_output_bytes_per_file,
                              cancel_during_write, progress_during_write);
    std::ostream stream(&buffer);
    bool serialized = false;
    try {
        serialized = options.serialize(target, stream, diagnostic);
    } catch (...) {
        diagnostic = "The format writer raised an exception.";
    }
    stream.flush();
    if (buffer.cancellation_check_failed() || cancellation_check_failed_during_write)
        return failed("EXPORT_CANCELLATION_CHECK_FAILED",
                      "The export cancellation state could not be checked.");
    if (buffer.cancelled())
        return failed("EXPORT_CANCELLED", "Export was cancelled while writing a staged output.",
                      AtomicFileOutcome::Cancelled);
    if (!serialized || !stream.good() || buffer.limit_exceeded()) {
        if (buffer.limit_exceeded())
            return failed("EXPORT_OUTPUT_LIMIT_EXCEEDED",
                          "The output exceeded the configured per-file byte limit.",
                          AtomicFileOutcome::ResourceLimit);
        return failed("EXPORT_SERIALIZATION_FAILED",
                      diagnostic.empty() ? "The format writer could not serialize this output." :
                                           std::move(diagnostic));
    }

    AtomicFileResult result;
    if (!hash_staged_file(staged->handle(), result.byte_size, result.sha256_hex))
        return failed("EXPORT_OUTPUT_HASH_FAILED", "The staged output was empty or could not be hashed.");

    diagnostic.clear();
    bool valid = false;
    HandleInputBuffer input_buffer(staged->handle());
    std::istream staged_stream(&input_buffer);
    try {
        valid = options.validate(target, staged_stream, result.byte_size, diagnostic);
    } catch (...) {
        diagnostic = "The format validator raised an exception.";
    }
    if (!valid || input_buffer.failed())
        return failed("EXPORT_OUTPUT_VALIDATION_FAILED",
                      input_buffer.failed() ?
                          "The staged output could not be read completely by the validator." :
                          (diagnostic.empty() ? "The staged output did not pass validation." :
                                                std::move(diagnostic)));

    bool callback_failed = false;
    if (cancellation_requested(options, cancellation_generation, callback_failed))
        return failed("EXPORT_CANCELLED",
                      "Export was cancelled before the staged output was published.",
                      AtomicFileOutcome::Cancelled);
    if (callback_failed)
        return failed("EXPORT_CANCELLATION_CHECK_FAILED",
                      "The export cancellation state could not be checked.");
    if (is_protected_source(output_path, options.protected_source_paths, diagnostic))
        return failed("EXPORT_SOURCE_OVERWRITE_BLOCKED", std::move(diagnostic),
                      AtomicFileOutcome::Rejected);

    bool published = false;
    bool publish_invoked = false;
    bool gate_result = false;
    bool gate_failed = false;
    try {
        gate_result = options.publish_if_current(cancellation_generation, [&] {
            publish_invoked = true;
            published = atomically_rename(staged->handle(), output_path,
                                           options.allow_replace_existing);
            return published;
        });
    } catch (...) {
        gate_failed = true;
    }
    if (!published) {
        if (!gate_failed && !publish_invoked && !gate_result)
            return failed("EXPORT_CANCELLED",
                          "Export was cancelled before the staged output was published.",
                          AtomicFileOutcome::Cancelled);
        const auto code = gate_failed ? "EXPORT_PUBLISH_GATE_FAILED" :
                                        "EXPORT_ATOMIC_COMMIT_FAILED";
        const auto message = gate_failed ? "The export publish state could not be checked." :
            "The validated output could not be atomically published (Windows error " +
                std::to_string(GetLastError()) + ").";
        return failed(code, message);
    }

    staged->mark_committed();
    result.outcome = AtomicFileOutcome::Committed;
    return result;
}

#endif

} // namespace pikdraw::interop::export_detail
