#include "pikdraw/desktop/presenters/tool_strip_presenter.hpp"

namespace pikdraw::desktop {

ToolStripPresenter::ToolStripPresenter() {
    tools_ = {
        {ToolMode::Select, "tool.select", "Chọn", "Select", "V", "cursor_arrow"},
        {ToolMode::Rectangle, "tool.rectangle", "Hình chữ nhật", "Rectangle", "R", "square"},
        {ToolMode::Ellipse, "tool.ellipse", "Hình elip", "Ellipse", "O", "circle"},
        {ToolMode::Text, "tool.text", "Văn bản", "Text", "T", "type"},
        {ToolMode::Pan, "tool.pan", "Di chuyển khung vẽ", "Pan", "H", "hand"},
        {ToolMode::Zoom, "tool.zoom", "Thu phóng", "Zoom", "Z", "search"}
    };
}

void ToolStripPresenter::set_active_tool(ToolMode tool) noexcept {
    if (active_tool_ != tool) {
        active_tool_ = tool;
        if (on_tool_changed_) {
            on_tool_changed_(active_tool_);
        }
    }
}

const ToolItem& ToolStripPresenter::active_tool_item() const noexcept {
    for (const auto& item : tools_) {
        if (item.mode == active_tool_) {
            return item;
        }
    }
    return tools_.front();
}

std::string_view ToolStripPresenter::tool_mode_to_string(ToolMode mode) noexcept {
    switch (mode) {
        case ToolMode::Select: return "Select";
        case ToolMode::Rectangle: return "Rectangle";
        case ToolMode::Ellipse: return "Ellipse";
        case ToolMode::Text: return "Text";
        case ToolMode::Pan: return "Pan";
        case ToolMode::Zoom: return "Zoom";
    }
    return "Unknown";
}

} // namespace pikdraw::desktop
