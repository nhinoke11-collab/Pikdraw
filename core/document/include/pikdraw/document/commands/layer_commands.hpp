#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/document/model/object.hpp"
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace pikdraw::document {

/// Command: Add a new layer to the active page
class AddLayerCommand final : public DocumentCommand {
public:
    explicit AddLayerCommand(std::string name, std::optional<size_t> insert_index = std::nullopt);

    [[nodiscard]] std::string_view name() const noexcept override { return "Add Layer"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

    [[nodiscard]] size_t created_layer_index() const noexcept { return actual_index_; }

private:
    std::string name_;
    std::optional<size_t> insert_index_;
    size_t actual_index_{0};
    bool executed_{false};
};

/// Command: Remove a layer from the active page
class RemoveLayerCommand final : public DocumentCommand {
public:
    explicit RemoveLayerCommand(size_t layer_idx);

    [[nodiscard]] std::string_view name() const noexcept override { return "Remove Layer"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    size_t layer_idx_{0};
    std::optional<Layer> removed_layer_;
    std::vector<std::pair<ObjectId, ObjectVariant>> removed_objects_;
    bool executed_{false};
};

/// Command: Rename an existing layer
class RenameLayerCommand final : public DocumentCommand {
public:
    RenameLayerCommand(size_t layer_idx, std::string new_name);

    [[nodiscard]] std::string_view name() const noexcept override { return "Rename Layer"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    size_t layer_idx_{0};
    std::string new_name_;
    std::string old_name_;
    bool executed_{false};
};

/// Command: Toggle or set layer visibility
class SetLayerVisibilityCommand final : public DocumentCommand {
public:
    SetLayerVisibilityCommand(size_t layer_idx, bool visible);

    [[nodiscard]] std::string_view name() const noexcept override { return "Set Layer Visibility"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    size_t layer_idx_{0};
    bool visible_{true};
    bool old_visible_{true};
    bool executed_{false};
};

/// Command: Toggle or set layer lock status
class SetLayerLockedCommand final : public DocumentCommand {
public:
    SetLayerLockedCommand(size_t layer_idx, bool locked);

    [[nodiscard]] std::string_view name() const noexcept override { return "Set Layer Locked"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    size_t layer_idx_{0};
    bool locked_{false};
    bool old_locked_{false};
    bool executed_{false};
};

/// Command: Reorder a layer in the page's layer stack
class ReorderLayerCommand final : public DocumentCommand {
public:
    ReorderLayerCommand(size_t from_idx, size_t to_idx);

    [[nodiscard]] std::string_view name() const noexcept override { return "Reorder Layer"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    size_t from_idx_{0};
    size_t to_idx_{0};
    bool executed_{false};
};

/// Command: Move an object from its current layer to a target layer
class MoveObjectToLayerCommand final : public DocumentCommand {
public:
    MoveObjectToLayerCommand(ObjectId id, size_t target_layer_idx, std::optional<size_t> insert_at = std::nullopt);

    [[nodiscard]] std::string_view name() const noexcept override { return "Move Object to Layer"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    ObjectId id_{kInvalidObjectId};
    size_t target_layer_idx_{0};
    std::optional<size_t> insert_at_;
    size_t source_layer_idx_{0};
    size_t source_position_{0};
    size_t actual_insert_pos_{0};
    bool executed_{false};
};

/// Command: Reorder an object inside its layer (Z-Order)
class ReorderObjectInLayerCommand final : public DocumentCommand {
public:
    enum class Step {
        BringToFront,
        SendToBack,
        BringForward,
        SendBackward,
        ToAbsoluteIndex
    };

    ReorderObjectInLayerCommand(ObjectId id, Step step, size_t absolute_index = 0);

    [[nodiscard]] std::string_view name() const noexcept override { return "Reorder Object"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    ObjectId id_{kInvalidObjectId};
    Step step_{Step::BringForward};
    size_t absolute_index_{0};
    size_t layer_idx_{0};
    size_t old_index_{0};
    size_t new_index_{0};
    bool executed_{false};
};

/// Command: Set object visibility directly
class SetObjectVisibilityCommand final : public DocumentCommand {
public:
    SetObjectVisibilityCommand(ObjectId id, bool visible);

    [[nodiscard]] std::string_view name() const noexcept override { return "Set Object Visibility"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    ObjectId id_{kInvalidObjectId};
    bool visible_{true};
    bool old_visible_{true};
    bool executed_{false};
};

/// Command: Set object lock directly
class SetObjectLockedCommand final : public DocumentCommand {
public:
    SetObjectLockedCommand(ObjectId id, bool locked);

    [[nodiscard]] std::string_view name() const noexcept override { return "Set Object Locked"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    ObjectId id_{kInvalidObjectId};
    bool locked_{false};
    bool old_locked_{false};
    bool executed_{false};
};

/// Command: Rename an object
class RenameObjectCommand final : public DocumentCommand {
public:
    RenameObjectCommand(ObjectId id, std::string new_name);

    [[nodiscard]] std::string_view name() const noexcept override { return "Rename Object"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    ObjectId id_{kInvalidObjectId};
    std::string new_name_;
    std::string old_name_;
    bool executed_{false};
};

} // namespace pikdraw::document
