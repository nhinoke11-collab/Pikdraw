#include "drawing_preferences.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pikdraw::desktop::tools {
namespace {

constexpr std::size_t kMaximumBytes = 4096;

std::filesystem::path preference_directory() {
#ifdef _WIN32
    if (const char* local = std::getenv("LOCALAPPDATA"); local && *local)
        return std::filesystem::path(local) / "PikDraw";
    if (const char* profile = std::getenv("USERPROFILE"); profile && *profile)
        return std::filesystem::path(profile) / "AppData" / "Local" / "PikDraw";
#else
    if (const char* config = std::getenv("XDG_CONFIG_HOME"); config && *config)
        return std::filesystem::path(config) / "PikDraw";
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".config" / "PikDraw";
#endif
    return std::filesystem::current_path() / ".pikdraw-user";
}

bool parse_bool(const std::map<std::string, std::string>& fields,
                const char* name, bool fallback, bool& value) {
    const auto found = fields.find(name);
    if (found == fields.end()) { value = fallback; return true; }
    if (found->second == "0") { value = false; return true; }
    if (found->second == "1") { value = true; return true; }
    return false;
}

} // namespace

std::filesystem::path drawing_preferences_path() {
    return preference_directory() / "drawing-preferences-v1.txt";
}

PreferencesLoadResult load_drawing_preferences(const std::filesystem::path& path) {
    PreferencesLoadResult result;
    std::error_code error;
    if (!std::filesystem::exists(path, error)) return result;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > kMaximumBytes) {
        result.diagnostic = "Drawing preferences exceed the 4096-byte limit";
        return result;
    }
    std::ifstream input(path, std::ios::binary);
    std::string line;
    std::map<std::string, std::string> fields;
    if (!std::getline(input, line) || line != "pikdraw-drawing-preferences-v1") {
        result.diagnostic = "Unsupported drawing preferences version";
        return result;
    }
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        const auto split = line.find('=');
        if (split == std::string::npos || split == 0 || split > 64 || line.size() > 512) {
            result.diagnostic = "Malformed drawing preferences entry";
            return result;
        }
        if (!fields.emplace(line.substr(0, split), line.substr(split + 1)).second) {
            result.diagnostic = "Duplicate drawing preferences entry";
            return result;
        }
    }
    constexpr std::array<const char*, 8> allowed{
        "profile", "snap_master", "endpoint", "source_node", "line_midpoint",
        "grid", "direction", "polar_step"};
    for (const auto& [key, value] : fields) {
        (void)value;
        if (std::none_of(allowed.begin(), allowed.end(), [&](const char* name) {
                return key == name;
            })) {
            result.diagnostic = "Unknown drawing preferences entry";
            return result;
        }
    }
    auto& value = result.value;
    if (const auto found = fields.find("profile"); found != fields.end()) {
        if (found->second != "PikDraw" && found->second != "Illustrator" &&
            found->second != "Corel" && found->second != "Custom") {
            result.diagnostic = "Unknown drawing input profile";
            return result;
        }
        value.profile = found->second;
    }
    if (!parse_bool(fields, "snap_master", true, value.snap_master) ||
        !parse_bool(fields, "endpoint", true, value.endpoint) ||
        !parse_bool(fields, "source_node", true, value.source_node) ||
        !parse_bool(fields, "line_midpoint", true, value.line_midpoint) ||
        !parse_bool(fields, "grid", true, value.grid)) {
        result.diagnostic = "Drawing preferences contain an invalid Boolean";
        return result;
    }
    if (const auto found = fields.find("direction"); found != fields.end()) {
        if (found->second == "free") value.direction = vector::precision::snap::DirectionMode::Free;
        else if (found->second == "ortho") value.direction = vector::precision::snap::DirectionMode::Ortho;
        else if (found->second == "polar") value.direction = vector::precision::snap::DirectionMode::Polar;
        else { result.diagnostic = "Unknown drawing direction mode"; return result; }
    }
    if (const auto found = fields.find("polar_step"); found != fields.end()) {
        double step = 0.0;
        const auto parsed = std::from_chars(found->second.data(),
            found->second.data() + found->second.size(), step);
        if (parsed.ec != std::errc{} || parsed.ptr !=
                found->second.data() + found->second.size() ||
            !vector::precision::snap::is_valid_preset_step(step)) {
            result.diagnostic = "Unsupported polar angle step";
            return result;
        }
        value.polar_step_degrees = step;
    }
    result.loaded = true;
    return result;
}

bool save_drawing_preferences(const std::filesystem::path& path,
                              const DrawingPreferences& value,
                              std::string* diagnostic) {
    if ((value.profile != "PikDraw" && value.profile != "Illustrator" &&
         value.profile != "Corel" && value.profile != "Custom") ||
        !vector::precision::snap::is_valid_preset_step(value.polar_step_degrees) ||
        value.direction > vector::precision::snap::DirectionMode::Polar) {
        if (diagnostic) *diagnostic = "Drawing preference values are invalid";
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        if (diagnostic) *diagnostic = "Could not create the drawing preferences directory";
        return false;
    }
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (diagnostic) *diagnostic = "Could not open the temporary preferences file";
        return false;
    }
    const char* direction = value.direction == vector::precision::snap::DirectionMode::Free
        ? "free" : value.direction == vector::precision::snap::DirectionMode::Ortho
        ? "ortho" : "polar";
    output << "pikdraw-drawing-preferences-v1\n"
           << "profile=" << value.profile << '\n'
           << "snap_master=" << value.snap_master << '\n'
           << "endpoint=" << value.endpoint << '\n'
           << "source_node=" << value.source_node << '\n'
           << "line_midpoint=" << value.line_midpoint << '\n'
           << "grid=" << value.grid << '\n'
           << "direction=" << direction << '\n'
           << "polar_step=" << value.polar_step_degrees << '\n';
    output.close();
    if (!output) {
        std::filesystem::remove(temporary, error);
        if (diagnostic) *diagnostic = "Could not finish writing drawing preferences";
        return false;
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
    std::filesystem::rename(temporary, path, error);
#endif
    if (error) {
        std::filesystem::remove(temporary, error);
        if (diagnostic) *diagnostic = "Could not atomically replace drawing preferences";
        return false;
    }
    return true;
}

bool apply_drawing_preferences(const DrawingPreferences& value,
                               vector::precision::snap::SnapConfiguration& config) {
    using namespace vector::precision::snap;
    return config.set_master_enabled(value.snap_master) &&
        config.set_option_enabled(SnapOption::Endpoint, value.endpoint) &&
        config.set_option_enabled(SnapOption::SourceNode, value.source_node) &&
        config.set_option_enabled(SnapOption::LineMidpoint, value.line_midpoint) &&
        config.set_option_enabled(SnapOption::Grid, value.grid) &&
        config.set_direction_mode(value.direction) &&
        config.set_polar_preset(value.polar_step_degrees);
}

} // namespace pikdraw::desktop::tools
