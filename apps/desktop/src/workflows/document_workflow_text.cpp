#include "pikdraw/desktop/workflows/document_workflow.hpp"
#include "pikdraw/desktop/tools/canvas_text_editor.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/commands/transaction_writer.hpp"
#include "pikdraw/vector/gestures/text_tool.hpp"
#include "pikdraw/vector/gestures/text_operations.hpp"
#include "pikdraw/typography/layout/case_conversion.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pikdraw::desktop {
namespace {
bool changed(const document::TextObject& a, const document::TextObject& b) {
    return a.text != b.text || a.font != b.font || a.alignment != b.alignment ||
           a.underline != b.underline || a.strikethrough != b.strikethrough ||
           a.frame_width_mm != b.frame_width_mm ||
           a.line_spacing_ratio != b.line_spacing_ratio ||
           a.letter_spacing_mm != b.letter_spacing_mm;
}
document::SnapshotPtr candidate_snapshot(document::SnapshotPtr base,
    const document::TextObject& text, bool existing, document::ObjectId parent) {
    auto page = base->page();
    auto objects = base->objects();
    if (existing) {
        const auto old = objects.get(text.id);
        if (!old || !std::holds_alternative<document::TextObject>(*old) ||
            !tools::editable_text_parent(*base, text.id)) return {};
    } else {
        if (objects.contains(text.id)) return {};
        if (parent != document::kInvalidObjectId) {
            const auto old = objects.get(parent);
            if (!old || !std::holds_alternative<document::GroupObject>(*old) ||
                !tools::editable_text_parent(*base, parent)) return {};
            auto group = std::get<document::GroupObject>(*old);
            group.children_ids.push_back(text.id);
            objects = objects.set(parent, std::move(group));
        } else {
            auto layer = std::find_if(page.layers.begin(), page.layers.end(),
                [](const auto& value) { return value.visible && !value.locked; });
            if (layer == page.layers.end()) return {};
            layer->object_ids.push_back(text.id);
        }
    }
    objects = objects.set(text.id, text);
    auto pages = base->pages();
    for (auto& item : pages) if (item.id == base->active_page_id()) {
        item.layout = std::move(page);
        item.objects = std::move(objects);
    }
    return std::make_shared<document::DocumentSnapshot>(base->epoch(), base->revision(),
        std::move(pages), base->active_page_id(), base->document_id());
}
class CommitTextEdit final : public document::DocumentCommand {
public:
    CommitTextEdit(document::SnapshotPtr base, document::SnapshotPtr after)
        : base_(std::move(base)), after_(std::move(after)) {}
    std::string_view name() const noexcept override { return "Edit Text"; }
    document::CommandResult execute(document::Document& doc) override {
        const auto current = doc.current_snapshot();
        if (!executed_ && (current->document_id() != base_->document_id() ||
            current->epoch() != base_->epoch() || current->revision() != base_->revision() ||
            current->active_page_id() != base_->active_page_id()))
            return document::CommandResult::fail("Text draft is stale");
        doc.apply_mutation(after_->page(), after_->objects());
        executed_ = true;
        return document::CommandResult::ok();
    }
    document::CommandResult undo(document::Document& doc) override {
        doc.apply_mutation(base_->page(), base_->objects());
        return document::CommandResult::ok();
    }
private:
    document::SnapshotPtr base_, after_;
    bool executed_{};
};
class SetTextLayout final : public document::DocumentCommand {
public:
    SetTextLayout(document::SnapshotPtr before, document::SnapshotPtr after)
        : before_(std::move(before)), after_(std::move(after)) {}
    std::string_view name() const noexcept override { return "Set Text Layout"; }
    document::CommandResult execute(document::Document& doc) override {
        const auto current = doc.current_snapshot();
        if (!executed_ && (current->document_id() != before_->document_id() ||
            current->epoch() != before_->epoch() || current->revision() != before_->revision() ||
            current->active_page_id() != before_->active_page_id()))
            return document::CommandResult::fail("Text layout target is stale");
        doc.apply_mutation(after_->page(), after_->objects());
        executed_ = true;
        return document::CommandResult::ok();
    }
    document::CommandResult undo(document::Document& doc) override {
        doc.apply_mutation(before_->page(), before_->objects());
        return document::CommandResult::ok();
    }
private:
    document::SnapshotPtr before_, after_;
    bool executed_{};
};

class UpdateTextObjectCommand final : public document::DocumentCommand {
public:
    explicit UpdateTextObjectCommand(document::TextObject text) : after_(std::move(text)) {}
    std::string_view name() const noexcept override { return "Update Text"; }
    document::CommandResult execute(document::Document& doc) override {
        auto opt = doc.objects().get(after_.id);
        if (!opt || !std::holds_alternative<document::TextObject>(*opt))
            return document::CommandResult::fail("Text target not found");
        before_ = std::get<document::TextObject>(*opt);
        doc.apply_mutation(doc.page(), doc.objects().set(after_.id, after_));
        return document::CommandResult::ok();
    }
    document::CommandResult undo(document::Document& doc) override {
        if (!before_) return document::CommandResult::fail("No before state");
        doc.apply_mutation(doc.page(), doc.objects().set(before_->id, *before_));
        return document::CommandResult::ok();
    }
private:
    document::TextObject after_;
    std::optional<document::TextObject> before_;
};

}

bool DocumentWorkflow::preview_text_edit(const document::TextObject& text,
    document::SnapshotPtr base, bool existing, document::ObjectId parent) {
    if (!base || base != root_.document().current_snapshot()) {
        clear_text_preview();
        return false;
    }
    try {
        auto preview = candidate_snapshot(base, text, existing, parent);
        if (!preview) return false;
        text_preview_ = std::move(preview);
        text_preview_base_ = std::move(base);
        return true;
    } catch (const std::exception&) { return false; }
}
bool DocumentWorkflow::commit_text_edit(const document::TextObject& text,
    document::SnapshotPtr base, bool existing, document::ObjectId parent) {
    clear_text_preview();
    if (!base || base != root_.document().current_snapshot()) return false;
    try {
        if (existing) {
            const auto old = base->objects().get(text.id);
            if (old && std::holds_alternative<document::TextObject>(*old) &&
                !changed(std::get<document::TextObject>(*old), text)) return true;
        } else if (text.text.empty()) return true;
        auto after = candidate_snapshot(base, text, existing, parent);
        if (!after) return false;
        const auto result = root_.document().execute_command(
            std::make_unique<CommitTextEdit>(std::move(base), std::move(after)));
        sync_status_bar();
        return result.success;
    } catch (const std::exception&) { return false; }
}
bool DocumentWorkflow::style_selected_text(const vector::TextPropertiesDelta& delta) {
    return update_selected_text_properties(delta);
}
bool DocumentWorkflow::set_selected_text_font(const vector::TextPropertiesDelta& delta) {
    return update_selected_text_properties(delta);
}
bool DocumentWorkflow::set_selected_text_layout(std::optional<double> frame_width_mm,
    std::optional<double> line_spacing_ratio) {
    if (!frame_width_mm && !line_spacing_ratio) return false;
    if ((frame_width_mm && (!std::isfinite(*frame_width_mm) || *frame_width_mm < 0.0 ||
                            *frame_width_mm > geometry::limits::kCanvasMaxMm)) ||
        (line_spacing_ratio && (!std::isfinite(*line_spacing_ratio) ||
                                *line_spacing_ratio < 0.25 || *line_spacing_ratio > 10.0)))
        return false;
    const auto before = root_.document().current_snapshot();
    auto after = before;
    bool changed_any = false;
    const auto& ids = root_.vector_controller().selection().selected_ids();
    if (ids.empty()) return false;
    for (const auto id : ids) {
        const auto object = after->objects().get(id);
        if (!object || !std::holds_alternative<document::TextObject>(*object) ||
            !tools::editable_text_parent(*after, id)) return false;
        auto text = std::get<document::TextObject>(*object);
        if (frame_width_mm) text.frame_width_mm = *frame_width_mm;
        if (line_spacing_ratio) text.line_spacing_ratio = *line_spacing_ratio;
        const auto old = std::get<document::TextObject>(*object);
        if (old.frame_width_mm == text.frame_width_mm &&
            old.line_spacing_ratio == text.line_spacing_ratio) continue;
        after = candidate_snapshot(after, text, true, document::kInvalidObjectId);
        if (!after) return false;
        changed_any = true;
    }
    if (!changed_any) return true;
    const auto result = root_.document().execute_command(
        std::make_unique<SetTextLayout>(before, std::move(after)));
    sync_status_bar();
    return result.success;
}

bool DocumentWorkflow::update_selected_text_properties(const vector::TextPropertiesDelta& delta) {
    auto command = std::make_unique<document::CompositeCommand>("Update Text Properties");
    const auto base = root_.document().current_snapshot();
    const auto& ids = root_.vector_controller().selection().selected_ids();
    if (ids.empty() || !vector::valid_text_properties_delta(delta)) return false;
    for (auto id : ids) {
        const auto object = base->objects().get(id);
        if (!object || !std::holds_alternative<document::TextObject>(*object) ||
            !tools::editable_text_parent(*base, id)) return false;
        command->add_command(std::make_unique<vector::UpdateTextPropertiesCommand>(id, delta));
    }
    if (command->command_count() == 0) return false;
    const auto result = root_.document().execute_command(std::move(command));
    sync_status_bar();
    return result.success;
}


bool DocumentWorkflow::change_case_selected_text(typography::ChangeCaseMode mode) {
    auto command = std::make_unique<document::CompositeCommand>("Change Case");
    const auto base = root_.document().current_snapshot();
    const auto& ids = root_.vector_controller().selection().selected_ids();
    if (ids.empty()) return false;
    for (auto id : ids) {
        const auto object = base->objects().get(id);
        if (!object || !std::holds_alternative<document::TextObject>(*object) ||
            !tools::editable_text_parent(*base, id)) return false;
        const auto& text = std::get<document::TextObject>(*object);
        std::string converted = typography::convert_case_utf8(text.text, mode);
        command->add_command(std::make_unique<vector::ChangeTextContentCommand>(id, std::move(converted), "Change Case"));
    }
    if (command->command_count() == 0) return false;
    const auto result = root_.document().execute_command(std::move(command));
    sync_status_bar();
    return result.success;
}

bool DocumentWorkflow::link_selected_text_frames() {
    const auto& ids = root_.vector_controller().selection().selected_ids();
    if (ids.size() != 2) return false;

    const auto base = root_.document().current_snapshot();
    auto opt0 = base->objects().get(ids[0]);
    auto opt1 = base->objects().get(ids[1]);
    if (!opt0 || !opt1 ||
        !std::holds_alternative<document::TextObject>(*opt0) ||
        !std::holds_alternative<document::TextObject>(*opt1)) {
        return false;
    }

    auto cmd = std::make_unique<vector::LinkTextFramesCommand>(ids[0], ids[1]);
    const auto result = root_.document().execute_command(std::move(cmd));
    sync_status_bar();
    return result.success;
}

bool DocumentWorkflow::unlink_selected_text_frame() {
    const auto& ids = root_.vector_controller().selection().selected_ids();
    if (ids.empty()) return false;

    const auto base = root_.document().current_snapshot();
    auto composite = std::make_unique<document::CompositeCommand>("Unlink Text Frames");
    for (auto id : ids) {
        auto opt = base->objects().get(id);
        if (opt && std::holds_alternative<document::TextObject>(*opt)) {
            const auto& t = std::get<document::TextObject>(*opt);
            if (t.is_linked()) {
                composite->add_command(std::make_unique<vector::UnlinkTextFramesCommand>(id));
            }
        }
    }

    if (composite->command_count() == 0) return false;
    const auto result = root_.document().execute_command(std::move(composite));
    sync_status_bar();
    return result.success;
}

bool DocumentWorkflow::combine_selected_text_objects() {
    const auto& ids = root_.vector_controller().selection().selected_ids();
    if (ids.size() < 2) return false;

    const auto base = root_.document().current_snapshot();
    std::vector<document::ObjectId> text_ids;
    for (auto id : ids) {
        auto opt = base->objects().get(id);
        if (opt && std::holds_alternative<document::TextObject>(*opt)) {
            text_ids.push_back(id);
        }
    }
    if (text_ids.size() < 2) return false;

    document::ObjectId created_id = document::kInvalidObjectId;
    auto cmd = std::make_unique<vector::CombineTextObjectsCommand>(text_ids, active_layer_index(), &created_id);
    const auto result = root_.document().execute_command(std::move(cmd));
    if (result.success && created_id != document::kInvalidObjectId) {
        root_.vector_controller().selection().select(created_id);
    }
    sync_status_bar();
    return result.success;
}

bool DocumentWorkflow::split_selected_text_lines() {
    const auto& ids = root_.vector_controller().selection().selected_ids();
    if (ids.empty()) return false;

    const auto base = root_.document().current_snapshot();
    auto opt = base->objects().get(ids.front());
    if (!opt || !std::holds_alternative<document::TextObject>(*opt)) return false;

    std::vector<document::ObjectId> created_ids;
    auto cmd = std::make_unique<vector::SplitTextLinesCommand>(ids.front(), active_layer_index(), &created_ids);
    const auto result = root_.document().execute_command(std::move(cmd));
    if (result.success && !created_ids.empty()) {
        auto& sel = root_.vector_controller().selection();
        sel.clear();
        for (auto cid : created_ids) {
            sel.select(cid, vector::SelectionMode::Add);
        }
    }
    sync_status_bar();
    return result.success;
}

bool DocumentWorkflow::attach_selected_text_to_path() {
    const auto& ids = root_.vector_controller().selection().selected_ids();
    if (ids.size() != 2) return false;

    const auto base = root_.document().current_snapshot();
    auto opt0 = base->objects().get(ids[0]);
    auto opt1 = base->objects().get(ids[1]);
    if (!opt0 || !opt1) return false;

    document::ObjectId text_id = document::kInvalidObjectId;
    document::ObjectId path_id = document::kInvalidObjectId;

    if (std::holds_alternative<document::TextObject>(*opt0) && std::holds_alternative<document::PathObject>(*opt1)) {
        text_id = ids[0];
        path_id = ids[1];
    } else if (std::holds_alternative<document::PathObject>(*opt0) && std::holds_alternative<document::TextObject>(*opt1)) {
        text_id = ids[1];
        path_id = ids[0];
    } else {
        return false;
    }

    auto cmd = std::make_unique<vector::AttachTextToPathCommand>(text_id, path_id);
    const auto result = root_.document().execute_command(std::move(cmd));
    if (result.success) {
        root_.vector_controller().selection().select(text_id);
    }
    sync_status_bar();
    return result.success;
}

bool DocumentWorkflow::detach_selected_text_from_path() {
    const auto& ids = root_.vector_controller().selection().selected_ids();
    if (ids.empty()) return false;

    const auto base = root_.document().current_snapshot();
    auto composite = std::make_unique<document::CompositeCommand>("Straighten Text");
    for (auto id : ids) {
        auto opt = base->objects().get(id);
        if (opt && std::holds_alternative<document::TextObject>(*opt)) {
            const auto& t = std::get<document::TextObject>(*opt);
            if (t.is_text_on_path()) {
                composite->add_command(std::make_unique<vector::DetachTextFromPathCommand>(id));
            }
        }
    }

    if (composite->command_count() == 0) return false;
    const auto result = root_.document().execute_command(std::move(composite));
    sync_status_bar();
    return result.success;
}

bool DocumentWorkflow::set_selected_text_path_offset(double offset_mm) {
    const auto base = root_.document().current_snapshot();
    const auto& ids = root_.vector_controller().selection().selected_ids();
    if (ids.empty()) return false;

    auto composite = std::make_unique<document::CompositeCommand>("Set Text on Path Offset");
    for (auto id : ids) {
        auto opt = base->objects().get(id);
        if (opt && std::holds_alternative<document::TextObject>(*opt)) {
            auto t = std::get<document::TextObject>(*opt);
            if (t.is_text_on_path()) {
                t.path_offset_mm = offset_mm;
                composite->add_command(std::make_unique<UpdateTextObjectCommand>(t));
            }
        }
    }

    if (composite->command_count() == 0) return false;
    const auto result = root_.document().execute_command(std::move(composite));
    sync_status_bar();
    return result.success;
}

bool DocumentWorkflow::toggle_selected_text_path_reverse() {
    const auto base = root_.document().current_snapshot();
    const auto& ids = root_.vector_controller().selection().selected_ids();
    if (ids.empty()) return false;

    auto composite = std::make_unique<document::CompositeCommand>("Reverse Text on Path");
    for (auto id : ids) {
        auto opt = base->objects().get(id);
        if (opt && std::holds_alternative<document::TextObject>(*opt)) {
            auto t = std::get<document::TextObject>(*opt);
            if (t.is_text_on_path()) {
                t.path_reversed = !t.path_reversed;
                composite->add_command(std::make_unique<UpdateTextObjectCommand>(t));
            }
        }
    }

    if (composite->command_count() == 0) return false;
    const auto result = root_.document().execute_command(std::move(composite));
    sync_status_bar();
    return result.success;
}

bool DocumentWorkflow::set_selected_text_path_baseline_offset(double offset_mm) {
    const auto base = root_.document().current_snapshot();
    const auto& ids = root_.vector_controller().selection().selected_ids();
    if (ids.empty()) return false;

    auto composite = std::make_unique<document::CompositeCommand>("Set Text Baseline Offset");
    for (auto id : ids) {
        auto opt = base->objects().get(id);
        if (opt && std::holds_alternative<document::TextObject>(*opt)) {
            auto t = std::get<document::TextObject>(*opt);
            if (t.is_text_on_path()) {
                t.path_baseline_offset_mm = offset_mm;
                composite->add_command(std::make_unique<UpdateTextObjectCommand>(t));
            }
        }
    }

    if (composite->command_count() == 0) return false;
    const auto result = root_.document().execute_command(std::move(composite));
    sync_status_bar();
    return result.success;
}

} // namespace pikdraw::desktop
