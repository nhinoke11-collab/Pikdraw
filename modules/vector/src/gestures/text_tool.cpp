#include "pikdraw/vector/gestures/text_tool.hpp"
namespace pikdraw::vector {
document::CommandResult UpdateTextCommand::execute(document::Document &doc) {
    if (!text_ && !font_)
        return document::CommandResult::fail("No source text/font attributes supplied");
    if (!editable_target(doc.page(), doc.objects(), id_))
        return document::CommandResult::fail("Text target missing, hidden or locked");
    const auto old = doc.objects().get(id_);
    const auto *text = std::get_if<document::TextObject>(&*old);
    if (!text)
        return document::CommandResult::fail("Target is not source text");
    auto updated = *text;
    if (text_)
        updated.text = *text_;
    if (font_)
        updated.font = *font_;
    before_ = *text;
    // Document publication validates UTF-8 and font identity, without font substitution.
    doc.apply_mutation(doc.page(), doc.objects().set(id_, std::move(updated)));
    return document::CommandResult::ok();
}
document::CommandResult UpdateTextCommand::undo(document::Document &doc) {
    if (!before_)
        return document::CommandResult::fail("Missing text before state");
    doc.apply_mutation(doc.page(), doc.objects().set(id_, *before_));
    return document::CommandResult::ok();
}
} // namespace pikdraw::vector
