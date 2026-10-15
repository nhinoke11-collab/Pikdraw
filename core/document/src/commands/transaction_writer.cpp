#include "pikdraw/document/commands/transaction_writer.hpp"
#include "pikdraw/document/document.hpp"
#include <stdexcept>

namespace pikdraw::document {

// --- CompositeCommand ---

CommandResult CompositeCommand::execute(Document& doc) {
    executed_count_ = 0;
    for (size_t i = 0; i < commands_.size(); ++i) {
        auto res = commands_[i]->execute(doc);
        if (!res.success) {
            // Rollback previously executed commands in reverse
            for (size_t j = executed_count_; j > 0; --j) {
                commands_[j - 1]->undo(doc);
            }
            executed_count_ = 0;
            return CommandResult::fail("Transaction step failed: " + res.error_message);
        }
        ++executed_count_;
    }
    return CommandResult::ok();
}

CommandResult CompositeCommand::undo(Document& doc) {
    for (size_t i = executed_count_; i > 0; --i) {
        auto res = commands_[i - 1]->undo(doc);
        if (!res.success) {
            return CommandResult::fail("Transaction undo step failed: " + res.error_message);
        }
    }
    executed_count_ = 0;
    return CommandResult::ok();
}

// --- TransactionWriter ---

void TransactionWriter::begin_transaction(std::string transaction_name) {
    doc_.require_idle_writer();
    if (active_transaction_) throw std::logic_error("A transaction is already active");
    active_transaction_ = std::make_unique<CompositeCommand>(std::move(transaction_name));
}

CommandResult TransactionWriter::stage_command(CommandPtr cmd) {
    doc_.require_idle_writer();
    if (!active_transaction_) {
        return CommandResult::fail("No active transaction to stage command");
    }
    if (!cmd) {
        return CommandResult::fail("Cannot stage null command");
    }
    active_transaction_->add_command(std::move(cmd));
    return CommandResult::ok();
}

CommandResult TransactionWriter::commit_transaction() {
    doc_.require_idle_writer();
    if (!active_transaction_) {
        return CommandResult::fail("No active transaction to commit");
    }
    auto tx = std::move(active_transaction_);
    active_transaction_ = nullptr;
    if (tx->command_count() == 0) {
        return CommandResult::ok();
    }
    return doc_.execute_command(std::move(tx));
}

void TransactionWriter::rollback_transaction() {
    doc_.require_writer();
    active_transaction_ = nullptr;
}

} // namespace pikdraw::document
