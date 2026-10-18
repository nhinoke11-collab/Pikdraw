#include "pikdraw/vector/gestures/style_tool.hpp"
#include <unordered_set>
namespace pikdraw::vector {
document::CommandResult apply_object_styles(document::Document &doc,
                                            const std::vector<document::ObjectId> &ids,
                                            const StylePatch &patch) {
    if (ids.empty())
        return document::CommandResult::fail("No selected objects");
    const auto snapshot = doc.current_snapshot();
    auto command = std::make_unique<document::CompositeCommand>("Set Selection Style");
    std::unordered_set<document::ObjectId> seen;
    for (const auto id : ids) {
        if (!seen.insert(id).second)
            continue;
        if (!editable_target(*snapshot, id))
            return document::CommandResult::fail("Selection contains a missing or locked target");
        auto fill = patch.fill, stroke = patch.stroke;
        const auto *object = snapshot->objects().lookup_ref(id);
        std::visit(
            [&](const auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, document::PathObject> || std::is_same_v<T, document::TextObject>) {
                    if (fill)
                        fill->set_overprint(value.fill.overprint());
                }
                if constexpr (std::is_same_v<T, document::PathObject>) {
                    if (stroke)
                        stroke->set_overprint(value.stroke.overprint());
                }
            },
            *object);
        command->add_command(std::make_unique<SetObjectStyleCommand>(id, fill, stroke, patch.width_mm,
                                                                     patch.has_fill, patch.has_stroke,
                                                                     patch.fill_overprint,
                                                                     patch.stroke_overprint));
    }
    return doc.execute_command(std::move(command));
}

document::CommandResult SetObjectStyleCommand::execute(document::Document &doc) {
    if (!fill_ && !stroke_ && !width_ && !has_fill_ && !has_stroke_ &&
        !fill_overprint_ && !stroke_overprint_)
        return document::CommandResult::fail("No source style attributes supplied");
    if (!editable_target(doc.page(), doc.objects(), id_))
        return document::CommandResult::fail("Style target missing, hidden, locked or unsupported group");
    if ((fill_ && !fill_->is_valid()) || (stroke_ && !stroke_->is_valid()) ||
        (width_ && (!std::isfinite(*width_) || *width_ < 0)))
        return document::CommandResult::fail("Invalid source style");
    const auto old = doc.objects().get(id_);
    auto updated = *old;
    if (auto *path = std::get_if<document::PathObject>(&updated)) {
        if (fill_) {
            path->fill = *fill_;
            path->has_fill = true;
        }
        if (stroke_) {
            path->stroke = *stroke_;
            path->has_stroke = true;
        }
        if (width_)
            path->stroke_width_mm = *width_;
        if (has_fill_)
            path->has_fill = *has_fill_;
        if (has_stroke_)
            path->has_stroke = *has_stroke_;
        if (fill_overprint_ || stroke_overprint_) {
            auto fill_overprint = path->fill.overprint();
            auto stroke_overprint = path->stroke.overprint();
            if (fill_overprint_) fill_overprint.fill = *fill_overprint_;
            if (stroke_overprint_) stroke_overprint.stroke = *stroke_overprint_;
            path->fill.set_overprint(fill_overprint);
            path->stroke.set_overprint(stroke_overprint);
        }
    } else if (auto *text = std::get_if<document::TextObject>(&updated)) {
        if (stroke_ || width_ || has_stroke_ || has_fill_ || stroke_overprint_)
            return document::CommandResult::fail("Text stroke/fill flags unsupported by source model");
        if (fill_)
            text->fill = *fill_;
        if (fill_overprint_) {
            auto overprint = text->fill.overprint();
            overprint.fill = *fill_overprint_;
            text->fill.set_overprint(overprint);
        }
    } else if (std::holds_alternative<document::ImageObject>(updated)) {
        return document::CommandResult::fail("Bitmap image style is not editable as vector fill/stroke");
    } else {
        return document::CommandResult::fail("Group style unsupported");
    }
    before_ = old;
    doc.apply_mutation(doc.page(), doc.objects().set(id_, std::move(updated)));
    return document::CommandResult::ok();
}
document::CommandResult SetObjectStyleCommand::undo(document::Document &doc) {
    if (!before_)
        return document::CommandResult::fail("No saved style before state");
    doc.apply_mutation(doc.page(), doc.objects().set(id_, *before_));
    return document::CommandResult::ok();
}
} // namespace pikdraw::vector
