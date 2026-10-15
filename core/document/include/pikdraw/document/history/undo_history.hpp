#pragma once

#include "pikdraw/document/commands/command.hpp"
#include <vector>
#include <memory>
#include <cstddef>

namespace pikdraw::document {

/// Undo/Redo stack manager with configurable memory/step budget
class UndoHistory {
public:
    explicit UndoHistory(size_t max_steps = 100) noexcept
        : max_steps_(max_steps) {}

    [[nodiscard]] bool can_undo() const noexcept {
        return !undo_stack_.empty();
    }

    [[nodiscard]] bool can_redo() const noexcept {
        return !redo_stack_.empty();
    }

    [[nodiscard]] size_t undo_count() const noexcept {
        return undo_stack_.size();
    }

    [[nodiscard]] size_t redo_count() const noexcept {
        return redo_stack_.size();
    }

    void push_undo(CommandPtr cmd) {
        if (!cmd) return;
        if (undo_stack_.size() >= max_steps_) {
            undo_stack_.erase(undo_stack_.begin());
        }
        undo_stack_.push_back(std::move(cmd));
        redo_stack_.clear(); // New user action clears redo branch
    }

    [[nodiscard]] CommandPtr pop_undo() {
        if (undo_stack_.empty()) return nullptr;
        CommandPtr cmd = std::move(undo_stack_.back());
        undo_stack_.pop_back();
        return cmd;
    }

    void push_redo(CommandPtr cmd) {
        if (!cmd) return;
        redo_stack_.push_back(std::move(cmd));
    }

    [[nodiscard]] CommandPtr pop_redo() {
        if (redo_stack_.empty()) return nullptr;
        CommandPtr cmd = std::move(redo_stack_.back());
        redo_stack_.pop_back();
        return cmd;
    }

    void clear() noexcept {
        undo_stack_.clear();
        redo_stack_.clear();
    }

private:
    size_t max_steps_{100};
    std::vector<CommandPtr> undo_stack_;
    std::vector<CommandPtr> redo_stack_;
};

} // namespace pikdraw::document
