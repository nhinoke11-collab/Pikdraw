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
        const auto rect_id = workflow.add_rectangle(10, 10, 30, 20);
        check(rect_id != 0, "Rectangle failed");
        check(workflow.add_ellipse(70, 40, 15, 10) != 0, "Ellipse failed");
        check(workflow.add_vietnamese_text(15, 60, "Hệ màu tài liệu") != 0, "Text failed");
        check(workflow.create_table(15, 80, 80, 40, 2, 2) != 0, "Table failed");
        check(workflow.import_svg("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10mm\" height=\"10mm\" viewBox=\"0 0 10 10\"><rect x=\"1\" y=\"1\" width=\"5\" height=\"5\" fill=\"#ff8000\"/></svg>"),
              "SVG color normalization import failed");

        // Verify single page supports both CMYK and RGB colors:
        window.root().vector_controller().selection().clear();
        window.root().vector_controller().selection().select(rect_id);
        check(window.apply_selection_color(false, color::CMYKColor{.1, .8, .9, 0, 1}), "CMYK color edit failed");

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

        // Confirm coexistence of both CMYK and RGB objects on this single page:
        const auto page_snapshot = window.root().document().current_snapshot();
        const auto* cmyk_target = page_snapshot->objects().lookup_ref(rect_id);
        const auto drawn_id = (*page_snapshot->objects().all().rbegin()).first;
        const auto* rgb_target = page_snapshot->objects().lookup_ref(drawn_id);
        check(cmyk_target && std::get<document::PathObject>(*cmyk_target).fill.color_space() == color::ColorSpace::Cmyk,
              "Target object lost its CMYK color space on single page");
        check(rgb_target && std::get<document::PathObject>(*rgb_target).fill.color_space() == color::ColorSpace::Rgb,
              "Drawn object lost its RGB color space on single page");
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
          (doc.color_profile_name() == "sRGB" || doc.color_profile_name() == "sRGB IEC61966-2.1"),
          "Reopen did not restore document color metadata");
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

void test_batch_color_conversion_and_select_same() {
    NativeWindow window(1440, 900);
    auto& workflow = window.workflow();
    workflow.new_document(200, 150, color::DocumentColorMode::Rgb);

    // Create test shapes:
    // rect1: Red RGB
    const auto rect1 = workflow.add_rectangle(10, 10, 20, 20, "#ff0000", "", 0.0);
    // rect2: Red RGB (same color as rect1)
    const auto rect2 = workflow.add_rectangle(40, 10, 20, 20, "#ff0000", "", 0.0);
    // rect3: Blue RGB
    const auto rect3 = workflow.add_rectangle(70, 10, 20, 20, "#0000ff", "", 0.0);
    // rect4: Pure CMYK Yellow
    const auto rect4 = workflow.add_rectangle(100, 10, 20, 20);
    window.root().vector_controller().selection().clear();
    window.root().vector_controller().selection().select(rect4);
    check(window.apply_selection_color(false, color::CMYKColor{0.0, 0.0, 1.0, 0.0, 1.0}), "Set CMYK failed");

    // 1. Test Select Same Fill Color:
    window.root().vector_controller().selection().clear();
    window.root().vector_controller().selection().select(rect1);
    check(workflow.select_same(SelectSameCriterion::FillColor), "Select same fill color failed");
    auto sel_ids = window.root().vector_controller().selection().selected_ids();
    check(sel_ids.size() == 2, "Expected 2 objects with red fill");
    check(std::find(sel_ids.begin(), sel_ids.end(), rect1) != sel_ids.end() &&
          std::find(sel_ids.begin(), sel_ids.end(), rect2) != sel_ids.end(),
          "Incorrect objects selected by same fill color");

    // 2. Test Select by Color Value:
    window.root().vector_controller().selection().clear();
    check(workflow.select_by_color(color::ColorValue::make_rgb(0.0, 0.0, 1.0), SelectSameCriterion::FillColor),
          "Select by blue RGB color failed");
    sel_ids = window.root().vector_controller().selection().selected_ids();
    check(sel_ids.size() == 1 && sel_ids.front() == rect3, "Expected rect3 to be selected by blue color");

    // 3. Test Select by Color Space:
    window.root().vector_controller().selection().clear();
    window.root().vector_controller().selection().select(rect1);
    check(workflow.select_same(SelectSameCriterion::ColorSpace), "Select same color space failed");
    sel_ids = window.root().vector_controller().selection().selected_ids();
    check(sel_ids.size() == 3, "Expected 3 RGB objects selected");

    check(workflow.select_same(SelectSameCriterion::CmykOnly), "Select CMYK only failed");
    sel_ids = window.root().vector_controller().selection().selected_ids();
    check(sel_ids.size() == 1 && sel_ids.front() == rect4, "Expected rect4 selected for CMYK only");

    // 4. Test Convert Selected Objects to CMYK:
    window.root().vector_controller().selection().clear();
    window.root().vector_controller().selection().select(rect1);
    window.root().vector_controller().selection().select(rect2, vector::SelectionMode::Add);
    check(workflow.convert_selected_objects_color_mode(color::DocumentColorMode::Cmyk),
          "Convert selected to CMYK failed");
    auto snap = window.root().document().current_snapshot();
    const auto* obj1 = snap->objects().lookup_ref(rect1);
    const auto* obj2 = snap->objects().lookup_ref(rect2);
    const auto* obj3 = snap->objects().lookup_ref(rect3);
    check(obj1 && std::get<document::PathObject>(*obj1).fill.color_space() == color::ColorSpace::Cmyk,
          "rect1 was not converted to CMYK");
    check(obj2 && std::get<document::PathObject>(*obj2).fill.color_space() == color::ColorSpace::Cmyk,
          "rect2 was not converted to CMYK");
    check(obj3 && std::get<document::PathObject>(*obj3).fill.color_space() == color::ColorSpace::Rgb,
          "rect3 should remain RGB");

    // Undo converting selected objects:
    check(workflow.undo(), "Undo convert selected failed");
    snap = window.root().document().current_snapshot();
    obj1 = snap->objects().lookup_ref(rect1);
    check(obj1 && std::get<document::PathObject>(*obj1).fill.color_space() == color::ColorSpace::Rgb,
          "Undo did not restore rect1 to RGB");

    // 5. Test Convert Active Page to CMYK:
    check(workflow.convert_active_page_color_mode(color::DocumentColorMode::Cmyk),
          "Convert active page to CMYK failed");
    snap = window.root().document().current_snapshot();
    check(snap->color_mode() == color::DocumentColorMode::Cmyk, "Page mode was not set to CMYK");
    for (auto id : {rect1, rect2, rect3, rect4}) {
        const auto* o = snap->objects().lookup_ref(id);
        check(o && std::get<document::PathObject>(*o).fill.color_space() == color::ColorSpace::Cmyk,
              "All objects on page must be CMYK after page conversion");
    }

    // Undo active page conversion:
    check(workflow.undo(), "Undo page conversion failed");
    snap = window.root().document().current_snapshot();
    obj3 = snap->objects().lookup_ref(rect3);
    check(obj3 && std::get<document::PathObject>(*obj3).fill.color_space() == color::ColorSpace::Rgb,
          "Undo did not restore page objects");

    // 6. Test Menu Action Dispatch for Batch Conversion & Selection:
    window.execute_menu_action(static_cast<int>(MenuActionId::EditSelectAllCmyk));
    sel_ids = window.root().vector_controller().selection().selected_ids();
    check(sel_ids.size() == 1 && sel_ids.front() == rect4, "Menu select CMYK failed");

    window.execute_menu_action(static_cast<int>(MenuActionId::ColorConvertDocumentToCmyk));
    snap = window.root().document().current_snapshot();
    check(snap->color_mode() == color::DocumentColorMode::Cmyk, "Menu convert document to CMYK failed");

    window.execute_menu_action(static_cast<int>(MenuActionId::EditUndo));
    snap = window.root().document().current_snapshot();
    check(snap->color_mode() == color::DocumentColorMode::Rgb, "Menu undo failed to restore RGB mode");
}
#endif
}
int main() {
    try {
#ifdef _WIN32
        native_color_workflow();
        test_batch_color_conversion_and_select_same();
#endif
        std::cout << "PASS native color creation/tools, batch color conversion, select same, gamut confirm/cancel, menu Undo/Redo, DPI and save/reopen\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

