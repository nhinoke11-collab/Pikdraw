#include "pikdraw/desktop/actions/action_dispatcher.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"

namespace pikdraw::desktop::actions {

bool ActionId::is_valid_action_id(std::string_view id) noexcept {
    return id.starts_with("action.") && id.length() > 7;
}

void ActionDispatcher::register_action(ActionDescriptor desc) {
    if (!desc.shortcut.empty()) {
        shortcut_map_[desc.shortcut] = desc.id;
    }
    actions_[desc.id] = std::move(desc);
}

bool ActionDispatcher::dispatch(std::string_view action_id) {
    auto it = actions_.find(std::string(action_id));
    if (it == actions_.end()) {
        return false;
    }

    const auto& desc = it->second;
    if (desc.is_enabled && !desc.is_enabled()) {
        return false;
    }

    last_dispatched_id_ = desc.id;
    history_.push_back(desc.id);

    if (desc.handler) {
        desc.handler();
    }
    return true;
}

bool ActionDispatcher::dispatch_shortcut(std::string_view shortcut) {
    auto it = shortcut_map_.find(std::string(shortcut));
    if (it == shortcut_map_.end()) {
        return false;
    }
    return dispatch(it->second);
}

bool ActionDispatcher::is_action_enabled(std::string_view action_id) const {
    auto it = actions_.find(std::string(action_id));
    if (it == actions_.end()) {
        return false;
    }
    return !it->second.is_enabled || it->second.is_enabled();
}

const ActionDescriptor* ActionDispatcher::get_action(std::string_view action_id) const {
    auto it = actions_.find(std::string(action_id));
    if (it == actions_.end()) {
        return nullptr;
    }
    return &it->second;
}

} // namespace pikdraw::desktop::actions
