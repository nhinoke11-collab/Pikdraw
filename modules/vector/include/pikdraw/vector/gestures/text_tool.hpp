#pragma once
#include "pikdraw/vector/gestures/edit_target.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/geometry/primitives/validation.hpp"
#include "pikdraw/typography/layout/text_layout.hpp"
#include <string>
#include <algorithm>
#include <cmath>
#include <optional>

namespace pikdraw::vector {

class UpdateTextCommand : public document::DocumentCommand {
  public:
    UpdateTextCommand(document::ObjectId id, std::string text) : id_(id), text_(std::move(text)) {}
    UpdateTextCommand(document::ObjectId id, std::optional<std::string> text,
                      std::optional<typography::FontIdentity> font)
        : id_(id), text_(std::move(text)), font_(std::move(font)) {}
    std::string_view name() const noexcept override {
        return "Update Text and Font";
    }
    document::CommandResult execute(document::Document &doc) override;
    document::CommandResult undo(document::Document &doc) override;

  private:
    document::ObjectId id_;
    std::optional<std::string> text_;
    std::optional<typography::FontIdentity> font_;
    std::optional<document::TextObject> before_;
};

class SetTextFontCommand final : public UpdateTextCommand {
  public:
    SetTextFontCommand(document::ObjectId id, typography::FontIdentity font)
        : UpdateTextCommand(id, std::nullopt, std::move(font)) {}
};

struct TextPropertiesDelta {
    std::optional<std::string> family;
    std::optional<double> size_pt;
    std::optional<int> weight;
    std::optional<bool> italic;
    std::optional<bool> underline;
    std::optional<bool> strikethrough;
    std::optional<typography::TextAlignment> alignment;
    std::optional<double> frame_width_mm;
    std::optional<double> frame_height_mm;
    std::optional<double> line_spacing_ratio;
    std::optional<double> letter_spacing_mm;
};

inline bool valid_text_properties_delta(const TextPropertiesDelta& delta) noexcept {
    if (delta.family && (delta.family->empty() || delta.family->find('\0') != std::string::npos))
        return false;
    if (delta.size_pt && (!std::isfinite(*delta.size_pt) || *delta.size_pt < 1.0 || *delta.size_pt > 2880.0))
        return false;
    if (delta.weight && (*delta.weight < 100 || *delta.weight > 900)) return false;
    if (delta.frame_width_mm && (!std::isfinite(*delta.frame_width_mm) || *delta.frame_width_mm < 0.0 ||
                                 *delta.frame_width_mm > geometry::limits::kCanvasMaxMm))
        return false;
    if (delta.frame_height_mm && (!std::isfinite(*delta.frame_height_mm) || *delta.frame_height_mm < 0.0 ||
                                  *delta.frame_height_mm > geometry::limits::kCanvasMaxMm))
        return false;
    if (delta.line_spacing_ratio && (!std::isfinite(*delta.line_spacing_ratio) ||
                                     *delta.line_spacing_ratio < 0.25 || *delta.line_spacing_ratio > 10.0))
        return false;
    if (delta.letter_spacing_mm && (!std::isfinite(*delta.letter_spacing_mm) ||
                                    std::abs(*delta.letter_spacing_mm) > geometry::limits::kCanvasMaxMm))
        return false;
    if (delta.alignment && *delta.alignment != typography::TextAlignment::Left &&
        *delta.alignment != typography::TextAlignment::Center &&
        *delta.alignment != typography::TextAlignment::Right &&
        *delta.alignment != typography::TextAlignment::Justify)
        return false;
    return true;
}

/// Granular command that only mutates specified text properties, preserving existing attributes
class UpdateTextPropertiesCommand : public document::DocumentCommand {
public:
    UpdateTextPropertiesCommand(document::ObjectId target_id, TextPropertiesDelta delta)
        : target_id_(target_id), delta_(std::move(delta)) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Update Text Properties"; }

    document::CommandResult execute(document::Document& doc) override {
        if (!valid_text_properties_delta(delta_))
            return document::CommandResult::fail("Invalid text property value");
        const auto opt = doc.objects().get(target_id_);
        if (!opt || !std::holds_alternative<document::TextObject>(*opt) ||
            !editable_target(doc.page(), doc.objects(), target_id_))
            return document::CommandResult::fail("Text target is missing, hidden or locked");
        auto text = std::get<document::TextObject>(*opt);
        before_ = text;

        if (delta_.family && !delta_.family->empty() && text.font.family_name != *delta_.family) {
            text.font.family_name = *delta_.family;
            text.font.postscript_name.clear();
        }
        if (delta_.size_pt) text.font.size_pt = *delta_.size_pt;
        if (delta_.weight) text.font.weight = static_cast<uint16_t>(*delta_.weight);
        if (delta_.italic) text.font.italic = *delta_.italic;
        if (delta_.underline) text.underline = *delta_.underline;
        if (delta_.strikethrough) text.strikethrough = *delta_.strikethrough;
        if (delta_.alignment) text.alignment = *delta_.alignment;
        if (delta_.frame_width_mm) text.frame_width_mm = *delta_.frame_width_mm;
        if (delta_.frame_height_mm) text.frame_height_mm = *delta_.frame_height_mm;
        if (delta_.line_spacing_ratio) text.line_spacing_ratio = *delta_.line_spacing_ratio;
        if (delta_.letter_spacing_mm) text.letter_spacing_mm = *delta_.letter_spacing_mm;

        if (text.font == before_->font && text.underline == before_->underline &&
            text.strikethrough == before_->strikethrough && text.alignment == before_->alignment &&
            text.frame_width_mm == before_->frame_width_mm &&
            text.frame_height_mm == before_->frame_height_mm &&
            text.line_spacing_ratio == before_->line_spacing_ratio &&
            text.letter_spacing_mm == before_->letter_spacing_mm)
            return document::CommandResult::no_change();

        doc.apply_mutation(doc.page(), doc.objects().set(target_id_, std::move(text)));
        return document::CommandResult::ok();
    }

    document::CommandResult undo(document::Document& doc) override {
        if (!before_) return document::CommandResult::fail("Missing text property before state");
        doc.apply_mutation(doc.page(), doc.objects().set(target_id_, *before_));
        return document::CommandResult::ok();
    }

private:
    document::ObjectId target_id_;
    TextPropertiesDelta delta_;
    std::optional<document::TextObject> before_;
};

/// Command to change the text content of a TextObject (with full undo/redo support)
class ChangeTextContentCommand : public document::DocumentCommand {
public:
    ChangeTextContentCommand(document::ObjectId target_id, std::string new_text, std::string label = "Change Text")
        : target_id_(target_id), new_text_(std::move(new_text)), label_(std::move(label)) {}

    [[nodiscard]] std::string_view name() const noexcept override { return label_; }

    document::CommandResult execute(document::Document& doc) override {
        const auto opt = doc.objects().get(target_id_);
        if (!opt || !std::holds_alternative<document::TextObject>(*opt) ||
            !editable_target(doc.page(), doc.objects(), target_id_))
            return document::CommandResult::fail("Text target is missing, hidden or locked");
        auto text = std::get<document::TextObject>(*opt);
        before_ = text;
        if (text.text == new_text_) return document::CommandResult::no_change();
        text.text = new_text_;
        doc.apply_mutation(doc.page(), doc.objects().set(target_id_, std::move(text)));
        return document::CommandResult::ok();
    }

    document::CommandResult undo(document::Document& doc) override {
        if (!before_) return document::CommandResult::fail("Missing text content before state");
        doc.apply_mutation(doc.page(), doc.objects().set(target_id_, *before_));
        return document::CommandResult::ok();
    }

private:
    document::ObjectId target_id_;
    std::string new_text_;
    std::string label_;
    std::optional<document::TextObject> before_;
};

} // namespace pikdraw::vector
