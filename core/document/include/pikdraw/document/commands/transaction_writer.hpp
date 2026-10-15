#pragma once

#include "pikdraw/document/commands/command.hpp"
#include <string>
#include <vector>
#include <memory>

namespace pikdraw::document {

class Document;

/// Composite command representing an atomic multi-step transaction
class CompositeCommand : public DocumentCommand {
public:
    explicit CompositeCommand(std::string name) : name_(std::move(name)) {}

    void add_command(CommandPtr cmd) {
        if (cmd) commands_.push_back(std::move(cmd));
    }

    [[nodiscard]] std::string_view name() const noexcept override {
        return name_;
    }

    [[nodiscard]] size_t command_count() const noexcept {
        return commands_.size();
    }

    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    std::string name_;
    std::vector<CommandPtr> commands_;
    size_t executed_count_{0};
};

/// TransactionWriter handles grouping multiple operations into atomic units
class TransactionWriter {
public:
    explicit TransactionWriter(Document& doc) noexcept : doc_(doc) {}

    void begin_transaction(std::string transaction_name);
    CommandResult commit_transaction();
    void rollback_transaction();

    [[nodiscard]] bool in_transaction() const noexcept {
        return active_transaction_ != nullptr;
    }

    CommandResult stage_command(CommandPtr cmd);

private:
    Document& doc_;
    std::unique_ptr<CompositeCommand> active_transaction_{nullptr};
};

} // namespace pikdraw::document
