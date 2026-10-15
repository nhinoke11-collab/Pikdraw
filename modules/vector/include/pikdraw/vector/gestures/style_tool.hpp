#pragma once
#include "pikdraw/vector/gestures/edit_target.hpp"
namespace pikdraw::vector {
class SetObjectStyleCommand : public document::DocumentCommand {
  public:
    SetObjectStyleCommand(document::ObjectId id, std::optional<color::ColorValue> fill,
                          std::optional<color::ColorValue> stroke, std::optional<double> width,
                          std::optional<bool> has_fill = {}, std::optional<bool> has_stroke = {})
        : id_(id), fill_(std::move(fill)), stroke_(std::move(stroke)), width_(width), has_fill_(has_fill),
          has_stroke_(has_stroke) {}
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
    std::optional<document::ObjectVariant> before_;
};
} // namespace pikdraw::vector
