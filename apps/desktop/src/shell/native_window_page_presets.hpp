#pragma once

#include <array>
#include <string>

namespace pikdraw::desktop {

struct PagePresetEntry {
    const wchar_t* name;
    const wchar_t* dim_label;
    double width_mm;
    double height_mm;
};

inline constexpr std::array<PagePresetEntry, 10> kPagePresetEntries{{
    {L"A4", L"210 × 297 mm", 210.0, 297.0},
    {L"A3", L"297 × 420 mm", 297.0, 420.0},
    {L"A2", L"420 × 594 mm", 420.0, 594.0},
    {L"A1", L"594 × 841 mm", 594.0, 841.0},
    {L"A0", L"841 × 1189 mm", 841.0, 1189.0},
    {L"Pano 2×1m", L"2000 × 1000 mm", 2000.0, 1000.0},
    {L"Banner 3×1m", L"3000 × 1000 mm", 3000.0, 1000.0},
    {L"Standee 0.8×1.8m", L"800 × 1800 mm", 800.0, 1800.0},
    {L"Card Visit", L"90 × 54 mm", 90.0, 54.0},
    {L"Vuông 1080", L"1080 × 1080 mm", 1080.0, 1080.0},
}};

inline constexpr int kPagePresetRowHeight = 28;
inline constexpr int kPagePresetPadding = 6;
inline constexpr int kPagePresetDropdownWidth = 240;

inline constexpr int page_preset_dropdown_height() noexcept {
    return 2 * kPagePresetPadding + static_cast<int>(kPagePresetEntries.size()) * kPagePresetRowHeight;
}

} // namespace pikdraw::desktop
