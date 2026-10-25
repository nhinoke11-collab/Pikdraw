#pragma once

#include "pikdraw/document/document.hpp"
#include "pikdraw/document/annotations/annotation_object.hpp"
#include "pikdraw/document/annotations/annotation_layout.hpp"
#include "pikdraw/document/commands/annotation_commands.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pikdraw::desktop::tools {

enum class AnnotationToolMode : uint8_t {
    DetailView,
    LeaderNote,
    PlainNote
};

enum class AnnotationSessionState : uint8_t {
    Idle,
    // Detail view states:
    DetailSourceDragging,
    DetailFramePlacing,
    // Leader note states:
    LeaderAnchorPicked,
    LeaderElbowPicked,
    // Plain note state:
    NoteTextEditing
};

class AnnotationToolController {
public:
    explicit AnnotationToolController(document::Document& doc);

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

    void set_note_text(std::string text);
    [[nodiscard]] const std::string& note_text() const noexcept { return note_text_; }

    // Pointer gesture handling
    bool handle_pointer_press(
        geometry::Point2D world_pos,
        std::optional<document::ObjectId> hit_object = std::nullopt,
        std::optional<uint64_t> hit_element = std::nullopt);
    void handle_pointer_move(geometry::Point2D world_pos);
    bool handle_pointer_release(geometry::Point2D world_pos);
    bool handle_escape() noexcept;

    // Direct commit
    bool commit();

private:
    void update_preview();

    document::Document& doc_;
    bool active_{false};
    AnnotationToolMode mode_{AnnotationToolMode::DetailView};
    AnnotationSessionState state_{AnnotationSessionState::Idle};

    double scale_ratio_{2.0};
    document::DetailViewShape detail_shape_{document::DetailViewShape::Circle};
    std::string detail_label_{"DETAIL A"};
    std::string note_text_{"Bo R2, khong cat qua lo"};

    geometry::Point2D press_point_{};
    geometry::Point2D current_point_{};

    document::AnnotationObject draft_annotation_;
    document::AnnotationLayout preview_annotation_layout_;

    document::DetailViewObject draft_detail_view_;
    document::DetailViewLayout preview_detail_view_layout_;

    bool has_preview_{false};
};

} // namespace pikdraw::desktop::tools
