#pragma once

#include <windows.h>
#include <optional>
#include <string>
#include <string_view>

namespace pikdraw::desktop {

class NativeClipboard {
public:
    [[nodiscard]] static UINT native_format_id() noexcept;
    [[nodiscard]] static UINT svg_format_id() noexcept;

    /// Writes data to Windows clipboard with retry logic.
    /// Supports:
    /// - Native snapshot (CF_CUSTOM "PikDraw.DocumentSnapshot.v1")
    /// - SVG vector data (CF_CUSTOM "image/svg+xml" and CF_UNICODETEXT)
    /// - Plain text (CF_UNICODETEXT)
    static bool write(
        HWND hwnd,
        std::string_view native_snapshot_json,
        std::string_view svg_data = {},
        std::string_view plain_text = {});

    /// Checks if the clipboard currently has PikDraw native snapshot format.
    [[nodiscard]] static bool has_native_snapshot();

    /// Checks if the clipboard has SVG vector data (either "image/svg+xml" or starts with <svg in CF_UNICODETEXT).
    [[nodiscard]] static bool has_svg();

    /// Checks if the clipboard has plain text.
    [[nodiscard]] static bool has_text();

    /// Reads native snapshot JSON from clipboard.
    [[nodiscard]] static std::optional<std::string> read_native_snapshot();

    /// Reads SVG data from clipboard.
    [[nodiscard]] static std::optional<std::string> read_svg();

    /// Reads plain text from clipboard.
    [[nodiscard]] static std::optional<std::string> read_text();
};

} // namespace pikdraw::desktop
