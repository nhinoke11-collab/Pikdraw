#include "pikdraw/desktop/tools/canvas_text_editor.hpp"
#include <cctype>
#include <functional>

namespace pikdraw::desktop::tools {
std::optional<geometry::Transform2D> editable_text_parent(
    const document::DocumentSnapshot& snapshot, document::ObjectId target) {
    std::function<std::optional<geometry::Transform2D>(document::ObjectId, geometry::Transform2D)> visit;
    visit = [&](document::ObjectId id, geometry::Transform2D parent)
        -> std::optional<geometry::Transform2D> {
        const auto value = snapshot.objects().get(id);
        if (!value || std::visit([](const auto& o) { return !o.visible || o.locked; }, *value))
            return std::nullopt;
        if (id == target) return parent;
        if (const auto* group = std::get_if<document::GroupObject>(&*value))
            for (auto child : group->children_ids)
                if (auto result = visit(child, parent * group->transform)) return result;
        return std::nullopt;
    };
    for (const auto& layer : snapshot.page().layers) {
        if (!layer.visible || layer.locked) continue;
        for (auto id : layer.object_ids)
            if (auto result = visit(id, {})) return result;
    }
    return std::nullopt;
}
typography::CaretHitResult text_caret_metrics(const typography::TextLayout& layout, size_t offset) {
    if (layout.lines().empty()) return {};
    const auto* line = &layout.lines().front();
    for (const auto& candidate : layout.lines())
        if (candidate.source_byte_offset <= offset) line = &candidate;
    double x = line->baseline_origin.x;
    const auto local = offset - std::min(offset, line->source_byte_offset);
    for (const auto& glyph : line->run.glyphs) {
        if (glyph.cluster_byte_offset >= local) break;
        x += glyph.x_advance_mm;
    }
    return {offset, {x, line->line_bounds.y}, line->line_bounds.height};
}
namespace {
enum class Kind { Space, Word, Punctuation };
Kind kind(std::string_view text, size_t offset) {
    if (offset >= text.size()) return Kind::Space;
    const auto byte = static_cast<unsigned char>(text[offset]);
    if (byte < 128 && std::isspace(byte)) return Kind::Space;
    if (byte >= 128 || std::isalnum(byte) || byte == '_') return Kind::Word;
    return Kind::Punctuation;
}
std::string normalize_lines(std::string value) {
    std::string result;
    result.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\r') {
            result += '\n';
            if (i + 1 < value.size() && value[i + 1] == '\n') ++i;
        } else if (value[i] != '\0') result += value[i];
    }
    return result;
}
}
std::string CanvasTextEditor::selected_text() const {
    return draft.text.substr(first(), last() - first());
}
void CanvasTextEditor::remember() {
    push_undo(draft);
}
void CanvasTextEditor::push_undo(const document::TextObject& baseline) {
    undo.push_back({baseline, caret, anchor});
    if (undo.size() > 128) undo.erase(undo.begin());
    redo.clear();
    desired_x.reset();
}
bool CanvasTextEditor::replace(std::string value) {
    value = normalize_lines(std::move(value));
    // Bound a single text object to 1 MiB, including paste/IME input.
    if (draft.text.size() - (last() - first()) + value.size() > 1024 * 1024)
        return false;
    (void)typography::next_grapheme_cluster(value, 0); // strict UTF-8 validation
    if (!has_selection() && value.empty()) return false;
    remember();
    const size_t start = first();
    draft.text.replace(start, last() - start, value);
    caret = anchor = start + value.size();
    return true;
}
bool CanvasTextEditor::erase(bool backward, bool by_word) {
    if (!has_selection()) {
        anchor = backward ? (by_word ? word_left(caret)
                                    : typography::prev_grapheme_cluster(draft.text, caret))
                          : (by_word ? word_right(caret)
                                     : typography::next_grapheme_cluster(draft.text, caret));
    }
    if (!has_selection()) return false;
    return replace({});
}
bool CanvasTextEditor::restore(bool forward) {
    auto& from = forward ? redo : undo;
    auto& to = forward ? undo : redo;
    if (from.empty()) return false;
    to.push_back({draft, caret, anchor});
    auto state = std::move(from.back());
    from.pop_back();
    draft = std::move(state.text);
    caret = state.caret;
    anchor = state.anchor;
    desired_x.reset();
    return true;
}
void CanvasTextEditor::move(size_t offset, bool extend) {
    caret = std::min(offset, draft.text.size());
    if (!extend) anchor = caret;
    caret_visible = true;
}
size_t CanvasTextEditor::word_left(size_t offset) const {
    using typography::prev_grapheme_cluster;
    size_t position = offset;
    while (position > 0 && kind(draft.text, prev_grapheme_cluster(draft.text, position)) == Kind::Space)
        position = prev_grapheme_cluster(draft.text, position);
    if (position == 0) return 0;
    const auto category = kind(draft.text, prev_grapheme_cluster(draft.text, position));
    while (position > 0 && kind(draft.text, prev_grapheme_cluster(draft.text, position)) == category)
        position = prev_grapheme_cluster(draft.text, position);
    return position;
}
size_t CanvasTextEditor::word_right(size_t offset) const {
    using typography::next_grapheme_cluster;
    size_t position = offset;
    const auto category = kind(draft.text, position);
    while (position < draft.text.size() && kind(draft.text, position) == category)
        position = next_grapheme_cluster(draft.text, position);
    while (position < draft.text.size() && kind(draft.text, position) == Kind::Space)
        position = next_grapheme_cluster(draft.text, position);
    return position;
}
void CanvasTextEditor::select_word(size_t offset) {
    caret = std::min(offset, draft.text.size());
    if (caret == draft.text.size() && caret > 0)
        caret = typography::prev_grapheme_cluster(draft.text, caret);
    const auto category = kind(draft.text, caret);
    anchor = caret;
    while (anchor > 0) {
        const auto previous = typography::prev_grapheme_cluster(draft.text, anchor);
        if (kind(draft.text, previous) != category) break;
        anchor = previous;
    }
    while (caret < draft.text.size() && kind(draft.text, caret) == category)
        caret = typography::next_grapheme_cluster(draft.text, caret);
}
} // namespace pikdraw::desktop::tools
