#include "native_clipboard.hpp"
#include <vector>
#include <cstring>

namespace pikdraw::desktop {

namespace {

struct ClipboardScope {
    bool opened{false};
    explicit ClipboardScope(HWND hwnd) {
        for (int retry = 0; retry < 5; ++retry) {
            if (OpenClipboard(hwnd)) {
                opened = true;
                break;
            }
            Sleep(10);
        }
    }
    ~ClipboardScope() {
        if (opened) {
            CloseClipboard();
        }
    }
};

std::wstring utf8_to_wide(std::string_view utf8) {
    if (utf8.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), count);
    return result;
}

std::string wide_to_utf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    int count = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), count, nullptr, nullptr);
    return result;
}

bool set_binary_data(UINT format, std::string_view data) {
    if (data.empty()) return false;
    const uint32_t len = static_cast<uint32_t>(data.size());
    HGLOBAL h_mem = GlobalAlloc(GMEM_MOVEABLE, sizeof(uint32_t) + len);
    if (!h_mem) return false;

    void* ptr = GlobalLock(h_mem);
    if (!ptr) {
        GlobalFree(h_mem);
        return false;
    }

    std::memcpy(ptr, &len, sizeof(uint32_t));
    std::memcpy(static_cast<char*>(ptr) + sizeof(uint32_t), data.data(), len);
    GlobalUnlock(h_mem);

    if (!SetClipboardData(format, h_mem)) {
        GlobalFree(h_mem);
        return false;
    }
    return true;
}

std::optional<std::string> get_binary_data(UINT format) {
    HANDLE h_data = GetClipboardData(format);
    if (!h_data) return std::nullopt;

    const void* ptr = GlobalLock(h_data);
    if (!ptr) return std::nullopt;

    const size_t max_size = GlobalSize(h_data);
    if (max_size < sizeof(uint32_t)) {
        GlobalUnlock(h_data);
        return std::nullopt;
    }

    uint32_t len = 0;
    std::memcpy(&len, ptr, sizeof(uint32_t));
    if (len > max_size - sizeof(uint32_t)) {
        GlobalUnlock(h_data);
        return std::nullopt;
    }

    std::string result(static_cast<const char*>(ptr) + sizeof(uint32_t), len);
    GlobalUnlock(h_data);
    return result;
}

bool set_raw_data(UINT format, std::string_view data) {
    if (data.empty()) return false;
    HGLOBAL h_mem = GlobalAlloc(GMEM_MOVEABLE, data.size() + 1);
    if (!h_mem) return false;

    void* ptr = GlobalLock(h_mem);
    if (!ptr) {
        GlobalFree(h_mem);
        return false;
    }

    std::memcpy(ptr, data.data(), data.size());
    static_cast<char*>(ptr)[data.size()] = '\0';
    GlobalUnlock(h_mem);

    if (!SetClipboardData(format, h_mem)) {
        GlobalFree(h_mem);
        return false;
    }
    return true;
}

bool set_unicode_text(std::string_view text) {
    std::wstring wide = utf8_to_wide(text);
    if (wide.empty()) return false;

    const size_t byte_size = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL h_mem = GlobalAlloc(GMEM_MOVEABLE, byte_size);
    if (!h_mem) return false;

    void* ptr = GlobalLock(h_mem);
    if (!ptr) {
        GlobalFree(h_mem);
        return false;
    }

    std::memcpy(ptr, wide.data(), wide.size() * sizeof(wchar_t));
    static_cast<wchar_t*>(ptr)[wide.size()] = L'\0';
    GlobalUnlock(h_mem);

    if (!SetClipboardData(CF_UNICODETEXT, h_mem)) {
        GlobalFree(h_mem);
        return false;
    }
    return true;
}

std::optional<std::string> get_raw_data(UINT format) {
    HANDLE h_data = GetClipboardData(format);
    if (!h_data) return std::nullopt;

    const void* ptr = GlobalLock(h_data);
    if (!ptr) return std::nullopt;

    const size_t max_size = GlobalSize(h_data);
    const char* str = static_cast<const char*>(ptr);
    size_t len = 0;
    while (len < max_size && str[len] != '\0') {
        ++len;
    }

    std::string result(str, len);
    GlobalUnlock(h_data);
    return result;
}

std::optional<std::string> get_unicode_text() {
    HANDLE h_data = GetClipboardData(CF_UNICODETEXT);
    if (!h_data) return std::nullopt;

    const wchar_t* ptr = static_cast<const wchar_t*>(GlobalLock(h_data));
    if (!ptr) return std::nullopt;

    const size_t max_wchars = GlobalSize(h_data) / sizeof(wchar_t);
    size_t len = 0;
    while (len < max_wchars && ptr[len] != L'\0') {
        ++len;
    }

    std::wstring wide(ptr, len);
    GlobalUnlock(h_data);
    return wide_to_utf8(wide);
}

} // namespace

UINT NativeClipboard::native_format_id() noexcept {
    static const UINT id = RegisterClipboardFormatW(L"PikDraw.DocumentSnapshot.v1");
    return id;
}

UINT NativeClipboard::svg_format_id() noexcept {
    static const UINT id = RegisterClipboardFormatW(L"image/svg+xml");
    return id;
}

bool NativeClipboard::write(
    HWND hwnd,
    std::string_view native_snapshot_json,
    std::string_view svg_data,
    std::string_view plain_text) {

    ClipboardScope scope(hwnd);
    if (!scope.opened) return false;

    if (!EmptyClipboard()) return false;

    if (!native_snapshot_json.empty()) {
        set_binary_data(native_format_id(), native_snapshot_json);
    }

    if (!svg_data.empty()) {
        set_raw_data(svg_format_id(), svg_data);
    }

    if (!plain_text.empty()) {
        set_unicode_text(plain_text);
    } else if (!svg_data.empty()) {
        set_unicode_text(svg_data);
    }

    return true;
}

bool NativeClipboard::has_native_snapshot() {
    return IsClipboardFormatAvailable(native_format_id()) != FALSE;
}

bool NativeClipboard::has_svg() {
    if (IsClipboardFormatAvailable(svg_format_id())) return true;
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        ClipboardScope scope(nullptr);
        if (scope.opened) {
            auto text = get_unicode_text();
            if (text && text->find("<svg") != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

bool NativeClipboard::has_text() {
    return IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE;
}

std::optional<std::string> NativeClipboard::read_native_snapshot() {
    ClipboardScope scope(nullptr);
    if (!scope.opened) return std::nullopt;
    return get_binary_data(native_format_id());
}

std::optional<std::string> NativeClipboard::read_svg() {
    ClipboardScope scope(nullptr);
    if (!scope.opened) return std::nullopt;

    auto svg = get_raw_data(svg_format_id());
    if (svg && !svg->empty()) return svg;

    auto text = get_unicode_text();
    if (text && text->find("<svg") != std::string::npos) {
        return text;
    }
    return std::nullopt;
}

std::optional<std::string> NativeClipboard::read_text() {
    ClipboardScope scope(nullptr);
    if (!scope.opened) return std::nullopt;
    return get_unicode_text();
}

} // namespace pikdraw::desktop
