#include "pikdraw/desktop/shell/native_window.hpp"
#include "../src/shell/native_window_menus.hpp"
#include "../src/shell/native_window_document_color.hpp"
#include "pikdraw/document/storage/native_format.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace pikdraw;
using namespace pikdraw::desktop;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void paints_follow_document(const document::DocumentSnapshot& snapshot) {
    const auto expected = snapshot.color_mode() == color::DocumentColorMode::Cmyk
        ? color::ColorSpace::Cmyk : color::ColorSpace::Rgb;
    const auto paint = [&](const color::ColorValue& value) {
        check(value.color_space() == expected || value.color_space() == color::ColorSpace::Spot,
              "Tool/import/style wrote an opposite-mode paint");
    };
    for (const auto& page : snapshot.pages())
        for (const auto& [id, object] : page.objects.all())
            std::visit([&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, document::PathObject> || std::is_same_v<T, document::TextObject>) {
                    paint(value.fill);
                    if constexpr (std::is_same_v<T, document::PathObject>) paint(value.stroke);
                }
            }, object);
}
#ifdef _WIN32
bool accept_conversion = true, saw_confirmation = false;
void CALLBACK answer_confirmation(HWND owner, UINT, UINT_PTR timer, DWORD) {
    const auto popup = GetLastActivePopup(owner);
    if (popup == owner || !GetDlgItem(popup, IDOK) || !GetDlgItem(popup, IDCANCEL)) return;
    KillTimer(owner, timer);
    saw_confirmation = true;
    PostMessageW(popup, WM_COMMAND, accept_conversion ? IDOK : IDCANCEL, 0);
}
void convert_from_menu(NativeWindow& window, int action, bool accept) {
    accept_conversion = accept; saw_confirmation = false;
    check(SetTimer(window.hwnd(), 0xC010, 20, answer_confirmation) != 0, "Could not script conversion dialog");
    window.execute_menu_action(action);
    KillTimer(window.hwnd(), 0xC010);
    check(saw_confirmation, "Menu conversion skipped its gamut confirmation");
}
void evidence(NativeWindow& window, const char* name) {
    const auto* directory = std::getenv("PIKDRAW_COLOR_EVIDENCE_DIR");
    if (!directory || !*directory) return;
    std::filesystem::create_directories(directory);
    window.capture_screenshot_bmp((std::filesystem::path(directory) / name).string());
}
void native_color_workflow() {
    NativeWindow window{1440, 900};
    check(window.initialize("PikDraw document color acceptance"), "Native window initialization failed");
    ShowWindow(window.hwnd(), SW_HIDE);
    if (window.is_recovery_modal_open()) SendMessageW(window.hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
    check(window.root().document().color_mode() == color::DocumentColorMode::Cmyk &&
          window.root().document().raster_dpi() == 300, "Default native document is not CMYK/300");
    for (auto mode : {color::DocumentColorMode::Rgb, color::DocumentColorMode::Cmyk}) {
        window.create_new_tab("Color test", 200, 150, mode == color::DocumentColorMode::Rgb ? "RGB" : "CMYK");
        check(window.root().document().color_mode() == mode, "New tab mode not stored in document");
        check(window.root().document().raster_dpi() == (mode == color::DocumentColorMode::Rgb ? 72 : 300),
              "New tab did not select mode-specific DPI");
        auto& workflow = window.workflow();
        check(workflow.add_rectangle(10, 10, 30, 20) != 0, "Rectangle failed");
        check(workflow.add_ellipse(70, 40, 15, 10) != 0, "Ellipse failed");
        check(workflow.add_vietnamese_text(15, 60, "Hệ màu tài liệu") != 0, "Text failed");
        check(workflow.create_table(15, 80, 80, 40, 2, 2) != 0, "Table failed");
        check(workflow.import_svg("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10mm\" height=\"10mm\" viewBox=\"0 0 10 10\"><rect x=\"1\" y=\"1\" width=\"5\" height=\"5\" fill=\"#ff8000\"/></svg>"),
              "SVG color normalization import failed");
        paints_follow_document(*window.root().document().current_snapshot());
        window.root().vector_controller().selection().clear();
        check(window.apply_selection_color(false, color::RGBColor{.2, .3, .4, .7}), "Default color edit failed");
        window.set_active_tool(1);
        window.fit_page_to_canvas();
        RECT client{}; GetClientRect(window.hwnd(), &client);
        const int width = client.right / window.dip(1), height = client.bottom / window.dip(1);
        const auto canvas = window.canvas_rect(width, height);
        const auto point = [&](double x, double y) {
            auto p = window.root().renderer().camera().world_to_screen({x, y});
            return POINT{canvas.left + static_cast<LONG>(p.x), canvas.top + static_cast<LONG>(p.y)};
        };
        const auto start = point(130, 20), finish = point(160, 45);
        const auto count = window.root().document().objects().size();
        window.handle_mouse_down(start.x, start.y);
        window.handle_mouse_move(finish.x, finish.y);
        window.handle_mouse_up(finish.x, finish.y);
        check(window.root().document().objects().size() == count + 1, "Native shape gesture failed");
        paints_follow_document(*window.root().document().current_snapshot());
        window.set_active_tool(0);
    }
    auto& doc = window.root().document();
    const auto before = doc.current_snapshot();
    convert_from_menu(window, document_color_ui::kRgb, false);
    check(doc.current_snapshot() == before, "Cancel conversion mutated document/history");
    convert_from_menu(window, document_color_ui::kRgb, true);
    check(doc.color_mode() == color::DocumentColorMode::Rgb && doc.revision() == before->revision() + 1,
          "Menu mode change did not publish one transaction");
    paints_follow_document(*doc.current_snapshot());
    window.execute_menu_action(static_cast<int>(MenuActionId::EditUndo));
    check(doc.color_mode() == color::DocumentColorMode::Cmyk, "Native menu undo did not restore mode");
    window.execute_menu_action(static_cast<int>(MenuActionId::EditRedo));
    window.execute_menu_action(document_color_ui::kDpi150);
    check(doc.raster_dpi() == 150, "DPI menu action did not edit the document");
    window.execute_menu_action(static_cast<int>(MenuActionId::EditUndo));
    check(doc.raster_dpi() == 300, "Native DPI undo lost original value");
    window.execute_menu_action(static_cast<int>(MenuActionId::EditRedo));
    check(doc.raster_dpi() == 150, "Native DPI redo failed");
    const auto file = std::filesystem::temp_directory_path() /
        ("pikdraw-color-" + std::to_string(GetCurrentProcessId()) + ".pkdr");
    check(window.workflow().save_native(file.string()), "Color workflow save failed");
    window.workflow().new_document(200, 150, color::DocumentColorMode::Cmyk);
    check(window.workflow().open_native(file.string()), "Color workflow reopen failed");
    std::filesystem::remove(file);
    check(doc.color_mode() == color::DocumentColorMode::Rgb && doc.raster_dpi() == 150 &&
          doc.color_profile_name() == "sRGB", "Reopen did not restore document color metadata");
    paints_follow_document(*doc.current_snapshot());
    window.switch_to_tab(0);
    check(window.root().document().color_mode() == color::DocumentColorMode::Cmyk, "Tab lost independent mode");
    window.switch_to_tab(2);
    check(window.root().document().color_mode() == color::DocumentColorMode::Rgb, "Tab switch changed source mode");
    window.root().vector_controller().selection().clear();
    window.open_object_properties_panel();
    for (auto language : {UILanguage::English, UILanguage::Vietnamese}) {
        window.set_language(language);
        window.set_theme(language == UILanguage::English ? ThemeMode::CyberDark : ThemeMode::Light);
        evidence(window, language == UILanguage::English ? "color-palette-en.bmp" : "color-palette-vi.bmp");
        window.open_menu(static_cast<int>(MenuId::Edit));
        evidence(window, language == UILanguage::English ? "color-edit-menu-en.bmp" : "color-edit-menu-vi.bmp");
        window.close_menu();
    }
    // File > New modal uses the same stored mode and chooses its default DPI.
    RECT client{}; GetClientRect(window.hwnd(), &client);
    const int width = client.right / window.dip(1), height = client.bottom / window.dip(1);
    const int x = (width - 680) / 2 + 24;
    const int modal_y = (height - 510) / 2;
    window.open_new_doc_modal();
    // cy=modal_y+58; form=cy+132, orientation=form+64.
    window.handle_mouse_down(x + 520, modal_y + 58 + 132 + 64 + 34);
    window.confirm_new_document();
    check(window.root().document().color_mode() == color::DocumentColorMode::Rgb &&
          window.root().document().raster_dpi() == 72, "File New RGB did not initialize RGB/72");
    window.open_new_doc_modal();
    window.handle_mouse_down(x + 370, modal_y + 58 + 132 + 64 + 34);
    window.confirm_new_document();
    check(window.root().document().color_mode() == color::DocumentColorMode::Cmyk &&
          window.root().document().raster_dpi() == 300, "File New CMYK did not initialize CMYK/300");
}
#endif
}
int main() {
    try {
#ifdef _WIN32
        native_color_workflow();
#endif
        std::cout << "PASS native color creation/tools, gamut confirm/cancel, menu Undo/Redo, DPI and save/reopen\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
