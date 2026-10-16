#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/typography/layout/text_layout.hpp"
#include <string>
#include <algorithm>

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
        auto opt = doc.objects().get(target_id_);
        if (!opt || !std::holds_alternative<document::TextObject>(*opt)) {
            return document::CommandResult::fail("Target text object not found");
        }

        auto text_obj = std::get<document::TextObject>(*opt);
        old_family_ = text_obj.font.family_name;
        old_size_pt_ = text_obj.font.size_pt;
        old_weight_ = text_obj.font.weight;
        old_italic_ = text_obj.font.italic;
        old_underline_ = text_obj.underline;
        old_alignment_ = text_obj.alignment;

        text_obj.font.family_name = new_family_;
        text_obj.font.size_pt = std::clamp(new_size_pt_, 6.0, 288.0);
        text_obj.font.weight = new_weight_;
        text_obj.font.italic = new_italic_;
        text_obj.underline = new_underline_;
        text_obj.alignment = new_alignment_;

        auto map = doc.objects().set(target_id_, std::move(text_obj));
        doc.apply_mutation(doc.page(), std::move(map));
        return document::CommandResult::ok();
    }

    document::CommandResult undo(document::Document& doc) override {
        auto opt = doc.objects().get(target_id_);
        if (!opt || !std::holds_alternative<document::TextObject>(*opt)) {
            return document::CommandResult::fail("Target object invalid on style undo");
        }

        auto text_obj = std::get<document::TextObject>(*opt);
        text_obj.font.family_name = old_family_;
        text_obj.font.size_pt = old_size_pt_;
        text_obj.font.weight = old_weight_;
        text_obj.font.italic = old_italic_;
        text_obj.underline = old_underline_;
        text_obj.alignment = old_alignment_;

        auto map = doc.objects().set(target_id_, std::move(text_obj));
        doc.apply_mutation(doc.page(), std::move(map));
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

    std::string old_family_;
    double old_size_pt_{12.0};
    int old_weight_{400};
    bool old_italic_{false};
    bool old_underline_{false};
    typography::TextAlignment old_alignment_{typography::TextAlignment::Left};
};

} // namespace pikdraw::vector
