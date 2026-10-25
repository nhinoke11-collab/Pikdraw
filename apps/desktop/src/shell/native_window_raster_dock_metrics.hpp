#pragma once

#include <cstdint>

namespace pikdraw::desktop::raster_dock_metrics {

inline constexpr int kInspectorDockWidth = 280;
inline constexpr int kInspectorPanelInset = 14;
inline constexpr int kInspectorPanelWidth = kInspectorDockWidth - 2 * kInspectorPanelInset;
inline constexpr int kRasterAdjustmentTabCount = 6;
inline constexpr int kRasterAdjustmentTabGap = 2;

[[nodiscard]] inline constexpr std::uint8_t raster_adjustment_tab_from_x(
    int x, int panel_left) noexcept {
    constexpr int tab_width =
        (kInspectorPanelWidth - kRasterAdjustmentTabGap *
         (kRasterAdjustmentTabCount - 1)) / kRasterAdjustmentTabCount;
    for (int tab = 0; tab < kRasterAdjustmentTabCount; ++tab) {
        const int left = panel_left + tab * (tab_width + kRasterAdjustmentTabGap);
        const int right = tab == kRasterAdjustmentTabCount - 1
            ? panel_left + kInspectorPanelWidth
            : left + tab_width;
        if (x < right) return static_cast<std::uint8_t>(tab + 1);
    }
    return static_cast<std::uint8_t>(kRasterAdjustmentTabCount);
}

inline constexpr int kInspectorContentTop = 138;
inline constexpr int kHistogramTopOffset = 146;
inline constexpr int kRasterAdjustmentTabsTopOffset = 120;
inline constexpr int kRasterAdjustmentTabsHeight = 22;
inline constexpr int kLightFieldsTopOffset = 150;
inline constexpr int kLightFieldHeight = 26;
inline constexpr int kLightFieldRowSpacing = 30;
inline constexpr int kLightFieldCount = 5;
inline constexpr int kLightFieldRows = 3;
inline constexpr int kLightFieldsHeight =
    (kLightFieldRows - 1) * kLightFieldRowSpacing + kLightFieldHeight;
inline constexpr int kColorFieldsTopOffset = 150;
inline constexpr int kColorFieldHeight = 26;
inline constexpr int kColorFieldRowSpacing = 30;
inline constexpr int kColorFieldCount = 4;
inline constexpr int kColorFieldRows = 2;
inline constexpr int kColorFieldsHeight =
    (kColorFieldRows - 1) * kColorFieldRowSpacing + kColorFieldHeight;
inline constexpr int kLevelsChannelButtonsTopOffset = 140;
inline constexpr int kLevelsChannelButtonsHeight = 24;
inline constexpr int kLevelsFieldsTopOffset = 170;
inline constexpr int kLevelsFieldHeight = 26;
inline constexpr int kLevelsFieldRowSpacing = 30;
inline constexpr int kLevelsFieldCount = 5;
inline constexpr int kLevelsFieldRows = 3;
inline constexpr int kLevelsFieldsHeight =
    (kLevelsFieldRows - 1) * kLevelsFieldRowSpacing + kLevelsFieldHeight;
inline constexpr int kWhiteBalanceFieldsTopOffset = 260;
inline constexpr int kWhiteBalanceFieldHeight = 26;
inline constexpr int kRasterAdvancedActionsTopOffset = 140;
inline constexpr int kRasterAdvancedActionHeight = 26;
inline constexpr int kRasterAdvancedActionGap = 8;

inline constexpr int kColorBalanceRangesTopOffset = 140;
inline constexpr int kColorBalanceRangeHeight = 24;
inline constexpr int kColorBalanceFieldsTopOffset = 168;
inline constexpr int kColorBalanceFieldHeight = 26;
inline constexpr int kColorBalanceFieldRowSpacing = 30;
inline constexpr int kColorBalanceFieldCount = 3;
inline constexpr int kColorBalanceFieldsHeight =
    (kColorBalanceFieldCount - 1) * kColorBalanceFieldRowSpacing +
    kColorBalanceFieldHeight;
inline constexpr int kColorBalancePreserveTopOffset = 258;
inline constexpr int kColorBalancePreserveHeight = 26;

}  // namespace pikdraw::desktop::raster_dock_metrics
