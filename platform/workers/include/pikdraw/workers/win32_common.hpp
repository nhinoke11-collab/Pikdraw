#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00 // Windows 10
#endif

#include <windows.h>
#include <string>
#include <string_view>
#include <vector>

namespace pikdraw::workers {

class ScopedHandle {
public:
    ScopedHandle() noexcept = default;
    explicit ScopedHandle(HANDLE h) noexcept : handle_(h) {}
    ~ScopedHandle() noexcept {
        Close();
    }

    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

    ScopedHandle(ScopedHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = INVALID_HANDLE_VALUE;
    }
    ScopedHandle& operator=(ScopedHandle&& other) noexcept {
        if (this != &other) {
            Close();
            handle_ = other.handle_;
            other.handle_ = INVALID_HANDLE_VALUE;
        }
        return *this;
    }

    [[nodiscard]] HANDLE Get() const noexcept { return handle_; }
    [[nodiscard]] bool IsValid() const noexcept {
        return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
    }

    HANDLE* GetAddressOf() noexcept {
        Close();
        return &handle_;
    }

    void Close() noexcept {
        if (IsValid()) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

    HANDLE Release() noexcept {
        HANDLE temp = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return temp;
    }

private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

class ScopedSid {
public:
    ScopedSid() noexcept = default;
    explicit ScopedSid(PSID sid) noexcept : sid_(sid) {}
    ~ScopedSid() noexcept {
        Free();
    }

    ScopedSid(const ScopedSid&) = delete;
    ScopedSid& operator=(const ScopedSid&) = delete;

    ScopedSid(ScopedSid&& other) noexcept : sid_(other.sid_) {
        other.sid_ = nullptr;
    }
    ScopedSid& operator=(ScopedSid&& other) noexcept {
        if (this != &other) {
            Free();
            sid_ = other.sid_;
            other.sid_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] PSID Get() const noexcept { return sid_; }
    [[nodiscard]] bool IsValid() const noexcept { return sid_ != nullptr; }

    PSID* GetAddressOf() noexcept {
        Free();
        return &sid_;
    }

    void Free() noexcept {
        if (sid_ != nullptr) {
            FreeSid(sid_);
            sid_ = nullptr;
        }
    }

private:
    PSID sid_{nullptr};
};

// UTF-8 <-> UTF-16 conversions
[[nodiscard]] std::wstring Utf8ToWide(std::string_view utf8);
[[nodiscard]] std::string WideToUtf8(std::wstring_view wide);

// Command line quoting for CreateProcessW
[[nodiscard]] std::wstring QuoteCommandLineArgument(std::wstring_view arg);

} // namespace pikdraw::workers
