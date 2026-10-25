#include "drawing_preferences.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
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

bool indexed_field(const std::string& key, std::string_view prefix, size_t count,
                   size_t& index) {
    if (!key.starts_with(prefix)) return false;
    const auto suffix = std::string_view(key).substr(prefix.size());
    if (suffix.empty()) return false;
    size_t parsed = 0;
    const auto result = std::from_chars(suffix.data(), suffix.data() + suffix.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != suffix.data() + suffix.size() || parsed >= count)
        return false;
    index = parsed;
    return true;
}

bool parse_finite(const std::map<std::string, std::string>& fields, const char* key,
                  double fallback, double& value) {
    const auto found = fields.find(key);
    if (found == fields.end()) { value = fallback; return true; }
    const auto parsed = std::from_chars(found->second.data(),
        found->second.data() + found->second.size(), value, std::chars_format::general);
    return parsed.ec == std::errc{} && parsed.ptr == found->second.data() + found->second.size() &&
        std::isfinite(value);
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
    if (!std::getline(input, line) ||
        (line != "pikdraw-drawing-preferences-v1" &&
         line != "pikdraw-drawing-preferences-v2")) {
        result.diagnostic = "Unsupported drawing preferences version";
        return result;
    }
    const bool version_two = line == "pikdraw-drawing-preferences-v2";
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
    constexpr std::array<const char*, 18> allowed{
        "profile", "snap_master", "endpoint", "source_node", "line_midpoint",
        "grid", "direction", "polar_step", "length_unit", "grid_spacing_mm",
        "smart_guides", "smart_spacing", "snap_objects", "snap_page", "snap_guides",
        "artboard_overview", "snap_acquire_radius_dip", "snap_release_radius_dip"};
    for (const auto& [key, value] : fields) {
        (void)value;
        size_t unused_index = 0;
        const bool indexed = version_two &&
            (indexed_field(key, "snap_group_", vector::precision::snap::kSnapGroupCount, unused_index) ||
             indexed_field(key, "snap_option_", vector::precision::snap::kSnapOptionCount, unused_index));
        if (!indexed && std::none_of(allowed.begin(), allowed.end(), [&](const char* name) {
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
        !parse_bool(fields, "snap_objects", true, value.snap_objects) ||
        !parse_bool(fields, "smart_guides", true, value.smart_guides) ||
        !parse_bool(fields, "smart_spacing", true, value.smart_spacing) ||
        !parse_bool(fields, "snap_page", true, value.snap_page) ||
        !parse_bool(fields, "snap_guides", false, value.snap_guides) ||
        !parse_bool(fields, "artboard_overview", false, value.artboard_overview) ||
        !parse_bool(fields, "endpoint", true, value.endpoint) ||
        !parse_bool(fields, "source_node", true, value.source_node) ||
        !parse_bool(fields, "line_midpoint", true, value.line_midpoint) ||
        !parse_bool(fields, "grid", true, value.grid)) {
        result.diagnostic = "Drawing preferences contain an invalid Boolean";
        return result;
    }
    using namespace vector::precision::snap;
    if (version_two) {
        for (size_t index = 0; index < kSnapGroupCount; ++index) {
            const std::string key = "snap_group_" + std::to_string(index);
            if (!parse_bool(fields, key.c_str(), value.snap_groups[index],
                            value.snap_groups[index])) {
                result.diagnostic = "Drawing preferences contain an invalid snap group";
                return result;
            }
        }
        for (size_t index = 0; index < kSnapOptionCount; ++index) {
            const std::string key = "snap_option_" + std::to_string(index);
            if (!parse_bool(fields, key.c_str(), value.snap_options[index],
                            value.snap_options[index])) {
                result.diagnostic = "Drawing preferences contain an invalid snap option";
                return result;
            }
        }
        if (!parse_finite(fields, "snap_acquire_radius_dip", 8.0,
                          value.snap_acquire_radius_dip) ||
            !parse_finite(fields, "snap_release_radius_dip", 12.0,
                          value.snap_release_radius_dip)) {
            result.diagnostic = "Drawing preferences contain invalid snap radii";
            return result;
        }
        SnapConfiguration validator;
        if (!validator.set_hysteresis(value.snap_acquire_radius_dip,
                                      value.snap_release_radius_dip)) {
            result.diagnostic = "Snap radii must be 1–64 DIP with release >= acquire";
            return result;
        }
    }
    // Preserve the original v1 fields and initialize the new indexed preferences from them.
    value.snap_groups[static_cast<size_t>(SnapGroup::ObjectPoints)] = value.snap_objects;
    value.snap_options[static_cast<size_t>(SnapOption::Endpoint)] = value.endpoint;
    value.snap_options[static_cast<size_t>(SnapOption::SourceNode)] = value.source_node;
    value.snap_options[static_cast<size_t>(SnapOption::LineMidpoint)] = value.line_midpoint;
    value.snap_options[static_cast<size_t>(SnapOption::Grid)] = value.grid;
    value.snap_options[static_cast<size_t>(SnapOption::PageEdge)] = value.snap_page;
    value.snap_options[static_cast<size_t>(SnapOption::PageCenter)] = value.snap_page;
    value.snap_options[static_cast<size_t>(SnapOption::Guide)] = value.snap_guides;
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
    if (const auto found = fields.find("grid_spacing_mm"); found != fields.end()) {
        double spacing = 0.0;
        const auto parsed = std::from_chars(found->second.data(),
            found->second.data() + found->second.size(), spacing,
            std::chars_format::general);
        if (parsed.ec != std::errc{} || parsed.ptr !=
                found->second.data() + found->second.size() ||
            !is_valid_grid_spacing_mm(spacing)) {
            result.diagnostic = "Grid spacing must be finite and within supported limits";
            return result;
        }
        value.grid_spacing_mm = spacing;
    }
    if (const auto found = fields.find("length_unit"); found != fields.end()) {
        using geometry::units::LengthUnit;
        if (found->second == "mm") value.default_length_unit = LengthUnit::Millimeter;
        else if (found->second == "cm") value.default_length_unit = LengthUnit::Centimeter;
        else if (found->second == "m") value.default_length_unit = LengthUnit::Meter;
        else if (found->second == "in") value.default_length_unit = LengthUnit::Inch;
        else if (found->second == "ft") value.default_length_unit = LengthUnit::Foot;
        else if (found->second == "yd") value.default_length_unit = LengthUnit::Yard;
        else if (found->second == "pt") value.default_length_unit = LengthUnit::Point;
        else if (found->second == "pc") value.default_length_unit = LengthUnit::Pica;
        else { result.diagnostic = "Unknown default length unit"; return result; }
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
        value.direction > vector::precision::snap::DirectionMode::Polar ||
        !is_valid_grid_spacing_mm(value.grid_spacing_mm)) {
        if (diagnostic) *diagnostic = "Drawing preference values are invalid";
        return false;
    }
    vector::precision::snap::SnapConfiguration snap_validator;
    if (!snap_validator.set_hysteresis(value.snap_acquire_radius_dip,
                                       value.snap_release_radius_dip)) {
        if (diagnostic) *diagnostic = "Snap radii must be 1–64 DIP with release >= acquire";
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
    const char* length_unit = value.default_length_unit == geometry::units::LengthUnit::Millimeter ? "mm"
        : value.default_length_unit == geometry::units::LengthUnit::Centimeter ? "cm"
        : value.default_length_unit == geometry::units::LengthUnit::Meter ? "m"
        : value.default_length_unit == geometry::units::LengthUnit::Inch ? "in"
        : value.default_length_unit == geometry::units::LengthUnit::Foot ? "ft"
        : value.default_length_unit == geometry::units::LengthUnit::Yard ? "yd"
        : value.default_length_unit == geometry::units::LengthUnit::Point ? "pt"
        : value.default_length_unit == geometry::units::LengthUnit::Pica ? "pc" : nullptr;
    if (!length_unit) {
        if (diagnostic) *diagnostic = "Unsupported default length unit";
        return false;
    }
    std::array<char, 64> spacing_text{};
    const auto spacing = std::to_chars(spacing_text.data(),
        spacing_text.data() + spacing_text.size(), value.grid_spacing_mm,
        std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (spacing.ec != std::errc{}) {
        if (diagnostic) *diagnostic = "Could not format drawing grid spacing";
        return false;
    }
    output << "pikdraw-drawing-preferences-v2\n"
           << "profile=" << value.profile << '\n'
           << "snap_master=" << value.snap_master << '\n'
           << "snap_objects=" << value.snap_objects << '\n'
           << "smart_guides=" << value.smart_guides << '\n'
           << "smart_spacing=" << value.smart_spacing << '\n'
           << "snap_page=" << value.snap_page << '\n'
           << "snap_guides=" << value.snap_guides << '\n'
           << "artboard_overview=" << value.artboard_overview << '\n'
           << "endpoint=" << value.endpoint << '\n'
           << "source_node=" << value.source_node << '\n'
           << "line_midpoint=" << value.line_midpoint << '\n'
           << "grid=" << value.grid << '\n'
           << "direction=" << direction << '\n'
           << "polar_step=" << value.polar_step_degrees << '\n'
           << "length_unit=" << length_unit << '\n'
           << "grid_spacing_mm=" << std::string(spacing_text.data(), spacing.ptr) << '\n';
    auto snap_groups = value.snap_groups;
    auto snap_options = value.snap_options;
    using namespace vector::precision::snap;
    snap_groups[static_cast<size_t>(SnapGroup::ObjectPoints)] = value.snap_objects;
    snap_options[static_cast<size_t>(SnapOption::Endpoint)] = value.endpoint;
    snap_options[static_cast<size_t>(SnapOption::SourceNode)] = value.source_node;
    snap_options[static_cast<size_t>(SnapOption::LineMidpoint)] = value.line_midpoint;
    snap_options[static_cast<size_t>(SnapOption::Grid)] = value.grid;
    snap_options[static_cast<size_t>(SnapOption::PageEdge)] = value.snap_page;
    snap_options[static_cast<size_t>(SnapOption::PageCenter)] = value.snap_page;
    snap_options[static_cast<size_t>(SnapOption::Guide)] = value.snap_guides;
    for (size_t index = 0; index < snap_groups.size(); ++index)
        output << "snap_group_" << index << '=' << snap_groups[index] << '\n';
    for (size_t index = 0; index < snap_options.size(); ++index)
        output << "snap_option_" << index << '=' << snap_options[index] << '\n';
    output << "snap_acquire_radius_dip=" << value.snap_acquire_radius_dip << '\n'
           << "snap_release_radius_dip=" << value.snap_release_radius_dip << '\n';
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
    auto groups = value.snap_groups;
    auto options = value.snap_options;
    groups[static_cast<size_t>(SnapGroup::ObjectPoints)] = value.snap_objects;
    options[static_cast<size_t>(SnapOption::Endpoint)] = value.endpoint;
    options[static_cast<size_t>(SnapOption::SourceNode)] = value.source_node;
    options[static_cast<size_t>(SnapOption::LineMidpoint)] = value.line_midpoint;
    options[static_cast<size_t>(SnapOption::Grid)] = value.grid;
    options[static_cast<size_t>(SnapOption::PageEdge)] = value.snap_page;
    options[static_cast<size_t>(SnapOption::PageCenter)] = value.snap_page;
    options[static_cast<size_t>(SnapOption::Guide)] = value.snap_guides;
    auto grid = config.grid_settings();
    grid.spacing_mm = value.grid_spacing_mm;
    for (size_t index = 0; index < groups.size(); ++index) {
        if (!config.set_group_enabled(static_cast<SnapGroup>(index), groups[index])) return false;
    }
    for (size_t index = 0; index < options.size(); ++index) {
        if (!config.set_option_enabled(static_cast<SnapOption>(index), options[index])) return false;
    }
    return config.set_master_enabled(value.snap_master) &&
        config.set_smart_guides_enabled(value.smart_guides) &&
        config.set_smart_spacing_enabled(value.smart_spacing) &&
        config.set_hysteresis(value.snap_acquire_radius_dip, value.snap_release_radius_dip) &&
        config.set_grid(grid) &&
        config.set_direction_mode(value.direction) &&
        config.set_polar_preset(value.polar_step_degrees);
}

} // namespace pikdraw::desktop::tools
