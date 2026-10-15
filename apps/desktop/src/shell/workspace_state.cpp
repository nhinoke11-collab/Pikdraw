#include "pikdraw/desktop/shell/workspace_state.hpp"
#include <stdexcept>

namespace pikdraw::desktop {

WorkspaceStateManager::WorkspaceStateManager() {
    workspaces_ = {
        {
            WorkspaceType::Vector,
            "workspace.vector",
            "Vector",
            "Vector",
            WorkspaceCapability::Operational,
            "Trình biên tập đồ họa vector chuẩn xác Milestone M1"
        },
        {
            WorkspaceType::InAn,
            "workspace.inan",
            "In ấn",
            "Print Production",
            WorkspaceCapability::PlaceholderM2,
            "Tách màu CMYK/Spot và kiểm tra bình bản (Dự kiến Milestone M2)"
        },
        {
            WorkspaceType::ThiCong,
            "workspace.thicong",
            "Thi công",
            "Fabrication CAD",
            WorkspaceCapability::PlaceholderM2,
            "Xuất mã G-code, nesting và bình phôi sản xuất (Dự kiến Milestone M2)"
        },
        {
            WorkspaceType::Scene3D,
            "workspace.scene3d",
            "3D",
            "3D Scene",
            WorkspaceCapability::PlaceholderM2,
            "Mô phỏng phối cảnh và dựng hình extrusion 3D (Dự kiến Milestone M2)"
        }
    };
}

const WorkspaceInfo& WorkspaceStateManager::active_info() const noexcept {
    for (const auto& ws : workspaces_) {
        if (ws.type == active_) {
            return ws;
        }
    }
    return workspaces_.front();
}

bool WorkspaceStateManager::switch_workspace(WorkspaceType type) noexcept {
    for (const auto& ws : workspaces_) {
        if (ws.type == type) {
            active_ = type;
            return true;
        }
    }
    return false;
}

std::string_view WorkspaceStateManager::workspace_type_to_string(WorkspaceType type) noexcept {
    switch (type) {
        case WorkspaceType::Vector: return "Vector";
        case WorkspaceType::InAn: return "In ấn";
        case WorkspaceType::ThiCong: return "Thi công";
        case WorkspaceType::Scene3D: return "3D";
    }
    return "Unknown";
}

} // namespace pikdraw::desktop
