#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/shell/cut_shape_tool_catalog.hpp"
#include "pikdraw/desktop/shell/cut_shape_svg_path.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/model/path_adapter.hpp"
#include "pikdraw/document/storage/native_format.hpp"
#include "../src/shell/native_window_menus.hpp"
#include "../src/shell/native_window_toolbox.hpp"
#include "../src/performance/canvas_trace.hpp"
#include "cut_shape_svg_icons.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using pikdraw::desktop::NativeWindow;

int main() {
    const auto check = [](bool condition, const char *message) {
        if (!condition) {
            std::cerr << "[FAIL] " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    };

    check(pikdraw::desktop::kCutShapeToolCatalog.size() == 8,
          "AC-CS-TOOL-01 catalog retains all eight Cut & Shape tools");
    auto notification_state =
        std::make_shared<pikdraw::desktop::CutGeometryJobState>();
    std::atomic<std::size_t> notification_count{0};
    std::jthread notification_worker([&](std::stop_token) {
        for (std::size_t index = 0; index < 512; ++index) {
            notification_state->dispatch_notification_if_enabled(
                [&] { notification_count.fetch_add(1, std::memory_order_relaxed); });
        }
    });
    notification_state->disable_notifications();
    notification_worker.join();
    const auto notifications_after_shutdown =
        notification_count.load(std::memory_order_relaxed);
    notification_state->dispatch_notification_if_enabled(
        [&] { notification_count.fetch_add(1, std::memory_order_relaxed); });
    check(notification_count.load(std::memory_order_relaxed) == notifications_after_shutdown,
          "AC-CS-ASYNC-01 shutdown prevents late UI notifications from detached cut jobs");
    for (size_t index = 0; index < pikdraw::desktop::kCutShapeToolCatalog.size(); ++index) {
        const auto& descriptor = pikdraw::desktop::kCutShapeToolCatalog[index];
        check(!descriptor.action_id.empty() && descriptor.label_en[0] != L'\0' &&
                  descriptor.label_vi[0] != L'\0' && !descriptor.svg_icon_asset.empty(),
              "AC-CS-TOOL-02 every flyout row has action, VI/EN labels, and an SVG asset");
        check(pikdraw::desktop::cut_shape_tool_for_active_id(
                  static_cast<int>(descriptor.id)) == &descriptor,
              "AC-CS-TOOL-03 active tool resolves through the typed catalog");
        check(pikdraw::desktop::cut_shape_tool_for_icon_id(descriptor.icon_id) == &descriptor,
              "AC-CS-TOOL-03b flyout icon ID resolves to its catalog SVG asset");
        check(pikdraw::desktop::visit_cut_shape_svg_path(
                  pikdraw::desktop::kCutShapeToolSvgPaths[index],
                  [](pikdraw::desktop::CutShapeSvgPathVerb, float, float) {}),
              "AC-CS-TOOL-03c every embedded SVG path uses supported geometry");
        for (size_t other = index + 1;
             other < pikdraw::desktop::kCutShapeToolCatalog.size(); ++other) {
            check(descriptor.id != pikdraw::desktop::kCutShapeToolCatalog[other].id,
                  "AC-CS-TOOL-04 catalog active IDs are unique");
            check(descriptor.action_id != pikdraw::desktop::kCutShapeToolCatalog[other].action_id,
                  "AC-CS-TOOL-05 catalog action IDs are unique");
        }
    }
    const auto has_tools_menu_action = [](pikdraw::desktop::MenuActionId action) {
        return std::any_of(pikdraw::desktop::kToolsMenuItems.begin(),
                           pikdraw::desktop::kToolsMenuItems.end(),
            [action](const auto& item) { return item.action == action; });
    };
    check(has_tools_menu_action(pikdraw::desktop::MenuActionId::ToolTrimObjects) &&
              has_tools_menu_action(pikdraw::desktop::MenuActionId::ToolSmartTrim),
          "AC-CS-TOOL-06 Trim Objects and Smart Trim are reachable from Tools menu");

    NativeWindow window(1440, 900);
    auto& workflow = window.workflow();
    workflow.new_document();

    auto snapshot = window.root().document().current_snapshot();
    assert(snapshot->page().width_mm == 297.0);
    assert(snapshot->page().height_mm == 210.0);
    assert(snapshot->object_count() == 0);

    const auto epoch_before_invalid_page = snapshot->epoch();
    workflow.new_document(0.0, 210.0);
    assert(window.root().document().current_snapshot()->epoch() == epoch_before_invalid_page);

    workflow.new_document(2000.0, 1000.0);
    window.fit_page_to_canvas();
    const auto& camera = window.root().renderer().camera();
    assert(std::abs(camera.center().x - 1000.0) < 1e-9);
    assert(std::abs(camera.center().y - 500.0) < 1e-9);
    const double expected_fit = std::min(1088.0 / 2000.0, 782.0 / 1000.0) * 0.9;
    assert(std::abs(camera.zoom() - expected_fit) < 1e-9);

    const double zoom_before = camera.zoom();
    window.zoom_in();
    assert(std::abs(camera.zoom() - zoom_before * 1.25) < 1e-9);
    window.zoom_out();
    assert(std::abs(camera.zoom() - zoom_before) < 1e-9);

#ifdef _WIN32
    assert(window.initialize("PikDraw pan direction regression"));
    ShowWindow(window.hwnd(), SW_SHOW);
    window.fit_page_to_canvas();

    const auto send_mouse = [&window](UINT message, WPARAM buttons, int x, int y) {
        SendMessageW(window.hwnd(), message, buttons, MAKELPARAM(x, y));
    };
    send_mouse(WM_MOUSEMOVE, 0, 600, 400);
    const int pan_slot_index = window.get_visible_index_for_slot(
        pikdraw::desktop::ToolboxSlotId::Pan);
    check(pan_slot_index >= 0, "AC-CV Pan toolbox slot is visible");
    const RECT pan_slot = window.get_toolbox_slot_rect(pan_slot_index);
    const int pan_tool_x = (pan_slot.left + pan_slot.right) / 2;
    const int pan_tool_y = (pan_slot.top + pan_slot.bottom) / 2;
    send_mouse(WM_LBUTTONDOWN, MK_LBUTTON, pan_tool_x, pan_tool_y);
    send_mouse(WM_LBUTTONUP, 0, pan_tool_x, pan_tool_y);
    assert(window.active_tool() == 3);

    RECT client{};
    check(GetClientRect(window.hwnd(), &client) != FALSE,
          "AC-CV canvas client bounds are available");
    const RECT canvas = window.canvas_rect(client.right, client.bottom);
    const POINT wheel_client{canvas.left + (canvas.right - canvas.left) / 3,
                             canvas.top + (canvas.bottom - canvas.top) / 3};
    const auto send_wheel = [&window, &check](int delta, POINT client_point) {
        POINT screen_point = client_point;
        check(ClientToScreen(window.hwnd(), &screen_point) != FALSE,
              "AC-CV wheel pointer converts to screen coordinates");
        SendMessageW(window.hwnd(), WM_MOUSEWHEEL,
                     MAKEWPARAM(0, static_cast<WORD>(delta)),
                     MAKELPARAM(screen_point.x, screen_point.y));
    };

    // AC-CV-01 — Given a fitted canvas, wheel input zooms in and out around the pointer.
    const auto pointer_world_before = camera.screen_to_world({
        static_cast<float>(wheel_client.x - canvas.left),
        static_cast<float>(wheel_client.y - canvas.top)});
    const double wheel_zoom_before = camera.zoom();
    send_wheel(WHEEL_DELTA, wheel_client);
    check(std::abs(camera.zoom() - wheel_zoom_before * 1.1) < 1e-9,
          "AC-CV wheel zooms in by one notch");
    const auto pointer_world_zoomed = camera.screen_to_world({
        static_cast<float>(wheel_client.x - canvas.left),
        static_cast<float>(wheel_client.y - canvas.top)});
    check(std::abs(pointer_world_zoomed.x - pointer_world_before.x) < 1e-3 &&
              std::abs(pointer_world_zoomed.y - pointer_world_before.y) < 1e-3,
          "AC-CV zoom-in keeps the world point under the pointer");
    send_wheel(-WHEEL_DELTA, wheel_client);
    check(std::abs(camera.zoom() - wheel_zoom_before) < 1e-9,
          "AC-CV wheel zooms out to the starting zoom");
    const auto pointer_world_restored = camera.screen_to_world({
        static_cast<float>(wheel_client.x - canvas.left),
        static_cast<float>(wheel_client.y - canvas.top)});
    check(std::abs(pointer_world_restored.x - pointer_world_before.x) < 1e-3 &&
              std::abs(pointer_world_restored.y - pointer_world_before.y) < 1e-3,
          "AC-CV zoom round trip keeps the world point under the pointer");

    // AC-CV-02 — Given the pan tool, native drags in either direction move the same anchor.
    const auto drag_and_check_pan = [&](POINT start, POINT end) {
        const auto anchor = camera.center();
        const auto before = camera.world_to_screen(anchor);
        send_mouse(WM_LBUTTONDOWN, MK_LBUTTON, start.x, start.y);
        send_mouse(WM_MOUSEMOVE, MK_LBUTTON, end.x, end.y);
        send_mouse(WM_LBUTTONUP, 0, end.x, end.y);
        const auto after = camera.world_to_screen(anchor);
        check(std::abs((after.x - before.x) - static_cast<float>(end.x - start.x)) < 0.1f &&
                  std::abs((after.y - before.y) - static_cast<float>(end.y - start.y)) < 0.1f,
              "AC-CV pan moves the anchor by the drag vector");
    };

    drag_and_check_pan({600, 400}, {609, 409});
    drag_and_check_pan({609, 409}, {603, 403});

    // Benchmark drag preview & shape creation performance
    // 1. Benchmark Rectangle Tool (Shape creation preview)
    window.fit_page_to_canvas();
    SendMessageW(window.hwnd(), WM_KEYDOWN, 'R', 0);
    SendMessageW(window.hwnd(), WM_KEYUP, 'R', 0);
    assert(window.active_tool() == 1);
    send_mouse(WM_LBUTTONDOWN, MK_LBUTTON, 200, 200); // start drawing shape

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 50; ++i) {
        send_mouse(WM_MOUSEMOVE, MK_LBUTTON, 200 + (i % 30), 200 + (i % 30));
        RedrawWindow(window.hwnd(), NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    send_mouse(WM_LBUTTONUP, 0, 250, 250); // commit rect

    double shape_drag_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "[BENCHMARK] 50 Shape Creation Frames: " << shape_drag_ms << " ms ("
              << (shape_drag_ms / 50.0) << " ms/frame)\n";
    const auto drawn_snapshot = window.root().document().current_snapshot();
    check(drawn_snapshot->object_count() == 1,
          "AC-CV rectangle input creates one document object");
    check(!drawn_snapshot->page().layers.empty() &&
              !drawn_snapshot->page().layers.front().object_ids.empty(),
          "AC-CV created rectangle has a layer object id");
    const auto drawn_id = drawn_snapshot->page().layers.front().object_ids.front();
    const auto bounds_before_drag = workflow.get_object_bounds(drawn_id);
    check(bounds_before_drag.has_value(), "AC-CV created rectangle has observable bounds");

    // AC-CV-03 — Given a created rectangle, native selection and drag move its bounds;
    // one Undo restores the exact pre-drag bounds.
    send_mouse(WM_LBUTTONDOWN, MK_LBUTTON, 20, 125); // select tool 0 (Select)
    send_mouse(WM_LBUTTONUP, 0, 20, 125);
    assert(window.active_tool() == 0);
    const auto drag_start_world = camera.screen_to_world({
        static_cast<float>(220 - canvas.left), static_cast<float>(220 - canvas.top)});
    const auto drag_end_world = camera.screen_to_world({
        static_cast<float>(230 - canvas.left), static_cast<float>(230 - canvas.top)});
    send_mouse(WM_LBUTTONDOWN, MK_LBUTTON, 220, 220); // click object and drag

    auto t2 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 50; ++i) {
        send_mouse(WM_MOUSEMOVE, MK_LBUTTON, 220 + (i % 30), 220 + (i % 30));
        RedrawWindow(window.hwnd(), NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    }
    auto t3 = std::chrono::high_resolution_clock::now();
    send_mouse(WM_LBUTTONUP, 0, 230, 230); // finish drag

    double obj_drag_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();
    std::cout << "[BENCHMARK] 50 Object Drag Frames: " << obj_drag_ms << " ms ("
              << (obj_drag_ms / 50.0) << " ms/frame)\n";
    const auto bounds_after_drag = workflow.get_object_bounds(drawn_id);
    check(bounds_after_drag.has_value(), "AC-CV dragged rectangle retains observable bounds");
    const double expected_dx = drag_end_world.x - drag_start_world.x;
    const double expected_dy = drag_end_world.y - drag_start_world.y;
    check(std::abs(bounds_after_drag->x - (bounds_before_drag->x + expected_dx)) < 0.75 &&
              std::abs(bounds_after_drag->y - (bounds_before_drag->y + expected_dy)) < 0.75 &&
              (std::abs(bounds_after_drag->x - bounds_before_drag->x) > 1.0 ||
               std::abs(bounds_after_drag->y - bounds_before_drag->y) > 1.0),
          "AC-CV native drag moves rectangle bounds by the pointer world delta");
    check(workflow.undo(), "AC-CV Undo applies after native object drag");
    const auto bounds_after_undo = workflow.get_object_bounds(drawn_id);
    check(bounds_after_undo.has_value() && bounds_after_undo->approx_equal(*bounds_before_drag),
          "AC-CV Undo restores the rectangle's original bounds");

    // AC-CSH-01 — Knife straight/freehand and Vector Eraser route captured
    // native pointer samples into one atomic, undoable path edit.
    NativeWindow vector_cut_window(1200, 800);
    check(vector_cut_window.initialize("PikDraw Knife and Vector Eraser native regression"),
          "AC-CSH native HWND initializes");
    ShowWindow(vector_cut_window.hwnd(), SW_SHOW);
    vector_cut_window.workflow().new_document();
    const auto cut_source = vector_cut_window.workflow().add_rectangle(80.0, 70.0, 120.0, 90.0);
    check(cut_source != pikdraw::document::kInvalidObjectId,
          "AC-CSH editable closed source is created");
    vector_cut_window.root().vector_controller().set_selection(
        {cut_source}, *vector_cut_window.root().document().current_snapshot());
    const auto atomic_cut_target = vector_cut_window.workflow().add_rectangle(
        300.0, 80.0, 100.0, 90.0);
    const auto unsupported_cut_text = vector_cut_window.workflow().add_vietnamese_text(
        420.0, 120.0, "Không hỗ trợ");
    check(atomic_cut_target != pikdraw::document::kInvalidObjectId &&
              unsupported_cut_text != pikdraw::document::kInvalidObjectId,
          "AC-CSH mixed-selection rejection fixtures are created");
    vector_cut_window.root().vector_controller().set_selection(
        {atomic_cut_target, unsupported_cut_text},
        *vector_cut_window.root().document().current_snapshot());
    const auto mixed_selection_revision = vector_cut_window.root().document().revision();
    check(!vector_cut_window.workflow().cut_objects_by_line(
              {350.0, 60.0}, {350.0, 190.0}) &&
              vector_cut_window.root().document().revision() == mixed_selection_revision &&
              vector_cut_window.root().document().objects().get(atomic_cut_target).has_value(),
          "AC-CSH explicit Knife selection fails closed without partial edits");
    check(!vector_cut_window.workflow().erase_objects_by_stroke(
              {{350.0, 120.0}, {360.0, 120.0}}, 6.0, true) &&
              vector_cut_window.root().document().revision() == mixed_selection_revision &&
              vector_cut_window.root().document().objects().get(atomic_cut_target).has_value(),
          "AC-CSH explicit Vector Eraser selection fails closed without partial edits");
    check(vector_cut_window.workflow().undo() && vector_cut_window.workflow().undo(),
          "AC-CSH mixed-selection rejection fixtures are removed by Undo");
    vector_cut_window.root().vector_controller().set_selection(
        {cut_source}, *vector_cut_window.root().document().current_snapshot());
    vector_cut_window.fit_page_to_canvas();
    RECT vector_cut_client{};
    check(GetClientRect(vector_cut_window.hwnd(), &vector_cut_client) != FALSE,
          "AC-CSH canvas client bounds are available");
    const RECT vector_cut_canvas = vector_cut_window.canvas_rect(
        vector_cut_client.right, vector_cut_client.bottom);
    const auto to_vector_cut_client = [&vector_cut_window, &vector_cut_canvas](
            pikdraw::geometry::Point2D world) {
        const auto screen = vector_cut_window.root().renderer().camera().world_to_screen(world);
        return POINT{vector_cut_canvas.left + static_cast<int>(std::lround(screen.x)),
                     vector_cut_canvas.top + static_cast<int>(std::lround(screen.y))};
    };
    const auto send_vector_cut_mouse = [&vector_cut_window](
            UINT message, WPARAM buttons, int x, int y) {
        SendMessageW(vector_cut_window.hwnd(), message, buttons, MAKELPARAM(x, y));
    };
    const auto wait_for_cut_geometry = [](NativeWindow& window,
                                          bool discard_worker_completion = false) {
        const ULONGLONG deadline = GetTickCount64() + 5000;
        while (window.cut_geometry_job_active() && GetTickCount64() < deadline) {
            MSG message{};
            if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (discard_worker_completion &&
                    message.message == pikdraw::desktop::kCutGeometryWorkerMessage) {
                    discard_worker_completion = false;
                    continue;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            } else {
                (void)MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
            }
        }
        return !window.cut_geometry_job_active();
    };
    pikdraw::desktop::CutGeometryRequest over_budget_request;
    over_budget_request.snapshot = vector_cut_window.root().document().current_snapshot();
    over_budget_request.points_world.assign(4097, {0.0, 0.0});
    const auto over_budget_proposal = pikdraw::desktop::compute_cut_geometry(
        over_budget_request, {});
    check(over_budget_proposal.status == pikdraw::desktop::CutGeometryStatus::BudgetExceeded &&
              over_budget_proposal.replacements.empty(),
          "AC-CSH gesture sample quota rejects oversized input before geometry work");
    pikdraw::desktop::CutGeometryRequest bounded_request;
    bounded_request.operation = pikdraw::desktop::CutGeometryOperation::Knife;
    bounded_request.snapshot = over_budget_request.snapshot;
    bounded_request.selected_ids = {cut_source};
    bounded_request.points_world = {{140.0, 50.0}, {140.0, 180.0}};
    bounded_request.max_approximation_error_mm = 0.001;
    const auto rejected_tolerance_proposal = pikdraw::desktop::compute_cut_geometry(
        bounded_request, {});
    check(rejected_tolerance_proposal.status ==
              pikdraw::desktop::CutGeometryStatus::ToleranceNotMet &&
              rejected_tolerance_proposal.replacements.empty() &&
              rejected_tolerance_proposal.approximation_error_bound_mm >
                  bounded_request.max_approximation_error_mm,
          "AC-CSH cut fails closed when the kernel's flattening/slit bound exceeds tolerance");
    bounded_request.max_approximation_error_mm =
        pikdraw::desktop::kCutGeometryDefaultToleranceMm;
    const auto accepted_tolerance_proposal = pikdraw::desktop::compute_cut_geometry(
        bounded_request, {});
    check(accepted_tolerance_proposal.status == pikdraw::desktop::CutGeometryStatus::Success &&
              accepted_tolerance_proposal.approximation_error_bound_mm >= 0.0 &&
              accepted_tolerance_proposal.approximation_error_bound_mm <=
                  accepted_tolerance_proposal.max_approximation_error_mm,
          "AC-CSH successful cut proposal retains its measured approximation bound");
    std::stop_source pre_cancelled_source;
    pre_cancelled_source.request_stop();
    pikdraw::desktop::CutGeometryRequest cancelled_request;
    cancelled_request.snapshot = over_budget_request.snapshot;
    cancelled_request.points_world = {{0.0, 0.0}, {1.0, 1.0}};
    const auto cancelled_proposal = pikdraw::desktop::compute_cut_geometry(
        cancelled_request, pre_cancelled_source.get_token());
    check(cancelled_proposal.status == pikdraw::desktop::CutGeometryStatus::Cancelled &&
              cancelled_proposal.replacements.empty(),
          "AC-CSH pre-cancelled geometry returns without a partial proposal");
    vector_cut_window.set_active_tool(5);
    const POINT knife_start = to_vector_cut_client({140.0, 50.0});
    const POINT knife_end = to_vector_cut_client({140.0, 180.0});
    const POINT knife_bend_a = to_vector_cut_client({125.0, 95.0});
    const POINT knife_bend_b = to_vector_cut_client({155.0, 130.0});
    auto knife_revision = vector_cut_window.root().document().revision();
    send_vector_cut_mouse(WM_LBUTTONDOWN, MK_LBUTTON, knife_start.x, knife_start.y);
    send_vector_cut_mouse(WM_MOUSEMOVE, MK_LBUTTON, knife_end.x, knife_end.y);
    send_vector_cut_mouse(WM_LBUTTONUP, 0, knife_end.x, knife_end.y);
    check(vector_cut_window.cut_geometry_job_active(),
          "AC-CSH mouse-up keeps the Knife geometry worker alive after clearing pointer draft state");
    const bool knife_worker_completed = wait_for_cut_geometry(vector_cut_window);
    const std::string knife_worker_failure =
        "AC-CSH Knife worker proposal failed: active=" +
        std::to_string(vector_cut_window.cut_geometry_job_active()) +
        " revision=" + std::to_string(knife_revision) + "->" +
        std::to_string(vector_cut_window.root().document().revision()) +
        " status=" + vector_cut_window.status_message();
    check(knife_worker_completed, knife_worker_failure.c_str());
    const std::string knife_commit_failure =
        "AC-CSH Knife commit mismatch: before=" + std::to_string(knife_revision) +
        " after=" + std::to_string(vector_cut_window.root().document().revision()) +
        " objects=" + std::to_string(vector_cut_window.root().document()
            .current_snapshot()->object_count()) +
        " status=" + vector_cut_window.status_message();
    check(vector_cut_window.root().document().revision() == knife_revision + 1 &&
              vector_cut_window.root().document().current_snapshot()->object_count() == 2,
          knife_commit_failure.c_str());
    send_vector_cut_mouse(WM_LBUTTONDOWN, MK_LBUTTON, knife_start.x, knife_start.y);
    send_vector_cut_mouse(WM_MOUSEMOVE, MK_LBUTTON, knife_end.x, knife_end.y);
    check(GetCapture() == vector_cut_window.hwnd(),
          "AC-CSH Knife gesture is captured before Undo");
    vector_cut_window.execute_menu_action(static_cast<int>(
        pikdraw::desktop::MenuActionId::EditUndo));
    check(GetCapture() != vector_cut_window.hwnd() &&
              vector_cut_window.root().document().current_snapshot()->object_count() == 1,
          "AC-CSH Undo cancels the draft before restoring the prior path");
    send_vector_cut_mouse(WM_LBUTTONUP, 0, knife_end.x, knife_end.y);
    check(vector_cut_window.root().document().current_snapshot()->object_count() == 1,
          "AC-CSH late mouse-up cannot cut the post-Undo snapshot");
    send_vector_cut_mouse(WM_LBUTTONDOWN, MK_LBUTTON, knife_start.x, knife_start.y);
    send_vector_cut_mouse(WM_MOUSEMOVE, MK_LBUTTON, knife_end.x, knife_end.y);
    vector_cut_window.execute_menu_action(static_cast<int>(
        pikdraw::desktop::MenuActionId::EditRedo));
    check(GetCapture() != vector_cut_window.hwnd() &&
              vector_cut_window.root().document().current_snapshot()->object_count() == 2,
          "AC-CSH Redo cancels the draft before restoring cut pieces");
    send_vector_cut_mouse(WM_LBUTTONUP, 0, knife_end.x, knife_end.y);
    check(vector_cut_window.root().document().current_snapshot()->object_count() == 2,
          "AC-CSH late mouse-up cannot cut the post-Redo snapshot");
    check(vector_cut_window.workflow().undo(),
          "AC-CSH Undo returns the Redo fixture to its source path");
    knife_revision = vector_cut_window.root().document().revision();
    send_vector_cut_mouse(WM_LBUTTONDOWN, MK_LBUTTON, knife_start.x, knife_start.y);
    send_vector_cut_mouse(WM_MOUSEMOVE, MK_LBUTTON, knife_end.x, knife_end.y);
    vector_cut_window.set_active_tool(0);
    check(GetCapture() != vector_cut_window.hwnd() &&
              vector_cut_window.root().document().revision() == knife_revision,
          "AC-CSH switching tools cancels the in-progress Knife draft");
    send_vector_cut_mouse(WM_LBUTTONUP, 0, knife_end.x, knife_end.y);
    check(vector_cut_window.root().document().revision() == knife_revision,
          "AC-CSH late mouse-up after tool switch does not commit");
    vector_cut_window.set_active_tool(5);
    send_vector_cut_mouse(WM_LBUTTONDOWN, MK_LBUTTON, knife_start.x, knife_start.y);
    send_vector_cut_mouse(WM_MOUSEMOVE, MK_LBUTTON, knife_end.x, knife_end.y);
    SendMessageW(vector_cut_window.hwnd(), WM_CANCELMODE, 0, 0);
    check(GetCapture() != vector_cut_window.hwnd() &&
              vector_cut_window.root().document().revision() == knife_revision,
          "AC-CSH WM_CANCELMODE cancels the Knife draft without a document edit");
    send_vector_cut_mouse(WM_LBUTTONUP, 0, knife_end.x, knife_end.y);
    check(vector_cut_window.root().document().revision() == knife_revision,
          "AC-CSH late mouse-up after system cancellation does not commit");
    send_vector_cut_mouse(WM_LBUTTONDOWN, MK_LBUTTON, knife_start.x, knife_start.y);
    send_vector_cut_mouse(WM_MOUSEMOVE, MK_LBUTTON, knife_end.x, knife_end.y);
    check(GetCapture() == vector_cut_window.hwnd(),
          "AC-CSH Knife captures the pointer during a gesture");
    ReleaseCapture();
    check(vector_cut_window.root().document().revision() == knife_revision,
          "AC-CSH Knife capture loss cancels without changing the document");
    vector_cut_window.root().vector_controller().set_selection(
        {cut_source}, *vector_cut_window.root().document().current_snapshot());
    send_vector_cut_mouse(WM_LBUTTONDOWN, MK_LBUTTON, 200, 80);
    send_vector_cut_mouse(WM_LBUTTONUP, 0, 200, 80);
    check(vector_cut_window.knife_freehand_mode(),
          "AC-CSH Property Bar enables Freehand Knife");
    knife_revision = vector_cut_window.root().document().revision();
    send_vector_cut_mouse(WM_LBUTTONDOWN, MK_LBUTTON, knife_start.x, knife_start.y);
    send_vector_cut_mouse(WM_MOUSEMOVE, MK_LBUTTON, knife_bend_a.x, knife_bend_a.y);
    send_vector_cut_mouse(WM_KEYDOWN, VK_ESCAPE, 0, 0);
    check(GetCapture() != vector_cut_window.hwnd() &&
              vector_cut_window.root().document().revision() == knife_revision,
          "AC-CSH Escape releases capture and cancels a freehand Knife before commit");
    send_vector_cut_mouse(WM_LBUTTONUP, 0, knife_end.x, knife_end.y);
    check(vector_cut_window.root().document().revision() == knife_revision,
          "AC-CSH late mouse-up after Escape does not commit a cut");
    knife_revision = vector_cut_window.root().document().revision();
    send_vector_cut_mouse(WM_LBUTTONDOWN, MK_LBUTTON, knife_start.x, knife_start.y);
    send_vector_cut_mouse(WM_MOUSEMOVE, MK_LBUTTON, knife_bend_a.x, knife_bend_a.y);
    send_vector_cut_mouse(WM_MOUSEMOVE, MK_LBUTTON, knife_bend_b.x, knife_bend_b.y);
    send_vector_cut_mouse(WM_MOUSEMOVE, MK_LBUTTON, knife_end.x, knife_end.y);
    send_vector_cut_mouse(WM_LBUTTONUP, 0, knife_end.x, knife_end.y);
    check(wait_for_cut_geometry(vector_cut_window),
          "AC-CSH freehand Knife worker proposal commits after native mouse-up");
    const std::string freehand_knife_failure =
        "AC-CSH Freehand Knife mismatch: before=" + std::to_string(knife_revision) +
        " after=" + std::to_string(vector_cut_window.root().document().revision()) +
        " objects=" + std::to_string(vector_cut_window.root().document()
            .current_snapshot()->object_count()) +
        " status=" + vector_cut_window.status_message();
    check(vector_cut_window.root().document().revision() == knife_revision + 1,
          freehand_knife_failure.c_str());
    check(vector_cut_window.workflow().undo(), "AC-CSH Freehand Knife has one Undo");

    // A document lifecycle change during a captured gesture must release the
    // pointer and prevent its eventual mouse-up from reaching either document.
    vector_cut_window.set_active_tool(5);
    auto* cut_document_before_tab_change = &vector_cut_window.root().document();
    const auto before_create_tab_revision = cut_document_before_tab_change->revision();
    send_vector_cut_mouse(WM_LBUTTONDOWN, MK_LBUTTON, knife_start.x, knife_start.y);
    send_vector_cut_mouse(WM_MOUSEMOVE, MK_LBUTTON, knife_end.x, knife_end.y);
    check(GetCapture() == vector_cut_window.hwnd(),
          "AC-CSH Knife holds capture before creating a tab");
    vector_cut_window.create_new_tab("Cut gesture cancellation", 297.0, 210.0);
    check(GetCapture() != vector_cut_window.hwnd() &&
              cut_document_before_tab_change->revision() == before_create_tab_revision,
          "AC-CSH creating a tab cancels the old document gesture");
    const auto create_tab_target = vector_cut_window.workflow().add_rectangle(
        80.0, 70.0, 120.0, 90.0);
    check(create_tab_target != pikdraw::document::kInvalidObjectId,
          "AC-CSH new tab has an editable target for late mouse-up regression");
    send_vector_cut_mouse(WM_LBUTTONUP, 0, knife_end.x, knife_end.y);
    check(vector_cut_window.root().document().current_snapshot()->object_count() == 1,
          "AC-CSH late mouse-up does not commit into the newly created tab");
    vector_cut_window.switch_to_tab(0);

    const auto before_switch_tab_revision = cut_document_before_tab_change->revision();
    send_vector_cut_mouse(WM_LBUTTONDOWN, MK_LBUTTON, knife_start.x, knife_start.y);
    send_vector_cut_mouse(WM_MOUSEMOVE, MK_LBUTTON, knife_end.x, knife_end.y);
    check(GetCapture() == vector_cut_window.hwnd(),
          "AC-CSH Knife holds capture before switching tabs");
    vector_cut_window.switch_to_tab(1);
    check(GetCapture() != vector_cut_window.hwnd() &&
              cut_document_before_tab_change->revision() == before_switch_tab_revision,
          "AC-CSH switching tabs cancels the old document gesture");
    send_vector_cut_mouse(WM_LBUTTONUP, 0, knife_end.x, knife_end.y);
    check(vector_cut_window.root().document().current_snapshot()->object_count() == 1,
          "AC-CSH late mouse-up does not commit into the switched-to tab");
    vector_cut_window.switch_to_tab(0);

    vector_cut_window.create_new_tab("Cut gesture close cancellation", 297.0, 210.0);
    const auto close_tab_target = vector_cut_window.workflow().add_rectangle(
        80.0, 70.0, 120.0, 90.0);
    check(close_tab_target != pikdraw::document::kInvalidObjectId,
          "AC-CSH closable tab has an editable target for late mouse-up regression");
    send_vector_cut_mouse(WM_LBUTTONDOWN, MK_LBUTTON, knife_start.x, knife_start.y);
    send_vector_cut_mouse(WM_MOUSEMOVE, MK_LBUTTON, knife_end.x, knife_end.y);
    check(GetCapture() == vector_cut_window.hwnd(),
          "AC-CSH Knife holds capture before closing its document tab");
    vector_cut_window.close_tab(2);
    check(GetCapture() != vector_cut_window.hwnd(),
          "AC-CSH closing the active document releases the gesture capture");
    send_vector_cut_mouse(WM_LBUTTONUP, 0, knife_end.x, knife_end.y);
    check(vector_cut_window.root().document().current_snapshot()->object_count() == 1,
          "AC-CSH late mouse-up does not commit into the remaining tab after close");
    vector_cut_window.switch_to_tab(0);

    vector_cut_window.execute_menu_action(static_cast<int>(
        pikdraw::desktop::MenuActionId::LayoutPageAdd));
    const auto second_page_snapshot = vector_cut_window.root().document().current_snapshot();
    check(second_page_snapshot && second_page_snapshot->page_count() == 2,
          "AC-CSH page lifecycle fixture adds a second page");
    const auto second_page_id = second_page_snapshot->active_page_id();
    const auto page_cut_target = vector_cut_window.workflow().add_rectangle(
        80.0, 70.0, 120.0, 90.0);
    check(page_cut_target != pikdraw::document::kInvalidObjectId,
          "AC-CSH active page has an editable target for late mouse-up regression");
    send_vector_cut_mouse(WM_LBUTTONDOWN, MK_LBUTTON, knife_start.x, knife_start.y);
    send_vector_cut_mouse(WM_MOUSEMOVE, MK_LBUTTON, knife_end.x, knife_end.y);
    check(GetCapture() == vector_cut_window.hwnd(),
          "AC-CSH Knife holds capture before changing pages");
    vector_cut_window.execute_menu_action(static_cast<int>(
        pikdraw::desktop::MenuActionId::LayoutPagePrevious));
    check(GetCapture() != vector_cut_window.hwnd() &&
              vector_cut_window.root().document().active_page_id() != second_page_id,
          "AC-CSH page switch cancels the gesture before changing document context");
    send_vector_cut_mouse(WM_LBUTTONUP, 0, knife_end.x, knife_end.y);
    const auto after_page_switch = vector_cut_window.root().document().current_snapshot();
    const auto* second_page_after = after_page_switch->find_page(second_page_id);
    check(after_page_switch->object_count() == 1 && second_page_after &&
              second_page_after->objects.size() == 1,
          "AC-CSH late mouse-up changes neither page after page navigation");

    vector_cut_window.root().vector_controller().set_selection(
        {cut_source}, *vector_cut_window.root().document().current_snapshot());
    vector_cut_window.set_active_tool(16);
    auto eraser_revision = vector_cut_window.root().document().revision();
    send_vector_cut_mouse(WM_LBUTTONDOWN, MK_LBUTTON, knife_start.x, knife_start.y);
    send_vector_cut_mouse(WM_MOUSEMOVE, MK_LBUTTON, knife_end.x, knife_end.y);
    check(GetCapture() == vector_cut_window.hwnd(),
          "AC-CSH Vector Eraser captures the pointer during a gesture");
    ReleaseCapture();
    check(vector_cut_window.root().document().revision() == eraser_revision,
          "AC-CSH Vector Eraser capture loss cancels without changing the document");
    eraser_revision = vector_cut_window.root().document().revision();
    send_vector_cut_mouse(WM_LBUTTONDOWN, MK_LBUTTON, knife_start.x, knife_start.y);
    send_vector_cut_mouse(WM_MOUSEMOVE, MK_LBUTTON, knife_end.x, knife_end.y);
    send_vector_cut_mouse(WM_LBUTTONUP, 0, knife_end.x, knife_end.y);
    check(wait_for_cut_geometry(vector_cut_window),
          "AC-CSH Vector Eraser worker proposal commits after native mouse-up");
    check(vector_cut_window.root().document().revision() == eraser_revision + 1 &&
              vector_cut_window.root().document().current_snapshot()->object_count() == 2,
          "AC-CSH Vector Eraser uses its swept stroke and commits both remaining pieces");
    check(vector_cut_window.workflow().undo(), "AC-CSH Vector Eraser has one Undo");

    vector_cut_window.set_active_tool(5);
    vector_cut_window.root().vector_controller().set_selection(
        {cut_source}, *vector_cut_window.root().document().current_snapshot());
    auto cancel_revision = vector_cut_window.root().document().revision();
    send_vector_cut_mouse(WM_LBUTTONDOWN, MK_LBUTTON, knife_start.x, knife_start.y);
    send_vector_cut_mouse(WM_MOUSEMOVE, MK_LBUTTON, knife_end.x, knife_end.y);
    send_vector_cut_mouse(WM_LBUTTONUP, 0, knife_end.x, knife_end.y);
    check(vector_cut_window.cut_geometry_job_active(),
          "AC-CSH Knife completion remains pending until the UI processes its result");
    SendMessageW(vector_cut_window.hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
    check(wait_for_cut_geometry(vector_cut_window) &&
              vector_cut_window.root().document().revision() == cancel_revision &&
              vector_cut_window.root().document().current_snapshot()->object_count() == 1,
          "AC-CSH Escape cancels a pending geometry result without changing the document");

    cancel_revision = vector_cut_window.root().document().revision();
    send_vector_cut_mouse(WM_LBUTTONDOWN, MK_LBUTTON, knife_start.x, knife_start.y);
    send_vector_cut_mouse(WM_MOUSEMOVE, MK_LBUTTON, knife_end.x, knife_end.y);
    send_vector_cut_mouse(WM_LBUTTONUP, 0, knife_end.x, knife_end.y);
    SendMessageW(vector_cut_window.hwnd(), WM_RBUTTONDOWN, 0,
                 MAKELPARAM(knife_end.x, knife_end.y));
    check(wait_for_cut_geometry(vector_cut_window) &&
              vector_cut_window.root().document().revision() == cancel_revision &&
              vector_cut_window.root().document().current_snapshot()->object_count() == 1,
          "AC-CSH right-click cancels pending geometry without a partial edit");

    cancel_revision = vector_cut_window.root().document().revision();
    send_vector_cut_mouse(WM_LBUTTONDOWN, MK_LBUTTON, knife_start.x, knife_start.y);
    send_vector_cut_mouse(WM_MOUSEMOVE, MK_LBUTTON, knife_end.x, knife_end.y);
    send_vector_cut_mouse(WM_LBUTTONUP, 0, knife_end.x, knife_end.y);
    const auto intervening_id = vector_cut_window.workflow().add_rectangle(
        280.0, 80.0, 20.0, 20.0);
    check(intervening_id != pikdraw::document::kInvalidObjectId,
          "AC-CSH stale-result fixture adds a synchronous edit before worker completion");
    check(wait_for_cut_geometry(vector_cut_window) &&
              vector_cut_window.root().document().revision() == cancel_revision + 1 &&
              vector_cut_window.root().document().current_snapshot()->object_count() == 2 &&
              vector_cut_window.root().document().current_snapshot()->contains_object(cut_source),
          "AC-CSH stale worker proposal is discarded after the source revision changes");
    check(vector_cut_window.workflow().undo() &&
              vector_cut_window.root().document().current_snapshot()->object_count() == 1,
          "AC-CSH stale-result fixture is removed with one Undo");

    pikdraw::document::PathObject open_stroke;
    open_stroke.id = pikdraw::document::generate_object_id();
    open_stroke.name = "Open stroke Eraser target";
    open_stroke.has_fill = false;
    open_stroke.has_stroke = true;
    open_stroke.stroke_width_mm = 2.0;
    open_stroke.path.move_to(80.0, 150.0);
    open_stroke.path.line_to(200.0, 150.0);
    const auto open_source = pikdraw::document::editable_path_from_path2d(open_stroke.path, nullptr);
    check(open_source.has_value(), "AC-CSH open stroke fixture retains editable topology");
    open_stroke.set_authoring_source(*open_source);
    const auto open_stroke_id = open_stroke.id;
    check(vector_cut_window.root().document().execute_command(
              std::make_unique<pikdraw::document::AddObjectCommand>(std::move(open_stroke), 0)).success,
          "AC-CSH open stroke fixture is added to the current layer");
    vector_cut_window.root().vector_controller().set_selection(
        {open_stroke_id}, *vector_cut_window.root().document().current_snapshot());
    vector_cut_window.set_active_tool(16);
    const auto open_erase_revision = vector_cut_window.root().document().revision();
    const POINT open_erase_start = to_vector_cut_client({140.0, 120.0});
    const POINT open_erase_end = to_vector_cut_client({140.0, 180.0});
    send_vector_cut_mouse(WM_LBUTTONDOWN, MK_LBUTTON,
                          open_erase_start.x, open_erase_start.y);
    send_vector_cut_mouse(WM_MOUSEMOVE, MK_LBUTTON,
                          open_erase_end.x, open_erase_end.y);
    send_vector_cut_mouse(WM_LBUTTONUP, 0, open_erase_end.x, open_erase_end.y);
    check(wait_for_cut_geometry(vector_cut_window, true),
          "AC-CSH Vector Eraser worker completes for an open stroke path");
    const auto open_erase_snapshot = vector_cut_window.root().document().current_snapshot();
    const auto open_erase_selection = vector_cut_window.root().vector_controller().selection().selected_ids();
    check(vector_cut_window.root().document().revision() == open_erase_revision + 1 &&
              open_erase_selection.size() == 2,
          "AC-CSH Vector Eraser removes the swept open-path segment into two editable pieces");
    bool open_pieces_editable = true;
    for (const auto id : open_erase_selection) {
        const auto object = open_erase_snapshot->objects().get(id);
        const auto* path = object ? std::get_if<pikdraw::document::PathObject>(&*object) : nullptr;
        open_pieces_editable = open_pieces_editable && path && path->authoring_source &&
            path->authoring_source->subpaths.size() == 1 &&
            !path->authoring_source->subpaths.front().closed;
    }
    check(open_pieces_editable,
          "AC-CSH both Vector Eraser results retain editable open-path topology");
    check(vector_cut_window.workflow().undo() &&
              vector_cut_window.root().document().current_snapshot()->objects().get(open_stroke_id).has_value(),
          "AC-CSH one Undo restores the original open stroke and its object ID");

    // AC-CS-02 — Scissors is activated by its shortcut, cancels on lost capture,
    // then commits one editable topology change from native HWND pointer events.
    NativeWindow scissors_window(1200, 800);
    check(scissors_window.initialize("PikDraw Scissors native gesture regression"),
          "AC-CS Scissors HWND initializes");
    ShowWindow(scissors_window.hwnd(), SW_SHOW);
    scissors_window.workflow().new_document();
    const auto scissors_id = scissors_window.workflow().add_rectangle(90.0, 70.0, 100.0, 60.0);
    scissors_window.fit_page_to_canvas();
    SendMessageW(scissors_window.hwnd(), WM_KEYDOWN, 'X', 0);
    SendMessageW(scissors_window.hwnd(), WM_KEYUP, 'X', 0);
    check(scissors_window.active_tool() == 19, "AC-CS X activates Scissors");
    const auto send_scissors_mouse = [&scissors_window](UINT message, WPARAM buttons,
                                                        int x, int y) {
        SendMessageW(scissors_window.hwnd(), message, buttons, MAKELPARAM(x, y));
    };
    const auto scissors_properties_revision = scissors_window.root().document().revision();
    send_scissors_mouse(WM_LBUTTONDOWN, MK_LBUTTON, 200, 80);
    send_scissors_mouse(WM_LBUTTONUP, 0, 200, 80);
    const auto scissors_properties_status = scissors_window.status_message();
    check((scissors_properties_status.find("Scissors") != std::string::npos) &&
              (scissors_properties_status.find("Properties") != std::string::npos ||
               scissors_properties_status.find("Hướng dẫn") != std::string::npos) &&
              scissors_window.root().document().revision() == scissors_properties_revision,
          "AC-CS property bar opens the Scissors help panel without editing the document");
    RECT scissors_client{};
    check(GetClientRect(scissors_window.hwnd(), &scissors_client) != FALSE,
          "AC-CS canvas client bounds are available");
    const RECT scissors_canvas = scissors_window.canvas_rect(
        scissors_client.right, scissors_client.bottom);
    const auto scissors_screen = scissors_window.root().renderer().camera().world_to_screen({140.0, 70.0});
    const int scissors_x = scissors_canvas.left + static_cast<int>(std::lround(scissors_screen.x));
    const int scissors_y = scissors_canvas.top + static_cast<int>(std::lround(scissors_screen.y));
    const auto scissors_revision = scissors_window.root().document().revision();
    send_scissors_mouse(WM_LBUTTONDOWN, MK_LBUTTON, scissors_x, scissors_y);
    check(GetCapture() == scissors_window.hwnd(), "AC-CS pointer down captures the native window");
    check(ReleaseCapture() != FALSE, "AC-CS capture loss can be injected");
    send_scissors_mouse(WM_LBUTTONUP, 0, scissors_x, scissors_y);
    check(scissors_window.root().document().revision() == scissors_revision,
          "AC-CS lost capture cancels without document mutation");

    send_scissors_mouse(WM_LBUTTONDOWN, MK_LBUTTON, scissors_x, scissors_y);
    send_scissors_mouse(WM_LBUTTONUP, 0, scissors_x, scissors_y);
    check(scissors_window.cut_geometry_job_active(),
          "AC-CS click hands Scissors geometry to the cancellable worker");
    check(wait_for_cut_geometry(scissors_window),
          "AC-CS Scissors worker completes through native message/timer dispatch");
    check(scissors_window.root().document().revision() == scissors_revision + 1,
          "AC-CS click commits exactly one document revision");
    auto scissors_object = scissors_window.root().document().current_snapshot()->objects().get(scissors_id);
    check(scissors_object.has_value() &&
              std::holds_alternative<pikdraw::document::PathObject>(*scissors_object),
          "AC-CS Scissors preserves the edited object ID");
    const auto& scissors_path = std::get<pikdraw::document::PathObject>(*scissors_object);
    check(scissors_path.authoring_source &&
              !scissors_path.authoring_source->subpaths.front().closed &&
              scissors_path.authoring_source->subpaths.front().nodes.size() == 6 &&
              scissors_path.authoring_source->subpaths.front().segments.size() == 5,
          "AC-CS native Scissors opens the clicked closed path without losing a segment");
    check(scissors_window.workflow().undo(), "AC-CS one Undo restores the closed source path");
    scissors_object = scissors_window.root().document().current_snapshot()->objects().get(scissors_id);
    check(scissors_object &&
              std::get<pikdraw::document::PathObject>(*scissors_object)
                  .authoring_source->subpaths.front().closed,
          "AC-CS Undo restores closed topology");
    check(scissors_window.workflow().redo(), "AC-CS Redo reapplies the Scissors split");

    // AC-TRIM-UI-01 — Native role clicks choose Target/Sources, property-bar Apply
    // performs one edit, and Undo restores the original target geometry.
    NativeWindow trim_window(1200, 800);
    check(trim_window.initialize("PikDraw Trim Objects native workflow regression"),
          "AC-TRIM native HWND initializes");
    ShowWindow(trim_window.hwnd(), SW_SHOW);
    trim_window.workflow().new_document();
    const auto trim_target = trim_window.workflow().add_rectangle(70.0, 70.0, 100.0, 100.0);
    const auto trim_source = trim_window.workflow().add_rectangle(120.0, 70.0, 50.0, 100.0);
    check(trim_target != pikdraw::document::kInvalidObjectId &&
              trim_source != pikdraw::document::kInvalidObjectId,
          "AC-TRIM fixture paths are created");
    trim_window.fit_page_to_canvas();
    trim_window.set_active_tool(20);
    check(trim_window.active_tool() == 20, "AC-TRIM tool 20 activates through shared toolbox state");
    RECT trim_client{};
    check(GetClientRect(trim_window.hwnd(), &trim_client) != FALSE,
          "AC-TRIM canvas client bounds are available");
    const RECT trim_canvas = trim_window.canvas_rect(trim_client.right, trim_client.bottom);
    const auto to_trim_client = [&trim_window, &trim_canvas](pikdraw::geometry::Point2D world) {
        const auto screen = trim_window.root().renderer().camera().world_to_screen(world);
        return POINT{trim_canvas.left + static_cast<int>(std::lround(screen.x)),
                     trim_canvas.top + static_cast<int>(std::lround(screen.y))};
    };
    const auto send_trim_mouse = [&trim_window](UINT message, WPARAM buttons, int x, int y) {
        SendMessageW(trim_window.hwnd(), message, buttons, MAKELPARAM(x, y));
    };
    const POINT target_point = to_trim_client({90.0, 90.0});
    const POINT source_point = to_trim_client({145.0, 90.0});
    send_trim_mouse(WM_LBUTTONDOWN, MK_LBUTTON, target_point.x, target_point.y);
    send_trim_mouse(WM_LBUTTONUP, 0, target_point.x, target_point.y);
    check(trim_window.trim_objects_target_id() == trim_target &&
              trim_window.trim_objects_source_ids().empty(),
          "AC-TRIM first native click assigns Target role");
    send_trim_mouse(WM_LBUTTONDOWN, MK_LBUTTON, source_point.x, source_point.y);
    send_trim_mouse(WM_LBUTTONUP, 0, source_point.x, source_point.y);
    check(trim_window.trim_objects_source_ids().size() == 1 &&
              trim_window.trim_objects_source_ids().front() == trim_source,
          "AC-TRIM second native click assigns Source role");
    const auto trim_revision = trim_window.root().document().revision();
    send_trim_mouse(WM_LBUTTONDOWN, MK_LBUTTON, 250, 80);
    send_trim_mouse(WM_LBUTTONUP, 0, 250, 80);
    check(trim_window.cut_geometry_job_active(),
          "AC-TRIM Apply hands Boolean geometry to the worker");
    check(wait_for_cut_geometry(trim_window),
          "AC-TRIM worker completes through native message/timer dispatch");
    check(trim_window.cut_geometry_preview_ready(
              pikdraw::desktop::CutGeometryOperation::TrimObjects) &&
              trim_window.root().document().revision() == trim_revision,
          "AC-TRIM worker presents the exact target/source preview without committing");
    UpdateWindow(trim_window.hwnd());
    send_trim_mouse(WM_LBUTTONDOWN, MK_LBUTTON, 250, 80);
    send_trim_mouse(WM_LBUTTONUP, 0, 250, 80);
    check(trim_window.root().document().revision() == trim_revision + 1,
          "AC-TRIM second Apply commits the preview in exactly one document revision");
    const auto trimmed_bounds = trim_window.workflow().get_object_bounds(trim_target);
    check(trimmed_bounds && std::abs(trimmed_bounds->width - 50.0) < 0.5,
          "AC-TRIM subtraction removes the source-overlapped half of the target");
    const auto trimmed_bytes = pikdraw::document::NativeFormat::serialize(
        *trim_window.root().document().current_snapshot());
    std::string trimmed_reopen_error;
    const auto trimmed_reopened = pikdraw::document::NativeFormat::deserialize(
        trimmed_bytes.data(), trimmed_bytes.size(), &trimmed_reopen_error);
    const auto trimmed_reopened_target = trimmed_reopened
        ? trimmed_reopened->objects().get(trim_target) : std::nullopt;
    check(trimmed_reopened && trimmed_reopen_error.empty() && trimmed_reopened_target &&
              std::holds_alternative<pikdraw::document::PathObject>(*trimmed_reopened_target) &&
              std::abs(std::get<pikdraw::document::PathObject>(*trimmed_reopened_target)
                           .path.bounds().width - 50.0) < 0.5,
          "AC-TRIM save/reopen preserves the trimmed target ID and geometry");
    check(trim_window.workflow().undo(), "AC-TRIM one Undo restores the target");
    const auto restored_bounds = trim_window.workflow().get_object_bounds(trim_target);
    check(restored_bounds && std::abs(restored_bounds->width - 100.0) < 0.5,
          "AC-TRIM Undo restores the original target bounds");

    // A document change after Preview invalidates the proposal. Pressing Apply
    // must report stale state and must not silently start another worker.
    send_trim_mouse(WM_LBUTTONDOWN, MK_LBUTTON, target_point.x, target_point.y);
    send_trim_mouse(WM_LBUTTONUP, 0, target_point.x, target_point.y);
    send_trim_mouse(WM_LBUTTONDOWN, MK_LBUTTON, source_point.x, source_point.y);
    send_trim_mouse(WM_LBUTTONUP, 0, source_point.x, source_point.y);
    const auto stale_trim_selection =
        trim_window.root().vector_controller().selection().selected_ids();
    const auto stale_trim_start_revision = trim_window.root().document().revision();
    send_trim_mouse(WM_LBUTTONDOWN, MK_LBUTTON, 250, 80);
    send_trim_mouse(WM_LBUTTONUP, 0, 250, 80);
    check(wait_for_cut_geometry(trim_window) &&
              trim_window.cut_geometry_preview_ready(
                  pikdraw::desktop::CutGeometryOperation::TrimObjects) &&
              trim_window.root().document().revision() == stale_trim_start_revision,
          "AC-TRIM stale fixture reaches a non-committing preview");
    const auto stale_trim_intervening = trim_window.workflow().add_rectangle(
        280.0, 80.0, 20.0, 20.0);
    trim_window.root().vector_controller().set_selection(
        stale_trim_selection, *trim_window.root().document().current_snapshot());
    const auto stale_trim_revision = trim_window.root().document().revision();
    send_trim_mouse(WM_LBUTTONDOWN, MK_LBUTTON, 250, 80);
    send_trim_mouse(WM_LBUTTONUP, 0, 250, 80);
    check(stale_trim_intervening != pikdraw::document::kInvalidObjectId &&
              !trim_window.cut_geometry_job_active() &&
              !trim_window.cut_geometry_preview_ready(
                  pikdraw::desktop::CutGeometryOperation::TrimObjects) &&
              trim_window.root().document().revision() == stale_trim_revision &&
              trim_window.status_message().find("stale") != std::string::npos,
          "AC-TRIM stale Apply fails closed without starting a replacement worker");
    const auto stale_trim_target_bounds = trim_window.workflow().get_object_bounds(trim_target);
    check(stale_trim_target_bounds &&
              std::abs(stale_trim_target_bounds->width - 100.0) < 0.5,
          "AC-TRIM stale Apply preserves target geometry");

    // AC-ST-UI-01 — Native click trims only the open stroke interval bounded
    // by two crossing paths, with one Undo restoring the original topology.
    NativeWindow smart_trim_window(1200, 800);
    check(smart_trim_window.initialize("PikDraw Smart Trim native gesture regression"),
          "AC-ST native HWND initializes");
    ShowWindow(smart_trim_window.hwnd(), SW_SHOW);
    smart_trim_window.workflow().new_document();
    const auto add_open_stroke = [&smart_trim_window](std::string name,
            pikdraw::geometry::Point2D start, pikdraw::geometry::Point2D end) {
        pikdraw::document::PathObject path;
        path.id = pikdraw::document::generate_object_id();
        path.name = std::move(name);
        path.has_fill = false;
        path.has_stroke = true;
        path.stroke_width_mm = 1.0;
        path.path.move_to(start.x, start.y);
        path.path.line_to(end.x, end.y);
        const auto editable = pikdraw::document::editable_path_from_path2d(path.path, nullptr);
        if (!editable) return pikdraw::document::kInvalidObjectId;
        path.set_authoring_source(*editable);
        const auto id = path.id;
        const auto result = smart_trim_window.root().document().execute_command(
            std::make_unique<pikdraw::document::AddObjectCommand>(std::move(path), 0));
        return result.success ? id : pikdraw::document::kInvalidObjectId;
    };
    const auto add_open_curve = [&smart_trim_window](std::string name,
            pikdraw::geometry::Path2D curve) {
        pikdraw::document::PathObject path;
        path.id = pikdraw::document::generate_object_id();
        path.name = std::move(name);
        path.has_fill = false;
        path.has_stroke = true;
        path.stroke_width_mm = 1.0;
        path.path = std::move(curve);
        const auto editable = pikdraw::document::editable_path_from_path2d(path.path, nullptr);
        if (!editable) return pikdraw::document::kInvalidObjectId;
        path.set_authoring_source(*editable);
        const auto id = path.id;
        const auto result = smart_trim_window.root().document().execute_command(
            std::make_unique<pikdraw::document::AddObjectCommand>(std::move(path), 0));
        return result.success ? id : pikdraw::document::kInvalidObjectId;
    };
    const auto smart_trim_target = add_open_stroke("Target", {50.0, 50.0}, {250.0, 50.0});
    const auto smart_trim_source_a = add_open_stroke("Crossing A", {100.0, 20.0}, {100.0, 80.0});
    const auto smart_trim_source_b = add_open_stroke("Crossing B", {200.0, 20.0}, {200.0, 80.0});
    check(smart_trim_target && smart_trim_source_a && smart_trim_source_b,
          "AC-ST open editable stroke fixtures are created");
    smart_trim_window.fit_page_to_canvas();
    smart_trim_window.set_active_tool(21);
    check(smart_trim_window.active_tool() == 21 &&
              smart_trim_window.status_message().find("Smart Trim") != std::string::npos,
          "AC-ST Smart Trim is reachable through toolbox tool state");
    RECT smart_trim_client{};
    check(GetClientRect(smart_trim_window.hwnd(), &smart_trim_client) != FALSE,
          "AC-ST canvas client bounds are available");
    const RECT smart_trim_canvas = smart_trim_window.canvas_rect(
        smart_trim_client.right, smart_trim_client.bottom);
    const auto trim_hit_screen = smart_trim_window.root().renderer().camera().world_to_screen({150.0, 50.0});
    const int trim_hit_x = smart_trim_canvas.left + static_cast<int>(std::lround(trim_hit_screen.x));
    const int trim_hit_y = smart_trim_canvas.top + static_cast<int>(std::lround(trim_hit_screen.y));
    const auto trim_hit_world_from_event = smart_trim_window.canvas_to_world(trim_hit_x, trim_hit_y);
    const std::string smart_trim_coordinate_failure =
        "AC-ST pointer mapping mismatch: expected=150,50 actual=" +
        std::to_string(trim_hit_world_from_event.x) + "," +
        std::to_string(trim_hit_world_from_event.y);
    check(trim_hit_world_from_event.distance_to({150.0, 50.0}) <= 1.0,
          smart_trim_coordinate_failure.c_str());
    const auto smart_trim_revision = smart_trim_window.root().document().revision();
    const auto send_smart_trim_mouse = [&smart_trim_window](UINT message, WPARAM buttons,
                                                            int x, int y) {
        SendMessageW(smart_trim_window.hwnd(), message, buttons, MAKELPARAM(x, y));
    };
    POINT original_cursor{};
    const bool restore_original_cursor = GetCursorPos(&original_cursor) != FALSE;
    POINT smart_trim_cursor{trim_hit_x, trim_hit_y};
    const bool smart_trim_cursor_screen_ready =
        ClientToScreen(smart_trim_window.hwnd(), &smart_trim_cursor) != FALSE;
    check(smart_trim_cursor_screen_ready &&
              SetCursorPos(smart_trim_cursor.x, smart_trim_cursor.y) != FALSE,
          "AC-ST native hover test positions the OS pointer at the hit location");
    pikdraw::desktop::CutGeometryRequest smart_trim_direct_request;
    smart_trim_direct_request.operation =
        pikdraw::desktop::CutGeometryOperation::SmartTrim;
    smart_trim_direct_request.snapshot =
        smart_trim_window.root().document().current_snapshot();
    smart_trim_direct_request.selected_ids =
        smart_trim_window.root().vector_controller().selection().selected_ids();
    smart_trim_direct_request.points_world = {{150.0, 50.0}};
    smart_trim_direct_request.hit_tolerance_mm = 6.0 /
        smart_trim_window.root().renderer().camera().zoom();
    smart_trim_direct_request.preview_only = true;
    const auto smart_trim_direct_proposal = pikdraw::desktop::compute_cut_geometry(
        smart_trim_direct_request, {});
    const std::string smart_trim_direct_failure =
        "AC-ST direct hit resolution failed: status=" +
        std::to_string(static_cast<int>(smart_trim_direct_proposal.status)) +
        " selected=" + std::to_string(smart_trim_direct_request.selected_ids.size()) +
        " message=" + smart_trim_direct_proposal.message;
    check(smart_trim_direct_proposal.status ==
              pikdraw::desktop::CutGeometryStatus::Success,
          smart_trim_direct_failure.c_str());
    send_smart_trim_mouse(WM_MOUSEMOVE, 0, trim_hit_x, trim_hit_y);
    SendMessageW(smart_trim_window.hwnd(), WM_NULL, 0, 0);
    const ULONGLONG smart_trim_hover_deadline = GetTickCount64() + 5000;
    while (!smart_trim_window.cut_geometry_preview_ready(
               pikdraw::desktop::CutGeometryOperation::SmartTrim) &&
           !smart_trim_window.cut_geometry_job_active() &&
           GetTickCount64() < smart_trim_hover_deadline) {
        MSG message{};
        if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        } else {
            (void)MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
        }
    }
    const bool smart_trim_hover_completed =
        smart_trim_window.cut_geometry_preview_ready(
            pikdraw::desktop::CutGeometryOperation::SmartTrim) ||
        (smart_trim_window.cut_geometry_job_active() &&
         wait_for_cut_geometry(smart_trim_window));
    const auto observed_smart_trim_hover =
        smart_trim_window.smart_trim_hover_point_for_testing();
    const std::string smart_trim_hover_failure =
        "AC-ST hover proposal failed: status=" + smart_trim_window.status_message() +
        " tool=" + std::to_string(smart_trim_window.active_tool()) +
        " job=" + std::to_string(smart_trim_window.cut_geometry_job_active()) +
        " point=" + (observed_smart_trim_hover
            ? std::to_string(observed_smart_trim_hover->x) + "," +
              std::to_string(observed_smart_trim_hover->y) : "none") +
        " preview=" + std::to_string(smart_trim_window.cut_geometry_preview_ready(
            pikdraw::desktop::CutGeometryOperation::SmartTrim));
    check(smart_trim_hover_completed &&
              smart_trim_window.cut_geometry_preview_ready(
                  pikdraw::desktop::CutGeometryOperation::SmartTrim) &&
              smart_trim_window.root().document().revision() == smart_trim_revision,
          smart_trim_hover_failure.c_str());
    check(smart_trim_window.cut_geometry_preview_contour_count(
              pikdraw::desktop::CutGeometryOperation::SmartTrim) == 1 &&
              smart_trim_window.cut_geometry_preview_contour_removed(
                  pikdraw::desktop::CutGeometryOperation::SmartTrim, 0),
          "AC-ST hover preview highlights the exact removed interval in the removal style");
    UpdateWindow(smart_trim_window.hwnd());
    send_smart_trim_mouse(WM_LBUTTONDOWN, MK_LBUTTON, trim_hit_x, trim_hit_y);
    send_smart_trim_mouse(WM_LBUTTONUP, 0, trim_hit_x, trim_hit_y);
    check(!smart_trim_window.cut_geometry_preview_ready(
              pikdraw::desktop::CutGeometryOperation::SmartTrim),
          "AC-ST click consumes the matching hover proposal");
    const bool smart_trim_click_completed = !smart_trim_window.cut_geometry_job_active() ||
        wait_for_cut_geometry(smart_trim_window);
    const std::string smart_trim_click_failure =
        "AC-ST native click did not commit once: completed=" +
        std::to_string(smart_trim_click_completed) + " job=" +
        std::to_string(smart_trim_window.cut_geometry_job_active()) + " revision=" +
        std::to_string(smart_trim_revision) + "->" +
        std::to_string(smart_trim_window.root().document().revision()) + " status=" +
        smart_trim_window.status_message();
    check(smart_trim_click_completed &&
              smart_trim_window.root().document().revision() == smart_trim_revision + 1,
          smart_trim_click_failure.c_str());
    const auto smart_trim_object = smart_trim_window.root().document().current_snapshot()->objects().get(smart_trim_target);
    check(smart_trim_object && std::get<pikdraw::document::PathObject>(*smart_trim_object)
              .authoring_source->subpaths.size() == 2,
          "AC-ST crossing interval is removed and open path remainders stay editable");
    check(smart_trim_window.workflow().undo(), "AC-ST Undo restores the trimmed interval");
    const auto smart_trim_undone = smart_trim_window.root().document().current_snapshot()->objects().get(smart_trim_target);
    check(smart_trim_undone && std::get<pikdraw::document::PathObject>(*smart_trim_undone)
              .authoring_source->subpaths.size() == 1,
          "AC-ST one Undo restores the original path topology");

    smart_trim_window.root().vector_controller().set_selection(
        {smart_trim_target}, *smart_trim_window.root().document().current_snapshot());
    check(smart_trim_window.set_selection_fill_enabled(true), "Smart Trim fixture enables fill");
    smart_trim_direct_request.snapshot = smart_trim_window.root().document().current_snapshot();
    smart_trim_direct_request.selected_ids = {smart_trim_target};
    smart_trim_direct_request.preview_only = false;
    auto filled_trim = pikdraw::desktop::compute_cut_geometry(smart_trim_direct_request, {});
    check(filled_trim.status == pikdraw::desktop::CutGeometryStatus::Success,
          "Smart Trim resolves filled path intersections");
    check(smart_trim_window.workflow().apply_cut_geometry_proposal(std::move(filled_trim)),
          "Smart Trim applies to filled paths");
    const auto filled_trim_object = smart_trim_window.root().document().current_snapshot()->objects().get(smart_trim_target);
    check(filled_trim_object && std::get<pikdraw::document::PathObject>(*filled_trim_object).has_fill &&
              std::get<pikdraw::document::PathObject>(*filled_trim_object).authoring_source->subpaths.size() == 2,
          "Smart Trim preserves fill attribute and edits the path");
    check(smart_trim_window.workflow().undo(), "Undo restores filled Smart Trim path");
    check(smart_trim_window.workflow().undo(), "Undo restores original Smart Trim style");
    smart_trim_window.root().vector_controller().selection().clear();

    // Tangency, coincident overlaps and self-intersections must never produce
    // a guessed interval when the clicked station is ambiguous.
    pikdraw::geometry::Path2D tangent_curve;
    tangent_curve.move_to(100.0, 75.0);
    tangent_curve.cubic_to(133.3333333333, 41.6666666667,
                           166.6666666667, 41.6666666667, 200.0, 75.0);
    const auto tangent_id = add_open_curve("Tangent", std::move(tangent_curve));
    const auto tangent_revision = smart_trim_window.root().document().revision();
    pikdraw::desktop::CutGeometryRequest tangent_request;
    tangent_request.operation = pikdraw::desktop::CutGeometryOperation::SmartTrim;
    tangent_request.snapshot = smart_trim_window.root().document().current_snapshot();
    tangent_request.selected_ids = {smart_trim_target};
    tangent_request.points_world = {{150.0, 50.0}};
    tangent_request.hit_tolerance_mm = 6.0 /
        smart_trim_window.root().renderer().camera().zoom();
    tangent_request.preview_only = true;
    const auto tangent_proposal = pikdraw::desktop::compute_cut_geometry(
        tangent_request, {});
    const std::string tangent_failure =
        "AC-ST tangent was not rejected: object=" + std::to_string(tangent_id) +
        " status=" + std::to_string(static_cast<int>(tangent_proposal.status)) +
        " message=" + tangent_proposal.message +
        " edit=" + std::to_string(tangent_proposal.smart_trim_edit.has_value()) +
        " replacements=" + std::to_string(tangent_proposal.replacements.size()) +
        " revision=" + std::to_string(tangent_revision) + "->" +
        std::to_string(smart_trim_window.root().document().revision());
    check(tangent_id != pikdraw::document::kInvalidObjectId &&
              tangent_proposal.status != pikdraw::desktop::CutGeometryStatus::Success &&
              tangent_proposal.replacements.empty() &&
              !tangent_proposal.smart_trim_edit &&
              smart_trim_window.root().document().revision() == tangent_revision,
          tangent_failure.c_str());

    const auto coincident_id = add_open_stroke("Coincident", {120.0, 50.0}, {180.0, 50.0});
    const auto coincident_revision = smart_trim_window.root().document().revision();
    auto coincident_request = tangent_request;
    coincident_request.snapshot = smart_trim_window.root().document().current_snapshot();
    const auto coincident_proposal = pikdraw::desktop::compute_cut_geometry(
        coincident_request, {});
    check(coincident_id != pikdraw::document::kInvalidObjectId &&
              coincident_proposal.status == pikdraw::desktop::CutGeometryStatus::Ambiguous &&
              coincident_proposal.replacements.empty() &&
              !coincident_proposal.smart_trim_edit &&
              smart_trim_window.root().document().revision() == coincident_revision,
          "AC-ST coincident intervals are reported as ambiguous without a document edit");

    pikdraw::geometry::Path2D self_crossing_path;
    self_crossing_path.move_to(50.0, 50.0);
    self_crossing_path.line_to(150.0, 150.0);
    self_crossing_path.line_to(50.0, 150.0);
    self_crossing_path.line_to(150.0, 50.0);
    self_crossing_path.line_to(200.0, 100.0);
    const auto self_crossing_id = add_open_curve("Self crossing", std::move(self_crossing_path));
    const auto self_crossing_revision = smart_trim_window.root().document().revision();
    auto self_crossing_request = tangent_request;
    self_crossing_request.snapshot = smart_trim_window.root().document().current_snapshot();
    self_crossing_request.selected_ids = {self_crossing_id};
    self_crossing_request.points_world = {{100.0, 100.0}};
    const auto self_crossing_proposal = pikdraw::desktop::compute_cut_geometry(
        self_crossing_request, {});
    check(self_crossing_id != pikdraw::document::kInvalidObjectId &&
              self_crossing_proposal.status != pikdraw::desktop::CutGeometryStatus::Success &&
              self_crossing_proposal.replacements.empty() &&
              !self_crossing_proposal.smart_trim_edit &&
              smart_trim_window.root().document().revision() == self_crossing_revision,
          "AC-ST self-intersection click fails closed without guessing a branch");
    if (restore_original_cursor)
        SetCursorPos(original_cursor.x, original_cursor.y);

    // AC-SF-UI-01 — Smart Fill consumes a real native click, resolves a face
    // from overlapping and disconnected closed paths, and adds one editable path.
    NativeWindow smart_fill_window(1200, 800);
    check(smart_fill_window.initialize("PikDraw Smart Fill native gesture regression"),
          "AC-SF native HWND initializes");
    ShowWindow(smart_fill_window.hwnd(), SW_SHOW);
    smart_fill_window.workflow().new_document();
    const auto fill_a = smart_fill_window.workflow().add_rectangle(50.0, 50.0, 100.0, 100.0);
    const auto fill_b = smart_fill_window.workflow().add_rectangle(100.0, 50.0, 100.0, 100.0);
    const auto fill_disjoint = smart_fill_window.workflow().add_rectangle(220.0, 50.0, 40.0, 40.0);
    check(fill_a && fill_b && fill_disjoint, "AC-SF closed fill fixtures are created");
    smart_fill_window.fit_page_to_canvas();
    smart_fill_window.set_active_tool(23);
    check(smart_fill_window.active_tool() == 23, "AC-SF Smart Fill is reachable");
    RECT fill_client{};
    check(GetClientRect(smart_fill_window.hwnd(), &fill_client) != FALSE,
          "AC-SF canvas client bounds are available");
    const RECT fill_canvas = smart_fill_window.canvas_rect(fill_client.right, fill_client.bottom);
    const auto to_fill_client = [&smart_fill_window, &fill_canvas](pikdraw::geometry::Point2D world) {
        const auto screen = smart_fill_window.root().renderer().camera().world_to_screen(world);
        return POINT{fill_canvas.left + static_cast<int>(std::lround(screen.x)),
                     fill_canvas.top + static_cast<int>(std::lround(screen.y))};
    };
    const auto send_fill_mouse = [&smart_fill_window](UINT message, WPARAM buttons, int x, int y) {
        SendMessageW(smart_fill_window.hwnd(), message, buttons, MAKELPARAM(x, y));
    };
    smart_fill_window.root().vector_controller().selection().clear();
    const POINT disjoint_face = to_fill_client({230.0, 60.0});
    const auto fill_before = smart_fill_window.root().document().revision();
    send_fill_mouse(WM_LBUTTONDOWN, MK_LBUTTON, disjoint_face.x, disjoint_face.y);
    send_fill_mouse(WM_LBUTTONUP, 0, disjoint_face.x, disjoint_face.y);
    check(smart_fill_window.cut_geometry_job_active(),
          "AC-SF click hands face resolution to the worker");
    check(wait_for_cut_geometry(smart_fill_window),
          "AC-SF worker completes through native message/timer dispatch");
    check(smart_fill_window.root().document().revision() == fill_before + 1,
          "AC-SF click in a disconnected union component commits one document revision");
    const auto filled_snapshot = smart_fill_window.root().document().current_snapshot();
    check(filled_snapshot->object_count() == 4,
          "AC-SF creates a new object without removing source paths");
    const auto filled_selection = smart_fill_window.root().vector_controller().selection().selected_ids();
    check(filled_selection.size() == 3, "AC-SF retains source scope for subsequent clicks");
    const auto filled_id = filled_snapshot->page().layers[0].object_ids.back();
    const auto filled_object = filled_snapshot->objects().get(filled_id);
    check(filled_object && std::holds_alternative<pikdraw::document::PathObject>(*filled_object) &&
              std::get<pikdraw::document::PathObject>(*filled_object).authoring_source,
          "AC-SF result remains an editable PathObject");
    const auto filled_bounds = smart_fill_window.workflow().get_object_bounds(filled_id);
    check(filled_bounds && std::abs(filled_bounds->x - 220.0) < 0.1 &&
              std::abs(filled_bounds->y - 50.0) < 0.1 &&
              std::abs(filled_bounds->width - 40.0) < 0.1 &&
              std::abs(filled_bounds->height - 40.0) < 0.1,
          "AC-SF retains the clicked disconnected component instead of the first union component");
    const auto smart_fill_bytes = pikdraw::document::NativeFormat::serialize(*filled_snapshot);
    std::string smart_fill_reopen_error;
    const auto smart_fill_reopened = pikdraw::document::NativeFormat::deserialize(
        smart_fill_bytes.data(), smart_fill_bytes.size(), &smart_fill_reopen_error);
    const auto smart_fill_reopened_path = smart_fill_reopened
        ? smart_fill_reopened->objects().get(filled_id) : std::nullopt;
    check(smart_fill_reopened && smart_fill_reopen_error.empty() &&
              smart_fill_reopened->object_count() == 4 && smart_fill_reopened_path &&
              std::holds_alternative<pikdraw::document::PathObject>(*smart_fill_reopened_path) &&
              std::get<pikdraw::document::PathObject>(*smart_fill_reopened_path).authoring_source,
          "AC-SF save/reopen preserves the generated face ID and editable path");
    check(smart_fill_window.workflow().undo(), "AC-SF one Undo removes only the generated face");
    check(smart_fill_window.root().document().current_snapshot()->object_count() == 3,
          "AC-SF Undo leaves all original source objects");

    // Unfilled closed outlines must create a visible, editable overlap face.
    smart_fill_window.workflow().new_document();
    const auto add_unfilled_rectangle = [&smart_fill_window](double x) {
        pikdraw::document::PathObject path;
        path.id = pikdraw::document::generate_object_id();
        path.has_fill = false;
        path.has_stroke = true;
        path.stroke_width_mm = 1.0;
        path.path.move_to(x, 50.0);
        path.path.line_to(x + 100.0, 50.0);
        path.path.line_to(x + 100.0, 150.0);
        path.path.line_to(x, 150.0);
        path.path.close();
        const auto id = path.id;
        const auto added = smart_fill_window.root().document().execute_command(
            std::make_unique<pikdraw::document::AddObjectCommand>(std::move(path), 0));
        return added.success ? id : pikdraw::document::kInvalidObjectId;
    };
    const auto outline_a = add_unfilled_rectangle(50.0);
    const auto outline_b = add_unfilled_rectangle(100.0);
    check(outline_a && outline_b, "AC-SF unfilled closed outlines are created");
    smart_fill_window.fit_page_to_canvas();
    smart_fill_window.set_active_tool(23);
    smart_fill_window.root().vector_controller().selection().clear();
    const POINT outline_overlap_face = to_fill_client({125.0, 100.0});
    send_fill_mouse(WM_LBUTTONDOWN, MK_LBUTTON, outline_overlap_face.x, outline_overlap_face.y);
    send_fill_mouse(WM_LBUTTONUP, 0, outline_overlap_face.x, outline_overlap_face.y);
    check(wait_for_cut_geometry(smart_fill_window), "AC-SF unfilled outlines resolve through worker");
    const auto outline_snapshot = smart_fill_window.root().document().current_snapshot();
    const auto outline_selection = smart_fill_window.root().vector_controller().selection().selected_ids();
    if (outline_snapshot->object_count() != 3 || outline_selection.size() != 2)
        std::cerr << "Smart Fill status: " << smart_fill_window.status_message() << " count="
                  << outline_snapshot->object_count() << " selection=" << outline_selection.size() << "\n";
    check(outline_snapshot->object_count() == 3 && outline_selection.size() == 2,
          "AC-SF preserves unfilled sources as the fill scope");
    const auto outline_result_id = outline_snapshot->page().layers[0].object_ids.back();
    const auto outline_result = outline_snapshot->objects().get(outline_result_id);
    check(outline_result && std::holds_alternative<pikdraw::document::PathObject>(*outline_result) &&
              std::get<pikdraw::document::PathObject>(*outline_result).has_fill &&
              std::get<pikdraw::document::PathObject>(*outline_result).authoring_source,
          "AC-SF creates a filled editable face from unfilled sources");
    const auto outline_bounds = smart_fill_window.workflow().get_object_bounds(outline_result_id);
    check(outline_bounds && std::abs(outline_bounds->x - 100.0) < 0.1 &&
              std::abs(outline_bounds->width - 50.0) < 0.1,
          "AC-SF resolves the overlap of unfilled outlines");
    const POINT second_outline_face = to_fill_client({75.0, 100.0});
    send_fill_mouse(WM_LBUTTONDOWN, MK_LBUTTON, second_outline_face.x, second_outline_face.y);
    send_fill_mouse(WM_LBUTTONUP, 0, second_outline_face.x, second_outline_face.y);
    check(wait_for_cut_geometry(smart_fill_window), "AC-SF second click resolves using the original sources");
    const auto second_face_snapshot = smart_fill_window.root().document().current_snapshot();
    const auto second_face_id = second_face_snapshot->page().layers[0].object_ids.back();
    const auto second_face_bounds = smart_fill_window.workflow().get_object_bounds(second_face_id);
    check(second_face_snapshot->object_count() == 4 && second_face_id != outline_result_id &&
              second_face_bounds && std::abs(second_face_bounds->x - 50.0) < 0.1 &&
              std::abs(second_face_bounds->width - 50.0) < 0.1,
          "AC-SF consecutive clicks create distinct neighbouring faces");
    check(smart_fill_window.workflow().undo(), "AC-SF Undo removes the second face only");
    check(smart_fill_window.root().document().current_snapshot()->object_count() == 3,
          "AC-SF first face remains after Undo of second click");
    check(smart_fill_window.workflow().undo(), "AC-SF unfilled outline result can be undone");
    pikdraw::desktop::CutGeometryRequest selected_outline_request;
    selected_outline_request.operation = pikdraw::desktop::CutGeometryOperation::SmartFill;
    selected_outline_request.snapshot = smart_fill_window.root().document().current_snapshot();
    selected_outline_request.selected_ids = {outline_a, outline_b};
    selected_outline_request.points_world = {{125.0, 100.0}};
    const auto selected_outline_result = pikdraw::desktop::compute_smart_fill_geometry(selected_outline_request, {});
    check(selected_outline_result.status == pikdraw::desktop::CutGeometryStatus::Success &&
              selected_outline_result.additions.size() == 1 &&
              selected_outline_result.additions.front().object.has_fill,
          "AC-SF accepts explicitly selected unfilled outlines");

    pikdraw::desktop::CutGeometryRequest outline_request;
    outline_request.snapshot = smart_fill_window.root().document().current_snapshot();
    outline_request.selected_ids = {outline_a};
    outline_request.operation = pikdraw::desktop::CutGeometryOperation::Knife;
    outline_request.points_world = {{75.0, 40.0}, {75.0, 160.0}};
    auto outline_proposal = pikdraw::desktop::compute_cut_geometry(outline_request, {});
    check(outline_proposal.status == pikdraw::desktop::CutGeometryStatus::Success &&
              outline_proposal.replacements.size() == 1 &&
              outline_proposal.replacements.front().pieces.size() == 2 &&
              !outline_proposal.replacements.front().pieces.front().has_fill,
          "Knife splits an unfilled closed path while preserving its style");
    check(smart_fill_window.workflow().apply_cut_geometry_proposal(std::move(outline_proposal)),
          "Knife commits an unfilled closed path");
    check(smart_fill_window.workflow().undo(), "Undo restores the unfilled Knife source");
    outline_request.snapshot = smart_fill_window.root().document().current_snapshot();
    outline_request.operation = pikdraw::desktop::CutGeometryOperation::VectorEraser;
    outline_request.points_world = {{75.0, 45.0}, {75.0, 55.0}};
    outline_request.diameter_mm = 4.0;
    outline_proposal = pikdraw::desktop::compute_cut_geometry(outline_request, {});
    check(outline_proposal.status == pikdraw::desktop::CutGeometryStatus::Success &&
              !outline_proposal.replacements.empty(),
          "Vector Eraser removes an interval from a closed unfilled outline");
    check(smart_fill_window.workflow().apply_cut_geometry_proposal(std::move(outline_proposal)),
          "Vector Eraser commits a closed unfilled outline");
    check(smart_fill_window.workflow().undo(), "Undo restores unfilled erased outline");
    outline_request.snapshot = smart_fill_window.root().document().current_snapshot();
    outline_request.operation = pikdraw::desktop::CutGeometryOperation::TrimObjects;
    outline_request.target_id = outline_a;
    outline_request.source_ids = {outline_b};
    outline_proposal = pikdraw::desktop::compute_trim_objects_geometry(outline_request, {});
    check(outline_proposal.status == pikdraw::desktop::CutGeometryStatus::Success &&
              !outline_proposal.replacements.empty() &&
              !outline_proposal.replacements.front().pieces.front().has_fill,
          "Trim resolves unfilled source and target without adding fill");
    check(smart_fill_window.workflow().apply_cut_geometry_proposal(std::move(outline_proposal)),
          "Trim commits unfilled paths");
    check(smart_fill_window.workflow().undo(), "Undo restores unfilled Trim target");
    outline_request.snapshot = smart_fill_window.root().document().current_snapshot();
    outline_request.operation = pikdraw::desktop::CutGeometryOperation::ShapeBuilder;
    outline_request.selected_ids = {outline_a, outline_b};
    outline_request.points_world = {{125.0, 100.0}};
    outline_request.erase_faces = false;
    outline_proposal = pikdraw::desktop::compute_shape_builder_geometry(outline_request, {});
    check(outline_proposal.status == pikdraw::desktop::CutGeometryStatus::Success &&
              !outline_proposal.additions.empty() && !outline_proposal.additions.front().object.has_fill,
          "Shape Builder merge preserves the unfilled style");
    check(smart_fill_window.workflow().apply_cut_geometry_proposal(std::move(outline_proposal)),
          "Shape Builder commits an unfilled merge");
    check(smart_fill_window.workflow().undo(), "Undo removes unfilled Shape Builder merge");
    outline_request.snapshot = smart_fill_window.root().document().current_snapshot();
    outline_request.erase_faces = true;
    outline_proposal = pikdraw::desktop::compute_shape_builder_geometry(outline_request, {});
    check(outline_proposal.status == pikdraw::desktop::CutGeometryStatus::Success,
          "Shape Builder computes erase on unfilled paths");
    check(smart_fill_window.workflow().apply_cut_geometry_proposal(std::move(outline_proposal)),
          "Shape Builder commits erase on unfilled paths");
    check(smart_fill_window.workflow().undo(), "Undo restores unfilled Shape Builder sources");
    const auto boolean_snapshot = smart_fill_window.root().document().current_snapshot();
    smart_fill_window.root().vector_controller().set_selection({outline_a, outline_b}, *boolean_snapshot);
    check(smart_fill_window.workflow().can_boolean_operation_selected(pikdraw::geometry::BooleanOp::Intersection) &&
              smart_fill_window.workflow().boolean_operation_selected(pikdraw::geometry::BooleanOp::Intersection, true, true),
          "Boolean intersection supports unfilled paths");
    const auto boolean_selection = smart_fill_window.root().vector_controller().selection().selected_ids();
    const auto boolean_result = smart_fill_window.root().document().current_snapshot()->objects().get(boolean_selection.front());
    check(boolean_result && !std::get<pikdraw::document::PathObject>(*boolean_result).has_fill,
          "Boolean intersection preserves target fill state");
    check(smart_fill_window.workflow().undo(), "Undo removes Boolean intersection");

    // Four independent open segments enclose one face, with no source fill.
    smart_fill_window.workflow().new_document();
    const auto add_fill_boundary = [&smart_fill_window](pikdraw::geometry::Point2D a,
                                                       pikdraw::geometry::Point2D b) {
        pikdraw::document::PathObject path;
        path.id = pikdraw::document::generate_object_id();
        path.has_fill = false;
        path.has_stroke = true;
        path.stroke_width_mm = 1.0;
        path.path.move_to(a); path.path.line_to(b);
        const auto id = path.id;
        const auto result = smart_fill_window.root().document().execute_command(
            std::make_unique<pikdraw::document::AddObjectCommand>(std::move(path), 0));
        return result.success ? id : pikdraw::document::kInvalidObjectId;
    };
    const auto line_a = add_fill_boundary({50, 50}, {150, 50});
    const auto line_b = add_fill_boundary({150, 50}, {150, 150});
    const auto line_c = add_fill_boundary({150, 150}, {50, 150});
    const auto line_d = add_fill_boundary({50, 150}, {50, 50});
    pikdraw::desktop::CutGeometryRequest open_face_request;
    open_face_request.operation = pikdraw::desktop::CutGeometryOperation::SmartFill;
    open_face_request.snapshot = smart_fill_window.root().document().current_snapshot();
    open_face_request.selected_ids = {line_a, line_b, line_c, line_d};
    open_face_request.points_world = {{100, 100}};
    auto open_face_proposal = pikdraw::desktop::compute_smart_fill_geometry(open_face_request, {});
    check(open_face_proposal.status == pikdraw::desktop::CutGeometryStatus::Success &&
              open_face_proposal.additions.size() == 1 && open_face_proposal.additions.front().object.has_fill,
          "Smart Fill resolves an enclosed face formed by four open lines");
    check(smart_fill_window.workflow().apply_cut_geometry_proposal(std::move(open_face_proposal)),
          "Smart Fill commits a face formed by open lines");
    check(smart_fill_window.root().document().current_snapshot()->object_count() == 5,
          "Smart Fill preserves every open boundary");
    check(smart_fill_window.workflow().undo(), "Undo removes only the open-line face");
    open_face_request.snapshot = smart_fill_window.root().document().current_snapshot();
    open_face_request.selected_ids.clear();
    smart_fill_window.fit_page_to_canvas();
    smart_fill_window.set_active_tool(23);
    smart_fill_window.root().vector_controller().selection().clear();
    const POINT open_face_click = to_fill_client({100, 100});
    send_fill_mouse(WM_LBUTTONDOWN, MK_LBUTTON, open_face_click.x, open_face_click.y);
    send_fill_mouse(WM_LBUTTONUP, 0, open_face_click.x, open_face_click.y);
    check(wait_for_cut_geometry(smart_fill_window) &&
              smart_fill_window.root().document().current_snapshot()->object_count() == 5,
          "Smart Fill native click creates a face from open lines without selection");
    check(smart_fill_window.workflow().undo(), "Undo restores open-line sources after native click");
    open_face_request.snapshot = smart_fill_window.root().document().current_snapshot();
    open_face_request.points_world = {{200, 200}};
    check(pikdraw::desktop::compute_smart_fill_geometry(open_face_request, {}).status ==
              pikdraw::desktop::CutGeometryStatus::NoChange,
          "Smart Fill does not fill the unbounded outside face");
    open_face_request.points_world = {{50, 100}};
    check(pikdraw::desktop::compute_smart_fill_geometry(open_face_request, {}).status ==
              pikdraw::desktop::CutGeometryStatus::Ambiguous,
          "Smart Fill rejects a boundary click");
    open_face_request.points_world = {{100, 100}};
    open_face_request.selected_ids = {line_a, line_b, line_c};
    check(pikdraw::desktop::compute_smart_fill_geometry(open_face_request, {}).status ==
              pikdraw::desktop::CutGeometryStatus::NoChange,
          "Smart Fill does not bridge a gap implicitly");

    open_face_request.selected_ids.clear();
    const auto open_unselected = pikdraw::desktop::compute_smart_fill_geometry(open_face_request, {});
    check(open_unselected.status == pikdraw::desktop::CutGeometryStatus::Success,
          "Smart Fill finds open boundary lines without a selection");
    const auto divider = add_fill_boundary({100, 30}, {100, 170});
    open_face_request.snapshot = smart_fill_window.root().document().current_snapshot();
    open_face_request.points_world = {{75, 100}};
    const auto split_open_face = pikdraw::desktop::compute_smart_fill_geometry(open_face_request, {});
    check(divider && split_open_face.status == pikdraw::desktop::CutGeometryStatus::Success &&
              std::abs(split_open_face.additions.front().object.path.bounds().width - 50.0) < 0.01,
          "Smart Fill separates faces at crossing open lines");

    const auto inner_a = add_fill_boundary({60, 60}, {90, 60});
    const auto inner_b = add_fill_boundary({90, 60}, {90, 90});
    const auto inner_c = add_fill_boundary({90, 90}, {60, 90});
    const auto inner_d = add_fill_boundary({60, 90}, {60, 60});
    open_face_request.snapshot = smart_fill_window.root().document().current_snapshot();
    open_face_request.points_world = {{55, 55}};
    check(inner_a && inner_b && inner_c && inner_d &&
              pikdraw::desktop::compute_smart_fill_geometry(open_face_request, {}).status ==
                  pikdraw::desktop::CutGeometryStatus::Ambiguous,
          "Smart Fill rejects nested disconnected boundaries instead of filling across a hole");

    // AC-SB-UI-01 — Native face selection followed by Property Bar Merge/Erase
    // uses one command; erase preserves source IDs and Undo restores geometry.
    NativeWindow shape_builder_window(1200, 800);
    check(shape_builder_window.initialize("PikDraw Shape Builder native workflow regression"),
          "AC-SB native HWND initializes");
    ShowWindow(shape_builder_window.hwnd(), SW_SHOW);
    shape_builder_window.workflow().new_document();
    const auto builder_a = shape_builder_window.workflow().add_rectangle(50.0, 50.0, 100.0, 100.0);
    const auto builder_b = shape_builder_window.workflow().add_rectangle(100.0, 50.0, 100.0, 100.0);
    check(builder_a && builder_b, "AC-SB overlapping fill fixtures are created");
    shape_builder_window.fit_page_to_canvas();
    shape_builder_window.set_active_tool(22);
    check(shape_builder_window.active_tool() == 22,
          "AC-SB Shape Builder is active before selecting a face");
    RECT builder_client{};
    check(GetClientRect(shape_builder_window.hwnd(), &builder_client) != FALSE,
          "AC-SB canvas client bounds are available");
    const RECT builder_canvas = shape_builder_window.canvas_rect(
        builder_client.right, builder_client.bottom);
    const auto to_builder_client = [&shape_builder_window, &builder_canvas](
            pikdraw::geometry::Point2D world) {
        const auto screen = shape_builder_window.root().renderer().camera().world_to_screen(world);
        return POINT{builder_canvas.left + static_cast<int>(std::lround(screen.x)),
                     builder_canvas.top + static_cast<int>(std::lround(screen.y))};
    };
    const auto send_builder_mouse = [&shape_builder_window](
            UINT message, WPARAM buttons, int x, int y) {
        SendMessageW(shape_builder_window.hwnd(), message, buttons, MAKELPARAM(x, y));
    };
    const POINT overlap_face = to_builder_client({125.0, 100.0});
    send_builder_mouse(WM_LBUTTONDOWN, MK_LBUTTON, overlap_face.x, overlap_face.y);
    send_builder_mouse(WM_LBUTTONUP, 0, overlap_face.x, overlap_face.y);
    check(shape_builder_window.shape_builder_selected_face_count() == 1,
          "AC-SB first native click selects one face seed");
    const auto selected_face_revision = shape_builder_window.root().document().revision();
    check(selected_face_revision == 2,
          "AC-SB selecting a face does not mutate the document");
    send_builder_mouse(WM_LBUTTONDOWN, MK_LBUTTON, 150, 80); // Property Bar Merge
    send_builder_mouse(WM_LBUTTONUP, 0, 150, 80);
    check(shape_builder_window.cut_geometry_job_active(),
          "AC-SB Merge hands face arrangement to the worker");
    check(wait_for_cut_geometry(shape_builder_window),
          "AC-SB merge worker completes through native message/timer dispatch");
    check(shape_builder_window.cut_geometry_preview_ready(
              pikdraw::desktop::CutGeometryOperation::ShapeBuilder) &&
              shape_builder_window.root().document().revision() == selected_face_revision,
          "AC-SB merge worker displays its preview without changing the source document");
    UpdateWindow(shape_builder_window.hwnd());
    SendMessageW(shape_builder_window.hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
    check(!shape_builder_window.cut_geometry_preview_ready(
              pikdraw::desktop::CutGeometryOperation::ShapeBuilder) &&
              shape_builder_window.root().document().revision() == selected_face_revision &&
              shape_builder_window.shape_builder_selected_face_count() == 1,
          "AC-SB Esc discards only the preview and leaves source selection unchanged");
    send_builder_mouse(WM_LBUTTONDOWN, MK_LBUTTON, 150, 80);
    send_builder_mouse(WM_LBUTTONUP, 0, 150, 80);
    check(shape_builder_window.cut_geometry_job_active() &&
              wait_for_cut_geometry(shape_builder_window) &&
              shape_builder_window.cut_geometry_preview_ready(
                  pikdraw::desktop::CutGeometryOperation::ShapeBuilder),
          "AC-SB Merge can recompute after preview cancellation");
    send_builder_mouse(WM_LBUTTONDOWN, MK_LBUTTON, 150, 80);
    send_builder_mouse(WM_LBUTTONUP, 0, 150, 80);
    check(shape_builder_window.root().document().revision() == selected_face_revision + 1,
          "AC-SB second Merge click commits the preview in one document revision");
    check(shape_builder_window.root().document().current_snapshot()->object_count() == 3,
          "AC-SB Merge replaces selected source regions and adds editable output");
    const auto merged_selection =
        shape_builder_window.root().vector_controller().selection().selected_ids();
    const auto merged_bytes = pikdraw::document::NativeFormat::serialize(
        *shape_builder_window.root().document().current_snapshot());
    std::string merged_reopen_error;
    const auto merged_reopened = pikdraw::document::NativeFormat::deserialize(
        merged_bytes.data(), merged_bytes.size(), &merged_reopen_error);
    check(merged_reopened && merged_reopen_error.empty() &&
              merged_reopened->object_count() == 3 && merged_selection.size() == 3 &&
              merged_reopened->objects().get(merged_selection.back()).has_value(),
          "AC-SB merge save/reopen preserves the source remainders and new object");
    check(shape_builder_window.workflow().undo(), "AC-SB Merge has one Undo");
    check(shape_builder_window.root().document().current_snapshot()->object_count() == 2,
          "AC-SB Merge Undo restores the source-only document");
    check(shape_builder_window.root().vector_controller().selection().selected_ids().size() == 2,
          "AC-SB Undo keeps the restored source selections");

    // Re-select the overlap and use Property Bar Erase; both source objects are
    // replaced atomically, and the same IDs/bounds return on Undo.
    shape_builder_window.set_active_tool(22);
    check(shape_builder_window.active_tool() == 22,
          "AC-SB Shape Builder remains active after Undo");
    send_builder_mouse(WM_LBUTTONDOWN, MK_LBUTTON, overlap_face.x, overlap_face.y);
    send_builder_mouse(WM_LBUTTONUP, 0, overlap_face.x, overlap_face.y);
    const std::string face_selection_failure = "AC-SB second native face click: " +
        shape_builder_window.status_message();
    check(shape_builder_window.shape_builder_selected_face_count() == 1,
          face_selection_failure.c_str());
    check(shape_builder_window.status_message().find("Added face point") != std::string::npos,
          face_selection_failure.c_str());
    const auto erase_revision = shape_builder_window.root().document().revision();
    send_builder_mouse(WM_LBUTTONDOWN, MK_LBUTTON, 250, 80); // Property Bar Erase Faces
    send_builder_mouse(WM_LBUTTONUP, 0, 250, 80);
    check(shape_builder_window.cut_geometry_job_active(),
          "AC-SB Erase hands Boolean differences to the worker");
    check(wait_for_cut_geometry(shape_builder_window),
          "AC-SB erase worker completes through native message/timer dispatch");
    check(shape_builder_window.cut_geometry_preview_ready(
              pikdraw::desktop::CutGeometryOperation::ShapeBuilder) &&
              shape_builder_window.root().document().revision() == erase_revision,
          "AC-SB erase worker displays removed/remaining contours without committing");
    UpdateWindow(shape_builder_window.hwnd());
    send_builder_mouse(WM_LBUTTONDOWN, MK_LBUTTON, 250, 80);
    send_builder_mouse(WM_LBUTTONUP, 0, 250, 80);
    const std::string erase_failure = "AC-SB Property Bar Erase commits one atomic document revision: " +
        shape_builder_window.status_message();
    check(shape_builder_window.root().document().revision() == erase_revision + 1,
          erase_failure.c_str());
    const auto builder_a_after = shape_builder_window.workflow().get_object_bounds(builder_a);
    const auto builder_b_after = shape_builder_window.workflow().get_object_bounds(builder_b);
    check(builder_a_after && builder_b_after &&
              std::abs(builder_a_after->width - 50.0) < 0.2 &&
              std::abs(builder_b_after->width - 50.0) < 0.2,
          "AC-SB Erase removes the overlap face from each source path");
    const auto erased_faces_bytes = pikdraw::document::NativeFormat::serialize(
        *shape_builder_window.root().document().current_snapshot());
    std::string erased_faces_reopen_error;
    const auto erased_faces_reopened = pikdraw::document::NativeFormat::deserialize(
        erased_faces_bytes.data(), erased_faces_bytes.size(), &erased_faces_reopen_error);
    const auto erased_a = erased_faces_reopened
        ? erased_faces_reopened->objects().get(builder_a) : std::nullopt;
    const auto erased_b = erased_faces_reopened
        ? erased_faces_reopened->objects().get(builder_b) : std::nullopt;
    check(erased_faces_reopened && erased_faces_reopen_error.empty() && erased_a && erased_b &&
              std::abs(std::get<pikdraw::document::PathObject>(*erased_a).path.bounds().width - 50.0) < 0.2 &&
              std::abs(std::get<pikdraw::document::PathObject>(*erased_b).path.bounds().width - 50.0) < 0.2,
          "AC-SB erase save/reopen preserves both source IDs and the Boolean result");
    check(shape_builder_window.workflow().undo(), "AC-SB Erase has one Undo");
    const auto builder_a_undo = shape_builder_window.workflow().get_object_bounds(builder_a);
    const auto builder_b_undo = shape_builder_window.workflow().get_object_bounds(builder_b);
    check(builder_a_undo && builder_b_undo &&
              std::abs(builder_a_undo->width - 100.0) < 0.2 &&
              std::abs(builder_b_undo->width - 100.0) < 0.2,
          "AC-SB Undo restores both original source geometries");

    // AC-RE-UI-01 — Bitmap mouse down/move/up creates one non-destructive
    // graph command; source bytes survive save/reopen and Undo removes it.
    NativeWindow raster_eraser_window(1200, 800);
    check(raster_eraser_window.initialize("PikDraw Raster Eraser native gesture regression"),
          "AC-RE native HWND initializes");
    ShowWindow(raster_eraser_window.hwnd(), SW_SHOW);
    raster_eraser_window.workflow().new_document();
    pikdraw::document::ImageObject raster_fixture;
    raster_fixture.id = pikdraw::document::generate_object_id();
    raster_fixture.name = "Raster Eraser gesture source";
    raster_fixture.width_px = 10;
    raster_fixture.height_px = 10;
    raster_fixture.bounds_rect = {50.0, 50.0, 100.0, 100.0};
    raster_fixture.transform = pikdraw::geometry::Transform2D::translation(5.0, 10.0) *
        pikdraw::geometry::Transform2D::scale(1.5, 0.75);
    raster_fixture.pixels_rgba.assign(100, 0x6496c8ffu);
    raster_fixture.source.media_type = "application/x-pikdraw-rgba8";
    raster_fixture.source.original_bytes.reserve(400);
    for (size_t pixel = 0; pixel < 100; ++pixel)
        raster_fixture.source.original_bytes.insert(
            raster_fixture.source.original_bytes.end(), {100, 150, 200, 255});
    raster_fixture.source.preview_pixel_color_basis =
        pikdraw::document::RasterPixelColorBasis::AssumedSRgb;
    check(raster_eraser_window.root().document().execute_command(
              std::make_unique<pikdraw::document::AddObjectCommand>(raster_fixture)).success,
          "AC-RE source bitmap fixture is added");
    const auto raster_snapshot = raster_eraser_window.root().document().current_snapshot();
    raster_eraser_window.root().vector_controller().set_selection(
        {raster_fixture.id}, *raster_snapshot);
    raster_eraser_window.fit_page_to_canvas();
    const int cut_slot_index = raster_eraser_window.get_visible_index_for_slot(
        pikdraw::desktop::ToolboxSlotId::Knife);
    check(cut_slot_index >= 0, "AC-RE Cut & Shape toolbox group is visible");
    const RECT cut_slot = raster_eraser_window.get_toolbox_slot_rect(cut_slot_index);
    const int cut_group_y = cut_slot.bottom - 3;
    SendMessageW(raster_eraser_window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM(cut_slot.right - 3, cut_group_y));
    SendMessageW(raster_eraser_window.hwnd(), WM_LBUTTONUP, 0,
                 MAKELPARAM(cut_slot.right - 3, cut_group_y));
    const int raster_flyout_x = raster_eraser_window.toolbox_width() + 30;
    for (size_t index = 0; index < pikdraw::desktop::kCutShapeToolCatalog.size(); ++index) {
        if (index != 0) {
            SendMessageW(raster_eraser_window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON,
                         MAKELPARAM(cut_slot.right - 3, cut_group_y));
            SendMessageW(raster_eraser_window.hwnd(), WM_LBUTTONUP, 0,
                         MAKELPARAM(cut_slot.right - 3, cut_group_y));
        }
        const int flyout_y = cut_slot.top + 26 + static_cast<int>(index) * 29 + 12;
        SendMessageW(raster_eraser_window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON,
                     MAKELPARAM(raster_flyout_x, flyout_y));
        SendMessageW(raster_eraser_window.hwnd(), WM_LBUTTONUP, 0,
                     MAKELPARAM(raster_flyout_x, flyout_y));
        const auto* descriptor = pikdraw::desktop::cut_shape_tool_at(index);
        check(descriptor && raster_eraser_window.active_tool() == static_cast<int>(descriptor->id),
              "AC-RE every Cut & Shape flyout row selects its catalog tool");
    }
    check(raster_eraser_window.active_tool() == 24,
          "AC-RE final Cut & Shape flyout row selects Raster Eraser");
    check(raster_eraser_window.root().vector_controller().selection().selected_ids() ==
              std::vector<pikdraw::document::ObjectId>{raster_fixture.id},
          "AC-RE choosing the tool preserves the bitmap selection");
    RECT raster_client{};
    check(GetClientRect(raster_eraser_window.hwnd(), &raster_client) != FALSE,
          "AC-RE canvas client bounds are available");
    const RECT raster_canvas = raster_eraser_window.canvas_rect(
        raster_client.right, raster_client.bottom);
    const auto to_raster_client = [&raster_eraser_window, &raster_canvas](
            pikdraw::geometry::Point2D world) {
        const auto screen = raster_eraser_window.root().renderer().camera().world_to_screen(world);
        return POINT{raster_canvas.left + static_cast<int>(std::lround(screen.x)),
                     raster_canvas.top + static_cast<int>(std::lround(screen.y))};
    };
    const auto send_raster_mouse = [&raster_eraser_window](UINT message, WPARAM buttons,
                                                           int x, int y) {
        SendMessageW(raster_eraser_window.hwnd(), message, buttons, MAKELPARAM(x, y));
    };
    const POINT raster_start = to_raster_client(raster_fixture.transform.map_point({60.0, 100.0}));
    const POINT raster_middle = to_raster_client(raster_fixture.transform.map_point({100.0, 110.0}));
    const POINT raster_end = to_raster_client(raster_fixture.transform.map_point({140.0, 100.0}));
    const auto before_lost_capture = raster_eraser_window.root().document().revision();
    send_raster_mouse(WM_LBUTTONDOWN, MK_LBUTTON, raster_start.x, raster_start.y);
    const std::string capture_down_failure = "AC-RE pointer down captures the native window: " +
        raster_eraser_window.status_message();
    check(GetCapture() == raster_eraser_window.hwnd(), capture_down_failure.c_str());
    send_raster_mouse(WM_MOUSEMOVE, MK_LBUTTON, raster_middle.x, raster_middle.y);
    const std::string capture_failure = "AC-RE pointer down captures the native window: " +
        raster_eraser_window.status_message();
    check(GetCapture() == raster_eraser_window.hwnd(),
          capture_failure.c_str());
    check(ReleaseCapture() != FALSE, "AC-RE capture loss can be injected");
    send_raster_mouse(WM_LBUTTONUP, 0, raster_end.x, raster_end.y);
    check(raster_eraser_window.root().document().revision() == before_lost_capture,
          "AC-RE capture loss cancels without committing an eraser node");
    const auto raster_revision = raster_eraser_window.root().document().revision();
    send_raster_mouse(WM_LBUTTONDOWN, MK_LBUTTON, raster_start.x, raster_start.y);
    send_raster_mouse(WM_MOUSEMOVE, MK_LBUTTON, raster_middle.x, raster_middle.y);
    send_raster_mouse(WM_LBUTTONUP, 0, raster_end.x, raster_end.y);
    check(raster_eraser_window.root().document().revision() == raster_revision + 1,
          "AC-RE native stroke creates exactly one document revision");
    const auto* raster_value = raster_eraser_window.root().document().objects().lookup_ref(
        raster_fixture.id);
    check(raster_value && std::holds_alternative<pikdraw::document::ImageObject>(*raster_value),
          "AC-RE edited bitmap remains present");
    const auto& raster_edited = std::get<pikdraw::document::ImageObject>(*raster_value);
    check(raster_edited.source.original_data() == raster_fixture.source.original_bytes,
          "AC-RE source bytes remain unchanged");
    check(raster_edited.edit_graph.nodes.size() == 1 &&
              std::holds_alternative<pikdraw::document::RasterEraserParams>(
                  raster_edited.edit_graph.nodes.front().parameters),
          "AC-RE stroke is stored as a non-destructive eraser node");
    const auto serialized_raster = pikdraw::document::NativeFormat::serialize(
        *raster_eraser_window.root().document().current_snapshot());
    std::string raster_error;
    const auto reopened_raster = pikdraw::document::NativeFormat::deserialize(
        serialized_raster.data(), serialized_raster.size(), &raster_error);
    check(reopened_raster && raster_error.empty(),
          "AC-RE graph saves and reopens");
    check(raster_eraser_window.workflow().undo(), "AC-RE one Undo removes the stroke");
    const auto* raster_undone = raster_eraser_window.root().document().objects().lookup_ref(
        raster_fixture.id);
    check(raster_undone && std::get<pikdraw::document::ImageObject>(*raster_undone)
              .edit_graph.nodes.empty(),
          "AC-RE Undo restores the previous raster graph");

    // AC-CS-PERF-01 — trace the native Knife gesture through a successful
    // Direct2D present call or a flushed GDI fallback; report message-to-submit
    // latency without claiming that the OS has displayed the pixels.
    const std::filesystem::path present_trace_path =
        std::filesystem::temp_directory_path() /
        ("pikdraw-cut-present-" + std::to_string(GetCurrentProcessId()) + "-" +
         std::to_string(GetTickCount64()) + ".csv");
    wchar_t previous_trace_path[32768]{};
    const DWORD previous_trace_chars = GetEnvironmentVariableW(
        L"PIKDRAW_PERF_TRACE_PATH", previous_trace_path,
        static_cast<DWORD>(std::size(previous_trace_path)));
    const bool had_previous_trace_path = previous_trace_chars > 0 &&
        previous_trace_chars < std::size(previous_trace_path);
    const bool trace_environment_set = SetEnvironmentVariableW(
        L"PIKDRAW_PERF_TRACE_PATH", present_trace_path.c_str()) != FALSE;
    bool trace_window_initialized = false;
    if (trace_environment_set) {
        {
            NativeWindow trace_window(1000, 700);
            trace_window_initialized = trace_window.initialize(
                "PikDraw Cut gesture present latency probe");
            if (trace_window_initialized) {
                ShowWindow(trace_window.hwnd(), SW_SHOW);
                trace_window.workflow().new_document();
                const auto trace_target = trace_window.workflow().add_rectangle(
                    80.0, 70.0, 120.0, 90.0);
                trace_window.root().vector_controller().set_selection(
                    {trace_target}, *trace_window.root().document().current_snapshot());
                trace_window.fit_page_to_canvas();
                trace_window.set_active_tool(5);
                RECT client{};
                trace_window_initialized = trace_window_initialized &&
                    GetClientRect(trace_window.hwnd(), &client) != FALSE;
                const RECT canvas = trace_window.canvas_rect(client.right, client.bottom);
                const auto world_to_client = [&trace_window, &canvas](
                        pikdraw::geometry::Point2D world) {
                    const auto screen = trace_window.root().renderer().camera().world_to_screen(world);
                    return POINT{canvas.left + static_cast<int>(std::lround(screen.x)),
                                 canvas.top + static_cast<int>(std::lround(screen.y))};
                };
                const auto send_and_present = [&trace_window](UINT message, WPARAM buttons,
                                                               POINT point) {
                    SendMessageW(trace_window.hwnd(), message, buttons,
                                 MAKELPARAM(point.x, point.y));
                    UpdateWindow(trace_window.hwnd());
                };
                const POINT start = world_to_client({140.0, 50.0});
                const POINT end = world_to_client({140.0, 180.0});
                send_and_present(WM_LBUTTONDOWN, MK_LBUTTON, start);
                constexpr int kKnifeLatencyMoveSamples = 64;
                for (int sample = 1; sample <= kKnifeLatencyMoveSamples; ++sample) {
                    const double t = static_cast<double>(sample) /
                                     static_cast<double>(kKnifeLatencyMoveSamples + 1);
                    const POINT point{
                        static_cast<LONG>(std::lround(start.x + (end.x - start.x) * t)),
                        static_cast<LONG>(std::lround(start.y + (end.y - start.y) * t))};
                    send_and_present(WM_MOUSEMOVE, MK_LBUTTON, point);
                }
                SendMessageW(trace_window.hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
                UpdateWindow(trace_window.hwnd());
            }
        }
    }
    if (had_previous_trace_path)
        (void)SetEnvironmentVariableW(L"PIKDRAW_PERF_TRACE_PATH", previous_trace_path);
    else
        (void)SetEnvironmentVariableW(L"PIKDRAW_PERF_TRACE_PATH", nullptr);

    std::vector<double> input_to_present_ms;
    std::vector<double> paint_to_present_ms;
    std::vector<double> render_ms;
    std::vector<double> canvas_overlay_ms;
    std::vector<double> chrome_overlay_ms;
    std::vector<double> present_call_ms;
    std::vector<double> knife_move_to_present_ms;
    std::vector<double> knife_move_overlay_only_ms;
    std::unordered_set<uint64_t> knife_move_event_ids;
    std::unordered_set<uint64_t> overlay_only_event_ids;
    uint64_t trace_frequency = 0;
    uint64_t trace_overflows = 0;
    std::string trace_backend = "unavailable";
    std::ifstream present_trace(present_trace_path);
    if (present_trace) {
        std::string line;
        std::unordered_map<uint64_t, int64_t> input_ticks;
        std::unordered_map<uint64_t, int64_t> paint_begin_ticks;
        std::unordered_map<uint64_t, int64_t> render_begin_ticks;
        std::unordered_map<uint64_t, int64_t> render_end_ticks;
        std::unordered_map<uint64_t, int64_t> canvas_overlay_begin_ticks;
        std::unordered_map<uint64_t, int64_t> canvas_overlay_end_ticks;
        std::unordered_map<uint64_t, int64_t> chrome_overlay_begin_ticks;
        std::unordered_map<uint64_t, int64_t> chrome_overlay_end_ticks;
        std::unordered_map<uint64_t, int64_t> present_call_begin_ticks;
        std::unordered_map<uint64_t, int64_t> present_ticks;
        while (std::getline(present_trace, line)) {
            if (line.rfind("#counter_frequency=", 0) == 0) {
                trace_frequency = std::strtoull(line.c_str() + 19, nullptr, 10);
                continue;
            }
            if (line.rfind("#overflow_count=", 0) == 0) {
                trace_overflows = std::strtoull(line.c_str() + 16, nullptr, 10);
                continue;
            }
            if (line.rfind("#renderer_backend=", 0) == 0) {
                trace_backend = line.substr(18);
                continue;
            }
            if (line.empty() || line.front() == '#') continue;
            std::istringstream row(line);
            uint64_t sequence = 0, event_id = 0, frame_id = 0;
            int64_t ticks = 0;
            unsigned kind = 0;
            char comma = '\0';
            if (!(row >> sequence >> comma >> event_id >> comma >> frame_id >> comma >>
                  ticks >> comma >> kind)) continue;
            if (kind == static_cast<unsigned>(pikdraw::desktop::CanvasTraceKind::InputDequeued))
                input_ticks[event_id] = ticks;
            else if (kind == static_cast<unsigned>(pikdraw::desktop::CanvasTraceKind::PaintBegin))
                paint_begin_ticks[event_id] = ticks;
            else if (kind == static_cast<unsigned>(pikdraw::desktop::CanvasTraceKind::RenderBegin))
                render_begin_ticks[event_id] = ticks;
            else if (kind == static_cast<unsigned>(pikdraw::desktop::CanvasTraceKind::RenderEnd))
                render_end_ticks[event_id] = ticks;
            else if (kind == static_cast<unsigned>(
                         pikdraw::desktop::CanvasTraceKind::CanvasOverlayBegin))
                canvas_overlay_begin_ticks[event_id] = ticks;
            else if (kind == static_cast<unsigned>(
                         pikdraw::desktop::CanvasTraceKind::CanvasOverlayEnd))
                canvas_overlay_end_ticks[event_id] = ticks;
            else if (kind == static_cast<unsigned>(
                         pikdraw::desktop::CanvasTraceKind::ChromeOverlayBegin))
                chrome_overlay_begin_ticks[event_id] = ticks;
            else if (kind == static_cast<unsigned>(
                         pikdraw::desktop::CanvasTraceKind::ChromeOverlayEnd))
                chrome_overlay_end_ticks[event_id] = ticks;
            else if (kind == static_cast<unsigned>(
                         pikdraw::desktop::CanvasTraceKind::PresentCallBegin))
                present_call_begin_ticks[event_id] = ticks;
            else if (kind == static_cast<unsigned>(
                         pikdraw::desktop::CanvasTraceKind::PresentSubmitted))
                present_ticks.try_emplace(event_id, ticks);
            else if (kind == static_cast<unsigned>(
                         pikdraw::desktop::CanvasTraceKind::CutGestureMove))
                knife_move_event_ids.insert(event_id);
            else if (kind == static_cast<unsigned>(
                         pikdraw::desktop::CanvasTraceKind::OverlayOnlyPaint))
                overlay_only_event_ids.insert(event_id);
        }
        if (trace_frequency > 0) {
            const auto milliseconds = [trace_frequency](int64_t begin, int64_t end) {
                return static_cast<double>(end - begin) * 1000.0 /
                    static_cast<double>(trace_frequency);
            };
            for (const auto& [event_id, presented_at] : present_ticks) {
                const auto input = input_ticks.find(event_id);
                if (input == input_ticks.end() || presented_at < input->second) continue;
                input_to_present_ms.push_back(milliseconds(input->second, presented_at));
                if (knife_move_event_ids.contains(event_id)) {
                    knife_move_to_present_ms.push_back(
                        milliseconds(input->second, presented_at));
                    if (overlay_only_event_ids.contains(event_id))
                        knife_move_overlay_only_ms.push_back(
                            milliseconds(input->second, presented_at));
                }
                const auto paint = paint_begin_ticks.find(event_id);
                if (paint != paint_begin_ticks.end() && presented_at >= paint->second)
                    paint_to_present_ms.push_back(milliseconds(paint->second, presented_at));
                const auto render_begin = render_begin_ticks.find(event_id);
                const auto render_end = render_end_ticks.find(event_id);
                if (render_begin != render_begin_ticks.end() &&
                    render_end != render_end_ticks.end() &&
                    render_end->second >= render_begin->second)
                    render_ms.push_back(milliseconds(render_begin->second, render_end->second));
                const auto canvas_begin = canvas_overlay_begin_ticks.find(event_id);
                const auto canvas_end = canvas_overlay_end_ticks.find(event_id);
                if (canvas_begin != canvas_overlay_begin_ticks.end() &&
                    canvas_end != canvas_overlay_end_ticks.end() &&
                    canvas_end->second >= canvas_begin->second)
                    canvas_overlay_ms.push_back(milliseconds(
                        canvas_begin->second, canvas_end->second));
                const auto chrome_begin = chrome_overlay_begin_ticks.find(event_id);
                const auto chrome_end = chrome_overlay_end_ticks.find(event_id);
                if (chrome_begin != chrome_overlay_begin_ticks.end() &&
                    chrome_end != chrome_overlay_end_ticks.end() &&
                    chrome_end->second >= chrome_begin->second)
                    chrome_overlay_ms.push_back(milliseconds(
                        chrome_begin->second, chrome_end->second));
                const auto present_begin = present_call_begin_ticks.find(event_id);
                if (present_begin != present_call_begin_ticks.end() &&
                    presented_at >= present_begin->second)
                    present_call_ms.push_back(milliseconds(present_begin->second, presented_at));
            }
        }
    }
    std::error_code trace_remove_error;
    std::filesystem::remove(present_trace_path, trace_remove_error);
    std::sort(input_to_present_ms.begin(), input_to_present_ms.end());
    std::sort(paint_to_present_ms.begin(), paint_to_present_ms.end());
    std::sort(render_ms.begin(), render_ms.end());
    std::sort(canvas_overlay_ms.begin(), canvas_overlay_ms.end());
    std::sort(chrome_overlay_ms.begin(), chrome_overlay_ms.end());
    std::sort(present_call_ms.begin(), present_call_ms.end());
    std::sort(knife_move_to_present_ms.begin(), knife_move_to_present_ms.end());
    std::sort(knife_move_overlay_only_ms.begin(), knife_move_overlay_only_ms.end());
    const size_t knife_move_overlay_only_count = static_cast<size_t>(std::count_if(
        knife_move_event_ids.begin(), knife_move_event_ids.end(),
        [&overlay_only_event_ids](uint64_t event_id) {
            return overlay_only_event_ids.contains(event_id);
        }));
    const auto percentile_ms = [](const std::vector<double>& samples, double percentile) {
        if (samples.empty()) return 0.0;
        const auto index = static_cast<size_t>(std::ceil(
            percentile * static_cast<double>(samples.size()))) - 1;
        return samples[std::min(index, samples.size() - 1)];
    };
    std::cout << "[PERF] synthetic native Knife input-to-present-submit p50="
              << percentile_ms(input_to_present_ms, 0.50) << "ms p95="
              << percentile_ms(input_to_present_ms, 0.95)
              << "ms samples=" << input_to_present_ms.size()
              << " paint-to-submit p95=" << percentile_ms(paint_to_present_ms, 0.95)
              << "ms render p95=" << percentile_ms(render_ms, 0.95)
              << "ms canvas-overlay p95=" << percentile_ms(canvas_overlay_ms, 0.95)
              << "ms chrome-overlay p95=" << percentile_ms(chrome_overlay_ms, 0.95)
              << "ms present-call p95=" << percentile_ms(present_call_ms, 0.95) << "ms"
              << " Knife-move-to-submit p50="
              << percentile_ms(knife_move_to_present_ms, 0.50) << "ms p95="
              << percentile_ms(knife_move_to_present_ms, 0.95)
              << "ms fast-path p50=" << percentile_ms(knife_move_overlay_only_ms, 0.50)
              << "ms p95=" << percentile_ms(knife_move_overlay_only_ms, 0.95)
              << "ms overlay-only="
              << knife_move_overlay_only_count << '/' << knife_move_event_ids.size()
              << " backend=" << trace_backend << '\n';
    check(trace_environment_set && trace_window_initialized && trace_frequency > 0 &&
              trace_overflows == 0 && input_to_present_ms.size() >= 8 &&
              knife_move_to_present_ms.size() >= 8 &&
              knife_move_overlay_only_count >=
                  (knife_move_event_ids.size() * 9 + 9) / 10,
          "AC-CS-PERF-01 native Knife messages have bounded QPC input-to-present-submit samples");
#endif

    std::cout << "[PASS] A4 landscape default, page-fit camera, and zoom controls\n";
    return 0;
}
