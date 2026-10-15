#pragma once

#include "pikdraw/document/document.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"
#include <vector>

namespace pikdraw::vector {

/// Transient transform gesture supporting move/drag with single atomic commit or Escape cancel
class TransformGesture {
public:
    TransformGesture() = default;

    void begin(geometry::Point2D start_pt, std::vector<document::ObjectId> target_ids) {
        start_pt_ = start_pt;
        current_pt_ = start_pt;
        target_ids_ = std::move(target_ids);
        active_ = !target_ids_.empty();
    }

    [[nodiscard]] bool is_active() const noexcept { return active_; }

    [[nodiscard]] geometry::Transform2D update(geometry::Point2D current_pt) noexcept {
        if (!active_) return geometry::Transform2D::identity();
        current_pt_ = current_pt;
        double dx = current_pt_.x - start_pt_.x;
        double dy = current_pt_.y - start_pt_.y;
        return geometry::Transform2D::translation(dx, dy);
    }

    [[nodiscard]] geometry::Transform2D current_delta() const noexcept {
        if (!active_) return geometry::Transform2D::identity();
        return geometry::Transform2D::translation(
            current_pt_.x - start_pt_.x,
            current_pt_.y - start_pt_.y
        );
    }

    /// Commits the transformation as a single atomic transaction
    document::CommandResult commit(document::Document& doc) {
        if (!active_) {
            return document::CommandResult::fail("Cannot commit inactive gesture");
        }
        auto delta = current_delta();
        active_ = false;

        if (std::abs(delta.tx) < 1e-6 && std::abs(delta.ty) < 1e-6) {
            return document::CommandResult::ok(); // Zero movement, no-op
        }

        auto composite = std::make_unique<document::CompositeCommand>("Move Objects");
        for (auto id : target_ids_) {
            composite->add_command(std::make_unique<document::TransformObjectCommand>(id, delta));
        }

        return doc.execute_command(std::move(composite));
    }

    /// Cancels active gesture without touching document model
    void cancel() noexcept {
        active_ = false;
        target_ids_.clear();
    }

private:
    bool active_{false};
    geometry::Point2D start_pt_{0.0, 0.0};
    geometry::Point2D current_pt_{0.0, 0.0};
    std::vector<document::ObjectId> target_ids_;
};

} // namespace pikdraw::vector
