#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/model/object.hpp"
#include "pikdraw/geometry/operations/offset.hpp"
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pikdraw::document {

struct ContourFeature {
    uint64_t feature_id{0};
    ObjectId target_id{kInvalidObjectId};
    geometry::ContourDirection direction{geometry::ContourDirection::Outside};
    double spacing_mm{1.0};
    int steps{1};
    geometry::JoinType join_type{geometry::JoinType::Miter};
    double miter_limit{4.0};
    double arc_tolerance_mm{0.05};
    color::ColorValue outline_color{color::RGBColor{0.0, 0.0, 0.0, 1.0}};
    color::ColorValue fill_color{color::RGBColor{0.0, 0.0, 0.0, 1.0}};
    bool has_outline{true};
    bool has_fill{false};
    double stroke_width_mm{0.2};
    std::vector<geometry::Path2D> derived_loops;
};

/// Serialization helpers for contour features inside PathObject::contour_spec
[[nodiscard]] std::string serialize_contour_feature(const ContourFeature& feature);
[[nodiscard]] std::optional<ContourFeature> deserialize_contour_feature(std::string_view spec);

/// Evaluates contour geometry for a PathObject with given contour parameters
[[nodiscard]] geometry::ContourResult evaluate_contour(const PathObject& object, const ContourFeature& feature);

/// Snapshot-aware contour query from PathObject or Document
[[nodiscard]] std::optional<ContourFeature> get_object_contour(const PathObject& obj);
[[nodiscard]] std::optional<ContourFeature> get_object_contour(const Document& doc, ObjectId id);

/// Document-level attachment registry for live contour features (fallback / global query)
[[nodiscard]] std::optional<ContourFeature> get_attached_contour(ObjectId id);
void set_attached_contour(ObjectId id, ContourFeature feature);
void remove_attached_contour(ObjectId id);
void clear_all_attached_contours();

/// Command to apply or update a parametric contour on a PathObject
class ApplyContourCommand final : public DocumentCommand {
public:
    ApplyContourCommand(ObjectId target_id, ContourFeature feature) noexcept;

    [[nodiscard]] std::string_view name() const noexcept override {
        return "Apply Contour";
    }

    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

    [[nodiscard]] const ContourFeature& feature() const noexcept { return feature_; }

private:
    ObjectId target_id_{kInvalidObjectId};
    ContourFeature feature_;
    std::optional<ContourFeature> previous_feature_;
    bool executed_{false};
};

/// Command to break contour apart into independent PathObjects
class BreakContourApartCommand final : public DocumentCommand {
public:
    explicit BreakContourApartCommand(ObjectId target_id) noexcept;

    [[nodiscard]] std::string_view name() const noexcept override {
        return "Break Contour Apart";
    }

    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

    [[nodiscard]] const std::vector<ObjectId>& created_object_ids() const noexcept {
        return created_ids_;
    }

private:
    ObjectId target_id_{kInvalidObjectId};
    std::optional<ContourFeature> previous_feature_;
    std::vector<PathObject> created_objects_;
    std::vector<ObjectId> created_ids_;
    size_t layer_idx_{0};
    size_t insert_idx_{0};
    bool executed_{false};
};

/// Command to remove a live contour from a PathObject
class RemoveContourCommand final : public DocumentCommand {
public:
    explicit RemoveContourCommand(ObjectId target_id) noexcept;

    [[nodiscard]] std::string_view name() const noexcept override {
        return "Remove Contour";
    }

    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    ObjectId target_id_{kInvalidObjectId};
    std::optional<ContourFeature> previous_feature_;
    bool executed_{false};
};

} // namespace pikdraw::document
