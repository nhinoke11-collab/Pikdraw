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

    std::cout << "[PASS] A4 landscape default, page-fit camera, and zoom controls\n";
    return 0;
}
