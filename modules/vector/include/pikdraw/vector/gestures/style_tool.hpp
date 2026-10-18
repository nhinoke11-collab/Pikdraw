#pragma once
#include "pikdraw/vector/gestures/edit_target.hpp"
namespace pikdraw::vector {
struct StylePatch {
    std::optional<color::ColorValue> fill, stroke;
    std::optional<double> width_mm;
    std::optional<bool> has_fill, has_stroke;
    std::optional<bool> fill_overprint, stroke_overprint;
};
// One atomic command and one undo unit. Color-only edits preserve each object's overprint;
// the explicit overprint patch fields change only the requested flag.
document::CommandResult apply_object_styles(document::Document &doc,
                                            const std::vector<document::ObjectId> &ids,
                                            const StylePatch &patch);

class SetObjectStyleCommand : public document::DocumentCommand {
  public:
    SetObjectStyleCommand(document::ObjectId id, std::optional<color::ColorValue> fill,
                          std::optional<color::ColorValue> stroke, std::optional<double> width,
                          std::optional<bool> has_fill = {}, std::optional<bool> has_stroke = {},
                          std::optional<bool> fill_overprint = {},
                          std::optional<bool> stroke_overprint = {})
        : id_(id), fill_(std::move(fill)), stroke_(std::move(stroke)), width_(width), has_fill_(has_fill),
          has_stroke_(has_stroke), fill_overprint_(fill_overprint), stroke_overprint_(stroke_overprint) {}
    std::string_view name() const noexcept override {
        return "Set Style";
    }
    document::CommandResult execute(document::Document &doc) override;
    document::CommandResult undo(document::Document &doc) override;

  private:
    document::ObjectId id_;
    std::optional<color::ColorValue> fill_, stroke_;
    std::optional<double> width_;
    std::optional<bool> has_fill_, has_stroke_;
    std::optional<bool> fill_overprint_, stroke_overprint_;
    std::optional<document::ObjectVariant> before_;
};
} // namespace pikdraw::vector
