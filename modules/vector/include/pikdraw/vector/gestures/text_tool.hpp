#pragma once
#include "pikdraw/vector/gestures/edit_target.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/typography/layout/text_layout.hpp"
#include <string>
#include <algorithm>
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
/// Command for updating text typography style with full undo/redo transaction support
class UpdateTextStyleCommand : public document::DocumentCommand {
public:
    UpdateTextStyleCommand(
        document::ObjectId target_id,
        std::string font_family,
        double font_size_pt,
        int weight,
        bool italic,
        bool underline,
        typography::TextAlignment alignment
    ) : target_id_(target_id),
        new_family_(std::move(font_family)),
        new_size_pt_(font_size_pt),
        new_weight_(weight),
        new_italic_(italic),
        new_underline_(underline),
        new_alignment_(alignment) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Update Text Style"; }

    document::CommandResult execute(document::Document& doc) override {
        const auto opt = doc.objects().get(target_id_);
        if (!opt || !std::holds_alternative<document::TextObject>(*opt) ||
            !editable_target(doc.page(), doc.objects(), target_id_))
            return document::CommandResult::fail("Text target is missing, hidden or locked");
        auto text = std::get<document::TextObject>(*opt);
        before_ = text;
        if (text.font.family_name != new_family_) {
            text.font.family_name = new_family_;
            text.font.postscript_name = new_family_ + "-Regular";
        }
        text.font.size_pt = new_size_pt_;
        text.font.weight = static_cast<uint16_t>(new_weight_);
        text.font.italic = new_italic_;
        text.underline = new_underline_;
        text.alignment = new_alignment_;
        if (text.font == before_->font && text.underline == before_->underline &&
            text.alignment == before_->alignment) return document::CommandResult::no_change();
        doc.apply_mutation(doc.page(), doc.objects().set(target_id_, std::move(text)));
        return document::CommandResult::ok();
    }
    document::CommandResult undo(document::Document& doc) override {
        if (!before_) return document::CommandResult::fail("Missing text style before state");
        doc.apply_mutation(doc.page(), doc.objects().set(target_id_, *before_));
        return document::CommandResult::ok();
    }

private:
    document::ObjectId target_id_;
    std::string new_family_;
    double new_size_pt_{12.0};
    int new_weight_{400};
    bool new_italic_{false};
    bool new_underline_{false};
    typography::TextAlignment new_alignment_{typography::TextAlignment::Left};

    std::optional<document::TextObject> before_;
};

} // namespace pikdraw::vector
