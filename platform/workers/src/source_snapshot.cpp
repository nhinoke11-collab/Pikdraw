#include "pikdraw/workers/source_snapshot.hpp"

#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace pikdraw::workers {
namespace {

constexpr DWORD kIoBufferBytes = 256 * 1024;

class Sha256State final {
public:
    ~Sha256State() noexcept {
        if (hash_ != nullptr) BCryptDestroyHash(hash_);
        if (algorithm_ != nullptr) BCryptCloseAlgorithmProvider(algorithm_, 0);
    }

    [[nodiscard]] bool Start() noexcept {
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
            return false;
        }

        DWORD object_size = 0;
        DWORD bytes_written = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(
                algorithm_,
                BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&object_size),
                sizeof(object_size),
                &bytes_written,
                0)) ||
            object_size == 0 ||
            bytes_written != sizeof(object_size)) {
            return false;
        }

        try {
            object_.resize(object_size);
        } catch (...) {
            return false;
        }
        return BCRYPT_SUCCESS(BCryptCreateHash(
            algorithm_,
            &hash_,
            object_.data(),
            static_cast<ULONG>(object_.size()),
            nullptr,
            0,
            0));
    }

    [[nodiscard]] bool Update(const uint8_t* bytes, ULONG size) noexcept {
        return hash_ != nullptr && BCRYPT_SUCCESS(BCryptHashData(
            hash_, const_cast<PUCHAR>(bytes), size, 0));
    }

    [[nodiscard]] bool Finish(SourceDigest& digest) noexcept {
        return hash_ != nullptr && BCRYPT_SUCCESS(BCryptFinishHash(
            hash_, digest.data(), static_cast<ULONG>(digest.size()), 0));
    }

private:
    BCRYPT_ALG_HANDLE algorithm_{nullptr};
    BCRYPT_HASH_HANDLE hash_{nullptr};
    std::vector<uint8_t> object_;
};

[[nodiscard]] bool FillRandom(uint8_t* bytes, ULONG count) noexcept {
    return BCRYPT_SUCCESS(BCryptGenRandom(
        nullptr, bytes, count, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

[[nodiscard]] std::wstring HexToken(const SourceToken& token) {
    constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring text;
    text.reserve(token.size() * 2);
    for (const uint8_t byte : token) {
        text.push_back(digits[byte >> 4]);
        text.push_back(digits[byte & 0x0f]);
    }
    return text;
}

void SetFailure(
    SourceSnapshotFailure value,
    DWORD error,
    SourceSnapshotFailure* failure,
    DWORD* system_error) noexcept {
    if (failure != nullptr) *failure = value;
    if (system_error != nullptr) *system_error = error;
}

} // namespace

SourceSnapshot::~SourceSnapshot() noexcept {
    Reset();
}

SourceSnapshot::SourceSnapshot(SourceSnapshot&& other) noexcept
    : read_handle_(std::move(other.read_handle_)),
      staging_directory_(std::move(other.staging_directory_)),
      staging_file_(std::move(other.staging_file_)),
      owns_staging_file_(other.owns_staging_file_),
      size_bytes_(other.size_bytes_),
      sha256_(other.sha256_),
      token_(other.token_) {
    other.staging_directory_.clear();
    other.staging_file_.clear();
    other.owns_staging_file_ = false;
    other.size_bytes_ = 0;
    other.sha256_.fill(0);
    other.token_.fill(0);
}

SourceSnapshot& SourceSnapshot::operator=(SourceSnapshot&& other) noexcept {
    if (this == &other) return *this;
    Reset();
    read_handle_ = std::move(other.read_handle_);
    staging_directory_ = std::move(other.staging_directory_);
    staging_file_ = std::move(other.staging_file_);
    owns_staging_file_ = other.owns_staging_file_;
    size_bytes_ = other.size_bytes_;
    sha256_ = other.sha256_;
    token_ = other.token_;
    other.staging_directory_.clear();
    other.staging_file_.clear();
    other.owns_staging_file_ = false;
    other.size_bytes_ = 0;
    other.sha256_.fill(0);
    other.token_.fill(0);
    return *this;
}

bool SourceSnapshot::IsValid() const noexcept {
    return read_handle_.IsValid() && !staging_file_.empty();
}

std::optional<SourceSnapshot> SourceSnapshot::Create(
    const std::wstring& selected_source,
    uint64_t max_source_bytes,
    SourceSnapshotFailure* failure,
    DWORD* system_error) {
    SetFailure(SourceSnapshotFailure::None, ERROR_SUCCESS, failure, system_error);
    if (selected_source.empty() || max_source_bytes == 0) {
        SetFailure(SourceSnapshotFailure::InvalidLimit, ERROR_INVALID_PARAMETER, failure, system_error);
        return std::nullopt;
    }

    ScopedHandle input(CreateFileW(
        selected_source.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr));
    if (!input.IsValid()) {
        SetFailure(SourceSnapshotFailure::SourceOpenFailed, GetLastError(), failure, system_error);
        return std::nullopt;
    }

    FILE_ATTRIBUTE_TAG_INFO source_tag{};
    if (!GetFileInformationByHandleEx(input.Get(), FileAttributeTagInfo, &source_tag, sizeof(source_tag))) {
        SetFailure(SourceSnapshotFailure::SourceOpenFailed, GetLastError(), failure, system_error);
        return std::nullopt;
    }
    if ((source_tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        SetFailure(SourceSnapshotFailure::SourceIsReparsePoint, ERROR_CANT_ACCESS_FILE, failure, system_error);
        return std::nullopt;
    }

    FILE_STANDARD_INFO source_info{};
    if (!GetFileInformationByHandleEx(input.Get(), FileStandardInfo, &source_info, sizeof(source_info))) {
        SetFailure(SourceSnapshotFailure::SourceOpenFailed, GetLastError(), failure, system_error);
        return std::nullopt;
    }
    if (source_info.Directory || source_info.EndOfFile.QuadPart < 0) {
        SetFailure(SourceSnapshotFailure::SourceIsNotRegularFile, ERROR_INVALID_DATA, failure, system_error);
        return std::nullopt;
    }
    const uint64_t expected_size = static_cast<uint64_t>(source_info.EndOfFile.QuadPart);
    if (expected_size > max_source_bytes) {
        SetFailure(SourceSnapshotFailure::SourceTooLarge, ERROR_FILE_TOO_LARGE, failure, system_error);
        return std::nullopt;
    }

    SourceSnapshot snapshot;
    if (!FillRandom(snapshot.token_.data(), static_cast<ULONG>(snapshot.token_.size()))) {
        SetFailure(SourceSnapshotFailure::CryptographicFailure, ERROR_GEN_FAILURE, failure, system_error);
        return std::nullopt;
    }

    std::array<wchar_t, MAX_PATH + 1> temp_buffer{};
    const DWORD temp_chars = GetTempPathW(static_cast<DWORD>(temp_buffer.size()), temp_buffer.data());
    if (temp_chars == 0 || temp_chars >= temp_buffer.size()) {
        SetFailure(SourceSnapshotFailure::StagingCreateFailed, GetLastError(), failure, system_error);
        return std::nullopt;
    }

    snapshot.staging_directory_ =
        std::wstring(temp_buffer.data(), temp_chars) + L"PikDraw-" + HexToken(snapshot.token_);
    if (!CreateDirectoryW(snapshot.staging_directory_.c_str(), nullptr)) {
        SetFailure(SourceSnapshotFailure::StagingCreateFailed, GetLastError(), failure, system_error);
        snapshot.Reset();
        return std::nullopt;
    }
    snapshot.staging_file_ = snapshot.staging_directory_ + L"\\source.snapshot";
    snapshot.owns_staging_file_ = true;

    ScopedHandle output(CreateFileW(
        snapshot.staging_file_.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr));
    if (!output.IsValid()) {
        SetFailure(SourceSnapshotFailure::StagingCreateFailed, GetLastError(), failure, system_error);
        snapshot.Reset();
        return std::nullopt;
    }

    Sha256State digest_state;
    if (!digest_state.Start()) {
        SetFailure(SourceSnapshotFailure::CryptographicFailure, ERROR_GEN_FAILURE, failure, system_error);
        snapshot.Reset();
        return std::nullopt;
    }

    std::vector<uint8_t> buffer;
    try {
        buffer.resize(kIoBufferBytes);
    } catch (...) {
        SetFailure(SourceSnapshotFailure::StagingCreateFailed, ERROR_NOT_ENOUGH_MEMORY, failure, system_error);
        snapshot.Reset();
        return std::nullopt;
    }

    uint64_t copied = 0;
    while (true) {
        DWORD read_bytes = 0;
        if (!ReadFile(input.Get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read_bytes, nullptr)) {
            SetFailure(SourceSnapshotFailure::SourceReadFailed, GetLastError(), failure, system_error);
            snapshot.Reset();
            return std::nullopt;
        }
        if (read_bytes == 0) break;
        if (read_bytes > max_source_bytes - copied) {
            SetFailure(SourceSnapshotFailure::SourceTooLarge, ERROR_FILE_TOO_LARGE, failure, system_error);
            snapshot.Reset();
            return std::nullopt;
        }
        if (!digest_state.Update(buffer.data(), read_bytes)) {
            SetFailure(SourceSnapshotFailure::CryptographicFailure, ERROR_GEN_FAILURE, failure, system_error);
            snapshot.Reset();
            return std::nullopt;
        }

        DWORD written_total = 0;
        while (written_total < read_bytes) {
            DWORD written = 0;
            if (!WriteFile(
                    output.Get(),
                    buffer.data() + written_total,
                    read_bytes - written_total,
                    &written,
                    nullptr) ||
                written == 0) {
                SetFailure(SourceSnapshotFailure::StagingWriteFailed, GetLastError(), failure, system_error);
                snapshot.Reset();
                return std::nullopt;
            }
            written_total += written;
        }
        copied += read_bytes;
    }

    if (copied != expected_size || !FlushFileBuffers(output.Get()) ||
        !digest_state.Finish(snapshot.sha256_)) {
        const DWORD error = GetLastError();
        SetFailure(
            copied != expected_size ? SourceSnapshotFailure::SourceReadFailed :
                SourceSnapshotFailure::CryptographicFailure,
            error == ERROR_SUCCESS ? ERROR_FILE_INVALID : error,
            failure,
            system_error);
        snapshot.Reset();
        return std::nullopt;
    }
    output.Close();

    snapshot.read_handle_ = ScopedHandle(CreateFileW(
        snapshot.staging_file_.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr));
    if (!snapshot.read_handle_.IsValid()) {
        SetFailure(SourceSnapshotFailure::SnapshotVerificationFailed, GetLastError(), failure, system_error);
        snapshot.Reset();
        return std::nullopt;
    }

    FILE_ATTRIBUTE_TAG_INFO staged_tag{};
    FILE_STANDARD_INFO staged_info{};
    if (!GetFileInformationByHandleEx(
            snapshot.read_handle_.Get(), FileAttributeTagInfo, &staged_tag, sizeof(staged_tag)) ||
        (staged_tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        !GetFileInformationByHandleEx(
            snapshot.read_handle_.Get(), FileStandardInfo, &staged_info, sizeof(staged_info)) ||
        staged_info.Directory || staged_info.EndOfFile.QuadPart < 0 ||
        static_cast<uint64_t>(staged_info.EndOfFile.QuadPart) != copied) {
        SetFailure(
            SourceSnapshotFailure::SnapshotVerificationFailed,
            GetLastError() == ERROR_SUCCESS ? ERROR_FILE_INVALID : GetLastError(),
            failure,
            system_error);
        snapshot.Reset();
        return std::nullopt;
    }

    snapshot.size_bytes_ = copied;
    return snapshot;
}

std::optional<SourceSnapshot> SourceSnapshot::PinReadOnly(
    const std::wstring& selected_source,
    uint64_t max_source_bytes,
    SourceSnapshotFailure* failure,
    DWORD* system_error) {
    SetFailure(SourceSnapshotFailure::None, ERROR_SUCCESS, failure, system_error);
    if (selected_source.empty() || max_source_bytes == 0) {
        SetFailure(SourceSnapshotFailure::InvalidLimit, ERROR_INVALID_PARAMETER,
                   failure, system_error);
        return std::nullopt;
    }

    ScopedHandle input(CreateFileW(
        selected_source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_RANDOM_ACCESS,
        nullptr));
    if (!input.IsValid()) {
        SetFailure(SourceSnapshotFailure::SourceOpenFailed, GetLastError(),
                   failure, system_error);
        return std::nullopt;
    }

    FILE_ATTRIBUTE_TAG_INFO source_tag{};
    FILE_STANDARD_INFO source_info{};
    if (!GetFileInformationByHandleEx(input.Get(), FileAttributeTagInfo,
                                      &source_tag, sizeof(source_tag))) {
        SetFailure(SourceSnapshotFailure::SourceOpenFailed, GetLastError(),
                   failure, system_error);
        return std::nullopt;
    }
    if ((source_tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        SetFailure(SourceSnapshotFailure::SourceIsReparsePoint, ERROR_CANT_ACCESS_FILE,
                   failure, system_error);
        return std::nullopt;
    }
    if (!GetFileInformationByHandleEx(input.Get(), FileStandardInfo,
                                       &source_info, sizeof(source_info))) {
        SetFailure(SourceSnapshotFailure::SourceOpenFailed, GetLastError(),
                   failure, system_error);
        return std::nullopt;
    }
    if (source_info.Directory || source_info.EndOfFile.QuadPart < 0) {
        SetFailure(SourceSnapshotFailure::SourceIsNotRegularFile, ERROR_INVALID_DATA,
                   failure, system_error);
        return std::nullopt;
    }
    const auto expected_size = static_cast<uint64_t>(source_info.EndOfFile.QuadPart);
    if (expected_size > max_source_bytes) {
        SetFailure(SourceSnapshotFailure::SourceTooLarge, ERROR_FILE_TOO_LARGE,
                   failure, system_error);
        return std::nullopt;
    }

    SourceSnapshot lease;
    if (!FillRandom(lease.token_.data(), static_cast<ULONG>(lease.token_.size()))) {
        SetFailure(SourceSnapshotFailure::CryptographicFailure, ERROR_GEN_FAILURE,
                   failure, system_error);
        return std::nullopt;
    }
    uint64_t hashed_bytes = 0;
    if (!HashReadOnlyHandle(input.Get(), max_source_bytes, hashed_bytes,
                            lease.sha256_, system_error)) {
        const DWORD error = system_error ? *system_error : GetLastError();
        SetFailure(SourceSnapshotFailure::SourceReadFailed,
                   error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error,
                   failure, system_error);
        return std::nullopt;
    }

    FILE_STANDARD_INFO verified_info{};
    if (hashed_bytes != expected_size ||
        !GetFileInformationByHandleEx(input.Get(), FileStandardInfo,
                                      &verified_info, sizeof(verified_info)) ||
        verified_info.Directory || verified_info.EndOfFile.QuadPart < 0 ||
        static_cast<uint64_t>(verified_info.EndOfFile.QuadPart) != expected_size) {
        SetFailure(SourceSnapshotFailure::SnapshotVerificationFailed,
                   GetLastError() == ERROR_SUCCESS ? ERROR_FILE_INVALID : GetLastError(),
                   failure, system_error);
        return std::nullopt;
    }

    lease.read_handle_ = std::move(input);
    lease.staging_file_ = selected_source;
    lease.size_bytes_ = expected_size;
    return lease;
}

void SourceSnapshot::Reset() noexcept {
    read_handle_.Close();
    if (owns_staging_file_ && !staging_file_.empty()) DeleteFileW(staging_file_.c_str());
    if (!staging_directory_.empty()) RemoveDirectoryW(staging_directory_.c_str());
    staging_file_.clear();
    staging_directory_.clear();
    owns_staging_file_ = false;
    size_bytes_ = 0;
    sha256_.fill(0);
    token_.fill(0);
}

bool HashReadOnlyHandle(
    HANDLE file,
    uint64_t max_bytes,
    uint64_t& bytes_read,
    SourceDigest& digest,
    DWORD* system_error) noexcept {
    bytes_read = 0;
    digest.fill(0);
    if (system_error != nullptr) *system_error = ERROR_SUCCESS;
    if (file == nullptr || file == INVALID_HANDLE_VALUE || max_bytes == 0 ||
        GetFileType(file) != FILE_TYPE_DISK) {
        if (system_error != nullptr) *system_error = ERROR_INVALID_HANDLE;
        return false;
    }

    LARGE_INTEGER original{};
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(file, zero, &original, FILE_CURRENT) ||
        !SetFilePointerEx(file, zero, nullptr, FILE_BEGIN)) {
        if (system_error != nullptr) *system_error = GetLastError();
        return false;
    }

    Sha256State state;
    if (!state.Start()) {
        SetFilePointerEx(file, original, nullptr, FILE_BEGIN);
        if (system_error != nullptr) *system_error = ERROR_GEN_FAILURE;
        return false;
    }

    std::array<uint8_t, kIoBufferBytes> buffer{};
    bool success = true;
    while (true) {
        DWORD read_now = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read_now, nullptr)) {
            success = false;
            if (system_error != nullptr) *system_error = GetLastError();
            break;
        }
        if (read_now == 0) break;
        if (read_now > max_bytes - bytes_read) {
            success = false;
            if (system_error != nullptr) *system_error = ERROR_FILE_TOO_LARGE;
            break;
        }
        if (!state.Update(buffer.data(), read_now)) {
            success = false;
            if (system_error != nullptr) *system_error = ERROR_GEN_FAILURE;
            break;
        }
        bytes_read += read_now;
    }

    if (success && !state.Finish(digest)) {
        success = false;
        if (system_error != nullptr) *system_error = ERROR_GEN_FAILURE;
    }
    if (!SetFilePointerEx(file, original, nullptr, FILE_BEGIN)) {
        success = false;
        if (system_error != nullptr) *system_error = GetLastError();
    }
    return success;
}

} // namespace pikdraw::workers
