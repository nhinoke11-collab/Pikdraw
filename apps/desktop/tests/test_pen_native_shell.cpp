#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/document/model/editable_path.hpp"
#include "pikdraw/document/model/object.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <variant>

using namespace pikdraw;
using namespace pikdraw::desktop;

namespace {

void click_canvas(NativeWindow& window, int x, int y) {
    window.handle_mouse_down(x, y);
    window.handle_mouse_up(x, y);
}

document::PathObject only_path(const NativeWindow& window) {
    const auto snapshot = window.root().document().current_snapshot();
    assert(snapshot && snapshot->object_count() == 1);
    const auto& ids = window.root().vector_controller().selection().selected_ids();
    assert(ids.size() == 1);
    const auto object = snapshot->objects().get(ids.front());
    assert(object);
    const auto* path = std::get_if<document::PathObject>(&*object);
    assert(path && path->authoring_source);
    return *path;
}

RECT drawing_area(NativeWindow& window) {
    return window.canvas_rect(1440, 900);
}

void test_native_pen_clicks_make_editable_lines_and_enter_finishes() {
    // Given an empty document and the Pen tool, when the user clicks two anchors
    // and presses Enter, then one selected editable path with a Line segment is committed.
    NativeWindow window(1440, 900);
    window.set_active_tool(8);
    assert(window.active_tool() == 8);

    const RECT area = drawing_area(window);
    const int y = area.top + (area.bottom - area.top) / 2;
    click_canvas(window, area.left + 120, y);
    click_canvas(window, area.left + 260, y);
    assert(window.root().document().current_snapshot()->object_count() == 0);

    assert(window.handle_key_down(VK_RETURN));
    assert(window.active_tool() == 0);
    const auto path = only_path(window);
    const auto& subpath = path.authoring_source->subpaths.front();
    assert(subpath.nodes.size() == 2);
    assert(subpath.segments.size() == 1);
    assert(subpath.segments.front().kind == document::PathSegmentKind::Line);
}

void test_native_pen_drag_makes_cubic_and_escape_finishes() {
    // Given the Pen tool, when the second anchor is dragged and Esc is pressed,
    // then the committed source keeps a cubic verb and its editable handles.
    NativeWindow window(1440, 900);
    window.set_active_tool(8);

    const RECT area = drawing_area(window);
    const int start_x = area.left + 120;
    const int start_y = area.top + (area.bottom - area.top) / 2;
    click_canvas(window, start_x, start_y);
    window.handle_mouse_down(start_x + 180, start_y);
    window.handle_mouse_move(start_x + 180, start_y - 45);
    window.handle_mouse_up(start_x + 180, start_y - 45);

    assert(window.handle_key_down(VK_ESCAPE));
    assert(window.active_tool() == 0);
    const auto path = only_path(window);
    const auto& subpath = path.authoring_source->subpaths.front();
    assert(subpath.nodes.size() == 2);
    assert(subpath.segments.size() == 1);
    assert(subpath.segments.front().kind == document::PathSegmentKind::CubicBezier);
    assert(subpath.nodes.back().has_incoming_handle);
}

void test_native_pen_numeric_absolute_first_anchor_and_cancel_are_atomic() {
    // Given a new Pen draft, when #X;Y is committed as the first point,
    // then the path uses that world coordinate; cancelling a later one-point draft
    // leaves the document revision and object count unchanged.
    NativeWindow window(1440, 900);
    const auto before = window.root().document().current_snapshot();
    assert(before);
    window.set_active_tool(8);
    window.start_numeric_input(NativeWindow::NumericFieldId::PenExpression);
    for (const wchar_t ch : std::wstring(L"#100;80"))
        window.handle_numeric_char_input(ch);
    window.commit_numeric_field();
    assert(window.active_numeric_field() == NativeWindow::NumericFieldId::None);
    assert(window.root().document().current_snapshot()->object_count() == 0);

    const RECT area = drawing_area(window);
    const int y = area.top + (area.bottom - area.top) / 2;
    click_canvas(window, area.left + 260, y);
    assert(window.handle_key_down(VK_RETURN));
    const auto path = only_path(window);
    const auto& subpath = path.authoring_source->subpaths.front();
    assert(subpath.nodes.size() == 2);
    assert(std::abs(subpath.nodes.front().anchor.x - 100.0) < 1e-9);
    assert(std::abs(subpath.nodes.front().anchor.y - 80.0) < 1e-9);

    NativeWindow cancelled(1440, 900);
    const auto cancel_before = cancelled.root().document().current_snapshot();
    cancelled.set_active_tool(8);
    click_canvas(cancelled, area.left + 120, y);
    cancelled.set_active_tool(0); // An open draft cannot be silently discarded.
    assert(cancelled.active_tool() == 8);
    assert(cancelled.handle_key_down(VK_ESCAPE)); // A one-anchor draft is cancelled.
    assert(cancelled.active_tool() == 0);
    const auto cancel_after = cancelled.root().document().current_snapshot();
    assert(cancel_after && cancel_before);
    assert(cancel_after->revision() == cancel_before->revision());
    assert(cancel_after->object_count() == 0);
}

void test_native_pen_dimension_length_towards_cursor() {
    NativeWindow window(1440, 900);
    window.set_active_tool(8);
    const RECT area = drawing_area(window);
    const int start_x = area.left + 120;
    const int start_y = area.top + (area.bottom - area.top) / 2;
    click_canvas(window, start_x, start_y);

    // Mouse points horizontally to the right (+X)
    window.handle_mouse_move(start_x + 100, start_y);

    // Press 'L' to activate length input
    assert(window.handle_key_down('L'));
    assert(window.active_numeric_field() == NativeWindow::NumericFieldId::PenLength);

    // Type 60 mm
    window.handle_numeric_char_input(L'6');
    window.handle_numeric_char_input(L'0');

    // Press Enter to commit line segment
    assert(window.handle_key_down(VK_RETURN));
    assert(window.active_numeric_field() == NativeWindow::NumericFieldId::None);

    // Press Enter again to finish path session
    assert(window.handle_key_down(VK_RETURN));
    assert(window.active_tool() == 0);

    const auto path = only_path(window);
    const auto& subpath = path.authoring_source->subpaths.front();
    assert(subpath.nodes.size() == 2);
    const double dx = subpath.nodes[1].anchor.x - subpath.nodes[0].anchor.x;
    const double dy = subpath.nodes[1].anchor.y - subpath.nodes[0].anchor.y;
    const double length = std::hypot(dx, dy);
    assert(std::abs(length - 60.0) < 1e-3);
    assert(std::abs(dx - 60.0) < 1e-3);
    assert(std::abs(dy) < 1e-3);
}

void test_native_pen_dimension_length_tab_angle() {
    NativeWindow window(1440, 900);
    window.set_active_tool(8);
    const RECT area = drawing_area(window);
    const int start_x = area.left + 150;
    const int start_y = area.top + (area.bottom - area.top) / 2;
    click_canvas(window, start_x, start_y);

    // Press 'L' to activate length input
    assert(window.handle_key_down('L'));
    assert(window.active_numeric_field() == NativeWindow::NumericFieldId::PenLength);

    // Type 50 mm
    window.handle_numeric_char_input(L'5');
    window.handle_numeric_char_input(L'0');

    // Press Tab to switch to Angle input
    assert(window.handle_key_down(VK_TAB));
    assert(window.active_numeric_field() == NativeWindow::NumericFieldId::PenAngle);

    // Type 30 (degrees relative to horizontal X-axis)
    window.handle_numeric_char_input(L'3');
    window.handle_numeric_char_input(L'0');

    // Press Enter to commit line segment with L=50, Angle=30°
    assert(window.handle_key_down(VK_RETURN));
    assert(window.active_numeric_field() == NativeWindow::NumericFieldId::None);

    // Press Enter again to finish pen path
    assert(window.handle_key_down(VK_RETURN));
    assert(window.active_tool() == 0);

    const auto path = only_path(window);
    const auto& subpath = path.authoring_source->subpaths.front();
    assert(subpath.nodes.size() == 2);
    const double dx = subpath.nodes[1].anchor.x - subpath.nodes[0].anchor.x;
    const double dy = subpath.nodes[1].anchor.y - subpath.nodes[0].anchor.y;
    const double length = std::hypot(dx, dy);
    assert(std::abs(length - 50.0) < 1e-3);

    constexpr double kPi = 3.14159265358979323846;
    const double expected_dx = 50.0 * std::cos(30.0 * kPi / 180.0);
    const double expected_dy = 50.0 * std::sin(30.0 * kPi / 180.0);
    assert(std::abs(dx - expected_dx) < 1e-3);
    assert(std::abs(dy - expected_dy) < 1e-3);
}

} // namespace

int main() {
    test_native_pen_clicks_make_editable_lines_and_enter_finishes();
    test_native_pen_drag_makes_cubic_and_escape_finishes();
    test_native_pen_numeric_absolute_first_anchor_and_cancel_are_atomic();
    test_native_pen_dimension_length_towards_cursor();
    test_native_pen_dimension_length_tab_angle();
    std::cout << "Native Pen shell acceptance passed.\n";
    return 0;
}
