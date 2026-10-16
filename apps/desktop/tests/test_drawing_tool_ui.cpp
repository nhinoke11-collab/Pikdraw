#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"

#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>

#ifdef _WIN32
#include <windowsx.h>
#endif

#define REQUIRE(value) do { if (!(value)) \
    throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #value); } while (false)

using namespace pikdraw;

#ifdef _WIN32
namespace {
struct DrawingUiFixture {
    desktop::NativeWindow window{1440, 900};
    std::filesystem::path evidence;

    explicit DrawingUiFixture(std::filesystem::path output) : evidence(std::move(output)) {
        REQUIRE(window.initialize("PikDraw D09 Line and Polyline verification"));
        ShowWindow(window.hwnd(), SW_SHOW);
        SetForegroundWindow(window.hwnd());
        SetFocus(window.hwnd());
        UpdateWindow(window.hwnd());
        window.workflow().new_document(200, 150);
        window.fit_page_to_canvas();
        if (!evidence.empty()) std::filesystem::create_directories(evidence);
    }

    POINT point(double x, double y) {
        const auto screen = window.root().renderer().camera().world_to_screen({x, y});
        return {72 + static_cast<LONG>(std::lround(screen.x)),
                92 + static_cast<LONG>(std::lround(screen.y))};
    }

    void click(double x, double y) {
        const auto position = point(x, y);
        SendMessageW(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON,
                     MAKELPARAM(position.x, position.y));
        SendMessageW(window.hwnd(), WM_LBUTTONUP, 0,
                     MAKELPARAM(position.x, position.y));
    }

    void type_numeric(const std::string& text) {
        for (const unsigned char value : text) {
            if (std::isalpha(value))
                SendMessageW(window.hwnd(), WM_KEYDOWN,
                    static_cast<WPARAM>(std::toupper(value)), 0);
            SendMessageW(window.hwnd(), WM_CHAR, static_cast<WPARAM>(value), 0);
        }
    }

    void capture(const wchar_t* name) {
        if (!evidence.empty())
            window.capture_screenshot_bmp((evidence / name).string());
    }
};

} // namespace
#endif

int main(int argc, char** argv) {
    try {
#ifdef _WIN32
        DrawingUiFixture fixture(argc > 1 ? std::filesystem::path(argv[1])
                                          : std::filesystem::path{});
        auto& window = fixture.window;
        auto& dispatcher = window.root().window().action_dispatcher();
        auto& document = window.root().document();
        REQUIRE(dispatcher.get_action(desktop::actions::ActionId::kToolLine));
        REQUIRE(dispatcher.get_action(desktop::actions::ActionId::kToolPolyline));
        REQUIRE(dispatcher.get_action(desktop::actions::ActionId::kToolSnapPanel));
        REQUIRE(dispatcher.dispatch_shortcut("L"));
        REQUIRE(window.active_tool() == 4);
        REQUIRE(window.drawing_path_active());
        const auto start = fixture.point(20, 30);
        SendMessageW(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON,
                     MAKELPARAM(start.x, start.y));
        if (window.active_tool() != 4 || !window.drawing_path_active())
            throw std::runtime_error("Line lost its active state after click at " +
                std::to_string(start.x) + "," + std::to_string(start.y) +
                " (tool=" + std::to_string(window.active_tool()) + ")");
        REQUIRE(window.drawing_path_point_count() == 1);
        SendMessageW(window.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(start.x, start.y));
        REQUIRE(window.active_tool() == 4 && window.drawing_path_active());
        const auto cursor = fixture.point(90, 30);
        SendMessageW(window.hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(cursor.x, cursor.y));
        const auto snap_deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(2);
        while (!window.drawing_snap_index_ready() &&
               std::chrono::steady_clock::now() < snap_deadline) {
            SendMessageW(window.hwnd(), WM_MOUSEMOVE, 0,
                         MAKELPARAM(cursor.x, cursor.y));
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(window.drawing_snap_index_ready());
        REQUIRE(dispatcher.dispatch_shortcut("F3"));
        REQUIRE(window.drawing_snap_panel_open());
        fixture.capture(L"line-preview-snap-panel.bmp");
        REQUIRE(dispatcher.dispatch_shortcut("F3"));
        fixture.click(90, 30);
        REQUIRE(document.current_snapshot()->object_count() == 1);
        const auto objects_after_line = document.current_snapshot()->objects().all();
        REQUIRE(objects_after_line.size() == 1);
        const auto line = std::get<document::PathObject>((*objects_after_line.begin()).second);
        REQUIRE(line.authoring_source && line.authoring_source->subpaths.front().nodes.size() == 2);
        REQUIRE(!line.has_fill && line.has_stroke);

        REQUIRE(dispatcher.dispatch_shortcut("Shift+L"));
        REQUIRE(window.active_tool() == 5);
        SendMessageW(window.hwnd(), WM_CHAR, 'L', 0);
        for (const char value : std::string("#15;15"))
            SendMessageW(window.hwnd(), WM_CHAR, value, 0);
        SendMessageW(window.hwnd(), WM_KEYDOWN, VK_RETURN, 0);
        REQUIRE(window.drawing_path_point_count() == 1);
        SendMessageW(window.hwnd(), WM_KILLFOCUS, 0, 0);
        REQUIRE(window.active_tool() == 5 && window.drawing_path_active());
        REQUIRE(window.drawing_path_point_count() == 1);
        SendMessageW(window.hwnd(), WM_SETFOCUS, 0, 0);
        REQUIRE(window.active_tool() == 5 && window.drawing_path_active());
        const auto pan = fixture.point(50, 50);
        SendMessageW(window.hwnd(), WM_MBUTTONDOWN, MK_MBUTTON,
                     MAKELPARAM(pan.x, pan.y));
        SendMessageW(window.hwnd(), WM_KILLFOCUS, 0, 0);
        REQUIRE(window.active_tool() == 5 && window.drawing_path_active());
        REQUIRE(window.drawing_path_point_count() == 1);
        SendMessageW(window.hwnd(), WM_SETFOCUS, 0, 0);
        REQUIRE(window.active_tool() == 5 && window.drawing_path_active());

        fixture.type_numeric("@20mm<45deg");
        REQUIRE(window.active_tool() == 5 && window.drawing_path_active());
        SendMessageW(window.hwnd(), WM_KEYDOWN, VK_RETURN, 0);
        REQUIRE(window.drawing_path_point_count() == 2);
        fixture.type_numeric("@20mm<1.5707963267948966rad");
        REQUIRE(window.active_tool() == 5 && window.drawing_path_active());
        SendMessageW(window.hwnd(), WM_KEYDOWN, VK_RETURN, 0);
        REQUIRE(window.drawing_path_point_count() == 3);

        REQUIRE(document.current_snapshot()->object_count() == 1);
        SendMessageW(window.hwnd(), WM_KEYDOWN, VK_RETURN, 0);
        REQUIRE(document.current_snapshot()->object_count() == 2);
        SendMessageW(window.hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
        const auto saved_path = std::filesystem::temp_directory_path() /
            ("pikdraw-d09-ui-" + std::to_string(GetCurrentProcessId()) + ".pikdraw");
        REQUIRE(window.workflow().save_native(saved_path.string()));
        REQUIRE(window.workflow().open_native(saved_path.string()));
        REQUIRE(document.current_snapshot()->object_count() == 2);
        const auto reopened_objects = document.current_snapshot()->objects().all();
        auto reopened_entry = reopened_objects.begin();
        ++reopened_entry;
        const auto reopened = std::get<document::PathObject>((*reopened_entry).second);
        REQUIRE(reopened.authoring_source && reopened.authoring_source->subpaths.front().nodes.size() == 3);
        const geometry::Point2D expected_start{15.0, 15.0};
        REQUIRE(reopened.authoring_source->subpaths.front().nodes.front().anchor == expected_start);
        if (!fixture.evidence.empty()) fixture.capture(L"drawing-tools-save-reopen.bmp");
        std::filesystem::remove(saved_path);
        auto& action_dispatcher = window.root().window().action_dispatcher();
        REQUIRE(action_dispatcher.get_action(desktop::actions::ActionId::kToolLine)->shortcut == "L");
        REQUIRE(action_dispatcher.get_action(desktop::actions::ActionId::kToolPolyline)->shortcut == "Shift+L");
        REQUIRE(action_dispatcher.get_action(desktop::actions::ActionId::kToolSnapPanel)->shortcut == "F3");
        REQUIRE(!action_dispatcher.dispatch_shortcut("F8"));
        REQUIRE(!action_dispatcher.dispatch_shortcut("F10"));
        REQUIRE(!action_dispatcher.dispatch_shortcut("Shift+F2"));
        std::cout << "[PASS] Native Line/Polyline input, snap HUD, bindings, and native save/reopen\n";
#else
        (void)argc;
        (void)argv;
        std::cout << "[NOT_MEASURED] Native Win32 interaction is unavailable on this platform\n";
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
