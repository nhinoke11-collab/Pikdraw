#include "pikdraw/vector/precision/snap/direction_snap.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace pikdraw::vector::precision::snap {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kDegreesToRadians = kPi / 180.0;
constexpr double kRadiansToDegrees = 180.0 / kPi;

bool finite_point(geometry::Point2D point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

bool same_context(const SnapQueryStamp& first, const SnapQueryStamp& second) noexcept {
    return first.document_id == second.document_id &&
           first.document_epoch == second.document_epoch &&
           first.document_revision == second.document_revision &&
           first.camera_generation == second.camera_generation &&
           first.config_generation == second.config_generation &&
           first.capability_generation == second.capability_generation &&
           first.tool_context == second.tool_context &&
           first.query_generation < second.query_generation;
}

double circular_distance(double first, double second) noexcept {
    const double direct = std::abs(normalize_angle_degrees(first) -
                                   normalize_angle_degrees(second));
    return std::min(direct, 360.0 - direct);
}

std::vector<double> ray_angles(const SnapConfigSnapshot& config, double reference) {
    std::vector<double> angles;
    if (config.direction.mode == DirectionMode::Ortho) {
        angles = {0.0, 90.0, 180.0, 270.0};
    } else if (config.direction.polar.kind == PolarAngleKind::PresetStep) {
        const double step = config.direction.polar.step_degrees;
        if (!is_valid_preset_step(step)) return {};
        for (double angle = 0.0; angle < 360.0; angle += step)
            angles.push_back(angle);
    } else {
        if (validate_custom_angles(config.direction.polar.custom_angles_degrees)) return {};
        angles = config.direction.polar.custom_angles_degrees;
    }
    for (double& angle : angles)
        angle = normalize_angle_degrees(reference + angle);
    std::sort(angles.begin(), angles.end());
    angles.erase(std::unique(angles.begin(), angles.end()), angles.end());
    return angles;
}

double perpendicular_distance(geometry::Point2D delta_dip,
                              geometry::Point2D ray_dip) noexcept {
    const double ray_length = std::hypot(ray_dip.x, ray_dip.y);
    if (!std::isfinite(ray_length) || ray_length == 0.0)
        return std::numeric_limits<double>::infinity();
    const double delta_scale = std::max(std::abs(delta_dip.x), std::abs(delta_dip.y));
    if (delta_scale == 0.0) return 0.0;

    const geometry::Point2D unit_ray{ray_dip.x / ray_length, ray_dip.y / ray_length};
    const double scaled_cross = std::fma(delta_dip.x / delta_scale, unit_ray.y,
        -(delta_dip.y / delta_scale) * unit_ray.x);
    const double distance = std::abs(scaled_cross) * delta_scale;
    return std::isfinite(distance) ? distance : std::numeric_limits<double>::infinity();
}

enum class RayProjectionStatus : uint8_t { Projected, BehindRay, Invalid };

RayProjectionStatus project_to_ray(geometry::Point2D anchor_world,
                                   geometry::Point2D pointer_world,
                                   double angle_degrees,
                                   geometry::Point2D& projected_world) noexcept {
    const double radians = angle_degrees * kDegreesToRadians;
    const geometry::Point2D direction{std::cos(radians), std::sin(radians)};
    const geometry::Point2D delta_world = pointer_world - anchor_world;
    const double denominator = direction.x * direction.x + direction.y * direction.y;
    if (!std::isfinite(denominator) || denominator == 0.0)
        return RayProjectionStatus::Invalid;
    const double scalar = (delta_world.x * direction.x +
                           delta_world.y * direction.y) / denominator;
    if (!std::isfinite(scalar)) return RayProjectionStatus::Invalid;
    if (scalar < 0.0) return RayProjectionStatus::BehindRay;
    projected_world = anchor_world + direction * scalar;
    return finite_point(projected_world) ? RayProjectionStatus::Projected
                                         : RayProjectionStatus::Invalid;
}

std::optional<double> nearest_ray(const std::vector<double>& rays, double input_angle,
                                  const std::optional<DirectionLockState>& previous,
                                  const SnapQueryStamp& stamp,
                                  geometry::Point2D anchor) {
    if (rays.empty()) return std::nullopt;
    double best_distance = std::numeric_limits<double>::infinity();
    std::vector<double> tied;
    for (const double ray : rays) {
        const double distance = circular_distance(input_angle, ray);
        if (distance < best_distance) {
            best_distance = distance;
            tied.assign(1, ray);
        } else if (distance == best_distance) {
            tied.push_back(ray);
        }
    }
    if (previous && same_context(previous->stamp, stamp) &&
        previous->anchor_world == anchor) {
        const double prior = normalize_angle_degrees(previous->world_angle_degrees);
        if (std::find(tied.begin(), tied.end(), prior) != tied.end()) return prior;
    }
    return *std::min_element(tied.begin(), tied.end());
}

} // namespace

DirectionSnapResult constrain_direction(
    const SnapQueryStamp& stamp, geometry::Point2D anchor_world,
    geometry::Point2D pointer_world, double reference_angle_degrees,
    const SnapCameraTransform& camera, const SnapConfigSnapshot& config,
    const std::optional<DirectionLockState>& previous_lock) {
    DirectionSnapResult result;
    result.stamp = stamp;
    result.mode = config.direction.mode;
    result.world_point = pointer_world;
    if (stamp.config_generation != config.generation) {
        result.status = SnapQueryStatus::StaleConfig;
        return result;
    }
    if (stamp.camera_generation != camera.generation || !camera.valid()) {
        result.status = SnapQueryStatus::StaleCamera;
        return result;
    }
    if (stamp.tool_context.empty() || !finite_point(anchor_world) ||
        !finite_point(pointer_world)) {
        result.status = SnapQueryStatus::InvalidInput;
        return result;
    }
    if (config.direction.mode == DirectionMode::Free) return result;
    if (config.direction.mode > DirectionMode::Polar ||
        config.direction.reference > AngleReference::PreviousSegment ||
        !std::isfinite(reference_angle_degrees)) {
        result.status = SnapQueryStatus::InvalidInput;
        return result;
    }

    const double reference = config.direction.reference == AngleReference::WorldX
        ? 0.0 : normalize_angle_degrees(reference_angle_degrees);
    const geometry::Point2D delta_world = pointer_world - anchor_world;
    const auto anchor_dip = camera.world_to_dip(anchor_world);
    const auto pointer_dip = camera.world_to_dip(pointer_world);
    const geometry::Point2D delta_dip = pointer_dip - anchor_dip;
    result.ray_length_dip = std::hypot(delta_dip.x, delta_dip.y);
    if (!finite_point(delta_world) || !finite_point(delta_dip) ||
        !std::isfinite(result.ray_length_dip)) {
        result.status = SnapQueryStatus::InvalidInput;
        return result;
    }
    if (delta_world.x == 0.0 && delta_world.y == 0.0) return result;

    const double input_angle = std::atan2(delta_world.y, delta_world.x) * kRadiansToDegrees;
    const auto rays = ray_angles(config, reference);
    if (rays.empty()) {
        result.status = SnapQueryStatus::InvalidInput;
        return result;
    }
    const auto angle = nearest_ray(rays, input_angle, previous_lock, stamp, anchor_world);
    if (!angle) {
        result.status = SnapQueryStatus::InvalidInput;
        return result;
    }
    result.world_angle_degrees = *angle;
    const double radians = *angle * kDegreesToRadians;
    const auto ray_dip = camera.world_vector_to_dip({std::cos(radians), std::sin(radians)});
    result.perpendicular_distance_dip = perpendicular_distance(delta_dip, ray_dip);
    if (!std::isfinite(result.perpendicular_distance_dip)) {
        result.status = SnapQueryStatus::InvalidInput;
        return result;
    }

    if (config.direction.mode == DirectionMode::Polar) {
        bool retained = false;
        if (previous_lock && same_context(previous_lock->stamp, stamp) &&
            previous_lock->anchor_world == anchor_world &&
            result.ray_length_dip >= kPolarMinimumRayLengthDip) {
            const double prior = normalize_angle_degrees(previous_lock->world_angle_degrees);
            if (std::find(rays.begin(), rays.end(), prior) != rays.end()) {
                const auto prior_radians = prior * kDegreesToRadians;
                const auto prior_vector = camera.world_vector_to_dip(
                    {std::cos(prior_radians), std::sin(prior_radians)});
                const double prior_distance = perpendicular_distance(delta_dip, prior_vector);
                if (std::isfinite(prior_distance) &&
                    prior_distance <= kPolarReleaseRadiusDip) {
                    geometry::Point2D prior_projection;
                    if (project_to_ray(anchor_world, pointer_world, prior, prior_projection) ==
                        RayProjectionStatus::Projected) {
                        result.perpendicular_distance_dip = prior_distance;
                        result.world_angle_degrees = prior;
                        result.world_point = prior_projection;
                        retained = true;
                    }
                }
            }
        }
        if (!retained) {
            if (result.ray_length_dip < kPolarMinimumRayLengthDip ||
                result.perpendicular_distance_dip > kPolarAcquireRadiusDip)
                return result;
            const auto projection = project_to_ray(anchor_world, pointer_world, *angle,
                                                   result.world_point);
            if (projection == RayProjectionStatus::Invalid) {
                result.status = SnapQueryStatus::InvalidInput;
                return result;
            }
            if (projection == RayProjectionStatus::BehindRay) return result;
            result.world_angle_degrees = *angle;
        }
    } else {
        if (project_to_ray(anchor_world, pointer_world, *angle, result.world_point) !=
            RayProjectionStatus::Projected) {
            result.status = SnapQueryStatus::InvalidInput;
            return result;
        }
        result.world_angle_degrees = *angle;
    }

    result.constrained = true;
    if (config.direction.mode == DirectionMode::Polar) {
        result.next_lock = DirectionLockState{result.world_angle_degrees, anchor_world, stamp};
    }
    return result;
}

} // namespace pikdraw::vector::precision::snap
