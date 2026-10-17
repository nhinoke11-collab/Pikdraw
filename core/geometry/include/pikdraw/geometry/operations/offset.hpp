#pragma once

#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/curves/path.hpp"
#include <vector>

namespace pikdraw::geometry {

enum class JoinType {
    Miter,
    Round,
    Bevel
};

enum class ContourDirection {
    Outside,
    Inside,
    ToCenter
};

enum class OffsetStatus {
    Success,
    Empty,
    InvalidInput,
    Unsupported,
    TopologyAmbiguous,
    WorkBudgetExceeded,
    Cancelled
};

struct OffsetOptions {
    double distance_mm{0.0};
    JoinType join_type{JoinType::Miter};
    double miter_limit{4.0};
    double arc_tolerance_mm{0.01};
};

struct OffsetResult {
    bool success{false};
    Path2D path;
    const char* error_message{nullptr};
};

/// Computes an offset / dilation of a rectangle
[[nodiscard]] Rect2D offset_rect(const Rect2D& rect, double distance_mm);

/// Computes an offset of a path (retained for backward compatibility)
[[nodiscard]] OffsetResult offset_path(const Path2D& path, const OffsetOptions& options);

struct ContourRequest {
    Path2D path;
    ContourDirection direction{ContourDirection::Outside};
    double spacing_mm{1.0};
    int steps{1}; // 1..99
    JoinType join_type{JoinType::Miter};
    double miter_limit{4.0};
    double arc_tolerance_mm{0.005};
};

struct ContourStep {
    int step_index{1};
    double distance_mm{0.0};
    std::vector<Path2D> loops;
    Path2D combined_path;
    bool is_empty{false};
};

struct ContourResult {
    OffsetStatus status{OffsetStatus::InvalidInput};
    std::vector<ContourStep> steps;
    bool truncated{false};
    const char* error_message{nullptr};
};

/// High-precision parametric contour offset engine
[[nodiscard]] ContourResult compute_contour(const ContourRequest& request);

} // namespace pikdraw::geometry
