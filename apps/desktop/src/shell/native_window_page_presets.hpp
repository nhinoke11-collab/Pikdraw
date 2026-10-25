#pragma once

#include <array>
#include <string>

namespace pikdraw::desktop {

struct PagePresetEntry {
    const wchar_t* name;
    double width_mm;
    double height_mm;
};

inline constexpr std::array<PagePresetEntry, 10> kPagePresetEntries{{
    {L"A4", 210.0, 297.0},
    {L"A3", 297.0, 420.0},
    {L"A2", 420.0, 594.0},
    {L"A1", 594.0, 841.0},
    {L"A0", 841.0, 1189.0},
    {L"Pano 2×1m", 2000.0, 1000.0},
    {L"Banner 3×1m", 3000.0, 1000.0},
    {L"Standee 0.8×1.8m", 800.0, 1800.0},
    {L"Card Visit", 90.0, 54.0},
    {L"Vuông 1080", 1080.0, 1080.0},
}};

inline constexpr int kPagePresetRowHeight = 28;
inline constexpr int kPagePresetPadding = 6;
inline constexpr int kPagePresetDropdownWidth = 240;

inline constexpr int page_preset_dropdown_height() noexcept {
    return 2 * kPagePresetPadding + static_cast<int>(kPagePresetEntries.size()) * kPagePresetRowHeight;
}

} // namespace pikdraw::desktop
