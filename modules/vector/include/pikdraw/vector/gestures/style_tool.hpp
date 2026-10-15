#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/color/values/color_value.hpp"
#include <optional>

namespace pikdraw::vector {

/// Command modifying fill, stroke, and stroke width of a target object
class SetObjectStyleCommand : public document::DocumentCommand {
public:
    SetObjectStyleCommand(
        document::ObjectId target_id,
        std::optional<color::ColorValue> new_fill,
        std::optional<color::ColorValue> new_stroke,
        std::optional<double> new_stroke_width_mm)
        : target_id_(target_id),
          new_fill_(std::move(new_fill)),
          new_stroke_(std::move(new_stroke)),
          new_stroke_width_(new_stroke_width_mm) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Set Style"; }

    document::CommandResult execute(document::Document& doc) override {
        auto opt = doc.objects().get(target_id_);
        if (!opt) {
            return document::CommandResult::fail("Object not found for style update");
        }

        document::ObjectVariant updated = *opt;
        std::visit([&](auto& obj) {
            using T = std::decay_t<decltype(obj)>;
            if constexpr (std::is_same_v<T, document::PathObject>) {
                old_fill_ = obj.fill;
                old_stroke_ = obj.stroke;
                old_stroke_width_ = obj.stroke_width_mm;

                if (new_fill_) { obj.fill = *new_fill_; obj.has_fill = true; }
                if (new_stroke_) { obj.stroke = *new_stroke_; obj.has_stroke = true; }
                if (new_stroke_width_) { obj.stroke_width_mm = *new_stroke_width_; }
            } else if constexpr (std::is_same_v<T, document::TextObject>) {
                old_fill_ = obj.fill;
                if (new_fill_) { obj.fill = *new_fill_; }
            }
        }, updated);

        auto map = doc.objects().set(target_id_, std::move(updated));
        doc.apply_mutation(doc.page(), std::move(map));
        return document::CommandResult::ok();
    }

    document::CommandResult undo(document::Document& doc) override {
        auto opt = doc.objects().get(target_id_);
        if (!opt) {
            return document::CommandResult::fail("Object not found for style undo");
        }

        document::ObjectVariant updated = *opt;
        std::visit([&](auto& obj) {
            using T = std::decay_t<decltype(obj)>;
            if constexpr (std::is_same_v<T, document::PathObject>) {
                if (old_fill_) obj.fill = *old_fill_;
                if (old_stroke_) obj.stroke = *old_stroke_;
                if (old_stroke_width_) obj.stroke_width_mm = *old_stroke_width_;
            } else if constexpr (std::is_same_v<T, document::TextObject>) {
                if (old_fill_) obj.fill = *old_fill_;
            }
        }, updated);

        auto map = doc.objects().set(target_id_, std::move(updated));
        doc.apply_mutation(doc.page(), std::move(map));
        return document::CommandResult::ok();
    }

private:
    document::ObjectId target_id_;
    std::optional<color::ColorValue> new_fill_;
    std::optional<color::ColorValue> new_stroke_;
    std::optional<double> new_stroke_width_;

    std::optional<color::ColorValue> old_fill_;
    std::optional<color::ColorValue> old_stroke_;
    std::optional<double> old_stroke_width_;
};

} // namespace pikdraw::vector
