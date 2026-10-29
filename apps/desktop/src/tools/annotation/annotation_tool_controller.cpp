#include "annotation_tool_controller.hpp"
#include <algorithm>
#include <cmath>

namespace pikdraw::desktop::tools {

AnnotationToolController::AnnotationToolController(document::Document& doc)
    : doc_(&doc) {}

void AnnotationToolController::bind_document(document::Document& doc) noexcept {
    if (doc_ == &doc) return;
    (void)handle_escape();
    editing_annotation_id_.reset();
    note_text_before_edit_.clear();
    doc_ = &doc;
}

void AnnotationToolController::activate(AnnotationToolMode mode) {
    if (active_ && mode_ == mode) return;
    active_ = true;
    mode_ = mode;
    state_ = AnnotationSessionState::Idle;
    has_preview_ = false;
}

void AnnotationToolController::deactivate() {
    active_ = false;
    state_ = AnnotationSessionState::Idle;
    has_preview_ = false;
}

void AnnotationToolController::set_mode(AnnotationToolMode mode) noexcept {
    if (mode_ == mode) return;
    mode_ = mode;
    state_ = AnnotationSessionState::Idle;
    has_preview_ = false;
}

void AnnotationToolController::set_scale_ratio(double ratio) {
    if (ratio >= 0.1 && ratio <= 50.0) {
        scale_ratio_ = ratio;
        draft_detail_view_.scale_ratio = ratio;
        if (draft_detail_view_.frame_rect.width > 0.0 &&
            draft_detail_view_.frame_rect.height > 0.0) {
            const double top = draft_detail_view_.frame_rect.y + draft_detail_view_.frame_rect.height;
            const double width = draft_detail_view_.source_region.width * ratio;
            const double height = draft_detail_view_.source_region.height * ratio;
            draft_detail_view_.frame_rect = {draft_detail_view_.frame_rect.x, top - height, width, height};
        }
        if (has_preview_ && mode_ == AnnotationToolMode::DetailView) {
            update_preview();
        }
    }
}

void AnnotationToolController::set_detail_shape(document::DetailViewShape shape) {
    if (shape != document::DetailViewShape::Circle &&
        shape != document::DetailViewShape::Rectangle) return;
    detail_shape_ = shape;
    draft_detail_view_.shape = shape;
    if (shape == document::DetailViewShape::Circle &&
        draft_detail_view_.source_region.width > 0.0 &&
        draft_detail_view_.source_region.height > 0.0) {
        const auto center = draft_detail_view_.source_region.center();
        const double diameter = (std::min)(draft_detail_view_.source_region.width,
                                            draft_detail_view_.source_region.height);
        draft_detail_view_.source_region = {center.x - diameter * 0.5,
                                            center.y - diameter * 0.5,
                                            diameter, diameter};
        const double top = draft_detail_view_.frame_rect.y + draft_detail_view_.frame_rect.height;
        const double side = diameter * scale_ratio_;
        draft_detail_view_.frame_rect = {draft_detail_view_.frame_rect.x, top - side, side, side};
    }
    if (has_preview_ && mode_ == AnnotationToolMode::DetailView) {
        update_preview();
    }
}

void AnnotationToolController::set_detail_label(std::string label) {
    detail_label_ = std::move(label);
    draft_detail_view_.label = detail_label_;
    if (has_preview_ && mode_ == AnnotationToolMode::DetailView) {
        update_preview();
    }
}

void AnnotationToolController::set_show_connector(bool show) {
    show_connector_ = show;
    draft_detail_view_.show_connector = show;
    if (has_preview_ && mode_ == AnnotationToolMode::DetailView) update_preview();
}

bool AnnotationToolController::set_source_region(geometry::Rect2D region) {
    if (!region.is_finite() || region.width <= 0.0 || region.height <= 0.0 ||
        (detail_shape_ == document::DetailViewShape::Circle &&
         std::abs(region.width - region.height) > 1e-9)) return false;
    const double top = draft_detail_view_.frame_rect.y + draft_detail_view_.frame_rect.height;
    draft_detail_view_.source_region = region;
    const double width = region.width * scale_ratio_;
    const double height = region.height * scale_ratio_;
    draft_detail_view_.frame_rect = {draft_detail_view_.frame_rect.x, top - height, width, height};
    update_preview();
    return true;
}

bool AnnotationToolController::set_frame_top_left(geometry::Point2D top_left) {
    if (!std::isfinite(top_left.x) || !std::isfinite(top_left.y) ||
        draft_detail_view_.frame_rect.width <= 0.0 ||
        draft_detail_view_.frame_rect.height <= 0.0) return false;
    draft_detail_view_.frame_rect.x = top_left.x;
    draft_detail_view_.frame_rect.y = top_left.y - draft_detail_view_.frame_rect.height;
    update_preview();
    return true;
}

void AnnotationToolController::set_note_font_family(std::string family) {
    if (family.empty() || family.size() > 256) return;
    note_style_.font_family = std::move(family);
    draft_annotation_.style.font_family = note_style_.font_family;
    if (has_preview_ && mode_ != AnnotationToolMode::DetailView) update_preview();
}

void AnnotationToolController::set_note_text_size_mm(double size_mm) {
    if (!std::isfinite(size_mm) || size_mm <= 0.0 || size_mm > 1000.0) return;
    note_style_.text_size_mm = size_mm;
    draft_annotation_.style.text_size_mm = size_mm;
    if (has_preview_ && mode_ != AnnotationToolMode::DetailView) update_preview();
}

void AnnotationToolController::begin_edit_annotation(const document::AnnotationObject& annotation) {
    editing_annotation_id_ = annotation.id;
    note_text_before_edit_ = note_text_;
    mode_ = annotation.kind == document::AnnotationKind::PlainNote
        ? AnnotationToolMode::PlainNote : AnnotationToolMode::LeaderNote;
    draft_annotation_ = annotation;
    note_text_ = annotation.text;
    state_ = AnnotationSessionState::NoteTextEditing;
    pending_high_surrogate_ = 0;
    active_ = true;
    update_preview();
}

void AnnotationToolController::set_note_text(std::string text) {
    if (text.size() > 64u * 1024u) return;
    note_text_ = std::move(text);
    draft_annotation_.text = note_text_;
    if (has_preview_ && mode_ != AnnotationToolMode::DetailView) {
        update_preview();
    }
}

bool AnnotationToolController::append_note_character(uint32_t character) {
    if (!note_text_editing()) return false;
    if (character >= 0xd800 && character <= 0xdbff) {
        pending_high_surrogate_ = static_cast<char16_t>(character);
        return true;
    }
    if (character >= 0xdc00 && character <= 0xdfff) {
        if (!pending_high_surrogate_) return true;
        character = 0x10000u +
            ((static_cast<uint32_t>(pending_high_surrogate_) - 0xd800u) << 10) +
            character - 0xdc00u;
        pending_high_surrogate_ = 0;
    } else {
        pending_high_surrogate_ = 0;
    }
    if (character == 8) return erase_note_character();
    if (character == 13) character = 10;
    if (character < 0x20 && character != 10 && character != 9) return true;

    std::string encoded;
    if (character <= 0x7f) encoded.push_back(static_cast<char>(character));
    else if (character <= 0x7ff) {
        encoded.push_back(static_cast<char>(0xc0 | (character >> 6)));
        encoded.push_back(static_cast<char>(0x80 | (character & 0x3f)));
    } else if (character <= 0xffff) {
        encoded.push_back(static_cast<char>(0xe0 | (character >> 12)));
        encoded.push_back(static_cast<char>(0x80 | ((character >> 6) & 0x3f)));
        encoded.push_back(static_cast<char>(0x80 | (character & 0x3f)));
    } else if (character <= 0x10ffff) {
        encoded.push_back(static_cast<char>(0xf0 | (character >> 18)));
        encoded.push_back(static_cast<char>(0x80 | ((character >> 12) & 0x3f)));
        encoded.push_back(static_cast<char>(0x80 | ((character >> 6) & 0x3f)));
        encoded.push_back(static_cast<char>(0x80 | (character & 0x3f)));
    } else {
        return true;
    }
    if (encoded.size() > 64u * 1024u - note_text_.size()) return true;
    note_text_ += encoded;
    draft_annotation_.text = note_text_;
    update_preview();
    return true;
}

bool AnnotationToolController::erase_note_character() {
    if (!note_text_editing()) return false;
    pending_high_surrogate_ = 0;
    if (!note_text_.empty()) {
        size_t start = note_text_.size() - 1;
        while (start > 0 && (static_cast<uint8_t>(note_text_[start]) & 0xc0) == 0x80)
            --start;
        note_text_.erase(start);
        draft_annotation_.text = note_text_;
        update_preview();
    }
    return true;
}

bool AnnotationToolController::handle_pointer_press(
    geometry::Point2D world_pos,
    std::optional<document::ObjectId> hit_object,
    std::optional<uint64_t> hit_element,
    geometry::Point2D screen_dip) {
    if (!active_) return false;
    last_committed_id_.reset();

    if (screen_dip.x == 0.0 && screen_dip.y == 0.0) screen_dip = world_pos;
    press_point_ = world_pos;
    current_point_ = world_pos;
    press_screen_dip_ = current_screen_dip_ = screen_dip;

    if (mode_ == AnnotationToolMode::DetailView) {
        if (state_ == AnnotationSessionState::Idle) {
            state_ = AnnotationSessionState::DetailSourceDragging;
            draft_detail_view_ = document::DetailViewObject{};
            draft_detail_view_.source_page_id = doc_->active_page_id();
            draft_detail_view_.scale_ratio = scale_ratio_;
            draft_detail_view_.shape = detail_shape_;
            draft_detail_view_.label = detail_label_;
            draft_detail_view_.show_connector = show_connector_;
            draft_detail_view_.source_region = {};
            return true;
        } else if (state_ == AnnotationSessionState::DetailFramePlacing) {
            handle_pointer_move(world_pos);
            state_ = AnnotationSessionState::DetailPlacementPressed;
            return true;
        }
    } else if (mode_ == AnnotationToolMode::LeaderNote) {
        if (state_ == AnnotationSessionState::Idle) {
            state_ = AnnotationSessionState::LeaderAnchorPicked;
            draft_annotation_ = document::AnnotationObject{};
            draft_annotation_.kind = document::AnnotationKind::LeaderNote;
            draft_annotation_.style = note_style_;
            draft_annotation_.anchor.anchor_pt = world_pos;
            if (hit_object.has_value()) {
                draft_annotation_.anchor.source_object_id = *hit_object;
                draft_annotation_.anchor.state = document::AssociationState::Valid;
                draft_annotation_.anchor.element_id = hit_element;
            } else {
                draft_annotation_.anchor.state = document::AssociationState::Fixed;
            }
            draft_annotation_.text = note_text_;
            update_preview();
            return true;
        } else if (state_ == AnnotationSessionState::LeaderAnchorPicked) {
            if (draft_annotation_.leader_points.empty())
                draft_annotation_.leader_points.push_back(world_pos);
            else
                draft_annotation_.leader_points.back() = world_pos;
            draft_annotation_.landing_point = world_pos;
            draft_annotation_.text_point = world_pos;
            if (leader_line_mode_ == LeaderLineMode::Straight) {
                state_ = AnnotationSessionState::NoteTextEditing;
                note_text_ = draft_annotation_.text;
                pending_high_surrogate_ = 0;
            } else {
                state_ = AnnotationSessionState::LeaderElbowPicked;
            }
            update_preview();
            return true;
        } else if (state_ == AnnotationSessionState::LeaderElbowPicked) {
            draft_annotation_.landing_point = world_pos;
            draft_annotation_.text_point = world_pos;
            state_ = AnnotationSessionState::NoteTextEditing;
            note_text_ = draft_annotation_.text;
            pending_high_surrogate_ = 0;
            draft_annotation_.text.clear();
            update_preview();
            return true;
        }
    } else if (mode_ == AnnotationToolMode::PlainNote) {
        draft_annotation_ = document::AnnotationObject{};
        draft_annotation_.kind = document::AnnotationKind::PlainNote;
        draft_annotation_.style = note_style_;
        draft_annotation_.text_point = world_pos;
        draft_annotation_.text = note_text_;
        pending_high_surrogate_ = 0;
        state_ = AnnotationSessionState::NoteTextEditing;
        update_preview();
        return true;
    }

    return false;
}

void AnnotationToolController::handle_pointer_move(geometry::Point2D world_pos, geometry::Point2D screen_dip) {
    if (!active_) return;
    if (screen_dip.x == 0.0 && screen_dip.y == 0.0) screen_dip = world_pos;
    current_point_ = world_pos;
    current_screen_dip_ = screen_dip;

    if (mode_ == AnnotationToolMode::DetailView) {
        if (state_ == AnnotationSessionState::DetailSourceDragging) {
            const double screen_distance = std::hypot(
                screen_dip.x - press_screen_dip_.x,
                screen_dip.y - press_screen_dip_.y);
            if (screen_distance < 3.0) return;
            if (detail_shape_ == document::DetailViewShape::Circle) {
                double r = std::hypot(world_pos.x - press_point_.x,
                                      world_pos.y - press_point_.y);
                draft_detail_view_.source_region =
                    {press_point_.x - r, press_point_.y - r, 2 * r, 2 * r};
            } else {
                double x = (std::min)(press_point_.x, world_pos.x);
                double y = (std::min)(press_point_.y, world_pos.y);
                double width = std::abs(world_pos.x - press_point_.x);
                double height = std::abs(world_pos.y - press_point_.y);
                draft_detail_view_.source_region = {x, y, width, height};
            }
            update_preview();
        } else if (state_ == AnnotationSessionState::DetailFramePlacing) {
            const double width = draft_detail_view_.source_region.width * scale_ratio_;
            const double height = draft_detail_view_.source_region.height * scale_ratio_;
            // World coordinates have Y up; P is the visible top-left corner.
            draft_detail_view_.frame_rect = {world_pos.x, world_pos.y - height, width, height};
            update_preview();
        } else if (state_ == AnnotationSessionState::DetailPlacementPressed) {
            if (detail_shape_ == document::DetailViewShape::Circle) {
                const double width = draft_detail_view_.source_region.width * scale_ratio_;
                const double height = draft_detail_view_.source_region.height * scale_ratio_;
                draft_detail_view_.frame_rect = {world_pos.x, world_pos.y - height, width, height};
            } else {
                const double width = draft_detail_view_.source_region.width * scale_ratio_;
                const double height = draft_detail_view_.source_region.height * scale_ratio_;
                draft_detail_view_.frame_rect = {world_pos.x, world_pos.y - height, width, height};
            }
            update_preview();
        }
    } else if (mode_ == AnnotationToolMode::LeaderNote) {
        if (state_ == AnnotationSessionState::LeaderAnchorPicked) {
            if (!draft_annotation_.leader_points.empty()) {
                draft_annotation_.leader_points.back() = world_pos;
            } else {
                draft_annotation_.leader_points.push_back(world_pos);
            }
            draft_annotation_.landing_point = world_pos;
            draft_annotation_.text_point = world_pos;
            update_preview();
        } else if (state_ == AnnotationSessionState::LeaderElbowPicked) {
            draft_annotation_.landing_point = world_pos;
            draft_annotation_.text_point = world_pos;
            update_preview();
        }
    }
}

bool AnnotationToolController::handle_pointer_release(geometry::Point2D world_pos, geometry::Point2D screen_dip) {
    if (!active_) return false;
    if (screen_dip.x == 0.0 && screen_dip.y == 0.0) screen_dip = world_pos;

    if (mode_ == AnnotationToolMode::DetailView) {
        if (state_ == AnnotationSessionState::DetailSourceDragging) {
            handle_pointer_move(world_pos, screen_dip);
            if (state_ == AnnotationSessionState::DetailSourceDragging &&
                !has_preview_) {
                state_ = AnnotationSessionState::Idle;
                return false;
            }
            if (draft_detail_view_.source_region.width <= 0.0 ||
                draft_detail_view_.source_region.height <= 0.0) {
                state_ = AnnotationSessionState::Idle;
                has_preview_ = false;
                last_error_ = "Drag farther to select a valid source region";
                return false;
            }
            state_ = AnnotationSessionState::DetailFramePlacing;
            const double width = draft_detail_view_.source_region.width * scale_ratio_;
            const double height = draft_detail_view_.source_region.height * scale_ratio_;
            draft_detail_view_.frame_rect = {world_pos.x, world_pos.y - height, width, height};
            update_preview();
            return true;
        }
        if (state_ == AnnotationSessionState::DetailPlacementPressed) {
            handle_pointer_move(world_pos, screen_dip);
            state_ = AnnotationSessionState::DetailFramePlacing;
            return commit();
        }
    }

    return false;
}

bool AnnotationToolController::handle_escape() noexcept {
    if (!active_) return false;
    if (state_ != AnnotationSessionState::Idle) {
        state_ = AnnotationSessionState::Idle;
        has_preview_ = false;
        if (editing_annotation_id_) {
            note_text_ = note_text_before_edit_;
            editing_annotation_id_.reset();
            note_text_before_edit_.clear();
        } else {
            note_text_.clear();
        }
        pending_high_surrogate_ = 0;
        return true;
    }
    return false;
}

void AnnotationToolController::update_preview() {
    if (mode_ == AnnotationToolMode::DetailView) {
        preview_detail_view_layout_ = document::evaluate_detail_view_layout(draft_detail_view_);
        has_preview_ = true;
    } else {
        preview_annotation_layout_ = document::evaluate_annotation_layout(draft_annotation_);
        has_preview_ = true;
    }
}

bool AnnotationToolController::commit() {
    if (!active_) return false;
    last_error_.clear();

    if (mode_ == AnnotationToolMode::DetailView) {
        if (state_ != AnnotationSessionState::DetailPlacementPressed &&
            state_ != AnnotationSessionState::DetailFramePlacing) {
            last_error_ = "Select a source region and place the detail frame first";
            return false;
        }
        if (draft_detail_view_.id == document::kInvalidObjectId)
            draft_detail_view_.id = document::generate_object_id();
        const auto id = draft_detail_view_.id;
        auto cmd = std::make_unique<document::AddDetailViewCommand>(draft_detail_view_);
        const auto res = doc_->execute_command(std::move(cmd));
        if (res.success) {
            last_committed_id_ = id;
            state_ = AnnotationSessionState::Idle;
            has_preview_ = false;
        } else {
            last_error_ = res.error_message;
        }
        return res.success;
    } else {
        if (!note_text_editing()) {
            last_error_ = "Annotation text entry is not active";
            return false;
        }
        const bool has_non_whitespace = std::any_of(note_text_.begin(), note_text_.end(), [](unsigned char ch) {
            return ch > 0x20;
        });
        if (!has_non_whitespace) {
            last_error_ = "Annotation text cannot be empty";
            return false;
        }
        draft_annotation_.text = note_text_;
        const bool editing = editing_annotation_id_.has_value();
        if (editing) draft_annotation_.id = *editing_annotation_id_;
        else if (draft_annotation_.id == document::kInvalidObjectId)
            draft_annotation_.id = document::generate_object_id();
        const auto id = draft_annotation_.id;
        std::unique_ptr<document::DocumentCommand> cmd;
        if (editing)
            cmd = std::make_unique<document::UpdateAnnotationCommand>(id, draft_annotation_);
        else
            cmd = std::make_unique<document::AddAnnotationCommand>(draft_annotation_);
        const auto res = doc_->execute_command(std::move(cmd));
        if (res.success) {
            last_committed_id_ = id;
            state_ = AnnotationSessionState::Idle;
            has_preview_ = false;
            if (editing) {
                editing_annotation_id_.reset();
                note_text_ = note_text_before_edit_;
                note_text_before_edit_.clear();
            } else note_text_.clear();
            pending_high_surrogate_ = 0;
        } else last_error_ = res.error_message;
        return res.success;
    }
}

} // namespace pikdraw::desktop::tools
