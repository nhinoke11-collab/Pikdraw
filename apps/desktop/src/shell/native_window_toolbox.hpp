#pragma once

#include <cstdint>
#include <vector>

namespace pikdraw::desktop {

enum class ToolboxSlotId : int {
    Pick = 0,
    Node = 1,
    QuickShapes = 2,
    PathDrawing = 3,
    Pan = 4,
    Text = 5,
    Knife = 6,
    Blur = 7,
    DropShadow = 8,
    Contour = 9,
    Gradient = 10,
    Dimension = 11,
    Count = 12
};

struct ToolboxSlotInfo {
    ToolboxSlotId id;
    int tool_or_effect_id;
    int group_id;
    int icon_id;
    const wchar_t* name_en;
    const wchar_t* name_vi;
    const wchar_t* shortcut;
};

ToolboxSlotInfo get_toolbox_slot_info(ToolboxSlotId id) noexcept;

} // namespace pikdraw::desktop
