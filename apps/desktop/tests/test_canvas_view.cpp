#include "pikdraw/desktop/shell/native_window.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

using pikdraw::desktop::NativeWindow;

int main() {
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

    const auto drag_and_check_vertical_pan = [&](int start_y, int end_y) {
        const auto anchor = camera.center();
        const auto before = camera.world_to_screen(anchor);
        send_mouse(WM_LBUTTONDOWN, MK_LBUTTON, 600, start_y);
        send_mouse(WM_MOUSEMOVE, MK_LBUTTON, 600, end_y);
        send_mouse(WM_LBUTTONUP, 0, 600, end_y);
        const auto after = camera.world_to_screen(anchor);
        const float expected_delta = static_cast<float>(end_y - start_y);
        std::cout << "before: " << before.y << " after: " << after.y << " diff: " << (after.y - before.y) << " expected: " << expected_delta << std::endl;
        assert(std::abs((after.y - before.y) - expected_delta) < 0.1f);
    };

    drag_and_check_vertical_pan(400, 409);
    drag_and_check_vertical_pan(409, 403);

    // Benchmark drag preview & shape creation performance
    // 1. Benchmark Rectangle Tool (Shape creation preview)
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

    // 2. Benchmark Object Move (Select tool 0)
    send_mouse(WM_LBUTTONDOWN, MK_LBUTTON, 20, 125); // select tool 0 (Select)
    send_mouse(WM_LBUTTONUP, 0, 20, 125);
    assert(window.active_tool() == 0);
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
#endif

    std::cout << "[PASS] A4 landscape default, page-fit camera, and zoom controls\n";
    return 0;
}
