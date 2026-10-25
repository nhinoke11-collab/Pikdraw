#pragma once
#include "pikdraw/document/model/object_id.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include <span>
#include <algorithm>
#include <optional>
#include <unordered_set>
#include <vector>
namespace pikdraw::vector {
enum class SelectionMode { Replace, Toggle, Add };
enum class RegionMode { Window, Crossing };
enum class HitQuality { SourceLines, CurveApproximation, TextApproximation, ImageBounds };
struct HitResult {
    document::ObjectId id;
    HitQuality quality;
};
struct HitDiagnostics {
    size_t unsupported_objects{0};
    size_t approximation_budget_exceeded{0};
};
class SelectionModel {
  public:
    bool empty() const noexcept {
        return selected_ids_.empty();
    }
    size_t count() const noexcept {
        return selected_ids_.size();
    }
    const std::vector<document::ObjectId> &selected_ids() const noexcept {
        return selected_ids_;
    }
    bool is_selected(document::ObjectId id) const noexcept {
        return id_set_.contains(id);
    }
    void select(document::ObjectId id, SelectionMode mode = SelectionMode::Replace) {
        if (id == document::kInvalidObjectId)
            return;
        if (mode == SelectionMode::Replace)
            clear();
        if (mode == SelectionMode::Toggle && id_set_.erase(id)) {
            selected_ids_.erase(std::remove(selected_ids_.begin(), selected_ids_.end(), id),
                                selected_ids_.end());
        } else if (id_set_.insert(id).second)
            selected_ids_.push_back(id);
    }
    void clear() noexcept {
        selected_ids_.clear();
        id_set_.clear();
    }
    [[nodiscard]] std::optional<geometry::Rect2D> bounds(const document::DocumentSnapshot &snapshot) const;
    // Window contains the complete painted bounds. Crossing intersects source geometry,
    // including fill winding and stroke; curves use the same bounded hit approximation.
    static std::vector<document::ObjectId> query_region(const geometry::Rect2D &region, RegionMode mode,
                                                        const document::DocumentSnapshot &snapshot);
    // Fill winding and line geometry; curves approximated <=0.01mm in world space.
    // Text uses explicitly approximate logical bounds until shaping is integrated.
    static std::optional<HitResult> hit_test_detail(geometry::Point2D pt_mm,
                                                    const document::DocumentSnapshot &snapshot,
                                                    double tolerance_mm = 2.0,
                                                    bool allow_approximate_text = true,
                                                    HitDiagnostics *diagnostics = nullptr);
    // Color sampling follows visible painter order and includes locked objects,
    // since locking prevents edits but does not hide pixels from the canvas.
    static std::optional<HitResult> hit_test_visible_detail(
        geometry::Point2D pt_mm, const document::DocumentSnapshot &snapshot,
        double tolerance_mm = 2.0, bool allow_approximate_text = true,
        HitDiagnostics *diagnostics = nullptr);
    static std::optional<document::ObjectId> hit_test_in_scope(
        geometry::Point2D point, const document::DocumentSnapshot& snapshot,
        document::ObjectId scope, double tolerance_mm = 2.0, bool frame_only = false);
    static std::vector<document::ObjectId> scope_roots(
        const document::DocumentSnapshot& snapshot, document::ObjectId scope, bool frame_only = false);
    static std::vector<document::ObjectId> query_region_in_scope(
        const geometry::Rect2D& region, RegionMode mode,
        const document::DocumentSnapshot& snapshot, document::ObjectId scope);
    static std::optional<document::ObjectId>
    hit_test(geometry::Point2D pt_mm, const document::DocumentSnapshot &snapshot, double tolerance_mm = 2.0) {
        const auto result = hit_test_detail(pt_mm, snapshot, tolerance_mm);
        return result ? std::optional(result->id) : std::nullopt;
    }

  public:
    // Candidates are validated against the snapshot; callers supply bottom-to-top painter order.
    [[nodiscard]] static std::vector<document::ObjectId>
    query_region_candidates(const geometry::Rect2D &region, RegionMode mode,
                            const document::DocumentSnapshot &snapshot,
                            std::span<const document::ObjectId> candidates);
    [[nodiscard]] static std::optional<HitResult>
    hit_test_detail_candidates(geometry::Point2D pt_mm,
                               const document::DocumentSnapshot &snapshot,
                               std::span<const document::ObjectId> candidates,
                               double tolerance_mm = 2.0,
                               bool allow_approximate_text = true,
                               HitDiagnostics *diagnostics = nullptr);

  private:
    std::vector<document::ObjectId> selected_ids_;
    std::unordered_set<document::ObjectId> id_set_;
};
} // namespace pikdraw::vector
