#include "snap_status_controller.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pikdraw::desktop {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kDegToRad = kPi / 180.0;

} // namespace

SnapStatusController::SnapStatusController() = default;

void SnapStatusController::set_dwell_time_ms(uint32_t dwell_ms) noexcept {
    dwell_time_ms_ = std::clamp(dwell_ms, 100u, 1000u);
}

uint32_t SnapStatusController::dwell_time_ms() const noexcept {
    return dwell_time_ms_;
}

void SnapStatusController::set_max_acquired_points(size_t max_points) noexcept {
    max_acquired_points_ = std::clamp(max_points, size_t{1}, size_t{8});
}

size_t SnapStatusController::max_acquired_points() const noexcept {
    return max_acquired_points_;
}

void SnapStatusController::set_direction_mode(vector::precision::snap::DirectionMode mode) noexcept {
    direction_mode_ = mode;
}

void SnapStatusController::set_polar_step_degrees(double step_deg) noexcept {
    if (step_deg == 15.0 || step_deg == 30.0 || step_deg == 45.0 || step_deg == 90.0) {
        polar_step_degrees_ = step_deg;
    }
}

SnapInferenceResult SnapStatusController::update(
    geometry::Point2D pointer_world,
    const std::optional<vector::precision::snap::SnapCandidate>& hover_candidate,
    std::chrono::steady_clock::time_point now) {
    SnapInferenceResult result;

    if (is_locked_ && locked_ray_.has_value()) {
        const auto& ray = *locked_ray_;
        const auto v = pointer_world - ray.origin;
        const double t = std::max(0.0, v.x * ray.direction.x + v.y * ray.direction.y);
        const geometry::Point2D proj{ray.origin.x + ray.direction.x * t,
                                     ray.origin.y + ray.direction.y * t};
        state_ = InferenceState::Locked;
        result.state = state_;
        result.active_ray = ray;
        result.inference_point = proj;
        result.status_hint = "Locked on ray " + std::to_string(static_cast<int>(ray.angle_deg)) + "°";
        return result;
    }

    if (hover_candidate.has_value()) {
        if (pending_candidate_key_ == hover_candidate->key &&
            pointer_world.distance_to(pending_candidate_pos_) <= 10.0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - dwell_start_).count();
            if (elapsed >= static_cast<int64_t>(dwell_time_ms_)) {
                bool already_acquired = false;
                for (const auto& ap : acquired_points_) {
                    if (ap.key == hover_candidate->key) {
                        already_acquired = true;
                        break;
                    }
                }
                if (!already_acquired) {
                    if (acquired_points_.size() >= max_acquired_points_) {
                        acquired_points_.pop_front();
                    }
                    acquired_points_.push_back({hover_candidate->key, hover_candidate->world_point, now});
                }
                state_ = InferenceState::Acquired;
                result.status_hint = "Point acquired";
            } else {
                state_ = InferenceState::HoverCandidate;
                result.status_hint = "Hovering candidate";
            }
        } else {
            pending_candidate_key_ = hover_candidate->key;
            pending_candidate_pos_ = hover_candidate->world_point;
            dwell_start_ = now;
            state_ = InferenceState::HoverCandidate;
            result.status_hint = "Hovering candidate";
        }
        result.active_snap = hover_candidate;
        result.state = state_;
        return result;
    }

    // No direct candidate: evaluate tracking rays
    pending_candidate_key_ = std::nullopt;
    geometry::Point2D proj_point{};
    auto ray = find_best_tracking_ray(pointer_world, proj_point);
    if (ray.has_value()) {
        state_ = InferenceState::Tracking;
        active_ray_ = ray;
        result.active_ray = ray;
        result.inference_point = proj_point;
        result.status_hint = "Tracking ray " + std::to_string(static_cast<int>(ray->angle_deg)) + "°";
    } else {
        state_ = InferenceState::Idle;
        active_ray_ = std::nullopt;
    }
    result.state = state_;
    return result;
}

std::optional<TrackingRay> SnapStatusController::find_best_tracking_ray(
    geometry::Point2D pointer_world, geometry::Point2D& projected_point) const {
    if (acquired_points_.empty()) return std::nullopt;

    std::vector<double> angles;
    if (direction_mode_ == vector::precision::snap::DirectionMode::Ortho) {
        angles = {0.0, 90.0, 180.0, 270.0};
    } else {
        const double step = polar_step_degrees_;
        const size_t count = static_cast<size_t>(360.0 / step);
        angles.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            angles.push_back(static_cast<double>(i) * step);
        }
    }

    double best_dist = std::numeric_limits<double>::infinity();
    std::optional<TrackingRay> best_ray;
    geometry::Point2D best_proj{};

    constexpr double kMinRayLengthMm = 5.0;
    constexpr double kRayApertureMm = 6.0;

    for (const auto& pt : acquired_points_) {
        for (double angle : angles) {
            const double rad = angle * kDegToRad;
            const geometry::Point2D dir{std::cos(rad), std::sin(rad)};
            const auto v = pointer_world - pt.world_point;
            const double t = v.x * dir.x + v.y * dir.y;
            if (t >= kMinRayLengthMm) {
                const geometry::Point2D proj{pt.world_point.x + dir.x * t,
                                             pt.world_point.y + dir.y * t};
                const double dist = pointer_world.distance_to(proj);
                if (dist <= kRayApertureMm && dist < best_dist) {
                    best_dist = dist;
                    best_proj = proj;
                    best_ray = TrackingRay{pt.world_point, dir, angle};
                }
            }
        }
    }

    if (best_ray.has_value()) {
        projected_point = best_proj;
        return best_ray;
    }
    return std::nullopt;
}

bool SnapStatusController::handle_escape() noexcept {
    if (is_locked_) {
        unlock();
        return true;
    }
    if (!acquired_points_.empty() || state_ == InferenceState::Tracking) {
        clear_tracking();
        return true;
    }
    return false;
}

bool SnapStatusController::lock_current() noexcept {
    if (active_ray_.has_value()) {
        locked_ray_ = active_ray_;
        is_locked_ = true;
        state_ = InferenceState::Locked;
        return true;
    }
    return false;
}

void SnapStatusController::unlock() noexcept {
    is_locked_ = false;
    locked_ray_ = std::nullopt;
    state_ = InferenceState::Idle;
}

void SnapStatusController::clear_tracking() noexcept {
    acquired_points_.clear();
    active_ray_ = std::nullopt;
    locked_ray_ = std::nullopt;
    is_locked_ = false;
    state_ = InferenceState::Idle;
}

void SnapStatusController::reset() noexcept {
    clear_tracking();
    pending_candidate_key_ = std::nullopt;
}

InferenceState SnapStatusController::state() const noexcept {
    return state_;
}

const std::deque<AcquiredPoint>& SnapStatusController::acquired_points() const noexcept {
    return acquired_points_;
}

bool SnapStatusController::is_locked() const noexcept {
    return is_locked_;
}

} // namespace pikdraw::desktop
