#pragma once

#include "pikdraw/desktop/actions/action_id.hpp"

#include <array>
#include <cstddef>
#include <string_view>

namespace pikdraw::desktop {

enum class CutShapeToolId : int {
    Knife = 5,
    VectorEraser = 16,
    Scissors = 19,
    TrimObjects = 20,
    SmartTrim = 21,
    ShapeBuilder = 22,
    SmartFill = 23,
    RasterEraser = 24,
};

struct CutShapeToolDescriptor {
    CutShapeToolId id;
    int icon_id;
    std::string_view svg_icon_asset;
    std::string_view action_id;
    const wchar_t* label_en;
    const wchar_t* label_vi;
    const wchar_t* shortcut;
};

inline constexpr std::array<CutShapeToolDescriptor, 8> kCutShapeToolCatalog{{
    {CutShapeToolId::Knife, 5, "tool-knife.svg", actions::ActionId::kToolKnife,
     L"Knife Tool (K)", L"Dao cắt Knife (K)", L"K"},
    {CutShapeToolId::VectorEraser, 19, "tool-vector-eraser.svg", actions::ActionId::kToolVectorEraser,
     L"Vector Eraser (Shift+E)", L"Tẩy vector (Shift+E)", L"Shift+E"},
    {CutShapeToolId::Scissors, 21, "tool-scissors.svg", actions::ActionId::kToolScissors,
     L"Scissors (X)", L"Kéo Scissors (X)", L"X"},
    {CutShapeToolId::TrimObjects, 22, "tool-trim.svg", actions::ActionId::kToolTrimObjects,
     L"Trim Objects", L"Cắt bằng đối tượng", L""},
    {CutShapeToolId::SmartTrim, 23, "tool-smart-trim.svg", actions::ActionId::kToolSmartTrim,
     L"Smart Trim", L"Smart Trim", L""},
    {CutShapeToolId::ShapeBuilder, 24, "tool-shape-builder.svg", actions::ActionId::kToolShapeBuilder,
     L"Shape Builder", L"Tạo hình từ vùng", L""},
    {CutShapeToolId::SmartFill, 25, "tool-smart-fill.svg", actions::ActionId::kToolSmartFill,
     L"Smart Fill", L"Tô thông minh", L""},
    {CutShapeToolId::RasterEraser, 26, "tool-raster-eraser.svg", actions::ActionId::kToolRasterEraser,
     L"Raster Eraser", L"Tẩy ảnh bitmap", L""},
}};

consteval bool cut_shape_tool_icons_are_unique() {
    for (std::size_t left = 0; left < kCutShapeToolCatalog.size(); ++left)
        for (std::size_t right = left + 1; right < kCutShapeToolCatalog.size(); ++right)
            if (kCutShapeToolCatalog[left].icon_id == kCutShapeToolCatalog[right].icon_id)
                return false;
    return true;
}
static_assert(cut_shape_tool_icons_are_unique(),
              "Each Cut & Shape tool needs a distinct icon identity");

consteval bool cut_shape_tool_svg_assets_are_unique_and_present() {
    for (std::size_t left = 0; left < kCutShapeToolCatalog.size(); ++left) {
        if (kCutShapeToolCatalog[left].svg_icon_asset.empty()) return false;
        for (std::size_t right = left + 1; right < kCutShapeToolCatalog.size(); ++right)
            if (kCutShapeToolCatalog[left].svg_icon_asset ==
                kCutShapeToolCatalog[right].svg_icon_asset) return false;
    }
    return true;
}
static_assert(cut_shape_tool_svg_assets_are_unique_and_present(),
              "Each Cut & Shape tool needs a unique SVG source asset");

[[nodiscard]] constexpr const CutShapeToolDescriptor* cut_shape_tool_for_icon_id(
    int icon_id) noexcept {
    for (const auto& tool : kCutShapeToolCatalog)
        if (tool.icon_id == icon_id) return &tool;
    return nullptr;
}

[[nodiscard]] constexpr const CutShapeToolDescriptor* cut_shape_tool_at(
    std::size_t index) noexcept {
    return index < kCutShapeToolCatalog.size() ? &kCutShapeToolCatalog[index] : nullptr;
}

[[nodiscard]] constexpr const CutShapeToolDescriptor* cut_shape_tool_for_active_id(
    int active_id) noexcept {
    for (const auto& tool : kCutShapeToolCatalog)
        if (static_cast<int>(tool.id) == active_id) return &tool;
    return nullptr;
}

[[nodiscard]] constexpr bool is_cut_shape_tool(int active_id) noexcept {
    return cut_shape_tool_for_active_id(active_id) != nullptr;
}

} // namespace pikdraw::desktop
