#pragma once

#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/typography/layout/caret_navigation.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pikdraw::desktop::tools {

// Returns the inherited transform only for a visible, unlocked object and ancestors.
[[nodiscard]] std::optional<geometry::Transform2D> editable_text_parent(
    const document::DocumentSnapshot& snapshot, document::ObjectId id);
[[nodiscard]] typography::CaretHitResult text_caret_metrics(
    const typography::TextLayout& layout, size_t byte_offset);

// UTF-8 source offsets throughout; glyph layout supplies the visual positions.
// This is interaction state, never the mutable document or its undo history.
struct CanvasTextEditor {
    struct State {
        document::TextObject text;
        size_t caret{}, anchor{};
    };
    document::SnapshotPtr base;
    document::TextObject original;
    document::TextObject draft;
    document::ObjectId parent{document::kInvalidObjectId};
    geometry::Transform2D parent_transform;
    bool existing{}, selecting{}, caret_visible{true};
    size_t caret{}, anchor{};
    char16_t high_surrogate{};
    uint32_t suppress_character{};
    geometry::Point2D drag_start;
    bool creating_frame{};
    std::string composition;
    std::optional<double> desired_x;
    std::optional<document::TextObject> live_scale_baseline;
    geometry::Point2D live_scale_anchor_world{};
    geometry::Point2D live_scale_anchor_local{};
    geometry::Point2D live_scale_start_world{};
    uint64_t live_scale_last_update_ms{};
    bool is_moving_frame{};
    geometry::Point2D live_move_start_pos{};
    geometry::Point2D live_move_start_world{};
    std::optional<document::TextObject> live_move_baseline;
    std::vector<State> undo, redo;

    [[nodiscard]] geometry::Transform2D local_to_world() const {
        return parent_transform * draft.transform *
            geometry::Transform2D::translation(draft.position.x, draft.position.y) *
            geometry::Transform2D::scale(1, -1);
    }

    [[nodiscard]] size_t first() const { return std::min(caret, anchor); }
    [[nodiscard]] size_t last() const { return std::max(caret, anchor); }
    [[nodiscard]] bool has_selection() const { return caret != anchor; }
    [[nodiscard]] std::string selected_text() const;
    void remember();
    void push_undo(const document::TextObject& baseline);
    bool replace(std::string value);
    bool erase(bool backward, bool by_word = false);
    bool restore(bool forward);
    void move(size_t offset, bool extend);
    [[nodiscard]] size_t word_left(size_t offset) const;
    [[nodiscard]] size_t word_right(size_t offset) const;
    void select_word(size_t offset);
};

} // namespace pikdraw::desktop::tools
