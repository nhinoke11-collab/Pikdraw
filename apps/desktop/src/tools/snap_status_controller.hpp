#pragma once

#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/vector/precision/snap/snap_config.hpp"
#include "pikdraw/vector/precision/snap/snap_index.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace pikdraw::desktop {

enum class InferenceState : uint8_t {
    Idle,
    HoverCandidate,
    Acquired,
    Tracking,
    Locked
};

struct AcquiredPoint {
    vector::precision::snap::SnapCandidateKey key;
    geometry::Point2D world_point;
    std::chrono::steady_clock::time_point acquired_at;
};

struct TrackingRay {
    geometry::Point2D origin;
    geometry::Point2D direction; // normalized
    double angle_deg{0.0};
};

struct SnapInferenceResult {
    InferenceState state{InferenceState::Idle};
    std::optional<vector::precision::snap::SnapCandidate> active_snap;
    std::optional<geometry::Point2D> inference_point;
    std::optional<TrackingRay> active_ray;
    std::string status_hint;
};

class SnapStatusController {
public:
    SnapStatusController();

    void set_dwell_time_ms(uint32_t dwell_ms) noexcept;
    [[nodiscard]] uint32_t dwell_time_ms() const noexcept;

    void set_max_acquired_points(size_t max_points) noexcept;
    [[nodiscard]] size_t max_acquired_points() const noexcept;

    void set_direction_mode(vector::precision::snap::DirectionMode mode) noexcept;
    void set_polar_step_degrees(double step_deg) noexcept;

    SnapInferenceResult update(
        geometry::Point2D pointer_world,
        const std::optional<vector::precision::snap::SnapCandidate>& hover_candidate,
        std::chrono::steady_clock::time_point now);

    bool handle_escape() noexcept;
    bool lock_current() noexcept;
    void unlock() noexcept;
    void clear_tracking() noexcept;
    void reset() noexcept;

    [[nodiscard]] InferenceState state() const noexcept;
    [[nodiscard]] const std::deque<AcquiredPoint>& acquired_points() const noexcept;
    [[nodiscard]] bool is_locked() const noexcept;

private:
    std::optional<TrackingRay> find_best_tracking_ray(
        geometry::Point2D pointer_world, geometry::Point2D& projected_point) const;

    uint32_t dwell_time_ms_{300};
    size_t max_acquired_points_{4};
    vector::precision::snap::DirectionMode direction_mode_{vector::precision::snap::DirectionMode::Free};
    double polar_step_degrees_{45.0};

    InferenceState state_{InferenceState::Idle};
    std::deque<AcquiredPoint> acquired_points_;
    std::optional<vector::precision::snap::SnapCandidateKey> pending_candidate_key_;
    geometry::Point2D pending_candidate_pos_{};
    std::chrono::steady_clock::time_point dwell_start_{};
    std::optional<TrackingRay> active_ray_;
    std::optional<TrackingRay> locked_ray_;
    bool is_locked_{false};
};

} // namespace pikdraw::desktop
