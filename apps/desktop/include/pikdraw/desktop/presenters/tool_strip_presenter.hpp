#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <functional>

namespace pikdraw::desktop {

enum class ToolMode {
    Select,
    Rectangle,
    Ellipse,
    Text,
    Pan,
    Zoom
};

struct ToolItem {
    ToolMode mode;
    std::string id;
    std::string name_vi;
    std::string name_en;
    std::string shortcut;
    std::string icon_name;
};

class ToolStripPresenter {
public:
    ToolStripPresenter();

    [[nodiscard]] ToolMode active_tool() const noexcept { return active_tool_; }
    void set_active_tool(ToolMode tool) noexcept;

    [[nodiscard]] const std::vector<ToolItem>& tools() const noexcept { return tools_; }
    [[nodiscard]] const ToolItem& active_tool_item() const noexcept;

    void set_on_tool_changed(std::function<void(ToolMode)> cb) {
        on_tool_changed_ = std::move(cb);
    }

    [[nodiscard]] static std::string_view tool_mode_to_string(ToolMode mode) noexcept;

private:
    ToolMode active_tool_{ToolMode::Select};
    std::vector<ToolItem> tools_;
    std::function<void(ToolMode)> on_tool_changed_;
};

} // namespace pikdraw::desktop
