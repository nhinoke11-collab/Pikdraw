#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>
#include <functional>
#include <cstdint>

namespace pikdraw::desktop {

enum class ReloadStatus {
    Success,
    Rollback,
    SkippedInRelease
};

struct ReloadReport {
    ReloadStatus status{ReloadStatus::Rollback};
    uint64_t ui_generation{0};
    std::string message;
    size_t active_handlers{0};
};

/// Controller managing dynamic F5 UI reload with allowlist validation and rollback
class UIReloadController {
public:
    UIReloadController();

    /// Trigger reload of QSS stylesheet and panel forms
    ReloadReport reload(std::string_view qss_content, std::string_view panel_manifest_json);

    /// Check if a widget class name is on the security allowlist
    [[nodiscard]] bool is_class_allowed(std::string_view class_name) const noexcept;

    /// Current UI generation (increments strictly on successful reload)
    [[nodiscard]] uint64_t current_generation() const noexcept { return ui_generation_; }

    /// Total successful reloads
    [[nodiscard]] uint64_t reload_count() const noexcept { return reload_count_; }

    /// Active registered event handlers
    [[nodiscard]] size_t active_handler_count() const noexcept { return handler_count_; }

    /// Register an event handler tied to a specific UI generation
    void register_handler(uint64_t gen_id) noexcept;

    /// Unregister handlers on reload
    void clear_generation_handlers() noexcept;

    /// Validate QSS string syntax (balanced braces, valid tokens)
    [[nodiscard]] static bool validate_qss_syntax(std::string_view qss) noexcept;

    /// Validate panel manifest JSON structure
    [[nodiscard]] bool validate_panel_manifest(std::string_view json_str) const noexcept;

private:
    uint64_t ui_generation_{1};
    uint64_t reload_count_{0};
    size_t handler_count_{0};
    std::unordered_set<std::string> allowed_classes_;
    std::unordered_set<std::string> allowed_panels_;
    std::string current_qss_;
};

} // namespace pikdraw::desktop
