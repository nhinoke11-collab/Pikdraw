#pragma once

#include <memory>
#include <string>
#include "pikdraw/document/types.hpp"
#include "pikdraw/document/object.hpp"

namespace pikdraw::document {

class Document;

class DocumentCommand {
public:
    virtual ~DocumentCommand() = default;
    virtual void execute(Document& doc) = 0;
    virtual void undo(Document& doc) = 0;
    [[nodiscard]] virtual std::string description() const = 0;
};

class InsertObjectCommand : public DocumentCommand {
public:
    explicit InsertObjectCommand(std::shared_ptr<DocumentObject> obj);
    void execute(Document& doc) override;
    void undo(Document& doc) override;
    [[nodiscard]] std::string description() const override { return "InsertObject"; }
private:
    std::shared_ptr<DocumentObject> object_;
};

class RemoveObjectCommand : public DocumentCommand {
public:
    explicit RemoveObjectCommand(ObjectId id);
    void execute(Document& doc) override;
    void undo(Document& doc) override;
    [[nodiscard]] std::string description() const override { return "RemoveObject"; }
private:
    ObjectId id_;
    std::shared_ptr<DocumentObject> saved_object_;
};

class TransformObjectCommand : public DocumentCommand {
public:
    TransformObjectCommand(ObjectId id, pikdraw::geometry::Transform2D delta_transform);
    void execute(Document& doc) override;
    void undo(Document& doc) override;
    [[nodiscard]] std::string description() const override { return "TransformObject"; }
private:
    ObjectId id_;
    pikdraw::geometry::Transform2D delta_transform_;
    pikdraw::geometry::Transform2D old_transform_;
};

} // namespace pikdraw::document
