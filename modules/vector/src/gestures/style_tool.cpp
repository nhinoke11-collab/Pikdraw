#include "pikdraw/vector/gestures/style_tool.hpp"
namespace pikdraw::vector {
document::CommandResult SetObjectStyleCommand::execute(document::Document &doc) {
    if (!fill_ && !stroke_ && !width_ && !has_fill_ && !has_stroke_)
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
    } else if (auto *text = std::get_if<document::TextObject>(&updated)) {
        if (stroke_ || width_ || has_stroke_ || has_fill_)
            return document::CommandResult::fail("Text stroke/fill flags unsupported by source model");
        if (fill_)
            text->fill = *fill_;
    } else
        return document::CommandResult::fail("Group style unsupported");
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
