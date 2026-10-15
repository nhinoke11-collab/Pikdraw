#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/document.hpp"
#include <string>

namespace pikdraw::vector {

/// Command for updating text object content while preserving fonts and stable ObjectId
class UpdateTextCommand : public document::DocumentCommand {
public:
    UpdateTextCommand(document::ObjectId target_id, std::string new_text)
        : target_id_(target_id), new_text_(std::move(new_text)) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Update Text"; }

    document::CommandResult execute(document::Document& doc) override {
        auto opt = doc.objects().get(target_id_);
        if (!opt) {
            return document::CommandResult::fail("Target text object not found");
        }
        if (!std::holds_alternative<document::TextObject>(*opt)) {
            return document::CommandResult::fail("Target object is not a text object");
        }

        auto text_obj = std::get<document::TextObject>(*opt);
        old_text_ = text_obj.text;
        text_obj.text = new_text_;

        auto map = doc.objects().set(target_id_, std::move(text_obj));
        doc.apply_mutation(doc.page(), std::move(map));
        return document::CommandResult::ok();
    }

    document::CommandResult undo(document::Document& doc) override {
        auto opt = doc.objects().get(target_id_);
        if (!opt || !std::holds_alternative<document::TextObject>(*opt)) {
            return document::CommandResult::fail("Target object invalid on text undo");
        }

        auto text_obj = std::get<document::TextObject>(*opt);
        text_obj.text = old_text_;

        auto map = doc.objects().set(target_id_, std::move(text_obj));
        doc.apply_mutation(doc.page(), std::move(map));
        return document::CommandResult::ok();
    }

private:
    document::ObjectId target_id_;
    std::string new_text_;
    std::string old_text_;
};

} // namespace pikdraw::vector
