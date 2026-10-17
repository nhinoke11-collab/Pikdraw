#include "pikdraw/desktop/shell/native_window.hpp"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>

using pikdraw::desktop::NativeWindow;

int main() {
    const auto check = [](bool condition, const char *message) {
        if (!condition) {
            std::cerr << "[FAIL] " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    };

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
    send_mouse(WM_LBUTTONDOWN, MK_LBUTTON, 20, 240);
    send_mouse(WM_LBUTTONUP, 0, 20, 240);
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
    send_mouse(WM_LBUTTONDOWN, MK_LBUTTON, 20, 160); // select tool 1 (Rect)
    send_mouse(WM_LBUTTONUP, 0, 20, 160);
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
#endif

    std::cout << "[PASS] A4 landscape default, page-fit camera, and zoom controls\n";
    return 0;
}
