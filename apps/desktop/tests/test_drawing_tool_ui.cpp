#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include "../src/shell/native_window_menus.hpp"
#include "../src/tools/node_tool_controller.hpp"

#include <algorithm>
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
struct TemporaryNativeFile {
    std::filesystem::path path;
    explicit TemporaryNativeFile(const std::string& name)
        : path(std::filesystem::temp_directory_path() /
               ("pikdraw-r03-" + std::to_string(GetCurrentProcessId()) + name)) {}
    ~TemporaryNativeFile() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

struct DrawingUiFixture {
    desktop::NativeWindow window{1440, 900};
    std::filesystem::path evidence;

    explicit DrawingUiFixture(std::filesystem::path output) : evidence(std::move(output)) {
        REQUIRE(window.initialize("PikDraw D09 Line and Polyline verification"));
        if (window.is_recovery_modal_open())
            SendMessageW(window.hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
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
        const RECT canvas = window.canvas_rect(1440, 900);
        return {canvas.left + static_cast<LONG>(std::lround(screen.x)),
                canvas.top + static_cast<LONG>(std::lround(screen.y))};
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

    void require_line_preview(POINT first, POINT last) {
        // Inspect a rendered canvas bitmap, so a tool-state-only pass cannot hide
        // a missing overlay. Avoid endpoints, marker handles and the floating HUD.
        TemporaryNativeFile bitmap("-line-preview.bmp");
        window.capture_screenshot_bmp(bitmap.path.string());
        HBITMAP image = static_cast<HBITMAP>(LoadImageW(nullptr, bitmap.path.c_str(),
            IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION));
        REQUIRE(image);
        HDC dc = CreateCompatibleDC(nullptr);
        const auto previous = SelectObject(dc, image);
        int preview_pixels = 0;
        for (int percent = 20; percent <= 80; ++percent) {
            const int x = first.x + (last.x - first.x) * percent / 100;
            const int y = first.y + (last.y - first.y) * percent / 100;
            for (int dy = -2; dy <= 2; ++dy)
                if (GetPixel(dc, x, y + dy) == RGB(0, 196, 255))
                    ++preview_pixels;
        }
        SelectObject(dc, previous);
        DeleteDC(dc);
        DeleteObject(image);
        REQUIRE(preview_pixels >= 8);
    }
};

} // namespace
#endif

#ifdef _WIN32
// Given the native shell, when per-monitor DPI changes, paint and input share DIP.
void test_per_monitor_dpi_mapping(const std::filesystem::path& evidence) {
    desktop::NativeWindow window(1440, 900);
    REQUIRE(window.dip(100) == 100); // Headless default.
    REQUIRE(window.initialize("PikDraw DPI regression"));
    if (window.is_recovery_modal_open())
        SendMessageW(window.hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
    RECT bounds{}; GetWindowRect(window.hwnd(), &bounds);
    const auto revision = window.root().document().revision();
    for (const unsigned dpi : {120u, 144u, 192u, 96u}) {
        SendMessageW(window.hwnd(), WM_DPICHANGED, MAKEWPARAM(dpi, dpi),
                     reinterpret_cast<LPARAM>(&bounds));
        REQUIRE(window.dip(96) == static_cast<int>(dpi));
        RECT client{}; REQUIRE(GetClientRect(window.hwnd(), &client));
        const auto camera_before = window.root().renderer().camera();
        TemporaryNativeFile bitmap("-dpi-" + std::to_string(dpi) + ".bmp");
        window.capture_screenshot_bmp(bitmap.path.string());
        const auto image = static_cast<HBITMAP>(LoadImageW(nullptr, bitmap.path.c_str(),
            IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION));
        REQUIRE(image);
        BITMAP image_info{}; REQUIRE(GetObjectW(image, sizeof(image_info), &image_info));
        REQUIRE(std::abs(image_info.bmWidth - client.right) <= 1);
        REQUIRE(std::abs(image_info.bmHeight - client.bottom) <= 1);
        const auto image_dc = CreateCompatibleDC(nullptr);
        const auto old_image = SelectObject(image_dc, image);
        const auto area = window.canvas_rect(
            static_cast<int>(std::lround(client.right * 96.0 / dpi)),
            static_cast<int>(std::lround(client.bottom * 96.0 / dpi)));
        REQUIRE(GetPixel(image_dc, window.dip((area.left + area.right) / 2),
            window.dip((area.top + area.bottom) / 2)) == RGB(255, 255, 255));
        SelectObject(image_dc, old_image);
        DeleteDC(image_dc);
        DeleteObject(image);
        if (!evidence.empty()) {
            std::filesystem::create_directories(evidence);
            window.capture_screenshot_bmp((evidence / ("dpi-" + std::to_string(dpi) + ".bmp")).string());
        }
        const auto& camera_after = window.root().renderer().camera();
        REQUIRE(camera_after.width() == camera_before.width());
        REQUIRE(camera_after.height() == camera_before.height());
        REQUIRE(camera_after.zoom() == camera_before.zoom());
        // The first toolbox cell is Pick at DIP (26, 122).
        window.set_active_tool(6);
        SendMessageW(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON,
            MAKELPARAM(window.dip(26), window.dip(122)));
        SendMessageW(window.hwnd(), WM_LBUTTONUP, 0,
            MAKELPARAM(window.dip(26), window.dip(122)));
        REQUIRE(window.active_tool() == 0);
        REQUIRE(window.root().document().revision() == revision);
    }
}
#endif

int main(int argc, char** argv) {
    try {
#ifdef _WIN32
        test_per_monitor_dpi_mapping(argc > 1 ? std::filesystem::path(argv[1])
                                             : std::filesystem::path{});
#endif
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
        REQUIRE(window.active_tool() == 6);
        REQUIRE(window.drawing_path_active());
        const auto start = fixture.point(20, 30);
        SendMessageW(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON,
                     MAKELPARAM(start.x, start.y));
        if (window.active_tool() != 6 || !window.drawing_path_active())
            throw std::runtime_error("Line lost its active state after click at " +
                std::to_string(start.x) + "," + std::to_string(start.y) +
                " (tool=" + std::to_string(window.active_tool()) + ")");
        REQUIRE(window.drawing_path_point_count() == 1);
        SendMessageW(window.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(start.x, start.y));
        REQUIRE(window.active_tool() == 6 && window.drawing_path_active());
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
        fixture.require_line_preview(start, cursor);
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
        REQUIRE(window.active_tool() == 7);
        SendMessageW(window.hwnd(), WM_CHAR, 'L', 0);
        for (const char value : std::string("#15;15"))
            SendMessageW(window.hwnd(), WM_CHAR, value, 0);
        SendMessageW(window.hwnd(), WM_KEYDOWN, VK_RETURN, 0);
        REQUIRE(window.drawing_path_point_count() == 1);
        SendMessageW(window.hwnd(), WM_KILLFOCUS, 0, 0);
        REQUIRE(window.active_tool() == 7 && window.drawing_path_active());
        REQUIRE(window.drawing_path_point_count() == 1);
        SendMessageW(window.hwnd(), WM_SETFOCUS, 0, 0);
        REQUIRE(window.active_tool() == 7 && window.drawing_path_active());
        const auto pan = fixture.point(50, 50);
        SendMessageW(window.hwnd(), WM_MBUTTONDOWN, MK_MBUTTON,
                     MAKELPARAM(pan.x, pan.y));
        SendMessageW(window.hwnd(), WM_KILLFOCUS, 0, 0);
        REQUIRE(window.active_tool() == 7 && window.drawing_path_active());
        REQUIRE(window.drawing_path_point_count() == 1);
        SendMessageW(window.hwnd(), WM_SETFOCUS, 0, 0);
        REQUIRE(window.active_tool() == 7 && window.drawing_path_active());

        fixture.type_numeric("@20mm<45deg");
        REQUIRE(window.active_tool() == 7 && window.drawing_path_active());
        SendMessageW(window.hwnd(), WM_KEYDOWN, VK_RETURN, 0);
        REQUIRE(window.drawing_path_point_count() == 2);
        fixture.type_numeric("@20mm<1.5707963267948966rad");
        REQUIRE(window.active_tool() == 7 && window.drawing_path_active());
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

        const auto node_menu = std::find_if(desktop::kToolsMenuItems.begin(),
            desktop::kToolsMenuItems.end(), [](const desktop::MenuItem& item) {
                return item.action == desktop::MenuActionId::ToolNode;
            });
        REQUIRE(node_menu != desktop::kToolsMenuItems.end() && !node_menu->is_disabled);
        REQUIRE(action_dispatcher.get_action(desktop::actions::ActionId::kToolNodeEdit));
        REQUIRE(action_dispatcher.dispatch(desktop::actions::ActionId::kToolNodeEdit));
        REQUIRE(window.active_tool() == 11 && window.node_tool()->active());
        window.execute_menu_action(static_cast<int>(desktop::MenuActionId::ToolNode));
        REQUIRE(window.active_tool() == 11 && window.node_tool()->active());

        // Direct click on Row 1 (Node tool on primary toolbox strip directly next to Pick tool).
        window.set_active_tool(0);
        window.handle_mouse_down(26, 172);
        REQUIRE(window.active_tool() == 11 && window.node_tool()->active());

        // The Path Drawing flyout also continues to expose Node to mouse and keyboard users.
        window.set_active_tool(6);
        window.handle_mouse_down(42, 256);
        REQUIRE(window.toolbox_open_group() == 2);
        if (!fixture.evidence.empty()) fixture.capture(L"node-tool-flyout.bmp");
        window.handle_mouse_down(120, 365);
        REQUIRE(window.active_tool() == 11 && window.node_tool()->active());
        window.set_active_tool(6);
        REQUIRE(window.handle_toolbox_key(VK_TAB));
        REQUIRE(window.handle_toolbox_key(VK_TAB));
        REQUIRE(window.handle_toolbox_key(VK_DOWN, true));
        for (int i = 0; i < 3; ++i) REQUIRE(window.handle_toolbox_key(VK_DOWN));
        REQUIRE(window.handle_toolbox_key(VK_RETURN));
        REQUIRE(window.active_tool() == 11 && window.node_tool()->active());

        // Select a real node, then exercise its Inspector mode and X/Y controls.
        auto* node_tool = window.node_tool();
        const auto target_id = node_tool->target_object();
        const auto node_snapshot = document.current_snapshot();
        REQUIRE(target_id != document::kInvalidObjectId && node_snapshot);
        const auto* target_value = node_snapshot->objects().lookup_ref(target_id);
        REQUIRE(target_value);
        const auto* target = std::get_if<document::PathObject>(target_value);
        REQUIRE(target && target->authoring_source &&
                !target->authoring_source->subpaths.empty());
        const auto original_node = target->authoring_source->subpaths.front().nodes.front();
        const auto node_screen = fixture.point(original_node.anchor.x, original_node.anchor.y);
        window.handle_mouse_down(node_screen.x, node_screen.y);
        window.handle_mouse_up(node_screen.x, node_screen.y);
        REQUIRE(node_tool->selected_node_count() == 1);
        REQUIRE(node_tool->single_selected_node_position());
        const auto original_position = *node_tool->single_selected_node_position();
        const auto original_mode = node_tool->selected_nodes_mode();
        REQUIRE(original_mode);

        const int inspector_left = window.width() - 280;
        const int button_width = (252 - 12) / 3;
        const int smooth_button_x = inspector_left + 14 + button_width + 6 + button_width / 2;
        window.handle_mouse_down(smooth_button_x, 288);
        REQUIRE(node_tool->selected_nodes_mode() == document::PathNodeMode::Smooth);
        REQUIRE(document.undo());
        REQUIRE(node_tool->selected_nodes_mode() == original_mode);

        const int coordinate_x = inspector_left + 160;
        window.handle_mouse_down(coordinate_x, 196);
        REQUIRE(window.active_numeric_field() == desktop::NativeWindow::NumericFieldId::ObjectX);
        for (const wchar_t ch : std::wstring(L"24.5")) window.handle_numeric_char_input(ch);
        window.handle_numeric_char_input(9); // Tab commits X and focuses Y.
        REQUIRE(window.active_numeric_field() == desktop::NativeWindow::NumericFieldId::ObjectY);
        for (const wchar_t ch : std::wstring(L"35")) window.handle_numeric_char_input(ch);
        window.handle_numeric_char_input(13);
        auto moved_position = node_tool->single_selected_node_position();
        REQUIRE(moved_position && std::abs(moved_position->x - 24.5) < 1e-6);
        REQUIRE(std::abs(moved_position->y - 35.0) < 1e-6);
        REQUIRE(document.undo() && document.undo());
        auto restored_position = node_tool->single_selected_node_position();
        REQUIRE(restored_position && std::abs(restored_position->x - original_position.x) < 1e-6);
        REQUIRE(std::abs(restored_position->y - original_position.y) < 1e-6);

        window.handle_mouse_down(coordinate_x, 196);
        for (const wchar_t ch : std::wstring(L"20001")) window.handle_numeric_char_input(ch);
        window.handle_numeric_char_input(13);
        REQUIRE(window.active_numeric_field() == desktop::NativeWindow::NumericFieldId::ObjectX);
        REQUIRE(node_tool->single_selected_node_position() == restored_position);
        window.handle_numeric_char_input(27);
        window.handle_mouse_down(coordinate_x, 196);
        for (const wchar_t ch : std::wstring(L"99")) window.handle_numeric_char_input(ch);
        window.handle_numeric_char_input(27);
        REQUIRE(node_tool->single_selected_node_position() == restored_position);
        if (!fixture.evidence.empty()) fixture.capture(L"node-tool-properties.bmp");

        window.set_active_tool(6);
        const auto first_document_objects = document.current_snapshot()->object_count();
        TemporaryNativeFile first_file("-first.pikdraw");
        TemporaryNativeFile second_file("-second.pikdraw");
        window.active_tab().file_path = first_file.path.string();
        window.execute_menu_action(static_cast<int>(desktop::MenuActionId::FileSave));
        REQUIRE(std::filesystem::exists(first_file.path));
        window.create_new_tab("Second Document", 200.0, 150.0);
        REQUIRE(window.active_tab().file_path.empty());
        auto& second_document = window.root().document();
        auto& second_dispatcher = window.root().window().action_dispatcher();
        REQUIRE(second_document.current_snapshot()->object_count() == 0);
        REQUIRE(second_dispatcher.get_action(desktop::actions::ActionId::kToolLine));
        REQUIRE(window.active_tool() == 6 && window.drawing_path_active());
        REQUIRE(second_dispatcher.dispatch_shortcut("L"));
        fixture.click(20, 30);
        fixture.click(90, 70);
        REQUIRE(second_document.current_snapshot()->object_count() == 1);
        REQUIRE(document.current_snapshot()->object_count() == first_document_objects);
        window.active_tab().file_path = second_file.path.string();
        window.execute_menu_action(static_cast<int>(desktop::MenuActionId::FileSave));
        REQUIRE(std::filesystem::exists(second_file.path));

        window.switch_to_tab(0);
        REQUIRE(window.active_tab().file_path == first_file.path.string());
        window.execute_menu_action(static_cast<int>(desktop::MenuActionId::FileSave));
        REQUIRE(window.active_tool() == 6 && window.drawing_path_active());
        REQUIRE(document.current_snapshot()->object_count() == first_document_objects);
        window.set_active_tool(0);
        REQUIRE(window.workflow().open_native(first_file.path.string()));
        REQUIRE(document.current_snapshot()->object_count() == first_document_objects);
        window.switch_to_tab(1);
        REQUIRE(window.workflow().open_native(second_file.path.string()));
        REQUIRE(second_document.current_snapshot()->object_count() == 1);
        window.close_tab(1);
        REQUIRE(window.active_tab().file_path == first_file.path.string());
        window.execute_menu_action(static_cast<int>(desktop::MenuActionId::FileSave));
        window.close_tab(0);
        REQUIRE(window.active_tab().file_path.empty());
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
