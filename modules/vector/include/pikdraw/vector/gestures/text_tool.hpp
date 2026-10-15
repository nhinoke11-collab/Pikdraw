#pragma once
#include "pikdraw/vector/gestures/edit_target.hpp"
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
} // namespace pikdraw::vector
