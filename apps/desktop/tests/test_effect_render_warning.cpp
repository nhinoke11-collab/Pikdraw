#include "pikdraw/desktop/shell/native_window.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <variant>

using namespace pikdraw::desktop;

namespace {

#ifdef _WIN32
void force_canvas_paint(NativeWindow &window) {
    RECT client{};
    assert(GetClientRect(window.hwnd(), &client) != FALSE);
    const auto canvas = window.canvas_rect(client.right, client.bottom);
    assert(canvas.right > canvas.left && canvas.bottom > canvas.top);

    // Render once to surface the renderer diagnostic, then drive the same
    // NativeWindow WM_PAINT handler that turns it into user-facing status.
    const auto pixels = window.workflow().render_snapshot(
        canvas.right - canvas.left, canvas.bottom - canvas.top);
    assert(!pixels.empty());
    InvalidateRect(window.hwnd(), nullptr, FALSE);
    SendMessageW(window.hwnd(), WM_PAINT, 0, 0);
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
        pikdraw::document::GaussianBlurEffect{300.0, 300.0}));

    const auto authored = window.root().document().current_snapshot();
    assert(authored);
    const auto authored_revision = authored->revision();
    const auto object = authored->objects().get(path_id);
    assert(object && std::holds_alternative<pikdraw::document::PathObject>(*object));
    const auto &appearance = std::get<pikdraw::document::PathObject>(*object).appearance;
    assert(appearance.effects.size() == 1);
    assert(std::get<pikdraw::document::GaussianBlurEffect>(
               appearance.effects.front().parameters).sigma_x_mm == 300.0);

    // Exercise a real limit in either backend: at 64x, the 4-sigma GPU support
    // exceeds 16384 px as well as the software filter budget. Default fit zoom
    // only exceeded the old CPU budget and is renderable by Direct2D.
    window.root().renderer().camera().set_zoom(64.0);
    window.root().renderer().camera().set_center({52.0, 51.0});

    window.set_language(UILanguage::English);
    force_canvas_paint(window);
    const auto english_status = window.status_message();
    assert(english_status.find("Effect exceeds render limits") != std::string::npos);
    assert(window.root().document().current_snapshot() == authored);
    assert(window.root().document().current_snapshot()->revision() == authored_revision);

    window.set_language(UILanguage::Vietnamese);
    force_canvas_paint(window);
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
