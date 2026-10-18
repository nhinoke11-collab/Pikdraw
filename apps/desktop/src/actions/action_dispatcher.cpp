#include "pikdraw/desktop/actions/action_dispatcher.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"

namespace pikdraw::desktop::actions {

bool ActionId::is_valid_action_id(std::string_view id) noexcept {
    return id.starts_with("action.") && id.length() > 7;
}

void ActionDispatcher::register_action(ActionDescriptor desc) {
    const std::string id = desc.id;
    if (std::find(action_order_.begin(), action_order_.end(), id) == action_order_.end()) {
        action_order_.push_back(id);
    }
    if (default_shortcuts_.find(id) == default_shortcuts_.end()) {
        default_shortcuts_[id] = desc.shortcut;
    }
    if (!desc.shortcut.empty()) {
        shortcut_map_[desc.shortcut] = id;
    }
    actions_[id] = std::move(desc);
}

bool ActionDispatcher::set_action_shortcut(std::string_view action_id, std::string_view new_shortcut) {
    auto it = actions_.find(std::string(action_id));
    if (it == actions_.end()) {
        return false;
    }

    // Unmap old shortcut
    if (!it->second.shortcut.empty()) {
        shortcut_map_.erase(it->second.shortcut);
    }

    const std::string new_sc(new_shortcut);
    if (!new_sc.empty()) {
        // Resolve conflicting action if shortcut was already assigned
        auto conflict_it = shortcut_map_.find(new_sc);
        if (conflict_it != shortcut_map_.end() && conflict_it->second != it->second.id) {
            auto old_action_it = actions_.find(conflict_it->second);
            if (old_action_it != actions_.end()) {
                old_action_it->second.shortcut.clear();
            }
        }
        shortcut_map_[new_sc] = it->second.id;
    }

    it->second.shortcut = new_sc;
    return true;
}

bool ActionDispatcher::clear_action_shortcut(std::string_view action_id) {
    return set_action_shortcut(action_id, "");
}

void ActionDispatcher::reset_to_default_shortcuts() {
    shortcut_map_.clear();
    for (auto& [id, desc] : actions_) {
        auto def_it = default_shortcuts_.find(id);
        const std::string def_sc = (def_it != default_shortcuts_.end()) ? def_it->second : "";
        desc.shortcut = def_sc;
        if (!def_sc.empty()) {
            shortcut_map_[def_sc] = id;
        }
    }
}

std::string ActionDispatcher::get_action_shortcut(std::string_view action_id) const {
    auto it = actions_.find(std::string(action_id));
    return (it != actions_.end()) ? it->second.shortcut : "";
}

std::string ActionDispatcher::get_default_shortcut(std::string_view action_id) const {
    auto it = default_shortcuts_.find(std::string(action_id));
    return (it != default_shortcuts_.end()) ? it->second : "";
}

std::string ActionDispatcher::find_action_for_shortcut(std::string_view shortcut) const {
    auto it = shortcut_map_.find(std::string(shortcut));
    return (it != shortcut_map_.end()) ? it->second : "";
}

std::vector<const ActionDescriptor*> ActionDispatcher::all_actions() const {
    std::vector<const ActionDescriptor*> result;
    result.reserve(action_order_.size());
    for (const auto& id : action_order_) {
        auto it = actions_.find(id);
        if (it != actions_.end()) {
            result.push_back(&it->second);
        }
    }
    return result;
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
