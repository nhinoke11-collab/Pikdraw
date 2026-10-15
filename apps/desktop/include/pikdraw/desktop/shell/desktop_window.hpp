#pragma once

#include "pikdraw/desktop/shell/shell_geometry.hpp"
#include "pikdraw/desktop/shell/workspace_state.hpp"
#include "pikdraw/desktop/shell/status_bar_model.hpp"
#include "pikdraw/desktop/actions/action_dispatcher.hpp"
#include "pikdraw/desktop/presenters/tool_strip_presenter.hpp"
#include "pikdraw/desktop/presenters/ribbon_presenter.hpp"
#include "pikdraw/desktop/presenters/inspector_presenter.hpp"
#include "pikdraw/desktop/themes/theme_tokens.hpp"
#include "pikdraw/desktop/development/ui_reload_controller.hpp"

#include <string>
#include <memory>

namespace pikdraw::desktop {

/// Desktop shell window managing layout, action routing, and development UI reload
class DesktopWindow {
public:
    DesktopWindow(double width = 1920.0, double height = 1080.0, double dpi_scale = 1.0);

    void set_window_size(double width, double height) noexcept;
    void set_dpi_scale(double scale) noexcept;

    [[nodiscard]] double window_width() const noexcept { return width_; }
    [[nodiscard]] double window_height() const noexcept { return height_; }
    [[nodiscard]] double dpi_scale() const noexcept { return dpi_scale_; }

    [[nodiscard]] ShellGeometry::CanvasArea canvas_area() const noexcept;

    [[nodiscard]] WorkspaceStateManager& workspace_state() noexcept { return workspace_state_; }
    [[nodiscard]] const WorkspaceStateManager& workspace_state() const noexcept { return workspace_state_; }

    [[nodiscard]] actions::ActionDispatcher& action_dispatcher() noexcept { return dispatcher_; }
    [[nodiscard]] const actions::ActionDispatcher& action_dispatcher() const noexcept { return dispatcher_; }

    [[nodiscard]] ToolStripPresenter& tool_strip() noexcept { return tool_strip_; }
    [[nodiscard]] const ToolStripPresenter& tool_strip() const noexcept { return tool_strip_; }

    [[nodiscard]] RibbonPresenter& ribbon() noexcept { return ribbon_; }
    [[nodiscard]] const RibbonPresenter& ribbon() const noexcept { return ribbon_; }

    [[nodiscard]] InspectorPresenter& inspector() noexcept { return inspector_; }
    [[nodiscard]] const InspectorPresenter& inspector() const noexcept { return inspector_; }

    [[nodiscard]] StatusBarModel& status_bar() noexcept { return status_bar_; }
    [[nodiscard]] const StatusBarModel& status_bar() const noexcept { return status_bar_; }

    [[nodiscard]] ThemeManager& theme_manager() noexcept { return theme_manager_; }
    [[nodiscard]] const ThemeManager& theme_manager() const noexcept { return theme_manager_; }

    [[nodiscard]] UIReloadController& reload_controller() noexcept { return reload_controller_; }
    [[nodiscard]] const UIReloadController& reload_controller() const noexcept { return reload_controller_; }

    /// Re-evaluates QSS and forms dynamically without closing the window
    ReloadReport trigger_f5_reload(std::string_view qss_override = {}, std::string_view manifest_override = {});

    /// Setup default actions per Contract C7
    void setup_default_actions();

private:
    double width_{1920.0};
    double height_{1080.0};
    double dpi_scale_{1.0};

    WorkspaceStateManager workspace_state_;
    actions::ActionDispatcher dispatcher_;
    ToolStripPresenter tool_strip_;
    RibbonPresenter ribbon_;
    InspectorPresenter inspector_;
    StatusBarModel status_bar_;
    ThemeManager theme_manager_;
    UIReloadController reload_controller_;
};

} // namespace pikdraw::desktop
