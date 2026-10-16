#include "../src/tools/drawing_preferences.hpp"
#include "../src/tools/drawing_tool_controller.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

#define REQUIRE(value) do { if (!(value)) \
    throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #value); } while (false)

using namespace pikdraw;

namespace {

std::filesystem::path test_path(const char* name) {
    return std::filesystem::temp_directory_path() /
        (std::string("pikdraw-") + name + "-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt");
}

void test_preferences_are_bounded_persistent_user_settings() {
    const auto path = test_path("drawing-preferences");
    desktop::tools::DrawingPreferences value;
    value.profile = "Corel";
    value.snap_master = false;
    value.endpoint = false;
    value.grid = false;
    value.direction = vector::precision::snap::DirectionMode::Polar;
    value.polar_step_degrees = 30.0;
    std::string error;
    REQUIRE(desktop::tools::save_drawing_preferences(path, value, &error));
    const auto loaded = desktop::tools::load_drawing_preferences(path);
    REQUIRE(loaded.loaded && loaded.value.profile == "Corel");
    REQUIRE(!loaded.value.snap_master && !loaded.value.endpoint && !loaded.value.grid);
    REQUIRE(loaded.value.direction == vector::precision::snap::DirectionMode::Polar);
    REQUIRE(loaded.value.polar_step_degrees == 30.0);

    vector::precision::snap::SnapConfiguration config;
    REQUIRE(desktop::tools::apply_drawing_preferences(loaded.value, config));
    const auto snapshot = config.snapshot();
    REQUIRE(!snapshot.master_enabled && !snapshot.options[static_cast<size_t>(
        vector::precision::snap::SnapOption::Endpoint)]);
    REQUIRE(snapshot.direction.mode == vector::precision::snap::DirectionMode::Polar);
    {
        std::ofstream malformed(path, std::ios::binary | std::ios::trunc);
        malformed << "pikdraw-drawing-preferences-v1\nunknown=1\n";
    }
    REQUIRE(!desktop::tools::load_drawing_preferences(path).loaded);
    std::filesystem::remove(path);
}

void test_native_controller_commits_numeric_line_and_refreshes_snap_index_async() {
    const auto path = test_path("drawing-controller");
    document::Document document("d09-controller");
    desktop::tools::DrawingToolController controller(document, path);
    rendering::Camera2D camera({0.0, 0.0}, 1.0, 640, 480);
    REQUIRE(controller.activate(vector::tools::line::PathDrawingKind::Line));
    for (const char value : std::string("#10;20"))
        REQUIRE(controller.append_numeric_character(value));
    REQUIRE(controller.enter().code == vector::tools::line::PathDrawingResultCode::Applied);
    REQUIRE(controller.visual_state().points.size() == 1);
    for (const char value : std::string("@30<90"))
        REQUIRE(controller.append_numeric_character(value));
    const auto line = controller.enter();
    REQUIRE(line.committed() && document.current_snapshot()->object_count() == 1);
    REQUIRE(controller.active());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!controller.visual_state().snap_index_ready &&
           std::chrono::steady_clock::now() < deadline) {
        // The production UI consumes worker completions from its pointer timer.
        controller.pointer_move({12.0, 18.0}, camera, 1.0, false, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto snap_state = controller.visual_state();
    if (!snap_state.snap_index_ready)
        throw std::runtime_error("Snap index did not become ready: " + snap_state.message);
    REQUIRE(snap_state.snap_kind == vector::precision::snap::SnapOption::Endpoint);
    REQUIRE(snap_state.candidate.has_value());
    const geometry::Point2D expected_endpoint{10.0, 20.0};
    REQUIRE(*snap_state.candidate == expected_endpoint);
    REQUIRE(document.current_snapshot()->object_count() == 1);
    REQUIRE(document.undo() && document.current_snapshot()->object_count() == 0);
    std::filesystem::remove(path);
}

void test_polyline_enter_commits_one_editable_path_then_escape_exits() {
    const auto path = test_path("drawing-polyline");
    document::Document document("d09-polyline");
    desktop::tools::DrawingToolController controller(document, path);
    rendering::Camera2D camera({0.0, 0.0}, 1.0, 640, 480);
    REQUIRE(controller.activate(vector::tools::line::PathDrawingKind::Polyline));
    controller.pointer_move({10, 10}, camera, 1.0, false, false);
    REQUIRE(controller.confirm_candidate().code ==
            vector::tools::line::PathDrawingResultCode::Applied);
    controller.pointer_move({30, 10}, camera, 1.0, false, false);
    REQUIRE(controller.confirm_candidate().code ==
            vector::tools::line::PathDrawingResultCode::Applied);
    controller.pointer_move({30, 25}, camera, 1.0, false, false);
    REQUIRE(controller.confirm_candidate().code ==
            vector::tools::line::PathDrawingResultCode::Applied);
    REQUIRE(controller.enter().committed());
    REQUIRE(document.current_snapshot()->object_count() == 1 && controller.active());
    REQUIRE(controller.escape().code == vector::tools::line::PathDrawingResultCode::Cancelled);
    REQUIRE(!controller.active());
    std::filesystem::remove(path);
}

} // namespace

int main() {
    try {
        test_preferences_are_bounded_persistent_user_settings();
        test_native_controller_commits_numeric_line_and_refreshes_snap_index_async();
        test_polyline_enter_commits_one_editable_path_then_escape_exits();
        std::cout << "[PASS] D09 drawing preferences, numeric authoring, async snap and Polyline lifecycle\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
