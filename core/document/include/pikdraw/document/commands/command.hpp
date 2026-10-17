#pragma once

#include "pikdraw/document/model/object.hpp"
#include <string>
#include <memory>
#include <optional>

namespace pikdraw::document {

class Document;

struct CommandResult {
    bool success{false};
    std::string error_message;
    bool is_no_change{false};

    static CommandResult ok() { return {true, "", false}; }
    static CommandResult no_change() { return {true, "", true}; }
    static CommandResult fail(std::string msg) { return {false, std::move(msg), false}; }
};

/// Abstract base class for transactional document commands
class DocumentCommand {
public:
    virtual ~DocumentCommand() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    virtual CommandResult execute(Document& doc) = 0;
    virtual CommandResult undo(Document& doc) = 0;
};

using CommandPtr = std::unique_ptr<DocumentCommand>;

/// Command: Add an object to the document
class AddObjectCommand : public DocumentCommand {
public:
    AddObjectCommand(ObjectVariant object, size_t layer_idx = 0);

    [[nodiscard]] std::string_view name() const noexcept override { return "Add Object"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

    [[nodiscard]] ObjectId object_id() const noexcept;

private:
    ObjectVariant object_;
    size_t layer_idx_{0};
    bool executed_{false};
};

/// Command: Remove an object by stable ObjectId
class RemoveObjectCommand : public DocumentCommand {
public:
    explicit RemoveObjectCommand(ObjectId id);

    [[nodiscard]] std::string_view name() const noexcept override { return "Remove Object"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    ObjectId id_{kInvalidObjectId};
    std::optional<ObjectVariant> removed_object_;
    size_t layer_idx_{0};
    size_t object_idx_{0};
};

/// Command: Transform an object
class TransformObjectCommand : public DocumentCommand {
public:
    TransformObjectCommand(ObjectId id, geometry::Transform2D delta_transform);

    [[nodiscard]] std::string_view name() const noexcept override { return "Transform Object"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    ObjectId id_{kInvalidObjectId};
    geometry::Transform2D delta_;
    geometry::Transform2D previous_transform_;
};

} // namespace pikdraw::document
