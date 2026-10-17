#pragma once

#include "pikdraw/document/annotations/dimension_object.hpp"
#include "pikdraw/document/annotations/dimension_layout.hpp"
#include "pikdraw/document/commands/command.hpp"

#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pikdraw::document {

class DimensionRegistry {
public:
    static DimensionRegistry& instance();
    void set(ObjectId id, DimensionObject dim);
    void remove(ObjectId id);
    [[nodiscard]] std::optional<DimensionObject> get(ObjectId id) const;
    [[nodiscard]] bool contains(ObjectId id) const;
    void clear();
    [[nodiscard]] const std::unordered_map<ObjectId, DimensionObject>& all() const;

private:
    std::unordered_map<ObjectId, DimensionObject> dimensions_;
};

[[nodiscard]] std::optional<DimensionObject> get_dimension(const Document& doc, ObjectId id);

class AddDimensionCommand : public DocumentCommand {
public:
    explicit AddDimensionCommand(DimensionObject dimension, size_t layer_idx = 0);

    [[nodiscard]] std::string_view name() const noexcept override { return "Add Dimension"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

    [[nodiscard]] const DimensionObject& dimension() const noexcept { return dimension_; }

private:
    DimensionObject dimension_;
    size_t layer_idx_{0};
    ObjectId object_id_{kInvalidObjectId};
    bool executed_{false};
};

class RemoveDimensionCommand : public DocumentCommand {
public:
    explicit RemoveDimensionCommand(ObjectId dimension_id);

    [[nodiscard]] std::string_view name() const noexcept override { return "Remove Dimension"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    ObjectId dimension_id_{kInvalidObjectId};
    std::optional<DimensionObject> saved_dimension_;
    size_t saved_layer_idx_{0};
    size_t saved_object_idx_{0};
    bool executed_{false};
};

class UpdateDimensionStyleCommand : public DocumentCommand {
public:
    UpdateDimensionStyleCommand(ObjectId dimension_id, DimensionStyle new_style);

    [[nodiscard]] std::string_view name() const noexcept override { return "Update Dimension Style"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    ObjectId dimension_id_{kInvalidObjectId};
    DimensionStyle new_style_;
    DimensionStyle old_style_;
    bool executed_{false};
};

class UpdateDimensionPlacementCommand : public DocumentCommand {
public:
    UpdateDimensionPlacementCommand(ObjectId dimension_id, geometry::Point2D new_placement);

    [[nodiscard]] std::string_view name() const noexcept override { return "Update Dimension Placement"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    ObjectId dimension_id_{kInvalidObjectId};
    geometry::Point2D new_placement_;
    geometry::Point2D old_placement_;
    bool executed_{false};
};

class RelinkDimensionCommand : public DocumentCommand {
public:
    RelinkDimensionCommand(ObjectId dimension_id, std::vector<SourceReference> new_references);

    [[nodiscard]] std::string_view name() const noexcept override { return "Relink Dimension"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    ObjectId dimension_id_{kInvalidObjectId};
    std::vector<SourceReference> new_references_;
    std::vector<SourceReference> old_references_;
    bool executed_{false};
};

class ReevaluateDimensionsCommand : public DocumentCommand {
public:
    explicit ReevaluateDimensionsCommand(ObjectId modified_source_id);

    [[nodiscard]] std::string_view name() const noexcept override { return "Reevaluate Dimensions"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    ObjectId modified_source_id_{kInvalidObjectId};
    struct StateEntry {
        ObjectId id;
        PathObject before_path;
        PathObject after_path;
    };
    std::vector<StateEntry> entries_;
    bool executed_{false};
};

} // namespace pikdraw::document
