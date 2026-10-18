#include "../src/tools/drawing_preferences.hpp"
#include "../src/tools/drawing_tool_controller.hpp"
#include "../src/tools/pen_tool_controller.hpp"
#include "pikdraw/document/commands/command.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

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

document::PathObject add_resumable_path(document::Document& document,
                                        geometry::Transform2D transform) {
    document::EditableSubpath subpath;
    subpath.id = document::generate_path_element_id<document::SubpathId>();
    document::PathNode first;
    first.id = document::generate_path_element_id<document::NodeId>();
    first.anchor = {10.0, 10.0};
    document::PathNode last;
    last.id = document::generate_path_element_id<document::NodeId>();
    last.anchor = {20.0, 10.0};
    subpath.nodes = {first, last};
    document::EditableSegment segment;
    segment.id = document::generate_path_element_id<document::SegmentId>();
    segment.kind = document::PathSegmentKind::Line;
    subpath.segments.push_back(segment);

    document::EditablePath source;
    source.subpaths.push_back(subpath);
    document::PathObject object;
    object.id = document::generate_object_id();
    object.name = "Resume fixture";
    object.transform = transform;
    object.has_fill = false;
    object.has_stroke = true;
    object.stroke_width_mm = 3.5;
    object.set_authoring_source(std::move(source));
    const auto added = document.execute_command(
        std::make_unique<document::AddObjectCommand>(object));
    if (!added.success)
        throw std::runtime_error("Could not add Pen resume fixture: " +
                                 added.error_message);
    return object;
}

void place_pen_anchor(desktop::tools::PenToolController& controller,
                      geometry::Point2D world) {
    REQUIRE(controller.pointer_down(world).code ==
            vector::tools::pen::PenDrawingResultCode::PreviewUpdated);
    REQUIRE(controller.pointer_up().code ==
            vector::tools::pen::PenDrawingResultCode::Applied);
}

void require_point_near(geometry::Point2D actual, geometry::Point2D expected) {
    if (std::abs(actual.x - expected.x) > 1e-8 ||
        std::abs(actual.y - expected.y) > 1e-8)
        throw std::runtime_error("Pen candidate differs from analytic point: actual (" +
            std::to_string(actual.x) + ", " + std::to_string(actual.y) +
            "), expected (" + std::to_string(expected.x) + ", " +
            std::to_string(expected.y) + ")");
}

document::PathObject read_path(const document::Document& document,
                               document::ObjectId object_id) {
    const auto object = document.current_snapshot()->get_object(object_id);
    REQUIRE(object && std::holds_alternative<document::PathObject>(*object));
    return std::get<document::PathObject>(*object);
}

void test_pen_resumes_transformed_path_from_both_endpoints() {
    document::Document document("pen-resume-transformed");
    const geometry::Transform2D transform{2.0, 0.0, 0.0, 3.0, 100.0, 50.0};
    const auto object = add_resumable_path(document, transform);
    const auto source = object.authoring_source->subpaths.front();
    desktop::tools::PenToolController controller(document);
    const auto initial_revision = document.revision();

    REQUIRE(controller.resume(object.id, source.id, source.nodes.back().id).code ==
            vector::tools::pen::PenDrawingResultCode::Applied);
    REQUIRE(controller.pointer_down({170.0, 80.0}).code ==
            vector::tools::pen::PenDrawingResultCode::PreviewUpdated);
    REQUIRE(controller.pointer_up().code ==
            vector::tools::pen::PenDrawingResultCode::Applied);
    REQUIRE(document.revision() == initial_revision);
    const auto appended = controller.finish_open();
    REQUIRE(appended.committed() && appended.object_id == object.id);
    REQUIRE(document.revision() == initial_revision + 1);

    auto current = read_path(document, object.id);
    REQUIRE(current.name == object.name && current.stroke_width_mm == 3.5);
    REQUIRE(!current.has_fill && current.has_stroke);
    REQUIRE(current.transform.a == transform.a && current.transform.d == transform.d);
    auto subpath = current.authoring_source->subpaths.front();
    REQUIRE(subpath.nodes.size() == 3 && subpath.segments.size() == 2);
    REQUIRE(subpath.nodes[0].id == source.nodes[0].id);
    REQUIRE(subpath.nodes[1].id == source.nodes[1].id);
    const geometry::Point2D end_extension{35.0, 10.0};
    REQUIRE(subpath.nodes[2].anchor == end_extension);

    const auto before_start_resume = document.revision();
    REQUIRE(controller.resume(object.id, source.id, source.nodes.front().id).code ==
            vector::tools::pen::PenDrawingResultCode::Applied);
    REQUIRE(controller.pointer_down({90.0, 80.0}).code ==
            vector::tools::pen::PenDrawingResultCode::PreviewUpdated);
    REQUIRE(controller.pointer_up().code ==
            vector::tools::pen::PenDrawingResultCode::Applied);
    REQUIRE(controller.finish_open().committed());
    REQUIRE(document.revision() == before_start_resume + 1);
    current = read_path(document, object.id);
    subpath = current.authoring_source->subpaths.front();
    REQUIRE(subpath.nodes.size() == 4 && subpath.segments.size() == 3);
    REQUIRE(subpath.nodes[0].anchor == end_extension);
    REQUIRE(subpath.nodes[1].id == source.nodes[1].id);
    REQUIRE(subpath.nodes[2].id == source.nodes[0].id);
    const geometry::Point2D start_extension{-5.0, 10.0};
    REQUIRE(subpath.nodes[3].anchor == start_extension);
    REQUIRE(current.transform.tx == transform.tx && current.transform.ty == transform.ty);
    REQUIRE(document.undo());
    REQUIRE(read_path(document, object.id).authoring_source->subpaths.front().nodes.size() == 3);
    REQUIRE(document.undo());
    REQUIRE(read_path(document, object.id).authoring_source->subpaths.front().nodes.size() == 2);
}

void test_pen_resume_cancel_and_document_rejects_singular_transform() {
    document::Document document("pen-resume-cancel");
    const auto object = add_resumable_path(document, geometry::Transform2D::identity());
    const auto source = object.authoring_source->subpaths.front();
    desktop::tools::PenToolController controller(document);
    const auto revision = document.revision();

    REQUIRE(controller.resume(object.id, source.id, source.nodes.back().id).code ==
            vector::tools::pen::PenDrawingResultCode::Applied);
    REQUIRE(controller.pointer_down({30.0, 10.0}).code ==
            vector::tools::pen::PenDrawingResultCode::PreviewUpdated);
    REQUIRE(controller.pointer_up().code ==
            vector::tools::pen::PenDrawingResultCode::Applied);
    controller.cancel();
    REQUIRE(document.revision() == revision);
    REQUIRE(read_path(document, object.id).authoring_source->subpaths.front().nodes.size() == 2);

    document::Document singular_document("pen-resume-singular");
    document::PathObject singular;
    singular.id = document::generate_object_id();
    singular.transform = geometry::Transform2D::scale(0.0, 1.0);
    const auto rejected = singular_document.execute_command(
        std::make_unique<document::AddObjectCommand>(singular));
    REQUIRE(!rejected.success);
    REQUIRE(singular_document.current_snapshot()->object_count() == 0);
}

void test_pen_alt_splits_handles_and_profile_modifiers_lock_direction() {
    document::Document pikdraw_document("pen-modifiers-pikdraw");
    desktop::tools::PenToolController pikdraw(pikdraw_document);
    REQUIRE(pikdraw.set_gesture_preferences(
        {desktop::tools::PenShortcutProfile::PikDraw, 15.0}));
    REQUIRE(pikdraw.activate().code == vector::tools::pen::PenDrawingResultCode::Applied);
    REQUIRE(pikdraw.pointer_down({0.0, 0.0}).code ==
            vector::tools::pen::PenDrawingResultCode::PreviewUpdated);
    REQUIRE(pikdraw.pointer_move({10.0, 4.0}, true,
            {.shift = true}).code ==
            vector::tools::pen::PenDrawingResultCode::PreviewUpdated);
    const auto* symmetric_preview = pikdraw.pointer_preview()
        ? &*pikdraw.pointer_preview() : nullptr;
    REQUIRE(symmetric_preview && symmetric_preview->direction_locked);
    REQUIRE(symmetric_preview->node_mode == document::PathNodeMode::Symmetric);
    REQUIRE(symmetric_preview->incoming_handle && symmetric_preview->outgoing_handle);
    const auto symmetric_delta = *symmetric_preview->outgoing_handle;
    const double symmetric_length = std::hypot(symmetric_delta.x, symmetric_delta.y);
    REQUIRE(std::abs(symmetric_length - std::hypot(10.0, 4.0)) < 1e-8);
    REQUIRE(std::abs(std::atan2(symmetric_delta.y, symmetric_delta.x) *
                     180.0 / std::acos(-1.0) - 15.0) < 1e-8);
    REQUIRE(pikdraw.pointer_up().code ==
            vector::tools::pen::PenDrawingResultCode::Applied);
    const auto& symmetric_node = pikdraw.draft().nodes.front();
    REQUIRE(symmetric_node.mode == document::PathNodeMode::Symmetric);
    REQUIRE(symmetric_node.has_incoming_handle && symmetric_node.has_outgoing_handle);

    REQUIRE(pikdraw.pointer_down({20.0, 0.0}).code ==
            vector::tools::pen::PenDrawingResultCode::PreviewUpdated);
    REQUIRE(pikdraw.pointer_move({30.0, 4.0}, true,
            {.shift = true, .alt = true}).code ==
            vector::tools::pen::PenDrawingResultCode::PreviewUpdated);
    const auto* cusp_preview = pikdraw.pointer_preview()
        ? &*pikdraw.pointer_preview() : nullptr;
    REQUIRE(cusp_preview && cusp_preview->direction_locked);
    REQUIRE(cusp_preview->node_mode == document::PathNodeMode::Cusp);
    REQUIRE(cusp_preview->outgoing_handle && !cusp_preview->incoming_handle);
    REQUIRE(pikdraw.pointer_up().code ==
            vector::tools::pen::PenDrawingResultCode::Applied);
    const auto& cusp_node = pikdraw.draft().nodes.back();
    REQUIRE(cusp_node.mode == document::PathNodeMode::Cusp);
    REQUIRE(cusp_node.has_outgoing_handle && !cusp_node.has_incoming_handle);

    document::Document corel_document("pen-modifiers-corel");
    desktop::tools::PenToolController corel(corel_document);
    REQUIRE(corel.set_gesture_preferences(
        {desktop::tools::PenShortcutProfile::Corel, 30.0}));
    REQUIRE(!corel.set_gesture_preferences(
        {desktop::tools::PenShortcutProfile::Corel, 0.0}));
    REQUIRE(corel.activate().code == vector::tools::pen::PenDrawingResultCode::Applied);
    REQUIRE(corel.pointer_down({0.0, 0.0}).code ==
            vector::tools::pen::PenDrawingResultCode::PreviewUpdated);
    const double radians37 = 37.0 * std::acos(-1.0) / 180.0;
    const geometry::Point2D raw37{10.0 * std::cos(radians37),
                                  10.0 * std::sin(radians37)};
    REQUIRE(corel.pointer_move(raw37, true, {.shift = true}).code ==
            vector::tools::pen::PenDrawingResultCode::PreviewUpdated);
    REQUIRE(!corel.pointer_preview()->direction_locked);
    REQUIRE(corel.pointer_up().code ==
            vector::tools::pen::PenDrawingResultCode::Applied);

    REQUIRE(corel.pointer_down({20.0, 0.0}).code ==
            vector::tools::pen::PenDrawingResultCode::PreviewUpdated);
    REQUIRE(corel.pointer_move({20.0 + raw37.x, raw37.y}, true,
            {.control = true, .alt = true}).code ==
            vector::tools::pen::PenDrawingResultCode::PreviewUpdated);
    REQUIRE(corel.pointer_preview()->direction_locked);
    REQUIRE(corel.pointer_preview()->node_mode == document::PathNodeMode::Cusp);
    const auto corel_delta = *corel.pointer_preview()->outgoing_handle -
                             corel.pointer_preview()->anchor;
    REQUIRE(std::abs(std::atan2(corel_delta.y, corel_delta.x) *
                     180.0 / std::acos(-1.0) - 30.0) < 1e-8);
    REQUIRE(corel.pointer_up().code ==
            vector::tools::pen::PenDrawingResultCode::Applied);
    const auto& corel_cusp = corel.draft().nodes.back();
    REQUIRE(corel_cusp.mode == document::PathNodeMode::Cusp);
    REQUIRE(corel_cusp.has_outgoing_handle && !corel_cusp.has_incoming_handle);
}

void test_pen_recovery_checkpoint_roundtrips_history_and_base_provenance() {
    using vector::tools::pen::PenDrawingResultCode;
    const auto checkpoint_path = test_path("pen-recovery");
    const auto corrupt_path = test_path("pen-recovery-corrupt");
    const auto checkpoint_path_text = checkpoint_path.string();
    const auto corrupt_path_text = corrupt_path.string();
    constexpr uint32_t base_crc = 0xA1B2C3D4u;

    document::Document document("pen-recovery-roundtrip");
    desktop::tools::PenToolController source(document);
    REQUIRE(source.activate().code == PenDrawingResultCode::Applied);
    place_pen_anchor(source, {10.0, 10.0});
    place_pen_anchor(source, {20.0, 15.0});
    const auto second_node_id = source.draft().nodes.back().id;
    REQUIRE(source.undo_local().code == PenDrawingResultCode::Applied);
    REQUIRE(source.anchor_count() == 1);

    REQUIRE(source.write_recovery_checkpoint(checkpoint_path_text, base_crc).code ==
            PenDrawingResultCode::Applied);
    const auto original_record = document::SaveRecoveryService::load_recovery_draft(
        checkpoint_path_text);
    REQUIRE(original_record.has_value());

    document::SaveOptions injected_failure;
    injected_failure.fault = document::SaveFaultPoint::BeforeReplace;
    REQUIRE(source.write_recovery_checkpoint(checkpoint_path_text, base_crc,
                                              injected_failure).code ==
            PenDrawingResultCode::CommandFailed);
    const auto preserved_record = document::SaveRecoveryService::load_recovery_draft(
        checkpoint_path_text);
    REQUIRE(preserved_record.has_value());
    REQUIRE(preserved_record->payload == original_record->payload);

    auto corrupted_record = *original_record;
    corrupted_record.payload.front() ^= 0x20u;
    REQUIRE(document::SaveRecoveryService::atomic_write_recovery_draft(
        corrupt_path_text, corrupted_record).success);
    desktop::tools::PenToolController corrupt(document);
    REQUIRE(corrupt.stage_recovery_checkpoint(corrupt_path_text, base_crc).code ==
            PenDrawingResultCode::InvalidInput);
    REQUIRE(!corrupt.recovery_pending());

    desktop::tools::PenToolController stale(document);
    REQUIRE(stale.stage_recovery_checkpoint(checkpoint_path_text, base_crc + 1).code ==
            PenDrawingResultCode::Stale);
    REQUIRE(!stale.recovery_pending());

    desktop::tools::PenToolController restored(document);
    REQUIRE(restored.stage_recovery_checkpoint(checkpoint_path_text, base_crc).code ==
            PenDrawingResultCode::Applied);
    REQUIRE(restored.recovery_pending() && !restored.active());
    REQUIRE(restored.activate().code == PenDrawingResultCode::InvalidInput);
    REQUIRE(document.current_snapshot()->object_count() == 0);
    REQUIRE(restored.accept_recovery_checkpoint(base_crc).code ==
            PenDrawingResultCode::Applied);
    REQUIRE(restored.active() && !restored.recovery_pending());
    REQUIRE(restored.anchor_count() == 1);
    REQUIRE(restored.redo_local().code == PenDrawingResultCode::Applied);
    REQUIRE(restored.anchor_count() == 2);
    REQUIRE(restored.draft().nodes.back().id == second_node_id);
    REQUIRE(restored.finish_open().committed());
    REQUIRE(document.current_snapshot()->object_count() == 1);

    std::filesystem::remove(checkpoint_path);
    std::filesystem::remove(corrupt_path);
}

void test_pen_recovery_checkpoint_restores_resume_transform() {
    using vector::tools::pen::PenDrawingResultCode;
    const auto checkpoint_path = test_path("pen-resume-recovery");
    const auto checkpoint_path_text = checkpoint_path.string();
    constexpr uint32_t base_crc = 0x10293847u;
    document::Document document("pen-resume-recovery");
    const geometry::Transform2D transform{2.0, 0.0, 0.0, 3.0, 100.0, 50.0};
    const auto object = add_resumable_path(document, transform);
    const auto source_subpath = object.authoring_source->subpaths.front();

    desktop::tools::PenToolController source(document);
    REQUIRE(source.resume(object.id, source_subpath.id,
                          source_subpath.nodes.back().id).code ==
            PenDrawingResultCode::Applied);
    place_pen_anchor(source, {160.0, 95.0});
    const auto appended_id = source.draft().nodes.back().id;
    REQUIRE(source.write_recovery_checkpoint(checkpoint_path_text, base_crc).code ==
            PenDrawingResultCode::Applied);

    desktop::tools::PenToolController restored(document);
    REQUIRE(restored.stage_recovery_checkpoint(checkpoint_path_text, base_crc).code ==
            PenDrawingResultCode::Applied);
    REQUIRE(restored.accept_recovery_checkpoint(base_crc).code ==
            PenDrawingResultCode::Applied);
    REQUIRE(restored.resuming());
    REQUIRE(restored.anchor_count() == 2);
    REQUIRE(restored.draft().nodes.front().id == source_subpath.nodes.back().id);
    REQUIRE(restored.draft().nodes.back().id == appended_id);
    REQUIRE(restored.finish_open().committed());

    const auto extended = read_path(document, object.id);
    REQUIRE(extended.authoring_source->subpaths.front().nodes.size() == 3);
    require_point_near(transform.map_point(
        extended.authoring_source->subpaths.front().nodes.back().anchor),
        {160.0, 95.0});
    std::filesystem::remove(checkpoint_path);
}

void test_pen_recovery_can_be_discarded_and_rejects_changed_base() {
    using vector::tools::pen::PenDrawingResultCode;
    const auto checkpoint_path = test_path("pen-recovery-stale-base");
    const auto checkpoint_path_text = checkpoint_path.string();
    constexpr uint32_t base_crc = 0x76543210u;
    document::Document document("pen-recovery-stale-base");
    desktop::tools::PenToolController source(document);
    REQUIRE(source.activate().code == PenDrawingResultCode::Applied);
    place_pen_anchor(source, {5.0, 5.0});
    place_pen_anchor(source, {15.0, 5.0});
    REQUIRE(source.write_recovery_checkpoint(checkpoint_path_text, base_crc).code ==
            PenDrawingResultCode::Applied);

    vector::interaction::ToolSessionLimits smaller_limits;
    smaller_limits.max_checkpoint_bytes = 64;
    desktop::tools::PenToolController over_quota(document);
    REQUIRE(over_quota.stage_recovery_checkpoint(
        checkpoint_path_text, base_crc, smaller_limits).code ==
        PenDrawingResultCode::InvalidInput);
    REQUIRE(!over_quota.recovery_pending());

    desktop::tools::PenToolController discarded(document);
    REQUIRE(discarded.stage_recovery_checkpoint(checkpoint_path_text, base_crc).code ==
            PenDrawingResultCode::Applied);
    REQUIRE(discarded.recovery_pending());
    discarded.discard_recovery_checkpoint();
    REQUIRE(!discarded.recovery_pending());
    REQUIRE(discarded.activate().code == PenDrawingResultCode::Applied);
    discarded.cancel();

    desktop::tools::PenToolController stale(document);
    REQUIRE(stale.stage_recovery_checkpoint(checkpoint_path_text, base_crc).code ==
            PenDrawingResultCode::Applied);
    (void)add_resumable_path(document, geometry::Transform2D::identity());
    REQUIRE(stale.accept_recovery_checkpoint(base_crc).code ==
            PenDrawingResultCode::Stale);
    REQUIRE(!stale.recovery_pending() && !stale.active());
    REQUIRE(document.current_snapshot()->object_count() == 1);
    std::filesystem::remove(checkpoint_path);
}

void test_pen_focus_capture_pan_and_stale_rollback_lifecycle() {
    using vector::tools::pen::PenDrawingResultCode;
    document::Document document("pen-lifecycle");
    desktop::tools::PenToolController controller(document);
    REQUIRE(controller.activate().code == PenDrawingResultCode::Applied);
    place_pen_anchor(controller, {10.0, 10.0});
    REQUIRE(controller.pointer_down({20.0, 10.0}).code ==
            PenDrawingResultCode::PreviewUpdated);
    REQUIRE(controller.pointer_move({24.0, 13.0}, true).code ==
            PenDrawingResultCode::PreviewUpdated);
    REQUIRE(controller.pointer_preview()->outgoing_handle.has_value());
    REQUIRE(controller.on_capture_lost().applied());
    REQUIRE(!controller.pointer_pending() && !controller.pointer_preview());
    REQUIRE(controller.anchor_count() == 1);

    REQUIRE(controller.pointer_down({20.0, 10.0}).code ==
            PenDrawingResultCode::PreviewUpdated);
    REQUIRE(controller.pointer_move({24.0, 13.0}, true).code ==
            PenDrawingResultCode::PreviewUpdated);
    REQUIRE(controller.on_focus_lost().applied());
    REQUIRE(!controller.pointer_pending() && !controller.pointer_preview());
    REQUIRE(controller.anchor_count() == 1);

    REQUIRE(controller.update_numeric_candidate("#30;20").code ==
            PenDrawingResultCode::PreviewUpdated);
    const auto candidate = controller.candidate();
    REQUIRE(candidate.has_value());
    REQUIRE(controller.suspend_for_pan().applied());
    REQUIRE(controller.anchor_count() == 1);
    REQUIRE(controller.resume_after_pan().applied());
    require_point_near(controller.candidate()->anchor, candidate->anchor);

    (void)add_resumable_path(document, geometry::Transform2D::identity());
    REQUIRE(controller.pointer_down({30.0, 30.0}).code ==
            PenDrawingResultCode::Stale);
    REQUIRE(!controller.active() && controller.anchor_count() == 0);
}

} // namespace

void run_drawing_snap_input_tests();

int main() {
    try {
        run_drawing_snap_input_tests();
        test_preferences_are_bounded_persistent_user_settings();
        test_native_controller_commits_numeric_line_and_refreshes_snap_index_async();
        test_polyline_enter_commits_one_editable_path_then_escape_exits();
        test_pen_resumes_transformed_path_from_both_endpoints();
        test_pen_resume_cancel_and_document_rejects_singular_transform();
        test_pen_alt_splits_handles_and_profile_modifiers_lock_direction();
        test_pen_recovery_checkpoint_roundtrips_history_and_base_provenance();
        test_pen_recovery_checkpoint_restores_resume_transform();
        test_pen_recovery_can_be_discarded_and_rejects_changed_base();
        test_pen_focus_capture_pan_and_stale_rollback_lifecycle();
        std::cout << "[PASS] D09 drawing tools and Pen endpoint resume lifecycle\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
