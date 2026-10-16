#include "../../test_support.hpp"
#include "pikdraw/vector/precision/snap/direction_snap.hpp"

#include <cmath>

using namespace pikdraw;
using namespace pikdraw::vector::precision::snap;

namespace {

SnapCameraTransform camera(double pixels_per_mm, double pixels_per_dip,
                           uint64_t generation = 1) {
    return {geometry::Transform2D::scale(pixels_per_mm, pixels_per_mm),
            pixels_per_dip, pixels_per_dip, generation};
}

void run_direction_snap_tests() {
    SnapCapabilities capabilities;
    auto view = camera(2.0, 2.0, 7);
    SnapConfiguration configuration;
    CHECK(configuration.set_angle_reference(AngleReference::WorldX));
    CHECK(configuration.set_direction_mode(DirectionMode::Ortho));
    CHECK(configuration.set_master_enabled(false));
    auto config = configuration.snapshot();
    SnapQueryStamp stamp{"direction", 1, 1, view.generation, config.generation,
                         capabilities.generation, "pen", 1};
    const double rad_89_9 = 89.9 * 3.14159265358979323846 / 180.0;
    auto ortho = constrain_direction(stamp, {0.0, 0.0},
        {20.0 * std::cos(rad_89_9), 20.0 * std::sin(rad_89_9)}, 0.0,
        view, config);
    CHECK(ortho.constrained);
    CHECK(near(ortho.world_angle_degrees, 90.0));

    const SnapCameraTransform sheared_view{
        geometry::Transform2D{1.0, 0.0, 2.0, 1.0, 0.0, 0.0}, 1.0, 1.0, 8};
    CHECK(sheared_view.valid());
    SnapConfiguration sheared_configuration;
    CHECK(sheared_configuration.set_angle_reference(AngleReference::WorldX));
    CHECK(sheared_configuration.set_direction_mode(DirectionMode::Ortho));
    const auto sheared_config = sheared_configuration.snapshot();
    const SnapQueryStamp sheared_stamp{
        "direction", 1, 1, sheared_view.generation, sheared_config.generation,
        capabilities.generation, "pen", 1};
    const auto sheared_ortho = constrain_direction(
        sheared_stamp, {0.0, 0.0}, {1.0, -0.9}, 0.0, sheared_view, sheared_config);
    CHECK(sheared_ortho.status == SnapQueryStatus::Success);
    CHECK(sheared_ortho.constrained);
    CHECK(near(sheared_ortho.world_angle_degrees, 0.0));
    CHECK(near(sheared_ortho.world_point.x, 1.0));
    CHECK(near(sheared_ortho.world_point.y, 0.0));

    CHECK(configuration.set_master_enabled(true));
    CHECK(configuration.toggle_direction_mode(DirectionMode::Ortho));
    CHECK(configuration.set_direction_mode(DirectionMode::Polar));
    CHECK(configuration.set_polar_preset(45.0));
    config = configuration.snapshot();
    const auto angle_stamp = [&](uint64_t generation) {
        return SnapQueryStamp{"direction", 1, 1, view.generation, config.generation,
                              capabilities.generation, "pen", generation};
    };
    const auto point_at = [](double length, double degrees) {
        const double radians = degrees * 3.14159265358979323846 / 180.0;
        return geometry::Point2D{length * std::cos(radians), length * std::sin(radians)};
    };
    auto polar = constrain_direction(angle_stamp(2), {0.0, 0.0}, point_at(20.0, 44.9),
                                     0.0, view, config);
    CHECK(polar.constrained);
    CHECK(near(polar.world_angle_degrees, 45.0));
    auto tie_low = constrain_direction(angle_stamp(3), {0.0, 0.0}, point_at(20.0, 22.5),
                                        0.0, view, config);
    CHECK(near(tie_low.world_angle_degrees, 0.0));
    CHECK(!tie_low.constrained); // 7.65 DIP is beyond the soft acquire threshold.
    CHECK(polar.next_lock.has_value());
    auto tie_keep = constrain_direction(angle_stamp(3), {0.0, 0.0}, point_at(20.0, 22.5),
        0.0, view, config, polar.next_lock);
    CHECK(tie_keep.constrained);
    CHECK(near(tie_keep.world_angle_degrees, 45.0));

    const double outside_angle = std::asin(6.1 / 20.0) * 180.0 / 3.14159265358979323846;
    auto outside = constrain_direction(angle_stamp(4), {0.0, 0.0},
        point_at(20.0, outside_angle), 0.0, view, config);
    CHECK(!outside.constrained);
    CHECK(near(outside.perpendicular_distance_dip, 6.1));
    auto short_ray = constrain_direction(angle_stamp(5), {0.0, 0.0}, point_at(15.9, 0.0),
                                         0.0, view, config);
    CHECK(!short_ray.constrained);
    auto initial_lock = constrain_direction(angle_stamp(6), {0.0, 0.0}, point_at(20.0, 0.0),
                                            0.0, view, config);
    CHECK(initial_lock.next_lock.has_value());
    auto retained = constrain_direction(angle_stamp(7), {0.0, 0.0}, point_at(25.0, 21.1),
        0.0, view, config, initial_lock.next_lock);
    CHECK(retained.constrained);
    CHECK(near(retained.world_angle_degrees, 0.0));
    auto released = constrain_direction(angle_stamp(8), {0.0, 0.0}, point_at(25.0, 26.1),
        0.0, view, config, retained.next_lock);
    CHECK(!released.constrained);

    const auto unit_view = camera(1.0, 1.0, 8);
    const auto unit_stamp = [&](uint64_t generation) {
        return SnapQueryStamp{"direction", 1, 1, unit_view.generation, config.generation,
                              capabilities.generation, "pen", generation};
    };
    const auto horizontal_lock = constrain_direction(
        unit_stamp(9), {0.0, 0.0}, {100.0, 0.0}, 0.0, unit_view, config);
    CHECK(horizontal_lock.constrained);
    CHECK(near(horizontal_lock.world_angle_degrees, 0.0));
    CHECK(horizontal_lock.next_lock.has_value());
    const auto diagonal_reacquisition = constrain_direction(
        unit_stamp(10), {0.0, 0.0}, {100.0, 100.0}, 0.0, unit_view, config,
        horizontal_lock.next_lock);
    CHECK(diagonal_reacquisition.constrained);
    CHECK(near(diagonal_reacquisition.world_angle_degrees, 45.0));
    CHECK(diagonal_reacquisition.next_lock.has_value());
    CHECK(near(diagonal_reacquisition.perpendicular_distance_dip, 0.0));
    const auto reverse_ray_reacquisition = constrain_direction(
        unit_stamp(12), {0.0, 0.0}, {-100.0, 0.0}, 0.0, unit_view, config,
        horizontal_lock.next_lock);
    CHECK(reverse_ray_reacquisition.constrained);
    CHECK(near(reverse_ray_reacquisition.world_angle_degrees, 180.0));
    CHECK(near(reverse_ray_reacquisition.world_point.x, -100.0));
    CHECK(near(reverse_ray_reacquisition.world_point.y, 0.0));

    SnapConfiguration overflow_configuration;
    CHECK(overflow_configuration.set_direction_mode(DirectionMode::Polar));
    CHECK(overflow_configuration.set_custom_polar_angles({45.0}));
    const auto overflow_config = overflow_configuration.snapshot();
    const SnapCameraTransform compressed_view{
        geometry::Transform2D{1.0, 0.0, 0.0, 1e-308, 0.0, 0.0}, 1.0, 1.0, 9};
    CHECK(compressed_view.valid());
    const SnapQueryStamp overflow_stamp{
        "direction", 1, 1, compressed_view.generation, overflow_config.generation,
        capabilities.generation, "pen", 11};
    const auto overflow_projection = constrain_direction(
        overflow_stamp, {1.5e308, 0.0}, {1.7e308, 1.2e308}, 0.0,
        compressed_view, overflow_config);
    CHECK(overflow_projection.status == SnapQueryStatus::InvalidInput);
    CHECK(!overflow_projection.constrained);

    SnapConfiguration large_metric_configuration;
    CHECK(large_metric_configuration.set_direction_mode(DirectionMode::Polar));
    CHECK(large_metric_configuration.set_custom_polar_angles({45.0}));
    const auto large_metric_config = large_metric_configuration.snapshot();
    const auto large_metric_view = camera(1e100, 1.0, 10);
    CHECK(large_metric_view.valid());
    const SnapQueryStamp large_metric_stamp{
        "direction", 1, 1, large_metric_view.generation,
        large_metric_config.generation, capabilities.generation, "pen", 1};
    const auto large_metric = constrain_direction(
        large_metric_stamp, {0.0, 0.0}, {1e200, 1e200}, 0.0,
        large_metric_view, large_metric_config);
    CHECK(large_metric.status == SnapQueryStatus::Success);
    CHECK(std::isfinite(large_metric.perpendicular_distance_dip));
    if (large_metric.perpendicular_distance_dip > 6.0)
        CHECK(!large_metric.constrained);

    SnapConfiguration exact_axis_configuration;
    CHECK(exact_axis_configuration.set_direction_mode(DirectionMode::Polar));
    CHECK(exact_axis_configuration.set_custom_polar_angles({0.0}));
    const auto exact_axis_config = exact_axis_configuration.snapshot();
    const SnapQueryStamp exact_axis_stamp{
        "direction", 1, 1, large_metric_view.generation, exact_axis_config.generation,
        capabilities.generation, "pen", 2};
    const auto exact_axis = constrain_direction(
        exact_axis_stamp, {0.0, 0.0}, {1e200, 0.0}, 0.0,
        large_metric_view, exact_axis_config);
    CHECK(exact_axis.status == SnapQueryStatus::Success);
    CHECK(exact_axis.constrained);
    CHECK(exact_axis.world_angle_degrees == 0.0);
    CHECK(exact_axis.perpendicular_distance_dip == 0.0);
}
} // namespace

int main() {
    return run([] { run_direction_snap_tests(); });
}
