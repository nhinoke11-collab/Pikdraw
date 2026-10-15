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
    ShowWindow(window.hwnd(), SW_HIDE);
    window.fit_page_to_canvas();

    const auto send_mouse = [&window](UINT message, WPARAM buttons, int x, int y) {
        SendMessageW(window.hwnd(), message, buttons, MAKELPARAM(x, y));
    };
    send_mouse(WM_LBUTTONDOWN, MK_LBUTTON, 20, 220);
    send_mouse(WM_LBUTTONUP, 0, 20, 220);
    assert(window.active_tool() == 3);

    const auto drag_and_check_vertical_pan = [&](int start_y, int end_y) {
        const auto anchor = camera.center();
        const auto before = camera.world_to_screen(anchor);
        send_mouse(WM_LBUTTONDOWN, MK_LBUTTON, 600, start_y);
        send_mouse(WM_MOUSEMOVE, MK_LBUTTON, 600, end_y);
        send_mouse(WM_LBUTTONUP, 0, 600, end_y);
        const auto after = camera.world_to_screen(anchor);
        const float expected_delta = static_cast<float>(end_y - start_y);
        assert(std::abs((after.y - before.y) - expected_delta) < 0.1f);
    };

    drag_and_check_vertical_pan(400, 409);
    drag_and_check_vertical_pan(409, 403);
#endif

    std::cout << "[PASS] A4 landscape default, page-fit camera, and zoom controls\n";
    return 0;
}
