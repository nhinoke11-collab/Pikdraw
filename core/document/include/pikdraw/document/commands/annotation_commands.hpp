#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/annotations/annotation_object.hpp"
#include "pikdraw/document/annotations/annotation_layout.hpp"
#include "pikdraw/document/model/object.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pikdraw::document {

/// Helper to lazily instantiate group 'Chú thích' and the requested child layer.
/// Returns the index of the child layer in page.layers.
size_t ensure_annotation_layer(Page& page, LayerRole role);

class AddAnnotationCommand final : public DocumentCommand {
public:
    explicit AddAnnotationCommand(AnnotationObject annotation);

    [[nodiscard]] std::string_view name() const noexcept override { return "Add Annotation"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

    [[nodiscard]] ObjectId annotation_id() const noexcept { return annotation_.id; }

private:
    AnnotationObject annotation_;
    std::optional<size_t> created_layer_index_;
    std::optional<size_t> created_group_header_index_;
    bool executed_{false};
};

class UpdateAnnotationCommand final : public DocumentCommand {
public:
    UpdateAnnotationCommand(ObjectId id, AnnotationObject updated_annotation);

    [[nodiscard]] std::string_view name() const noexcept override { return "Update Annotation"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    ObjectId id_{kInvalidObjectId};
    AnnotationObject old_annotation_;
    AnnotationObject new_annotation_;
    PathObject old_path_object_;
    bool executed_{false};
};

class AddDetailViewCommand final : public DocumentCommand {
public:
    explicit AddDetailViewCommand(DetailViewObject detail_view);

    [[nodiscard]] std::string_view name() const noexcept override { return "Add Detail View"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

    [[nodiscard]] ObjectId detail_view_id() const noexcept { return detail_view_.id; }

private:
    DetailViewObject detail_view_;
    std::optional<size_t> created_layer_index_;
    std::optional<size_t> created_group_header_index_;
    bool executed_{false};
};

class UpdateDetailViewCommand final : public DocumentCommand {
public:
    UpdateDetailViewCommand(ObjectId id, DetailViewObject updated_detail_view);

    [[nodiscard]] std::string_view name() const noexcept override { return "Update Detail View"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    ObjectId id_{kInvalidObjectId};
    DetailViewObject old_detail_view_;
    DetailViewObject new_detail_view_;
    PathObject old_path_object_;
    bool executed_{false};
};

class ToggleAnnotationGroupCommand final : public DocumentCommand {
public:
    enum class ToggleKind { Visible, Locked, Printable };

    explicit ToggleAnnotationGroupCommand(ToggleKind kind);

    [[nodiscard]] std::string_view name() const noexcept override { return "Toggle Annotation Group"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    ToggleKind kind_{ToggleKind::Visible};
    bool old_val_{false};
    bool executed_{false};
};

[[nodiscard]] std::optional<AnnotationObject> get_annotation(const Document& doc, ObjectId id);
[[nodiscard]] std::optional<DetailViewObject> get_detail_view(const Document& doc, ObjectId id);

} // namespace pikdraw::document
