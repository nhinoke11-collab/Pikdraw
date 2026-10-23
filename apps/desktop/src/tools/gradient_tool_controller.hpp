#pragma once

#include "pikdraw/desktop/presenters/gradient_properties.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/vector/tools/gradient_tool.hpp"

#include <memory>
#include <string>
#include <vector>

namespace pikdraw::desktop::tools {

/// Session token tracking document revision, selection generation,
/// and draft generation to reject stale asynchronous results or delayed preview publishes.
struct GradientSessionToken {
    uint64_t document_revision{0};
    uint64_t selection_generation{0};
    uint64_t draft_generation{0};

    [[nodiscard]] bool matches(uint64_t cur_rev, uint64_t cur_sel, uint64_t cur_draft) const noexcept {
        return document_revision == cur_rev &&
               selection_generation == cur_sel &&
               draft_generation == cur_draft;
    }
};

/// Desktop tool controller mediating between document selection,
/// vector GradientTool gestures, contextual bar/Properties presenter,
/// and atomic SetFillPaintCommand commit routing.
class GradientToolController {
public:
    explicit GradientToolController(document::Document& document);
    ~GradientToolController() = default;

    GradientToolController(const GradientToolController&) = delete;
    GradientToolController& operator=(const GradientToolController&) = delete;

    // Activation lifecycle
    bool activate(std::vector<document::ObjectId> target_ids = {});
    void deactivate();
    [[nodiscard]] bool is_active() const noexcept { return is_active_; }
    [[nodiscard]] bool active() const noexcept { return is_active_; }
    [[nodiscard]] const std::vector<document::ObjectId>& target_objects() const noexcept {
        return active_targets_;
    }

    // External change handlers
    void on_selection_changed(std::vector<document::ObjectId> new_targets);
    void on_document_changed();

    // Session token guard
    [[nodiscard]] GradientSessionToken current_token() const noexcept;
    [[nodiscard]] bool validate_token(const GradientSessionToken& token) const noexcept;
    bool publish_preview(const GradientSessionToken& token);
    bool apply_with_token(const GradientSessionToken& token);

    // Canvas gesture forwarding
    void on_pointer_down(geometry::Point2D object_pos, bool shift, bool ctrl, double hit_tolerance_mm = 3.0);
    void on_pointer_move(geometry::Point2D object_pos, bool shift, bool ctrl);
    bool on_pointer_up(geometry::Point2D object_pos);
    void on_cancel();

    // Properties transaction routing
    bool apply();
    void cancel();
    [[nodiscard]] bool has_unapplied_changes() const noexcept;
    [[nodiscard]] const std::string& status_message() const noexcept { return status_message_; }
    void clear_status_message() noexcept { status_message_.clear(); }

    // Sub-component accessors
    [[nodiscard]] vector::tools::GradientTool& tool() noexcept { return *tool_; }
    [[nodiscard]] const vector::tools::GradientTool& tool() const noexcept { return *tool_; }
    [[nodiscard]] GradientPropertiesPresenter& presenter() noexcept { return presenter_; }
    [[nodiscard]] const GradientPropertiesPresenter& presenter() const noexcept { return presenter_; }
    [[nodiscard]] const document::GradientPaint* active_draft_paint() const noexcept;

private:
    geometry::Rect2D compute_selection_bounds() const;
    void sync_presenter_from_document();

    document::Document& document_;
    std::unique_ptr<vector::tools::GradientTool> tool_;
    GradientPropertiesPresenter presenter_;

    std::vector<document::ObjectId> active_targets_;
    uint64_t selection_generation_{1};
    uint64_t last_document_revision_{0};
    std::string status_message_;
    bool is_active_{false};
};

} // namespace pikdraw::desktop::tools
