#pragma once

#include "pikdraw/color/values/color_value.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pikdraw::desktop {

struct SpotPaletteSwatch {
    std::string name;
    std::string group;
    color::RGBColor alternate_rgb;
};

struct SpotPaletteLibrary {
    std::string name;
    std::filesystem::path source_path;
    std::vector<SpotPaletteSwatch> swatches;
};

[[nodiscard]] std::filesystem::path spot_palette_directory();
[[nodiscard]] SpotPaletteLibrary sample_spot_palette();
[[nodiscard]] std::vector<SpotPaletteLibrary> load_spot_palette_libraries(
    const std::filesystem::path& directory);
[[nodiscard]] std::optional<SpotPaletteLibrary> load_ase_spot_palette(
    const std::filesystem::path& path, std::string* error = nullptr);
[[nodiscard]] bool import_ase_spot_palette(const std::filesystem::path& source,
    const std::filesystem::path& directory, SpotPaletteLibrary& imported,
    std::string* error = nullptr);

} // namespace pikdraw::desktop
