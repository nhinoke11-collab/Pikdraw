#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace pikdraw::desktop {

enum class WorkspaceType {
    Vector,
    InAn,       // Print & prepress
    ThiCong,    // CAD / CAM fabrication
    Scene3D     // 3D extrusion and visualization
};

enum class WorkspaceCapability {
    Operational,     // Fully interactive in Milestone M1
    PlaceholderM2,   // Reserved tab, displays planned M2 capability indicator
    Unsupported
};

struct WorkspaceInfo {
    WorkspaceType type;
    std::string id;
    std::string title_vi;
    std::string title_en;
    WorkspaceCapability capability;
    std::string capability_description;
};

class WorkspaceStateManager {
public:
    WorkspaceStateManager();

    [[nodiscard]] WorkspaceType active_workspace() const noexcept { return active_; }
    [[nodiscard]] const WorkspaceInfo& active_info() const noexcept;

    /// Attempts to switch workspace; returns true if switched
    bool switch_workspace(WorkspaceType type) noexcept;

    [[nodiscard]] const std::vector<WorkspaceInfo>& all_workspaces() const noexcept {
        return workspaces_;
    }

    [[nodiscard]] static std::string_view workspace_type_to_string(WorkspaceType type) noexcept;

private:
    WorkspaceType active_{WorkspaceType::Vector};
    std::vector<WorkspaceInfo> workspaces_;
};

} // namespace pikdraw::desktop
