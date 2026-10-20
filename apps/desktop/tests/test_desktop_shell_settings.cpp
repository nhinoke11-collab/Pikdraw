#include "desktop_shell_test_helpers.hpp"
#include "pikdraw/desktop/shell/native_window_shortcuts_settings.hpp"

void test_native_window_theme_and_settings_state() {
    NativeWindow win(1440, 900);
    assert(win.theme() == ThemeMode::Light);
    assert(win.language() == UILanguage::English);
    assert(!win.is_settings_modal_open());
    assert(win.canvas_background_color().r == 255);
    assert(win.canvas_background_color().g == 255);
    assert(win.canvas_background_color().b == 255);
#ifdef _WIN32
    assert(win.canvas_background_colorref() == RGB(255, 255, 255));
#endif

    win.set_theme(ThemeMode::CyberDark);
    assert(win.theme() == ThemeMode::CyberDark);
    assert(win.settings_theme_choice() == 0);
    assert(win.canvas_background_color().r == 68);
    assert(win.canvas_background_color().g == 72);
    assert(win.canvas_background_color().b == 80);
#ifdef _WIN32
    assert(win.canvas_background_colorref() == RGB(68, 72, 80));
#endif

    win.toggle_theme();
    assert(win.theme() == ThemeMode::Light);
    assert(win.canvas_background_color().r == 255);
    assert(win.canvas_background_color().g == 255);
    assert(win.canvas_background_color().b == 255);
#ifdef _WIN32
    assert(win.canvas_background_colorref() == RGB(255, 255, 255));
#endif

    win.open_settings_modal();
    assert(win.is_settings_modal_open());

    win.set_settings_tab(2);
    assert(win.settings_active_tab() == 2);

    win.set_settings_accent_choice(1);
    assert(win.settings_accent_choice() == 1);

    win.set_settings_unit_choice(2);
    assert(win.settings_unit_choice() == 2);

    win.close_settings_modal();
    assert(!win.is_settings_modal_open());

    assert(pikdraw::desktop::actions::ActionId::is_valid_action_id(
        pikdraw::desktop::actions::ActionId::kSettingsAiConnections));
    bool found_ai_connections = false;
    for (const auto& item : kSettingsMenuItems)
        found_ai_connections = found_ai_connections || item.action == MenuActionId::SettingsAiConnections;
    assert(found_ai_connections);
    assert(pikdraw::desktop::actions::ActionId::is_valid_action_id(
        pikdraw::desktop::actions::ActionId::kHelpAiAutomation));
    bool found_ai_help = false;
    for (const auto& item : kHelpMenuItems)
        found_ai_help = found_ai_help || item.action == MenuActionId::HelpAiAutomation;
    assert(found_ai_help);

    std::cout << "[PASS] test_native_window_theme_and_settings_state\n";
}

void test_open_cdr_fits_artwork_without_changing_geometry() {
    NativeWindow win(1440, 900);
    auto& workflow = win.workflow();
    workflow.new_document(210.0, 297.0);
    const auto id = workflow.add_rectangle(1160.0, -67.0, 440.0, 600.0);
    const auto original_bounds = workflow.get_object_bounds(id);
    assert(original_bounds);

    bool outside_page = false;
    assert(win.fit_document_content_to_canvas(&outside_page));
    assert(outside_page);
    const auto content_center = win.root().renderer().camera().center();
    assert(std::abs(content_center.x - 1380.0) < 1e-9);
    assert(std::abs(content_center.y - 233.0) < 1e-9);
    const auto content_fit_snapshot = win.root().document().current_snapshot();
    assert(content_fit_snapshot->page().width_mm == 210.0);
    assert(content_fit_snapshot->page().height_mm == 297.0);
    const auto content_fit_bounds = workflow.get_object_bounds(id);
    assert(content_fit_bounds);
    assert(std::abs(content_fit_bounds->x - original_bounds->x) < 1e-9);
    assert(std::abs(content_fit_bounds->y - original_bounds->y) < 1e-9);
    assert(std::abs(content_fit_bounds->width - original_bounds->width) < 1e-9);
    assert(std::abs(content_fit_bounds->height - original_bounds->height) < 1e-9);

    win.fit_page_to_canvas();
    const auto page_center = win.root().renderer().camera().center();
    assert(std::abs(page_center.x - 105.0) < 1e-9);
    assert(std::abs(page_center.y - 148.5) < 1e-9);
    const auto page_fit_bounds = workflow.get_object_bounds(id);
    assert(page_fit_bounds);
    assert(std::abs(page_fit_bounds->x - original_bounds->x) < 1e-9);
    assert(std::abs(page_fit_bounds->y - original_bounds->y) < 1e-9);
    assert(std::abs(page_fit_bounds->width - original_bounds->width) < 1e-9);
    assert(std::abs(page_fit_bounds->height - original_bounds->height) < 1e-9);

    std::cout << "[PASS] test_open_cdr_fits_artwork_without_changing_geometry\n";
}

void test_ai_connections_settings_draft_state() {
    NativeWindow win(1440, 900);
    assert(!win.mcp_listener_active());
    assert(!win.mcp_edit_access_active());

    win.open_ai_connections_settings();
    assert(win.is_settings_modal_open());
    assert(win.settings_active_tab() == 6);
    assert(win.ai_connections_focus_index() == 0);
    assert(!win.ai_connections_read_choice());
    assert(!win.ai_connections_edit_choice());
    capture_shell_screenshot(win, "ai-connections-settings-english.bmp");
    win.set_language(UILanguage::Vietnamese);
    capture_shell_screenshot(win, "ai-connections-settings-vietnamese.bmp");
    win.set_language(UILanguage::English);

    win.toggle_ai_connections_read_choice();
    win.toggle_ai_connections_edit_choice();
    assert(win.ai_connections_read_choice());
    assert(win.ai_connections_edit_choice());
    assert(!win.mcp_listener_active());

    win.close_settings_modal();
    assert(!win.is_settings_modal_open());
    assert(!win.ai_connections_read_choice());
    assert(!win.ai_connections_edit_choice());
    assert(!win.mcp_listener_active());

    win.open_ai_connections_settings();
    win.toggle_ai_connections_read_choice();
    assert(!win.apply_ai_connections_choices());
    assert(!win.mcp_listener_active());
    assert(!win.mcp_error_message().empty());
    win.close_settings_modal();
}

void test_compact_icons_and_hover_tooltips() {
    NativeWindow win(1440, 900);

    // 1. Hover over the new-document plus area in the tab bar.
    win.set_language(UILanguage::Vietnamese);
    win.update_hover_tooltip(235, 50);
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"Tạo tài liệu mới") != std::wstring::npos);
    assert(win.tooltip().text.find(L"Ctrl+N") != std::wstring::npos);

    // Switch to English: Tooltip changes language immediately
    win.set_language(UILanguage::English);
    win.update_hover_tooltip(235, 50);
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"New Document Tab") != std::wstring::npos);
    assert(win.tooltip().text.find(L"Ctrl+N") != std::wstring::npos);

    // 2. Hover over Left Tool Strip: Tool 0 (Pick), Tool 11 (Node), Tool 4 (Text), Tool 5 (Knife)
    win.set_language(UILanguage::Vietnamese);
    win.update_hover_tooltip(20, 115); // Tool 0 (content_top + 10 = 112)
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"chọn") != std::wstring::npos);
    assert(win.tooltip().text.find(L"Phím V") != std::wstring::npos);

    win.update_hover_tooltip(20, 115 + 1 * 42); // Node Tool (Row 1)
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"Node") != std::wstring::npos);

    win.update_hover_tooltip(20, 115 + 5 * 42); // Tool 4 (Text Tool, Row 5)
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"văn bản") != std::wstring::npos);
    assert(win.tooltip().text.find(L"F8") != std::wstring::npos);

    win.update_hover_tooltip(20, 115 + 6 * 42); // Tool 5 (Knife Tool, Row 6)
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"Dao cắt") != std::wstring::npos);
    assert(win.tooltip().text.find(L"Phím K") != std::wstring::npos);

    // 3. Hover over Ribbon: Text Mode (Convert to Curves `⤹ Q`, Table `▦ Table`)
    win.set_active_tool(4); // Text Tool active
    win.update_hover_tooltip(580, 80); // Curves button (556 to 616)
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"đường cong") != std::wstring::npos);
    assert(win.tooltip().text.find(L"Ctrl+Q") != std::wstring::npos);

    win.update_hover_tooltip(650, 80); // Table button (622 to 692)
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"bảng") != std::wstring::npos);

    // 4. Properties has no legacy Align hitbox; the selection context bar owns the action.
    int insp_x = 1440 - 280;
    win.update_hover_tooltip(insp_x + 30, 420);
    assert(!win.tooltip().visible);

    // 5. Context bar tooltips describe the actual command and do not invent shortcuts.
    win.set_active_tool(0); // Return to Pick so selection actions own the context bar.
    auto tip_a = win.workflow().add_rectangle(40.0, 40.0, 30.0, 20.0);
    auto tip_b = win.workflow().add_rectangle(60.0, 40.0, 30.0, 20.0);
    win.root().vector_controller().selection().clear();
    win.root().vector_controller().selection().select(tip_a, pikdraw::vector::SelectionMode::Add);
    win.root().vector_controller().selection().select(tip_b, pikdraw::vector::SelectionMode::Add);
    win.set_language(UILanguage::Vietnamese);
    win.update_hover_tooltip(850, 80); // Weld icon in the two-path selection context.
    std::wcout << L"ACTUAL TOOLTIP AT 850: '" << win.tooltip().text << L"'" << std::endl;
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"Hàn") != std::wstring::npos);

    // 6. English mode keeps the same icon action name.
    win.set_language(UILanguage::English);
    win.update_hover_tooltip(850, 80);
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"Weld") != std::wstring::npos);

    // 7. Hovering empty canvas clears tooltip
    win.update_hover_tooltip(500, 500);
    assert(!win.tooltip().visible);
    assert(win.tooltip().text.empty());

    // 8. Capture visual proof of compact, icon-only context actions.
    win.set_language(UILanguage::Vietnamese);
    win.update_hover_tooltip(850, 80);
    capture_shell_screenshot(win, "object-context-actions-hover.bmp");

    // Also capture Text Ribbon with hover tooltip over ⤹ Q
    win.set_active_tool(4);
    win.update_hover_tooltip(585, 80);
    capture_shell_screenshot(win, "m1_pikdraw_compact_text_ribbon_hover.bmp");

    // 8. Capture visual proof of multiple selected objects with compact ribbon buttons
    win.set_active_tool(0); // Pick tool
    auto r1 = win.workflow().add_rectangle(50.0, 50.0, 80.0, 60.0, "#00f2c3", "#0ea5e9", 1.0);
    auto r2 = win.workflow().add_rectangle(150.0, 70.0, 100.0, 80.0, "#eab308", "#f59e0b", 1.0);
    win.root().vector_controller().selection().clear();
    win.root().vector_controller().selection().select(r1, pikdraw::vector::SelectionMode::Add);
    win.root().vector_controller().selection().select(r2, pikdraw::vector::SelectionMode::Add);
    win.set_language(UILanguage::Vietnamese);
    win.update_hover_tooltip(590, 80); // Hover over Align Left on the object context bar.
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"Căn cạnh trái") != std::wstring::npos);
    capture_shell_screenshot(win, "m1_pikdraw_compact_object_ribbon_hover.bmp");

    // The former Delete slot is now the rotation field; the former Duplicate slot is empty.
    win.update_hover_tooltip(490, 80);
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"góc xoay") != std::wstring::npos);
    win.update_hover_tooltip(545, 80);
    assert(!win.tooltip().visible);

    // Single-selection verification
    auto s1 = win.workflow().add_ellipse(50.0, 50.0, 40.0, 40.0, "#00f2c3", "#0ea5e9", 1.0);
    win.root().vector_controller().selection().clear();
    win.root().vector_controller().selection().select(s1, pikdraw::vector::SelectionMode::Add);
    win.update_hover_tooltip(490, 80);
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"góc xoay") != std::wstring::npos);
    win.update_hover_tooltip(545, 80);
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"Lật ngang") != std::wstring::npos);
    win.update_hover_tooltip(590, 80);
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"Lật dọc") != std::wstring::npos);

    std::cout << "[PASS] test_compact_icons_and_hover_tooltips\n";
}

void test_numeric_keyboard_input_property_bar() {
    NativeWindow win(1440, 900);

    // 1. Test Direct Keyboard Input for Page Width & Height (Replacing +/- buttons)
    win.start_numeric_input(NativeWindow::NumericFieldId::PageWidth);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::PageWidth);
    assert(!win.numeric_input_buffer().empty());

    while (!win.numeric_input_buffer().empty()) {
        win.handle_numeric_char_input(8); // Backspace
    }
    win.handle_numeric_char_input(L'5');
    win.handle_numeric_char_input(L'0');
    win.handle_numeric_char_input(L'0');
    assert(win.numeric_input_buffer() == "500");

    win.handle_numeric_char_input(13); // Enter
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::None);
    assert(std::abs(win.active_tab().page_width_mm - 500.0) < 0.1);
    assert(std::abs(win.root().document().current_snapshot()->page().width_mm - 500.0) < 0.1);

    // Type on Page Height with Tab cycling
    win.start_numeric_input(NativeWindow::NumericFieldId::PageWidth);
    win.handle_numeric_char_input(9); // Tab cycles to PageHeight!
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::PageHeight);
    while (!win.numeric_input_buffer().empty()) {
        win.handle_numeric_char_input(8);
    }
    win.handle_numeric_char_input(L'3');
    win.handle_numeric_char_input(L'5');
    win.handle_numeric_char_input(L'0');
    win.commit_numeric_field();
    assert(std::abs(win.active_tab().page_height_mm - 350.0) < 0.1);
    assert(std::abs(win.root().document().current_snapshot()->page().height_mm - 350.0) < 0.1);

    // Capture clean Page Ribbon evidence (no +/- buttons)
    capture_shell_screenshot(win, "m1_pikdraw_numeric_input_page_ribbon.bmp");

    // 2. Test Direct Keyboard Input for Object X, Y, Width, Height
    auto rect_id = win.workflow().add_rectangle(50.0, 60.0, 100.0, 80.0, "#00f2c3", "#0ea5e9", 2.0);
    win.root().vector_controller().selection().select(rect_id);

    // Object X
    win.start_numeric_input(NativeWindow::NumericFieldId::ObjectX);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::ObjectX);
    while (!win.numeric_input_buffer().empty()) win.handle_numeric_char_input(8);
    win.handle_numeric_char_input(L'1');
    win.handle_numeric_char_input(L'2');
    win.handle_numeric_char_input(L'5');
    win.commit_numeric_field();
    auto b_obj = win.workflow().get_object_bounds(rect_id);
    assert(b_obj.has_value());
    assert(std::abs(b_obj->x - 125.0) < 0.1);

    // Object Y
    win.start_numeric_input(NativeWindow::NumericFieldId::ObjectY);
    while (!win.numeric_input_buffer().empty()) win.handle_numeric_char_input(8);
    win.handle_numeric_char_input(L'1');
    win.handle_numeric_char_input(L'7');
    win.handle_numeric_char_input(L'5');
    win.commit_numeric_field();
    b_obj = win.workflow().get_object_bounds(rect_id);
    assert(b_obj.has_value());
    assert(std::abs(b_obj->y - 175.0) < 0.1);

    // Aspect ratio locked scaling via numeric Width input:
    if (!win.is_aspect_ratio_locked()) win.toggle_aspect_ratio_lock();
    win.start_numeric_input(NativeWindow::NumericFieldId::ObjectWidth);
    while (!win.numeric_input_buffer().empty()) win.handle_numeric_char_input(8);
    win.handle_numeric_char_input(L'2');
    win.handle_numeric_char_input(L'0');
    win.handle_numeric_char_input(L'0');
    win.commit_numeric_field();
    b_obj = win.workflow().get_object_bounds(rect_id);
    assert(b_obj.has_value());
    assert(std::abs(b_obj->width - 200.0) < 0.5);
    assert(std::abs(b_obj->height - 160.0) < 0.5);

    // Clamping test: typing > 40m clamps to 40,000 mm
    win.start_numeric_input(NativeWindow::NumericFieldId::ObjectWidth);
    while (!win.numeric_input_buffer().empty()) win.handle_numeric_char_input(8);
    win.handle_numeric_char_input(L'9');
    win.handle_numeric_char_input(L'9');
    win.handle_numeric_char_input(L'9');
    win.handle_numeric_char_input(L'9');
    win.handle_numeric_char_input(L'9');
    win.commit_numeric_field();
    b_obj = win.workflow().get_object_bounds(rect_id);
    assert(b_obj.has_value());
    assert(b_obj->width <= pikdraw::geometry::kMaxObjectDimensionMm + 0.1);

    // Capture clean Object Ribbon evidence
    capture_shell_screenshot(win, "m1_pikdraw_numeric_input_object_ribbon.bmp");

    // Capture Focused State screenshot with cyan highlight and cursor |
    win.start_numeric_input(NativeWindow::NumericFieldId::ObjectWidth);
    capture_shell_screenshot(win, "m1_pikdraw_numeric_input_focused.bmp");
    win.cancel_numeric_field();

    // 3. Test New Document Modal Numeric Inputs (No [-10]/[+10] buttons)
    win.open_new_doc_modal();
    assert(win.is_new_doc_modal_open());

    win.start_numeric_input(NativeWindow::NumericFieldId::ModalWidth);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::ModalWidth);
    while (!win.numeric_input_buffer().empty()) win.handle_numeric_char_input(8);
    win.handle_numeric_char_input(L'1');
    win.handle_numeric_char_input(L'2');
    win.handle_numeric_char_input(L'0');
    win.handle_numeric_char_input(L'0');
    win.commit_numeric_field();
    assert(std::abs(win.new_doc_width() - 1200.0) < 0.1);

    win.start_numeric_input(NativeWindow::NumericFieldId::ModalHeight);
    while (!win.numeric_input_buffer().empty()) win.handle_numeric_char_input(8);
    win.handle_numeric_char_input(L'6');
    win.handle_numeric_char_input(L'0');
    win.handle_numeric_char_input(L'0');
    win.commit_numeric_field();
    assert(std::abs(win.new_doc_height() - 600.0) < 0.1);

    // Capture clean Modal screenshot
    capture_shell_screenshot(win, "m1_pikdraw_new_doc_clean_inputs.bmp");
    win.close_new_doc_modal();

    std::cout << "[PASS] test_numeric_keyboard_input_property_bar\n";
}

void test_page_preset_dropdown_and_modal_row_clicks() {
    NativeWindow win(1440, 900);
    win.workflow().new_document(297.0, 210.0); // Default A4 landscape
    win.fit_page_to_canvas();

    // 1. Verify New Document Modal Row 2 Click Hit-Testing (Fixing click offset bug)
    win.open_new_doc_modal();
    assert(win.is_new_doc_modal_open());

    int modal_w = 680, modal_h = 510;
    int modal_x = (1440 - modal_w) / 2; // 380
    int modal_y = (900 - modal_h) / 2;  // 195
    int cx = modal_x + 24;              // 404
    int cy = modal_y + 58;              // 253

    // Row 1 presets: i=0 (A4: 210x297), i=1 (A3: 297x420), i=2 (A2: 420x594)
    // Row 2 presets: i=3 (Pano 2x1m: 2000x1000), i=4 (Banner 3x1m: 3000x1000), i=5 (Vuông 1080: 1080x1080)
    // Click on Row 2 Card 0 (Pano 2x1m): px = cx + 0 = 404, py = cy + 24 + 48 = 325
    win.handle_mouse_down(cx + 50, cy + 24 + 48 + 20);
    assert(std::abs(win.new_doc_width() - 2000.0) < 0.1);
    assert(std::abs(win.new_doc_height() - 1000.0) < 0.1);

    // Click on Row 2 Card 1 (Banner 3x1m): px = cx + 212 = 616, py = cy + 24 + 48 = 325
    win.handle_mouse_down(cx + 212 + 50, cy + 24 + 48 + 20);
    assert(std::abs(win.new_doc_width() - 3000.0) < 0.1);
    assert(std::abs(win.new_doc_height() - 1000.0) < 0.1);

    // Click on Row 2 Card 2 (Vuông 1080): px = cx + 424 = 828, py = cy + 24 + 48 = 325
    win.handle_mouse_down(cx + 424 + 50, cy + 24 + 48 + 20);
    assert(std::abs(win.new_doc_width() - 1080.0) < 0.1);
    assert(std::abs(win.new_doc_height() - 1080.0) < 0.1);

    // Click on Row 1 Card 0 (A4): px = cx = 404, py = cy + 24 = 277
    win.handle_mouse_down(cx + 50, cy + 24 + 20);
    assert(std::abs(win.new_doc_width() - 297.0) < 0.1 || std::abs(win.new_doc_width() - 210.0) < 0.1);

    win.close_new_doc_modal();
    assert(!win.is_new_doc_modal_open());

    // 2. Test Page Preset Combobox & Dropdown on Ribbon
    assert(!win.is_page_preset_dropdown_open());

    // Click Combobox Button at x=100, y=80 (ribbon y: 66 to 102)
    win.handle_mouse_down(100, 80);
    assert(win.is_page_preset_dropdown_open());

    // Capture visual proof of Page Preset Dropdown List OPEN
    capture_shell_screenshot(win, "m1_pikdraw_page_preset_dropdown_open.bmp");

    // Click on preset in Dropdown (item 1: A3)
    int drop_y = 38 + 28 + 36;
    int a3_click_y = drop_y + 6 + 1 * 28 + 14;
    win.handle_mouse_down(100, a3_click_y);
    assert(!win.is_page_preset_dropdown_open()); // Closed after selection
    assert(std::abs(win.active_tab().page_width_mm - 420.0) < 0.1);
    assert(std::abs(win.active_tab().page_height_mm - 297.0) < 0.1);

    // Open dropdown again and click Pano 2x1m (item 5)
    win.handle_mouse_down(100, 80);
    assert(win.is_page_preset_dropdown_open());
    int pano_click_y = drop_y + 6 + 5 * 28 + 14;
    win.handle_mouse_down(100, pano_click_y);
    assert(!win.is_page_preset_dropdown_open());
    assert(std::abs(win.active_tab().page_width_mm - 2000.0) < 0.1);
    assert(std::abs(win.active_tab().page_height_mm - 1000.0) < 0.1);

    // Open dropdown again and press ESC to close
    win.handle_mouse_down(100, 80);
    assert(win.is_page_preset_dropdown_open());
    win.handle_key_down(VK_ESCAPE);
    assert(!win.is_page_preset_dropdown_open());

    // Vietnamese language mode dropdown
    win.set_language(UILanguage::Vietnamese);
    win.handle_mouse_down(100, 80);
    assert(win.is_page_preset_dropdown_open());
    capture_shell_screenshot(win, "m1_pikdraw_page_preset_dropdown_vi.bmp");
    win.close_page_preset_dropdown();

    // Capture visual proof of Compact Ribbon with Preset Combobox (Collapsed)
    capture_shell_screenshot(win, "m1_pikdraw_page_ribbon_compact_combobox.bmp");

    std::cout << "[PASS] test_page_preset_dropdown_and_modal_row_clicks\n";
}

void test_corel_pick_auto_select_resume_guidelines_and_space_shortcut() {
    NativeWindow win;
    assert(win.initialize("Test Corel Pick & Guidelines Flow"));
    if (win.is_recovery_modal_open()) {
        win.handle_key_down(VK_ESCAPE);
    }
    auto& workflow = win.workflow();
    workflow.new_document(297.0, 210.0);
    win.fit_page_to_canvas();
    auto& sel = win.root().vector_controller().selection();
    const auto area = win.canvas_rect(win.width(), win.height());

    // 1. Phím tắt V và Space
    assert(win.active_tool() == 0);
    win.set_active_tool(1);
    assert(win.active_tool() == 1);

    // Bấm Space -> Tạm thời chuyển sang Pick Tool (tool 0)
    assert(win.handle_key_down(VK_SPACE));
    assert(win.active_tool() == 0);
    assert(win.previous_drawing_tool() == 1);

    // Bấm Space lần nữa -> Khôi phục lại công cụ Rectangle (tool 1)
    assert(win.handle_key_down(VK_SPACE));
    assert(win.active_tool() == 1);

    // Bấm V -> Chuyển sang Pick Tool (tool 0)
    assert(win.handle_key_down('V'));
    assert(win.active_tool() == 0);
    assert(win.previous_drawing_tool() == 1);

    // 2. CorelDRAW Flow: Vẽ xong 1 đối tượng -> Tự động chọn (Pick) ngay lập tức
    win.set_active_tool(2);
    assert(win.active_tool() == 2);

    const int x1 = area.left + 60;
    const int y1 = area.top + 60;
    const int x2 = area.left + 160;
    const int y2 = area.top + 140;

    win.handle_mouse_down(x1, y1, 0);
    win.handle_mouse_move(x2, y2, 0);
    win.handle_mouse_up(x2, y2, 0);

    // a) Đối tượng mới tạo được tự động chọn!
    assert(sel.count() == 1);
    // b) Hệ thống tự động chuyển sang công cụ Pick (active_tool == 0)!
    assert(win.active_tool() == 0);
    // c) previous_drawing_tool lưu lại là tool 2 (Elip)!
    assert(win.previous_drawing_tool() == 2);

    // 3. Khi người dùng click ra ngoài khoảng trống (deselect) -> Tự động kích hoạt lại công cụ Elip (tool 2)!
    const int empty_x = area.left + 20;
    const int empty_y = area.top + 20;
    assert(win.is_canvas_point(empty_x, empty_y));

    win.handle_mouse_down(empty_x, empty_y, 0);
    win.handle_mouse_up(empty_x, empty_y, 0);

    // a) Vùng chọn đã bị bỏ
    assert(sel.count() == 0);
    // b) Công cụ vẽ trước đó (Elip - tool 2) tự động kích hoạt trở lại để tiếp tục vẽ!
    assert(win.active_tool() == 2);

    // 3b. Khi đang ở Quick Shape (tool 2), click vào đối tượng đã có -> Tự động PICK chọn đối tượng!
    const int obj_center_x = area.left + 110;
    const int obj_center_y = area.top + 100;
    win.handle_mouse_move(obj_center_x, obj_center_y, 0);
    assert(win.canvas_cursor_at(obj_center_x, obj_center_y) == 32646); // IDC_SIZEALL cursor
    win.handle_mouse_down(obj_center_x, obj_center_y, 0);
    win.handle_mouse_up(obj_center_x, obj_center_y, 0);
    assert(sel.count() == 1);
    assert(win.active_tool() == 2);

    // 4. Guidelines: Pick chọn và di chuyển vị trí thước gióng
    win.set_active_tool(0);
    win.clear_guidelines();
    assert(win.guideline_count() == 0);

    win.add_guideline(NativeWindow::GuidelineOrientation::Horizontal, 80.0);
    win.add_guideline(NativeWindow::GuidelineOrientation::Vertical, 120.0);
    assert(win.guideline_count() == 2);

    const auto gl_h_screen = win.root().renderer().camera().world_to_screen({0.0, 80.0});
    const int gl_h_y = area.top + static_cast<int>(std::round(gl_h_screen.y));
    const int test_x = area.left + 200;

    // Cursor tại đường gióng ngang là IDC_SIZENS (32645)
    assert(win.canvas_cursor_at(test_x, gl_h_y) == 32645);

    // Pick đường gióng ngang và kéo dời sang vị trí mới Y = 110.0 mm
    win.handle_mouse_down(test_x, gl_h_y, 0);
    assert(win.is_dragging_guideline());
    assert(win.selected_guideline_index() == 0);

    const auto target_pos = win.root().renderer().camera().world_to_screen({0.0, 110.0});
    const int new_gl_y = area.top + static_cast<int>(std::round(target_pos.y));
    win.handle_mouse_move(test_x, new_gl_y, 0);
    win.handle_mouse_up(test_x, new_gl_y, 0);

    assert(win.guidelines()[0].orientation == NativeWindow::GuidelineOrientation::Horizontal);
    assert(std::abs(win.guidelines()[0].position_mm - 110.0) < 1.0);

    // Kéo đường gióng ra ngoài canvas (thả vào khu vực menu/topbar y < area.top) -> Guideline bị xóa
    win.handle_mouse_down(test_x, new_gl_y, 0);
    assert(win.is_dragging_guideline());
    win.handle_mouse_move(test_x, area.top - 20, 0);
    win.handle_mouse_up(test_x, area.top - 20, 0);
    assert(win.guideline_count() == 1);
    assert(win.guidelines()[0].orientation == NativeWindow::GuidelineOrientation::Vertical);

    std::cout << "[PASS] test_corel_pick_auto_select_resume_guidelines_and_space_shortcut\n";
}

void test_shortcuts_customization_and_zorder_navigation();

void test_layers_and_objects_dock_suite() {
    NativeWindow win(1440, 900);
    const int w = win.width();
    const int h = win.height();
    const int insp_x = w - 280;

    // 1. Initial Dock Tab is Inspector (0). Switch to Layers tab (1) via chrome tab click.
    assert(win.active_dock_tab() == 0);
    const int tab_w = 280 / 3;
    bool tab_clicked = win.handle_chrome_click(insp_x + tab_w + 10, 115);
    assert(tab_clicked);
    assert(win.active_dock_tab() == 1);

    // 2. Initial state: 1 default layer ("Layer 1"), All filter active
    auto snap = win.root().document().current_snapshot();
    assert(snap->page().layers.size() == 1);
    assert(win.layers_dock_state().filter == LayerFilterType::All);

    // 3. Test filter bar click: click Path filter (index 1)
    const int filter_bar_y = 138 + 26 + 10;
    bool filter_clicked = win.handle_chrome_click(insp_x + 12 + 51 + 10, filter_bar_y);
    assert(filter_clicked);
    assert(win.layers_dock_state().filter == LayerFilterType::Path);

    // Click back to All filter (index 0)
    win.handle_chrome_click(insp_x + 12 + 10, filter_bar_y);
    assert(win.layers_dock_state().filter == LayerFilterType::All);

    // 4. Test footer action bar: Click "+ Layer" button (btn 0)
    const int footer_y = h - 26 - 30 + 10;
    bool add_layer_clicked = win.handle_chrome_click(insp_x + 12 + 10, footer_y);
    assert(add_layer_clicked);
    snap = win.root().document().current_snapshot();
    assert(snap->page().layers.size() == 2);
    assert(snap->page().layers[1].name == "Layer 2");

    // 5. Test layer row expand/collapse toggle
    assert(win.layers_dock_state().collapsed_layers.empty());
    bool expand_clicked = win.handle_chrome_click(insp_x + 16, 200);
    assert(expand_clicked);
    assert(win.layers_dock_state().collapsed_layers.count(0) == 1);
    win.handle_chrome_click(insp_x + 16, 200);
    assert(win.layers_dock_state().collapsed_layers.empty());

    // 6. Test layer eye (visibility) toggle
    assert(snap->page().layers[0].visible == true);
    bool eye_clicked = win.handle_chrome_click(insp_x + 32, 200);
    assert(eye_clicked);
    snap = win.root().document().current_snapshot();
    assert(snap->page().layers[0].visible == false);

    // Undo visibility change
    win.workflow().undo();
    snap = win.root().document().current_snapshot();
    assert(snap->page().layers[0].visible == true);

    // 7. Test layer lock toggle
    assert(snap->page().layers[0].locked == false);
    bool lock_clicked = win.handle_chrome_click(insp_x + 48, 200);
    assert(lock_clicked);
    snap = win.root().document().current_snapshot();
    assert(snap->page().layers[0].locked == true);

    // Undo lock change
    win.workflow().undo();
    snap = win.root().document().current_snapshot();
    assert(snap->page().layers[0].locked == false);

    // 8. Test mouse wheel scroll in layers dock
    assert(win.layers_dock_state().scroll_offset_y == 0);
    win.layers_dock_state().total_content_height = 800;
    bool wheel_ok = win.handle_layers_dock_mouse_wheel(-120);
    assert(wheel_ok);
    assert(win.layers_dock_state().scroll_offset_y > 0);

    std::cout << "[PASS] test_layers_and_objects_dock_suite\n";
    test_shortcuts_customization_and_zorder_navigation();
}

void test_shortcuts_customization_and_zorder_navigation() {
    using namespace pikdraw;
    // 1. Format string test
    assert(format_key_shortcut_string(VK_PRIOR, false, true, false) == "Shift+PgUp");
    assert(format_key_shortcut_string(VK_NEXT, false, true, false) == "Shift+PgDn");
    assert(format_key_shortcut_string(VK_PRIOR, true, false, false) == "Ctrl+PgUp");
    assert(format_key_shortcut_string(VK_NEXT, true, false, false) == "Ctrl+PgDn");
    assert(format_key_shortcut_string(VK_UP, false, false, false) == "Up");
    assert(format_key_shortcut_string(VK_UP, false, true, false) == "Shift+Up");
    assert(format_key_shortcut_string(VK_UP, true, false, false) == "Ctrl+Up");

    // 2. Z-order navigation test
    NativeWindow win(1440, 900);
    ensure_shortcuts_initialized(win);

    auto r1 = vector::ShapeFactory::create_rectangle(
        geometry::Rect2D{10.0, 10.0, 30.0, 30.0},
        color::ColorValue::make_rgb(1.0, 0.0, 0.0),
        color::ColorValue::make_rgb(0.0, 0.0, 0.0), 1.0);
    auto r2 = vector::ShapeFactory::create_rectangle(
        geometry::Rect2D{20.0, 20.0, 30.0, 30.0},
        color::ColorValue::make_rgb(0.0, 1.0, 0.0),
        color::ColorValue::make_rgb(0.0, 0.0, 0.0), 1.0);
    auto r3 = vector::ShapeFactory::create_rectangle(
        geometry::Rect2D{30.0, 30.0, 30.0, 30.0},
        color::ColorValue::make_rgb(0.0, 0.0, 1.0),
        color::ColorValue::make_rgb(0.0, 0.0, 0.0), 1.0);
    auto id1 = r1.id;
    auto id2 = r2.id;
    auto id3 = r3.id;
    win.root().document().execute_command(std::make_unique<document::AddObjectCommand>(document::ObjectVariant{r1}));
    win.root().document().execute_command(std::make_unique<document::AddObjectCommand>(document::ObjectVariant{r2}));
    win.root().document().execute_command(std::make_unique<document::AddObjectCommand>(document::ObjectVariant{r3}));

    auto snap = win.root().document().current_snapshot();
    assert(snap->page().layers[0].object_ids.size() == 3);
    assert(snap->page().layers[0].object_ids[0] == id1);
    assert(snap->page().layers[0].object_ids[1] == id2);
    assert(snap->page().layers[0].object_ids[2] == id3);

    win.root().vector_controller().selection().select(id1);
    auto& dispatcher = win.root().window().action_dispatcher();

    // BringToFront: Shift+PgUp
    bool ok = dispatcher.dispatch_shortcut("Shift+PgUp");
    assert(ok);
    snap = win.root().document().current_snapshot();
    assert(snap->page().layers[0].object_ids[0] == id2);
    assert(snap->page().layers[0].object_ids[1] == id3);
    assert(snap->page().layers[0].object_ids[2] == id1);

    // SendBackward: Ctrl+PgDn
    ok = dispatcher.dispatch_shortcut("Ctrl+PgDn");
    assert(ok);
    snap = win.root().document().current_snapshot();
    assert(snap->page().layers[0].object_ids[0] == id2);
    assert(snap->page().layers[0].object_ids[1] == id1);
    assert(snap->page().layers[0].object_ids[2] == id3);

    // SendToBack: Shift+PgDn
    ok = dispatcher.dispatch_shortcut("Shift+PgDn");
    assert(ok);
    snap = win.root().document().current_snapshot();
    assert(snap->page().layers[0].object_ids[0] == id1);
    assert(snap->page().layers[0].object_ids[1] == id2);
    assert(snap->page().layers[0].object_ids[2] == id3);

    // BringForward: Ctrl+PgUp
    ok = dispatcher.dispatch_shortcut("Ctrl+PgUp");
    assert(ok);
    snap = win.root().document().current_snapshot();
    assert(snap->page().layers[0].object_ids[0] == id2);
    assert(snap->page().layers[0].object_ids[1] == id1);
    assert(snap->page().layers[0].object_ids[2] == id3);

    // Undo / Redo
    win.workflow().undo();
    snap = win.root().document().current_snapshot();
    assert(snap->page().layers[0].object_ids[0] == id1);
    win.workflow().redo();
    snap = win.root().document().current_snapshot();
    assert(snap->page().layers[0].object_ids[1] == id1);

    // 3. Nudge test
    const double tx_orig = std::get<document::PathObject>(*snap->get_object(id1)).transform.tx;
    ok = dispatcher.dispatch_shortcut("Right");
    assert(ok);
    snap = win.root().document().current_snapshot();
    const double tx_nudge = std::get<document::PathObject>(*snap->get_object(id1)).transform.tx;
    assert(std::abs(tx_nudge - (tx_orig + 1.0)) < 1e-4);

    ok = dispatcher.dispatch_shortcut("Shift+Right");
    assert(ok);
    snap = win.root().document().current_snapshot();
    const double tx_super = std::get<document::PathObject>(*snap->get_object(id1)).transform.tx;
    assert(std::abs(tx_super - (tx_nudge + 10.0)) < 1e-4);

    ok = dispatcher.dispatch_shortcut("Ctrl+Right");
    assert(ok);
    snap = win.root().document().current_snapshot();
    const double tx_micro = std::get<document::PathObject>(*snap->get_object(id1)).transform.tx;
    assert(std::abs(tx_micro - (tx_super + 0.1)) < 1e-4);

    // 4. Settings Tab 4 (Shortcuts) test
    win.open_settings_modal();
    win.set_settings_tab(4);
    assert(win.settings_active_tab() == 4);
    auto& state = get_shortcuts_settings_state();

    const int modal_w = 720, modal_h = 500;
    const int modal_x = (win.width() - modal_w) / 2;
    const int modal_y = (win.height() - modal_h) / 2;
    const int cx = modal_x + 205;
    const int cy = modal_y + 54;

    // Switch to Adobe Illustrator profile
    bool click_ok = handle_shortcuts_settings_click(win, cx + 162 + 20, cy + 30);
    assert(click_ok);
    assert(state.active_profile == 1);
    assert(dispatcher.get_action_shortcut(actions::ActionId::kObjectOrderToFront) == "Ctrl+Shift+]");

    // Switch back to CorelDRAW profile
    click_ok = handle_shortcuts_settings_click(win, cx + 20, cy + 30);
    assert(click_ok);
    assert(state.active_profile == 0);
    assert(dispatcher.get_action_shortcut(actions::ActionId::kObjectOrderToFront) == "Shift+PgUp");

    // Category filter pills
    click_ok = handle_shortcuts_settings_click(win, cx + 97 + 10, cy + 60);
    assert(click_ok);
    assert(state.active_category == ShortcutCategory::Order);

    // Row selection & Key recording
    const int cat_y = cy + 52;
    const int rows_start_y = cat_y + 28 + 22;
    const int btns_y = rows_start_y + 9 * 24 + 10;
    click_ok = handle_shortcuts_settings_click(win, cx + 50, btns_y + 10);
    assert(click_ok);
    assert(state.is_recording);

    // Clear and Reset buttons
    click_ok = handle_shortcuts_settings_click(win, cx + 180, btns_y + 10);
    assert(click_ok);
    click_ok = handle_shortcuts_settings_click(win, cx + 300, btns_y + 10);
    assert(click_ok);

    win.close_settings_modal();
    assert(!win.is_settings_modal_open());

    std::cout << "[PASS] test_shortcuts_customization_and_zorder_navigation\n";
}


