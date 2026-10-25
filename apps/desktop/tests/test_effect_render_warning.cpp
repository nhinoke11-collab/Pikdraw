#include "pikdraw/desktop/shell/native_window.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <variant>

using namespace pikdraw::desktop;

namespace {

#ifdef _WIN32
bool force_canvas_paint(NativeWindow &window) {
    RECT client{};
    assert(GetClientRect(window.hwnd(), &client) != FALSE);
    const auto canvas = window.canvas_rect(client.right, client.bottom);
    assert(canvas.right > canvas.left && canvas.bottom > canvas.top);

    // Render once to surface the renderer diagnostic, then drive the same
    // NativeWindow WM_PAINT handler that turns it into user-facing status.
    const auto pixels = window.workflow().render_snapshot(
        canvas.right - canvas.left, canvas.bottom - canvas.top);
    const bool rendered = !pixels.empty();
    if (rendered) {
        const auto &metric = window.root().renderer().last_metric();
        std::cerr << "Unexpectedly rendered over-budget effect: zoom="
                  << window.root().renderer().camera().zoom()
                  << " objects=" << metric.rendered_objects << "/"
                  << metric.total_objects << '\n';
        for (const auto &diagnostic : metric.diagnostics)
            std::cerr << "Render diagnostic: " << diagnostic.object_id << " "
                      << diagnostic.error << " " << diagnostic.message << '\n';
    }
    InvalidateRect(window.hwnd(), nullptr, FALSE);
    SendMessageW(window.hwnd(), WM_PAINT, 0, 0);
    return rendered;
}

void test_over_budget_blur_warning_is_localized_and_keeps_source() {
    NativeWindow window(1440, 900);
    assert(window.initialize("Effect render warning acceptance"));
    ShowWindow(window.hwnd(), SW_HIDE);

    const auto path_id = window.workflow().add_rectangle(
        40.0, 42.0, 24.0, 18.0, "#80b8dc", "#28445e", 0.0);
    assert(path_id != pikdraw::document::kInvalidObjectId);
    auto &selection = window.root().vector_controller().selection();
    selection.select(path_id);
    assert(window.workflow().add_selected_effect(
        pikdraw::document::GaussianBlurEffect{500.0, 500.0}));

    const auto authored = window.root().document().current_snapshot();
    assert(authored);
    const auto authored_revision = authored->revision();
    const auto object = authored->objects().get(path_id);
    assert(object && std::holds_alternative<pikdraw::document::PathObject>(*object));
    const auto &appearance = std::get<pikdraw::document::PathObject>(*object).appearance;
    assert(appearance.effects.size() == 1);
    assert(std::get<pikdraw::document::GaussianBlurEffect>(
               appearance.effects.front().parameters).sigma_x_mm == 500.0);

    // This document uses an explicit 300 DPI raster grid, so the backend renders
    // at that density even when viewport zoom changes. At 500 mm, the 4-sigma
    // support exceeds the Direct2D 16384 px budget and the CPU filter budget.
    // Keep the object centered so culling cannot turn this into a false pass.
    window.root().renderer().camera().set_zoom(64.0);
    window.root().renderer().camera().set_center({52.0, 51.0});

    window.set_language(UILanguage::English);
    assert(!force_canvas_paint(window));
    const auto english_status = window.status_message();
    if (english_status.find("Effect exceeds render limits") == std::string::npos) {
        std::cerr << "Unexpected EN status: " << english_status << '\n';
        for (const auto &diagnostic : window.root().renderer().last_metric().diagnostics)
            std::cerr << "Render diagnostic: " << diagnostic.object_id << " "
                      << diagnostic.error << " " << diagnostic.message << '\n';
    }
    assert(english_status.find("Effect exceeds render limits") != std::string::npos);
    assert(window.root().document().current_snapshot() == authored);
    assert(window.root().document().current_snapshot()->revision() == authored_revision);

    window.set_language(UILanguage::Vietnamese);
    assert(!force_canvas_paint(window));
    const auto vietnamese_status = window.status_message();
    assert(vietnamese_status.find("Hiệu ứng vượt giới hạn render") != std::string::npos);
    assert(window.root().document().current_snapshot() == authored);
    assert(window.root().document().current_snapshot()->revision() == authored_revision);
    std::cout << "[PASS] over-budget effect warning is actionable in EN/VI and preserves the authored source\n";
}
#endif

} // namespace

int main() {
#ifdef _WIN32
    test_over_budget_blur_warning_is_localized_and_keeps_source();
#else
    std::cout << "[NOT_MEASURED] Native Windows canvas paint is unavailable\n";
#endif
    return 0;
}
