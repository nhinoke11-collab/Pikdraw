#include "pikdraw/desktop/workflows/document_workflow.hpp"
#include "pikdraw/desktop/tools/canvas_text_editor.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/commands/transaction_writer.hpp"
#include "pikdraw/vector/gestures/text_tool.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pikdraw::desktop {
namespace {
bool changed(const document::TextObject& a, const document::TextObject& b) {
    return a.text != b.text || a.font != b.font || a.alignment != b.alignment ||
           a.underline != b.underline || a.frame_width_mm != b.frame_width_mm ||
           a.line_spacing_ratio != b.line_spacing_ratio;
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
class SetTextFont final : public document::DocumentCommand {
public:
    SetTextFont(document::SnapshotPtr before, document::SnapshotPtr after)
        : before_(std::move(before)), after_(std::move(after)) {}
    std::string_view name() const noexcept override { return "Choose Text Font"; }
    document::CommandResult execute(document::Document& doc) override {
        const auto current = doc.current_snapshot();
        if (!executed_ && (current->document_id() != before_->document_id() ||
            current->epoch() != before_->epoch() || current->revision() != before_->revision() ||
            current->active_page_id() != before_->active_page_id()))
            return document::CommandResult::fail("Text font target is stale");
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
bool DocumentWorkflow::style_selected_text(const std::string& family, double size, bool bold,
    bool italic, bool underline, typography::TextAlignment alignment) {
    auto command = std::make_unique<document::CompositeCommand>("Format Text");
    const auto base = root_.document().current_snapshot();
    for (auto id : root_.vector_controller().selection().selected_ids()) {
        const auto object = base->objects().get(id);
        if (!object || !std::holds_alternative<document::TextObject>(*object)) continue;
        if (!tools::editable_text_parent(*base, id)) return false;
        const auto& text = std::get<document::TextObject>(*object);
        command->add_command(std::make_unique<vector::UpdateTextStyleCommand>(id,
            family.empty() ? text.font.family_name : family, size, bold ? 700 : 400,
            italic, underline, alignment));
    }
    if (command->command_count() == 0) return false;
    const auto result = root_.document().execute_command(std::move(command));
    sync_status_bar();
    return result.success;
}
bool DocumentWorkflow::set_selected_text_font(const std::string& family, double size_pt,
    bool bold, bool italic) {
    if (family.empty() || !std::isfinite(size_pt) || size_pt < 1.0 || size_pt > 2880.0)
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
        if (text.font.family_name != family) {
            text.font.family_name = family;
            text.font.postscript_name = family + "-Regular";
        }
        text.font.size_pt = size_pt;
        text.font.weight = static_cast<uint16_t>(bold ? 700 : 400);
        text.font.italic = italic;
        const auto& old = std::get<document::TextObject>(*object);
        if (old.font == text.font) continue;
        after = candidate_snapshot(after, text, true, document::kInvalidObjectId);
        if (!after) return false;
        changed_any = true;
    }
    if (!changed_any) return true;
    const auto result = root_.document().execute_command(
        std::make_unique<SetTextFont>(before, std::move(after)));
    sync_status_bar();
    return result.success;
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
} // namespace pikdraw::desktop
