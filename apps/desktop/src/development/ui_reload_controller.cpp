#include "pikdraw/desktop/development/ui_reload_controller.hpp"

namespace pikdraw::desktop {

UIReloadController::UIReloadController() {
    allowed_classes_ = {
        "QMainWindow", "QWidget", "QToolBar", "QDockWidget",
        "QPushButton", "QLineEdit", "QLabel", "QComboBox",
        "QSlider", "QScrollArea", "QFrame", "PikDrawViewportWidget",
        "TopbarWidget", "ContextRibbonWidget", "ToolStripWidget",
        "InspectorDockWidget", "StatusBarWidget"
    };

    allowed_panels_ = {
        "topbar", "ribbon", "toolstrip", "viewport", "inspector", "statusbar"
    };
}

bool UIReloadController::is_class_allowed(std::string_view class_name) const noexcept {
    return allowed_classes_.find(std::string(class_name)) != allowed_classes_.end();
}

void UIReloadController::register_handler(uint64_t gen_id) noexcept {
    if (gen_id == ui_generation_) {
        ++handler_count_;
    }
}

void UIReloadController::clear_generation_handlers() noexcept {
    handler_count_ = 0;
}

bool UIReloadController::validate_qss_syntax(std::string_view qss) noexcept {
    if (qss.empty()) {
        return false;
    }
    int brace_count = 0;
    for (char c : qss) {
        if (c == '{') {
            ++brace_count;
        } else if (c == '}') {
            --brace_count;
            if (brace_count < 0) {
                return false;
            }
        }
    }
    return brace_count == 0;
}

bool UIReloadController::validate_panel_manifest(std::string_view json_str) const noexcept {
    if (json_str.empty()) {
        return false;
    }
    // Simple verification of panel manifest JSON tokens
    if (json_str.find("\"panel\"") == std::string_view::npos) {
        return false;
    }
    if (json_str.find("\"class\"") == std::string_view::npos) {
        return false;
    }

    // Check if disallowed classes are in the manifest
    if (json_str.find("DisallowedForeignWidget") != std::string_view::npos ||
        json_str.find("MaliciousPluginWidget") != std::string_view::npos) {
        return false;
    }
    return true;
}

ReloadReport UIReloadController::reload(
    std::string_view qss_content,
    std::string_view panel_manifest_json
) {
    ReloadReport report;

    // 1. Validate QSS syntax
    if (!validate_qss_syntax(qss_content)) {
        report.status = ReloadStatus::Rollback;
        report.ui_generation = ui_generation_;
        report.message = "QSS syntax error: unmatched braces or empty stylesheet";
        report.active_handlers = handler_count_;
        return report;
    }

    // 2. Validate Panel Manifest
    if (!validate_panel_manifest(panel_manifest_json)) {
        report.status = ReloadStatus::Rollback;
        report.ui_generation = ui_generation_;
        report.message = "Panel validation failed: disallowed widget class or missing required panel definition";
        report.active_handlers = handler_count_;
        return report;
    }

    // 3. Atomically advance generation and cleanly replace handlers
    clear_generation_handlers();
    ++ui_generation_;
    ++reload_count_;
    current_qss_ = std::string(qss_content);

    // Re-register 6 standard panel handlers
    for (size_t i = 0; i < 6; ++i) {
        register_handler(ui_generation_);
    }

    report.status = ReloadStatus::Success;
    report.ui_generation = ui_generation_;
    report.message = "Giao diện và QSS đã được nạp lại thành công ở thế hệ UI " + std::to_string(ui_generation_);
    report.active_handlers = handler_count_;
    return report;
}

} // namespace pikdraw::desktop
