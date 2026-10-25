#pragma once

#include "pikdraw/document/document.hpp"
#include "pikdraw/document/commands/path_cut_commands.hpp"
#include "pikdraw/geometry/operations/path_cut.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pikdraw::vector::tools {

enum class CutToolMode {
    Knife,
    VectorEraser
};

enum class KnifeCutStyle {
    Straight,
    Freehand
};

class CutToolController {
public:
    explicit CutToolController(document::Document& document) noexcept
        : document_(document) {}

    void set_mode(CutToolMode mode) noexcept { mode_ = mode; reset_gesture(); }
    [[nodiscard]] CutToolMode mode() const noexcept { return mode_; }

    // Knife options
    void set_knife_style(KnifeCutStyle style) noexcept { knife_style_ = style; }
    [[nodiscard]] KnifeCutStyle knife_style() const noexcept { return knife_style_; }

    void set_knife_gap_mm(double gap) noexcept {
        if (gap >= 0.0 && gap <= 100.0) knife_gap_mm_ = gap;
    }
    [[nodiscard]] double knife_gap_mm() const noexcept { return knife_gap_mm_; }

    // Eraser options
    void set_eraser_diameter_mm(double diameter) noexcept {
        if (diameter >= 0.01 && diameter <= 1000.0) eraser_diameter_mm_ = diameter;
    }
    [[nodiscard]] double eraser_diameter_mm() const noexcept { return eraser_diameter_mm_; }

    void set_eraser_round_nib(bool round) noexcept { eraser_round_nib_ = round; }
    [[nodiscard]] bool eraser_round_nib() const noexcept { return eraser_round_nib_; }

    // Trim options
    void set_keep_source(bool keep) noexcept { keep_source_ = keep; }
    [[nodiscard]] bool keep_source() const noexcept { return keep_source_; }

    // Gesture handling
    void on_pointer_down(const geometry::Point2D& pt_world);
    void on_pointer_move(const geometry::Point2D& pt_world);
    [[nodiscard]] bool on_pointer_up(const geometry::Point2D& pt_world,
                                     const std::vector<document::ObjectId>& candidate_ids);
    void cancel_gesture() noexcept { reset_gesture(); }

    [[nodiscard]] bool is_gesture_active() const noexcept { return gesture_active_; }
    [[nodiscard]] const std::vector<geometry::Point2D>& gesture_points() const noexcept {
        return gesture_points_;
    }

    /// Direct execution helpers for UI/workflows
    bool execute_knife_cut(const std::vector<document::ObjectId>& candidate_ids,
                           const geometry::Point2D& p1, const geometry::Point2D& p2,
                           double gap_mm = 0.0);

    bool execute_eraser_cut(const std::vector<document::ObjectId>& candidate_ids,
                            const std::vector<geometry::Point2D>& stroke,
                            double diameter_mm, bool round_nib = true);

    bool execute_trim_objects(document::ObjectId target_id,
                             const std::vector<document::ObjectId>& source_ids,
                             bool keep_sources = true);

    [[nodiscard]] const std::vector<document::ObjectId>& last_created_ids() const noexcept {
        return last_created_ids_;
    }

private:
    void reset_gesture() noexcept;

    document::Document& document_;
    CutToolMode mode_{CutToolMode::Knife};
    KnifeCutStyle knife_style_{KnifeCutStyle::Straight};
    double knife_gap_mm_{0.0};
    double eraser_diameter_mm_{2.0};
    bool eraser_round_nib_{true};
    bool keep_source_{true};

    bool gesture_active_{false};
    std::vector<geometry::Point2D> gesture_points_;
    std::vector<document::ObjectId> last_created_ids_;
};

} // namespace pikdraw::vector::tools
