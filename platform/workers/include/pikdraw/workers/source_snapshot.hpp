#pragma once

#include "win32_common.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace pikdraw::workers {

using SourceDigest = std::array<uint8_t, 32>;
using SourceToken = std::array<uint8_t, 16>;

enum class SourceSnapshotFailure {
    None,
    InvalidLimit,
    SourceOpenFailed,
    SourceIsReparsePoint,
    SourceIsNotRegularFile,
    SourceTooLarge,
    StagingCreateFailed,
    SourceReadFailed,
    StagingWriteFailed,
    SnapshotVerificationFailed,
    CryptographicFailure
};

class SourceSnapshot final {
public:
    SourceSnapshot() noexcept = default;
    ~SourceSnapshot() noexcept;

    SourceSnapshot(const SourceSnapshot&) = delete;
    SourceSnapshot& operator=(const SourceSnapshot&) = delete;
    SourceSnapshot(SourceSnapshot&& other) noexcept;
    SourceSnapshot& operator=(SourceSnapshot&& other) noexcept;

    [[nodiscard]] static std::optional<SourceSnapshot> Create(
        const std::wstring& selected_source,
        uint64_t max_source_bytes,
        SourceSnapshotFailure* failure = nullptr,
        DWORD* system_error = nullptr);

    /// Pins the selected file read-only for one import job, denying concurrent
    /// write/delete opens and hashing the exact inherited handle without a
    /// second full-file staging copy.
    [[nodiscard]] static std::optional<SourceSnapshot> PinReadOnly(
        const std::wstring& selected_source,
        uint64_t max_source_bytes,
        SourceSnapshotFailure* failure = nullptr,
        DWORD* system_error = nullptr);

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] HANDLE ReadHandle() const noexcept { return read_handle_.Get(); }
    [[nodiscard]] uint64_t SizeBytes() const noexcept { return size_bytes_; }
    [[nodiscard]] const SourceDigest& Sha256() const noexcept { return sha256_; }
    [[nodiscard]] const SourceToken& Token() const noexcept { return token_; }

private:
    void Reset() noexcept;

    ScopedHandle read_handle_;
    std::wstring staging_directory_;
    std::wstring staging_file_;
    bool owns_staging_file_{false};
    uint64_t size_bytes_{0};
    SourceDigest sha256_{};
    SourceToken token_{};
};

// Reads from the current file position and computes a bounded digest.
[[nodiscard]] bool HashReadOnlyHandle(
    HANDLE file,
    uint64_t max_bytes,
    uint64_t& bytes_read,
    SourceDigest& digest,
    DWORD* system_error = nullptr) noexcept;

} // namespace pikdraw::workers
