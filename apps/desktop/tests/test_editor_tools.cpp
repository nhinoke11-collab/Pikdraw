#include "pikdraw/desktop/shell/native_window.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#define REQUIRE(value)                                                                                       \
    do {                                                                                                     \
        if (!(value))                                                                                        \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #value);                      \
    } while (false)
using namespace pikdraw;

#ifdef _WIN32
namespace {
struct EditorFixture {
    desktop::NativeWindow window{1440, 900};
    std::filesystem::path evidence;
    int width{}, height{};
    explicit EditorFixture(std::filesystem::path output) : evidence(std::move(output)) {
        REQUIRE(window.initialize("PikDraw editor tools verification"));
        ShowWindow(window.hwnd(), SW_HIDE);
        RECT client{};
        GetClientRect(window.hwnd(), &client);
        width = client.right;
        height = client.bottom;
        window.workflow().new_document(200, 150);
        window.fit_page_to_canvas();
        if (!evidence.empty())
            std::filesystem::create_directories(evidence);
    }
    POINT point(double x, double y) {
        const auto screen = window.root().renderer().camera().world_to_screen({x, y});
        return {72 + static_cast<LONG>(std::lround(screen.x)), 92 + static_cast<LONG>(std::lround(screen.y))};
    }
    void mouse(UINT message, POINT p, WPARAM buttons = 0) {
        SendMessageW(window.hwnd(), message, buttons, MAKELPARAM(p.x, p.y));
    }
    void click(double x, double y, WPARAM modifiers = 0) {
        const auto p = point(x, y);
        mouse(WM_LBUTTONDOWN, p, MK_LBUTTON | modifiers);
        mouse(WM_LBUTTONUP, p, modifiers);
    }
    void capture(const char *name) {
        if (!evidence.empty())
            window.capture_screenshot_bmp((evidence / name).string());
    }
    document::PathObject object(document::ObjectId id) {
        return std::get<document::PathObject>(*window.root().document().current_snapshot()->get_object(id));
    }
};
struct DialogScript {
    bool cancel{false}, stroke{false}, seen{false};
    int attempts{0};
};
DialogScript *active_script = nullptr;
void CALLBACK edit_dialog(HWND owner, UINT, UINT_PTR timer, DWORD) {
    if (!active_script)
        return;
    const auto dialog = GetLastActivePopup(owner);
    if (dialog == owner || !GetDlgItem(dialog, 201)) {
        if (++active_script->attempts > 100 && dialog != owner)
            PostMessageW(dialog, WM_CLOSE, 0, 0);
        return;
    }
    KillTimer(owner, timer);
    active_script->seen = true;
    if (!active_script->cancel) {
        if (active_script->stroke) {
            SetDlgItemTextW(dialog, 206, L"3.125");
        } else {
            SendDlgItemMessageW(dialog, 100, CB_SETCURSEL, 1, 0);
            SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(100, CBN_SELCHANGE),
                         reinterpret_cast<LPARAM>(GetDlgItem(dialog, 100)));
            const wchar_t *values[] = {L"25", L"50", L"75", L"10", L"80"};
            for (int i = 0; i < 5; ++i)
                SetDlgItemTextW(dialog, 201 + i, values[i]);
        }
    }
    PostMessageW(dialog, WM_COMMAND, active_script->cancel ? IDCANCEL : IDOK, 0);
}
void exercise_dialog(EditorFixture &f, bool stroke, bool cancel) {
    const int dock = f.width - 280;
    f.mouse(WM_LBUTTONDOWN, {dock + (stroke ? 200 : 70), 108 + 94}, MK_LBUTTON);
    DialogScript script{cancel, stroke};
    active_script = &script;
    REQUIRE(SetTimer(f.window.hwnd(), 991, 20, edit_dialog));
    f.mouse(WM_LBUTTONDOWN, {dock + 130, 108 + 140}, MK_LBUTTON);
    KillTimer(f.window.hwnd(), 991);
    active_script = nullptr;
    REQUIRE(script.seen);
}
void verify_editor(EditorFixture &f) {
    auto &window = f.window;
    auto &doc = window.root().document();
    auto &selection = window.root().vector_controller().selection();
    const auto a = window.workflow().add_rectangle(20, 30, 20, 20, "#00a6cc", "#222222", .5);
    const auto b = window.workflow().add_rectangle(60, 30, 20, 20, "#f5b83d", "#222222", .5);
    REQUIRE(a && b);
    const auto original = doc.current_snapshot();
    const auto start = f.point(5, 65), finish = f.point(70, 15);
    f.mouse(WM_LBUTTONDOWN, start, MK_LBUTTON);
    f.mouse(WM_MOUSEMOVE, finish, MK_LBUTTON);
    REQUIRE(doc.current_snapshot() == original);
    f.capture("window-selection.bmp");
    f.mouse(WM_LBUTTONUP, finish);
    REQUIRE(selection.count() == 1 && selection.is_selected(a));
    const auto reverse_start = f.point(70, 65), reverse_end = f.point(5, 15);
    f.mouse(WM_LBUTTONDOWN, reverse_start, MK_LBUTTON);
    f.mouse(WM_MOUSEMOVE, reverse_end, MK_LBUTTON);
    f.capture("crossing-selection.bmp");
    f.mouse(WM_LBUTTONUP, reverse_end);
    REQUIRE(selection.count() == 2 && selection.is_selected(a) && selection.is_selected(b));
    f.click(30, 40, MK_CONTROL);
    REQUIRE(selection.count() == 1 && selection.is_selected(b));
    f.click(30, 40, MK_SHIFT);
    REQUIRE(selection.count() == 2);
    REQUIRE(doc.current_snapshot() == original);

    window.workflow().select_object(a);
    const auto move_start = f.point(30, 40), move_end = f.point(35, 47);
    f.mouse(WM_LBUTTONDOWN, move_start, MK_LBUTTON);
    f.mouse(WM_MOUSEMOVE, move_end, MK_LBUTTON);
    REQUIRE(doc.current_snapshot() == original);
    f.mouse(WM_LBUTTONUP, move_end);
    REQUIRE(doc.revision() == original->revision() + 1);
    REQUIRE(std::abs(f.object(a).bounds().x - 25) < .3 && std::abs(f.object(a).bounds().y - 37) < .3);
    REQUIRE(doc.undo());
    REQUIRE(f.object(a).bounds().approx_equal({20, 30, 20, 20}));

    window.workflow().select_object(a);
    const auto resize_start = f.point(40, 50), resize_end = f.point(50, 55);
    const auto revision = doc.revision();
    f.mouse(WM_LBUTTONDOWN, resize_start, MK_LBUTTON);
    f.mouse(WM_MOUSEMOVE, resize_end, MK_LBUTTON | MK_SHIFT);
    REQUIRE(doc.revision() == revision);
    f.mouse(WM_LBUTTONUP, resize_end, MK_SHIFT);
    const auto resized = f.object(a).bounds();
    REQUIRE(std::abs(resized.width - 30) < .4 && std::abs(resized.width - resized.height) < 1e-6);
    REQUIRE(std::abs(resized.x - 20) < 1e-6 && std::abs(resized.y - 30) < 1e-6);
    REQUIRE(doc.revision() == revision + 1);
    REQUIRE(doc.undo());
    window.workflow().select_object(a);
    const auto before_cancel = doc.current_snapshot();
    f.mouse(WM_LBUTTONDOWN, move_start, MK_LBUTTON);
    f.mouse(WM_MOUSEMOVE, move_end, MK_LBUTTON);
    SendMessageW(window.hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
    f.mouse(WM_LBUTTONUP, move_end);
    REQUIRE(doc.current_snapshot() == before_cancel);

    // Exercise actual painted palette hit regions, including right-click stroke.
    const int dock = f.width - 280;
    f.mouse(WM_LBUTTONDOWN, {dock + 200, 108 + 310}, MK_LBUTTON); // CMYK tab
    f.mouse(WM_LBUTTONUP, {dock + 200, 108 + 310});
    f.mouse(WM_LBUTTONDOWN, {dock + 12 + 4 * 28 + 10, 108 + 348}, MK_LBUTTON);
    f.mouse(WM_LBUTTONUP, {dock + 12 + 4 * 28 + 10, 108 + 348});
    REQUIRE(f.object(a).fill.color_space() == color::ColorSpace::Cmyk);
    const auto red = std::get<color::CMYKColor>(f.object(a).fill.payload());
    REQUIRE(red.c == 0 && red.m == 1 && red.y == 1 && red.k == 0);
    f.mouse(WM_RBUTTONDOWN, {dock + 22, 108 + 376});
    REQUIRE(f.object(a).stroke.color_space() == color::ColorSpace::Cmyk);
    const auto cyan = std::get<color::CMYKColor>(f.object(a).stroke.payload());
    REQUIRE(cyan.c == 1 && cyan.m == 0 && cyan.y == 0 && cyan.k == 0);
    const auto before_dialog = doc.revision();
    exercise_dialog(f, false, false);
    const auto numeric_color = std::get<color::CMYKColor>(f.object(a).fill.payload());
    REQUIRE(numeric_color.approx_equal({.25, .5, .75, .1, .8}, 1e-12));
    REQUIRE(doc.revision() == before_dialog + 1);
    const auto before_cancel_dialog = doc.current_snapshot();
    exercise_dialog(f, false, true);
    REQUIRE(doc.current_snapshot() == before_cancel_dialog);
    exercise_dialog(f, true, false);
    REQUIRE(f.object(a).stroke_width_mm == 3.125);
    REQUIRE(std::get<color::CMYKColor>(f.object(a).stroke.payload()).approx_equal(cyan, 1e-12));
    // Return palette target to fill for the evidence and subsequent color operations.
    f.mouse(WM_LBUTTONDOWN, {dock + 70, 108 + 94}, MK_LBUTTON);

    window.root().vector_controller().set_selection({a, b}, *doc.current_snapshot());
    const auto before_style = doc.revision();
    const auto source = color::ColorValue::make_cmyk(.125, .5, .75, .2);
    REQUIRE(window.apply_selection_color(false, source));
    REQUIRE(doc.revision() == before_style + 1);
    REQUIRE(f.object(a).fill.color_space() == color::ColorSpace::Cmyk &&
            f.object(b).fill.color_space() == color::ColorSpace::Cmyk);
    REQUIRE(doc.undo());
    REQUIRE(std::get<color::CMYKColor>(f.object(a).fill.payload()).approx_equal(numeric_color, 1e-12));
    REQUIRE(f.object(b).fill.color_space() == color::ColorSpace::Rgb);
    REQUIRE(doc.redo());
    REQUIRE(window.set_selection_stroke_width(2.25));
    REQUIRE(f.object(a).stroke_width_mm == 2.25 && f.object(b).stroke_width_mm == 2.25);
    REQUIRE(!window.set_selection_stroke_width(-1));
    const auto stable = doc.current_snapshot();
    REQUIRE(!window.apply_selection_color(false, color::ColorValue::make_cmyk(2, 0, 0, 0)));
    REQUIRE(doc.current_snapshot() == stable);
    REQUIRE(window.set_selection_fill_enabled(false));
    REQUIRE(!f.object(a).has_fill && !f.object(b).has_fill);
    REQUIRE(doc.undo());
    REQUIRE(f.object(a).has_fill && f.object(b).has_fill);

    // Real ICC preview must affect pixels while leaving source/revision untouched.
    const auto srgb = color::IccPreview::system_srgb_profile();
    const auto swop = srgb.parent_path() / "RSWOP.icm";
    if (std::filesystem::is_regular_file(swop)) {
        const auto before_profile = doc.current_snapshot();
        std::string error;
        REQUIRE(window.configure_preview_profiles({}, swop, &error));
        REQUIRE(doc.current_snapshot() == before_profile);
        const auto expected = color::IccPreview::create({}, swop)->convert(source).to_device();
        const auto &camera = window.root().renderer().camera();
        const auto pixels = window.workflow().render_snapshot(camera.width(), camera.height());
        const auto center = camera.world_to_screen({30, 40});
        const auto index =
            (static_cast<size_t>(center.y) * camera.width() + static_cast<size_t>(center.x)) * 4;
        REQUIRE(std::abs(static_cast<int>(pixels[index]) - expected.r) <= 1);
        REQUIRE(std::abs(static_cast<int>(pixels[index + 1]) - expected.g) <= 1);
        REQUIRE(std::abs(static_cast<int>(pixels[index + 2]) - expected.b) <= 1);
        REQUIRE(!window.configure_preview_profiles("missing-pikdraw.icc", {}, &error));
        REQUIRE(doc.current_snapshot() == before_profile);
    }
    const auto native = std::filesystem::temp_directory_path() /
                        ("pikdraw-editor-tools-" + std::to_string(GetCurrentProcessId()) + ".pikdraw");
    const auto old_epoch = doc.epoch();
    REQUIRE(window.workflow().save_native(native.string()));
    REQUIRE(!doc.is_dirty());
    REQUIRE(window.workflow().open_native(native.string()));
    REQUIRE(doc.epoch() > old_epoch && selection.empty());
    const auto reopened = std::get<color::CMYKColor>(f.object(a).fill.payload());
    REQUIRE(reopened.approx_equal(std::get<color::CMYKColor>(source.payload()), 1e-12));
    REQUIRE(f.object(a).stroke_width_mm == 2.25 && f.object(a).has_fill && f.object(a).has_stroke);
    REQUIRE(std::filesystem::remove(native));
    window.root().vector_controller().set_selection({a}, *doc.current_snapshot());
    f.capture("pick-style-dark.bmp");
    window.set_theme(desktop::ThemeMode::Light);
    f.capture("pick-style-light.bmp");

    // Empty selection edits set typed defaults; creating a shape is still one undo unit.
    window.workflow().new_document(200, 150);
    const auto empty_revision = doc.revision();
    REQUIRE(window.apply_selection_color(false, source));
    REQUIRE(window.set_selection_stroke_width(.75));
    REQUIRE(doc.revision() == empty_revision);
    window.set_active_tool(1);
    f.mouse(WM_LBUTTONDOWN, f.point(20, 20), MK_LBUTTON);
    f.mouse(WM_LBUTTONUP, f.point(50, 40));
    REQUIRE(doc.current_snapshot()->object_count() == 1 && doc.revision() == empty_revision + 1);
    const auto new_id = selection.selected_ids().front();
    REQUIRE(f.object(new_id).fill.color_space() == color::ColorSpace::Cmyk);
    REQUIRE(f.object(new_id).stroke_width_mm == .75);
    REQUIRE(doc.undo() && doc.current_snapshot()->object_count() == 0);
    std::cout << "[PASS] Native mouse window/crossing/toggle/add, move/resize/cancel, palette fill/stroke, "
                 "atomic style undo, ICC pixels, native reopen and typed creation defaults\n";
}
} // namespace
#endif
int main(int argc, char **argv) {
    try {
#ifdef _WIN32
        EditorFixture fixture(argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path{});
        verify_editor(fixture);
#else
        (void)argc;
        (void)argv;
        std::cout << "[NOT_MEASURED] Native Windows input unavailable\n";
#endif
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
