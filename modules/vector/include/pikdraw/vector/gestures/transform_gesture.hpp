#pragma once
#include "pikdraw/vector/gestures/edit_target.hpp"
namespace pikdraw::vector {
class TransformGesture {
  public:
    bool begin(geometry::Point2D start, std::vector<document::ObjectId> ids,
               const document::DocumentSnapshot &snapshot) {
        cancel();
        if (!finite_point(start) || ids.empty())
            return false;
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        for (auto id : ids)
            if (!editable_target(snapshot, id))
                return false;
        stamp_ = GestureStamp(snapshot);
        start_ = current_ = start;
        ids_ = std::move(ids);
        active_ = true;
        return true;
    }
    bool is_active() const noexcept {
        return active_;
    }
    geometry::Transform2D update(geometry::Point2D current) noexcept {
        if (!active_ || !finite_point(current)) {
            cancel();
            return {};
        }
        current_ = current;
        if (!current_delta().is_finite()) {
            cancel();
            return {};
        }
        return current_delta();
    }
    geometry::Transform2D current_delta() const noexcept {
        if (!active_)
            return {};
        return geometry::Transform2D::translation(current_.x - start_.x, current_.y - start_.y);
    }
    document::CommandResult commit(document::Document &doc) {
        if (!active_)
            return document::CommandResult::fail("Inactive/cancelled gesture");
        const auto delta = current_delta();
        const auto snapshot = doc.current_snapshot();
        active_ = false;
        if (!stamp_.matches(*snapshot)) {
            ids_.clear();
            return document::CommandResult::fail("Stale gesture document/epoch/revision");
        }
        for (auto id : ids_)
            if (!editable_target(*snapshot, id)) {
                ids_.clear();
                return document::CommandResult::fail("Gesture target is no longer editable");
            }
        if (delta.tx == 0 && delta.ty == 0) {
            ids_.clear();
            return document::CommandResult::ok();
        }
        auto command = std::make_unique<document::CompositeCommand>("Move Objects");
        for (auto id : ids_)
            command->add_command(std::make_unique<document::TransformObjectCommand>(id, delta));
        ids_.clear();
        return doc.execute_command(std::move(command));
    }
    void cancel() noexcept {
        active_ = false;
        ids_.clear();
    }

  private:
    bool active_{false};
    geometry::Point2D start_, current_;
    std::vector<document::ObjectId> ids_;
    GestureStamp stamp_;
};
} // namespace pikdraw::vector
