#include "pikdraw/com_server/com_ids.hpp"

#ifdef _WIN32

#include <windows.h>
#include <objbase.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace pikdraw::com_server {
namespace {
constexpr std::wstring_view kClassesPrefix = L"Software\\Classes\\";
constexpr std::wstring_view kDescription = L"PikDraw Automation";

std::wstring clsid_text() {
    wchar_t buffer[40]{};
    const int length = StringFromGUID2(kPikDrawAutomationClsid, buffer,
                                       static_cast<int>(std::size(buffer)));
    return length > 1 ? std::wstring(buffer, static_cast<size_t>(length - 1)) : std::wstring{};
}

std::wstring clsid_path() {
    return std::wstring(kClassesPrefix) + L"CLSID\\" + clsid_text();
}

bool set_string(std::wstring_view path, std::wstring_view name, std::wstring_view value,
                LSTATUS& error) {
    HKEY key = nullptr;
    const std::wstring owned_path(path);
    error = RegCreateKeyExW(HKEY_CURRENT_USER, owned_path.c_str(), 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &key, nullptr);
    if (error != ERROR_SUCCESS) return false;
    const std::wstring owned_name(name);
    std::wstring terminated(value);
    error = RegSetValueExW(key, owned_name.empty() ? nullptr : owned_name.c_str(), 0, REG_SZ,
        reinterpret_cast<const BYTE*>(terminated.c_str()),
        static_cast<DWORD>((terminated.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return error == ERROR_SUCCESS;
}

LSTATUS read_string(std::wstring_view path, std::wstring_view name, std::wstring& value) {
    const std::wstring owned_path(path);
    const std::wstring owned_name(name);
    DWORD bytes = 0;
    LSTATUS result = RegGetValueW(HKEY_CURRENT_USER, owned_path.c_str(),
        owned_name.empty() ? nullptr : owned_name.c_str(), RRF_RT_REG_SZ, nullptr, nullptr, &bytes);
    if (result != ERROR_SUCCESS) return result;
    if (bytes == 0 || bytes > 64 * 1024 || (bytes % sizeof(wchar_t)) != 0) return ERROR_INVALID_DATA;
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t));
    result = RegGetValueW(HKEY_CURRENT_USER, owned_path.c_str(),
        owned_name.empty() ? nullptr : owned_name.c_str(), RRF_RT_REG_SZ, nullptr,
        buffer.data(), &bytes);
    if (result == ERROR_SUCCESS) value.assign(buffer.data());
    return result;
}

bool validate_existing(std::wstring_view path, std::wstring_view name,
                       std::wstring_view expected, LSTATUS& error) {
    std::wstring actual;
    error = read_string(path, name, actual);
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        error = ERROR_SUCCESS;
        return true;
    }
    if (error != ERROR_SUCCESS) return false;
    if (actual == expected) return true;
    error = ERROR_ALREADY_EXISTS;
    return false;
}

bool delete_owned_tree(std::wstring_view path, std::wstring_view name,
                       std::wstring_view expected, LSTATUS& error) {
    std::wstring actual;
    error = read_string(path, name, actual);
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        error = ERROR_SUCCESS;
        return true;
    }
    if (error != ERROR_SUCCESS) return false;
    if (actual != expected) {
        error = ERROR_ACCESS_DENIED;
        return false;
    }
    const std::wstring owned_path(path);
    error = RegDeleteTreeW(HKEY_CURRENT_USER, owned_path.c_str());
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) error = ERROR_SUCCESS;
    return error == ERROR_SUCCESS;
}

bool executable_path(std::wstring& path) {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
        if (length == 0) return false;
        if (length < buffer.size() - 1) {
            path.assign(buffer.data(), length);
            return true;
        }
        if (buffer.size() >= 32768) return false;
        buffer.resize(std::min<size_t>(buffer.size() * 2, 32768));
    }
}

} // namespace

LSTATUS register_com_server() {
    std::wstring exe;
    if (!executable_path(exe)) return GetLastError() ? GetLastError() : ERROR_BAD_PATHNAME;
    const std::wstring guid = clsid_text();
    if (guid.empty()) return ERROR_INVALID_DATA;

    const std::wstring version_clsid = std::wstring(kClassesPrefix) +
        kPikDrawAutomationProgId + L"\\CLSID";
    const std::wstring version_independent_curver = std::wstring(kClassesPrefix) +
        kPikDrawAutomationVersionIndependentProgId + L"\\CurVer";
    const std::wstring clsid = clsid_path();
    const std::wstring local_server = clsid + L"\\LocalServer32";
    LSTATUS error = ERROR_SUCCESS;
    if (!validate_existing(version_clsid, L"", guid, error) ||
        !validate_existing(version_independent_curver, L"", kPikDrawAutomationProgId, error) ||
        !validate_existing(clsid, L"", kDescription, error) ||
        !validate_existing(clsid, L"ProgID", kPikDrawAutomationProgId, error) ||
        !validate_existing(clsid, L"VersionIndependentProgID",
                           kPikDrawAutomationVersionIndependentProgId, error)) return error;

    // COM appends " -Embedding" when it starts a LocalServer32 executable.
    const std::wstring command = L"\"" + exe + L"\"";
    const std::wstring version_root = std::wstring(kClassesPrefix) + kPikDrawAutomationProgId;
    const std::wstring version_clsid_root = version_root + L"\\CLSID";
    const std::wstring stable_root = std::wstring(kClassesPrefix) +
        kPikDrawAutomationVersionIndependentProgId;
    const std::wstring stable_clsid_root = stable_root + L"\\CLSID";
    const std::wstring stable_curver_root = stable_root + L"\\CurVer";
    const std::wstring version_text(kPikDrawAutomationVersion);

    if (!set_string(std::wstring(kClassesPrefix) + L"CLSID\\" + guid, L"", kDescription, error) ||
        !set_string(clsid, L"ProgID", kPikDrawAutomationProgId, error) ||
        !set_string(clsid, L"VersionIndependentProgID",
                    kPikDrawAutomationVersionIndependentProgId, error) ||
        !set_string(local_server, L"", command, error) ||
        !set_string(local_server, L"ServerExecutable", exe, error) ||
        !set_string(version_root, L"", kDescription, error) ||
        !set_string(version_clsid_root, L"", guid, error) ||
        !set_string(stable_root, L"", kDescription, error) ||
        !set_string(stable_clsid_root, L"", guid, error) ||
        !set_string(stable_curver_root, L"", kPikDrawAutomationProgId, error)) return error;
    return ERROR_SUCCESS;
}

LSTATUS unregister_com_server() {
    const std::wstring guid = clsid_text();
    if (guid.empty()) return ERROR_INVALID_DATA;
    const std::wstring version_root = std::wstring(kClassesPrefix) + kPikDrawAutomationProgId;
    const std::wstring version_clsid_root = version_root + L"\\CLSID";
    const std::wstring stable_root = std::wstring(kClassesPrefix) +
        kPikDrawAutomationVersionIndependentProgId;
    const std::wstring stable_curver_root = stable_root + L"\\CurVer";
    const std::wstring clsid = clsid_path();

    LSTATUS error = ERROR_SUCCESS;
    if (!delete_owned_tree(version_root, L"", kDescription, error) ||
        !delete_owned_tree(stable_root, L"", kDescription, error) ||
        !delete_owned_tree(clsid, L"", kDescription, error)) return error;
    // If a user removed the parent ProgID keys manually, the subordinate key
    // may still exist; remove it only when it maps back to this CLSID.
    if (!delete_owned_tree(version_clsid_root, L"", guid, error) ||
        !delete_owned_tree(stable_curver_root, L"", kPikDrawAutomationProgId, error)) return error;
    return ERROR_SUCCESS;
}

} // namespace pikdraw::com_server

#endif
