#include "snap_math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pikdraw::vector::precision::snap {
namespace {

std::optional<geometry::Transform2D> inverse_exact(
    const geometry::Transform2D& transform) noexcept {
    const double determinant = transform.determinant();
    if (!transform.is_finite() || !std::isfinite(determinant) || determinant == 0.0)
        return std::nullopt;
    const double inverse_determinant = 1.0 / determinant;
    geometry::Transform2D result{
        transform.d * inverse_determinant,
        -transform.b * inverse_determinant,
        -transform.c * inverse_determinant,
        transform.a * inverse_determinant,
        (transform.c * transform.ty - transform.d * transform.tx) * inverse_determinant,
        (transform.b * transform.tx - transform.a * transform.ty) * inverse_determinant};
    return result.is_finite() ? std::optional{result} : std::nullopt;
}

geometry::Transform2D world_to_dip_transform(const SnapCameraTransform& camera) noexcept {
    const double inverse_x = 1.0 / camera.dpi_scale_x;
    const double inverse_y = 1.0 / camera.dpi_scale_y;
    return {camera.world_to_device_pixels.a * inverse_x,
            camera.world_to_device_pixels.b * inverse_y,
            camera.world_to_device_pixels.c * inverse_x,
            camera.world_to_device_pixels.d * inverse_y,
            camera.world_to_device_pixels.tx * inverse_x,
            camera.world_to_device_pixels.ty * inverse_y};
}

} // namespace

namespace detail {

bool finite_point(geometry::Point2D point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

double distance_dip(geometry::Point2D first, geometry::Point2D second) noexcept {
    return std::hypot(first.x - second.x, first.y - second.y);
}

std::optional<SnapWorldBounds> world_bounds_for_dip_square(
    const SnapCameraTransform& camera, geometry::Point2D center_world, double radius_dip) {
    const auto inverse = inverse_exact(world_to_dip_transform(camera));
    if (!inverse) return std::nullopt;
    const auto center = camera.world_to_dip(center_world);
    const std::array<geometry::Point2D, 4> corners{{
        {center.x - radius_dip, center.y - radius_dip},
        {center.x + radius_dip, center.y - radius_dip},
        {center.x + radius_dip, center.y + radius_dip},
        {center.x - radius_dip, center.y + radius_dip}}};
    SnapWorldBounds bounds{std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity()};
    for (const auto corner : corners) {
        const auto world = inverse->map_point(corner);
        if (!finite_point(world)) return std::nullopt;
        bounds.min_x = std::min(bounds.min_x, world.x);
        bounds.min_y = std::min(bounds.min_y, world.y);
        bounds.max_x = std::max(bounds.max_x, world.x);
        bounds.max_y = std::max(bounds.max_y, world.y);
    }
    bounds.min_x = std::nextafter(bounds.min_x, -std::numeric_limits<double>::infinity());
    bounds.min_y = std::nextafter(bounds.min_y, -std::numeric_limits<double>::infinity());
    bounds.max_x = std::nextafter(bounds.max_x, std::numeric_limits<double>::infinity());
    bounds.max_y = std::nextafter(bounds.max_y, std::numeric_limits<double>::infinity());
    return bounds;
}

} // namespace detail

bool SnapCameraTransform::valid() const noexcept {
    const double determinant = world_to_device_pixels.determinant();
    return world_to_device_pixels.is_finite() && std::isfinite(determinant) &&
           determinant != 0.0 && std::isfinite(dpi_scale_x) && dpi_scale_x > 0.0 &&
           std::isfinite(dpi_scale_y) && dpi_scale_y > 0.0;
}

geometry::Point2D SnapCameraTransform::world_to_dip(geometry::Point2D world) const noexcept {
    const auto pixel = world_to_device_pixels.map_point(world);
    return {pixel.x / dpi_scale_x, pixel.y / dpi_scale_y};
}

geometry::Point2D SnapCameraTransform::world_vector_to_dip(
    geometry::Point2D vector) const noexcept {
    const auto pixel = world_to_device_pixels.map_vector(vector);
    return {pixel.x / dpi_scale_x, pixel.y / dpi_scale_y};
}

} // namespace pikdraw::vector::precision::snap
