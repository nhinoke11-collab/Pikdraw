#pragma once

#include "pikdraw/document/document.hpp"
#include "pikdraw/document/annotations/annotation_object.hpp"
#include "pikdraw/document/annotations/annotation_layout.hpp"
#include "pikdraw/document/commands/annotation_commands.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"

#include <optional>
#include <cstdint>
#include <string>
#include <vector>

namespace pikdraw::desktop::tools {

enum class AnnotationToolMode : uint8_t {
    DetailView,
    LeaderNote,
    PlainNote
};

enum class LeaderLineMode : uint8_t { Straight, Elbow };

enum class AnnotationSessionState : uint8_t {
    Idle,
    // Detail view states:
    DetailSourceDragging,
    DetailFramePlacing,
    DetailPlacementPressed,
    // Leader note states:
    LeaderAnchorPicked,
    LeaderElbowPicked,
    // Plain note state:
    NoteTextEditing
};

class AnnotationToolController {
public:
    explicit AnnotationToolController(document::Document& doc);
    void bind_document(document::Document& doc) noexcept;
    [[nodiscard]] bool bound_to(const document::Document& doc) const noexcept { return doc_ == &doc; }

    void activate(AnnotationToolMode mode = AnnotationToolMode::DetailView);
    void deactivate();
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] AnnotationToolMode mode() const noexcept { return mode_; }
    void set_mode(AnnotationToolMode mode) noexcept;

    [[nodiscard]] AnnotationSessionState state() const noexcept { return state_; }
    [[nodiscard]] bool has_preview() const noexcept { return has_preview_; }

    [[nodiscard]] const document::AnnotationObject& draft_annotation() const noexcept { return draft_annotation_; }
    [[nodiscard]] const document::AnnotationLayout& preview_annotation_layout() const noexcept { return preview_annotation_layout_; }

    [[nodiscard]] const document::DetailViewObject& draft_detail_view() const noexcept { return draft_detail_view_; }
    [[nodiscard]] const document::DetailViewLayout& preview_detail_view_layout() const noexcept { return preview_detail_view_layout_; }

    // Style & options
    void set_scale_ratio(double ratio);
    [[nodiscard]] double scale_ratio() const noexcept { return scale_ratio_; }
    void set_detail_shape(document::DetailViewShape shape);
    [[nodiscard]] document::DetailViewShape detail_shape() const noexcept { return detail_shape_; }
    void set_detail_label(std::string label);
    [[nodiscard]] const std::string& detail_label() const noexcept { return detail_label_; }
    void set_show_connector(bool show);
    [[nodiscard]] bool show_connector() const noexcept { return show_connector_; }
    bool set_source_region(geometry::Rect2D region);
    bool set_frame_top_left(geometry::Point2D top_left);
    void set_leader_line_mode(LeaderLineMode mode) noexcept { leader_line_mode_ = mode; }
    [[nodiscard]] LeaderLineMode leader_line_mode() const noexcept { return leader_line_mode_; }
    void set_note_font_family(std::string family);
    [[nodiscard]] const std::string& note_font_family() const noexcept { return note_style_.font_family; }
    void set_note_text_size_mm(double size_mm);
    [[nodiscard]] double note_text_size_mm() const noexcept { return note_style_.text_size_mm; }

    void set_note_text(std::string text);
    [[nodiscard]] const std::string& note_text() const noexcept { return note_text_; }
    [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }
    void begin_edit_annotation(const document::AnnotationObject& annotation);
    [[nodiscard]] bool editing_existing_annotation() const noexcept { return editing_annotation_id_.has_value(); }
    [[nodiscard]] std::optional<document::ObjectId> last_committed_id() const noexcept {
        return last_committed_id_;
    }
    [[nodiscard]] bool note_text_editing() const noexcept {
        return state_ == AnnotationSessionState::NoteTextEditing;
    }
    bool append_note_character(uint32_t character);
    bool erase_note_character();

    // Pointer gesture handling
    bool handle_pointer_press(
        geometry::Point2D world_pos,
        std::optional<document::ObjectId> hit_object = std::nullopt,
        std::optional<uint64_t> hit_element = std::nullopt,
        geometry::Point2D screen_dip = {});
    void handle_pointer_move(geometry::Point2D world_pos, geometry::Point2D screen_dip = {});
    bool handle_pointer_release(geometry::Point2D world_pos, geometry::Point2D screen_dip = {});
    bool handle_escape() noexcept;

    // Direct commit
    bool commit();

private:
    void update_preview();

    document::Document* doc_{nullptr};
    bool active_{false};
    AnnotationToolMode mode_{AnnotationToolMode::DetailView};
    AnnotationSessionState state_{AnnotationSessionState::Idle};

    double scale_ratio_{2.0};
    document::DetailViewShape detail_shape_{document::DetailViewShape::Circle};
    bool show_connector_{true};
    LeaderLineMode leader_line_mode_{LeaderLineMode::Elbow};
    document::AnnotationStyle note_style_{document::AnnotationStyle::make_default()};
    std::string detail_label_{"DETAIL A"};
    std::string note_text_;
    std::string last_error_;
    char16_t pending_high_surrogate_{0};

    geometry::Point2D press_point_{};
    geometry::Point2D current_point_{};
    geometry::Point2D press_screen_dip_{};
    geometry::Point2D current_screen_dip_{};

    document::AnnotationObject draft_annotation_;
    document::AnnotationLayout preview_annotation_layout_;

    document::DetailViewObject draft_detail_view_;
    document::DetailViewLayout preview_detail_view_layout_;

    bool has_preview_{false};
    std::optional<document::ObjectId> last_committed_id_;
    std::optional<document::ObjectId> editing_annotation_id_;
    std::string note_text_before_edit_;
};

} // namespace pikdraw::desktop::tools
