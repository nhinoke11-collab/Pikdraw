#pragma once

#include <string>
#include <string_view>
#include <functional>
#include <unordered_map>
#include <vector>

namespace pikdraw::desktop::actions {

struct ActionDescriptor {
    std::string id;
    std::string title_vi;
    std::string title_en;
    std::string shortcut;
    std::function<void()> handler;
    std::function<bool()> is_enabled;
};

class ActionDispatcher {
public:
    ActionDispatcher() = default;

    /// Register a new action
    void register_action(ActionDescriptor desc);

    /// Dispatch action by string identifier
    bool dispatch(std::string_view action_id);

    /// Dispatch action matching keyboard shortcut (e.g. "Ctrl+Z")
    bool dispatch_shortcut(std::string_view shortcut);

    /// Check if action is currently enabled
    [[nodiscard]] bool is_action_enabled(std::string_view action_id) const;

    /// Retrieve action descriptor
    [[nodiscard]] const ActionDescriptor* get_action(std::string_view action_id) const;

    /// Total registered actions
    [[nodiscard]] size_t action_count() const noexcept { return actions_.size(); }

    /// Last dispatched action ID (for verification)
    [[nodiscard]] const std::string& last_dispatched_id() const noexcept { return last_dispatched_id_; }

    /// Full dispatch history
    [[nodiscard]] const std::vector<std::string>& dispatch_history() const noexcept { return history_; }

    /// Clear dispatch history
    void clear_history() noexcept { history_.clear(); }

private:
    std::unordered_map<std::string, ActionDescriptor> actions_;
    std::unordered_map<std::string, std::string> shortcut_map_;
    std::string last_dispatched_id_;
    std::vector<std::string> history_;
};

} // namespace pikdraw::desktop::actions
