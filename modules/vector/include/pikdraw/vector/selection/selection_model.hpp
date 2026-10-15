#pragma once

#include "pikdraw/document/model/object_id.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include <vector>
#include <unordered_set>
#include <optional>
#include <algorithm>

namespace pikdraw::vector {

enum class SelectionMode {
    Replace,
    Toggle,
    Add
};

/// Tracks selected document objects in model space without UI dependencies
class SelectionModel {
public:
    SelectionModel() = default;

    [[nodiscard]] bool empty() const noexcept { return selected_ids_.empty(); }
    [[nodiscard]] size_t count() const noexcept { return selected_ids_.size(); }
    [[nodiscard]] const std::vector<document::ObjectId>& selected_ids() const noexcept { return selected_ids_; }

    [[nodiscard]] bool is_selected(document::ObjectId id) const noexcept {
        return id_set_.find(id) != id_set_.end();
    }

    void select(document::ObjectId id, SelectionMode mode = SelectionMode::Replace) {
        if (id == document::kInvalidObjectId) return;

        if (mode == SelectionMode::Replace) {
            clear();
            selected_ids_.push_back(id);
            id_set_.insert(id);
        } else if (mode == SelectionMode::Add) {
            if (id_set_.find(id) == id_set_.end()) {
                selected_ids_.push_back(id);
                id_set_.insert(id);
            }
        } else if (mode == SelectionMode::Toggle) {
            auto it = id_set_.find(id);
            if (it != id_set_.end()) {
                id_set_.erase(it);
                auto vec_it = std::find(selected_ids_.begin(), selected_ids_.end(), id);
                if (vec_it != selected_ids_.end()) selected_ids_.erase(vec_it);
            } else {
                selected_ids_.push_back(id);
                id_set_.insert(id);
            }
        }
    }

    void clear() noexcept {
        selected_ids_.clear();
        id_set_.clear();
    }

    /// Hit-tests world mm point against snapshot objects in top-to-bottom compositing order
    [[nodiscard]] static std::optional<document::ObjectId> hit_test(
        const geometry::Point2D& pt_mm,
        const document::DocumentSnapshot& snapshot,
        double tolerance_mm = 2.0) {
        const auto& all = snapshot.objects().all();
        // Check in reverse (topmost first)
        for (auto it = all.rbegin(); it != all.rend(); ++it) {
            const auto& [id, obj_var] = *it;
            bool hit = false;
            std::visit([&](const auto& obj) {
                using T = std::decay_t<decltype(obj)>;
                if (!obj.visible) return;
                geometry::Rect2D bounds;
                if constexpr (std::is_same_v<T, document::PathObject> || std::is_same_v<T, document::TextObject>) {
                    bounds = obj.bounds();
                } else {
                    return;
                }
                // Expand bounds by tolerance
                geometry::Rect2D hit_box{
                    bounds.x - tolerance_mm,
                    bounds.y - tolerance_mm,
                    bounds.width + 2.0 * tolerance_mm,
                    bounds.height + 2.0 * tolerance_mm
                };
                if (hit_box.contains(pt_mm)) {
                    hit = true;
                }
            }, obj_var);

            if (hit) {
                return id;
            }
        }
        return std::nullopt;
    }

private:
    std::vector<document::ObjectId> selected_ids_;
    std::unordered_set<document::ObjectId> id_set_;
};

} // namespace pikdraw::vector
