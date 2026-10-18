#include "desktop_shell_test_helpers.hpp"

void test_typography_inline_editing_ribbon_and_table() {
    NativeWindow win(1440, 900);
    auto& workflow = win.workflow();
    auto& sel = win.root().vector_controller().selection();

    // ----------------------------------------------------
    // Item 1: UI & Vector Text Object Interaction
    // ----------------------------------------------------
    win.set_active_tool(4);
    assert(win.status_message().find("text") != std::string::npos ||
           win.status_message().find("Văn bản") != std::string::npos);

    const auto text_id = workflow.add_vietnamese_text(
        40.0, 50.0, "PikDraw — Biển hiệu Ánh Sáng", "Be Vietnam Pro", 28.0, "#00f2c3"
    );
    assert(text_id != pikdraw::document::kInvalidObjectId);
    sel.select(text_id);
    assert(win.is_text_object_selected());

    auto b_opt = workflow.get_object_bounds(text_id);
    assert(b_opt.has_value());
    assert(b_opt->width > 0.0 && b_opt->height > 0.0);

    bool move_ok = workflow.transform_object(text_id, 10.0, -5.0);
    assert(move_ok);
    assert(workflow.undo());

    // ----------------------------------------------------
    // Item 2: Core Typography Engine Verification
    // ----------------------------------------------------
    pikdraw::typography::FontIdentity font{"Be Vietnam Pro", "BeVietnamPro-Bold", 700, false, 72.0};
    assert(std::abs(font.size_mm() - 25.4) < 1e-6);

    auto& resolver = pikdraw::typography::FontResolver::instance();
    auto res_font = resolver.resolve(pikdraw::typography::FontIdentity{"MissingFontXYZ", "Missing", 400, false, 14.0});
    assert(res_font.is_fallback);
    assert(res_font.requested.family_name == "MissingFontXYZ");

    std::string vi_nfc = "Biển hiệu";
    auto run_nfc = pikdraw::typography::shape_text(vi_nfc, font);
    assert(!run_nfc.glyphs.empty() && run_nfc.total_advance_mm > 0.0);

    std::string vi_nfd = "Bi\u0065\u0309n";
    auto run_nfd = pikdraw::typography::shape_text(vi_nfd, font);
    // DirectWrite returns font glyph indices, not Unicode code points. NFC/NFD
    // equivalents must shape identically while preserving editable source bytes.
    auto equivalent = pikdraw::typography::shape_text("Bi\u1EBBn", font);
    assert(run_nfd.text_utf8 == vi_nfd);
    assert(run_nfd.glyphs.size() == equivalent.glyphs.size());
    assert(std::abs(run_nfd.total_advance_mm - equivalent.total_advance_mm) < 1e-6);
    for (size_t i = 0; i < run_nfd.glyphs.size(); ++i) {
        const auto& glyph = run_nfd.glyphs[i];
        assert(glyph.glyph_id != 0);
        assert(glyph.glyph_id == equivalent.glyphs[i].glyph_id);
        assert(glyph.cluster_byte_offset < vi_nfd.size());
        assert((static_cast<unsigned char>(vi_nfd[glyph.cluster_byte_offset]) & 0xC0) != 0x80);
    }

    std::string multiline = "Dòng 1: Thiết kế quảng cáo\nDòng 2: Cắt khắc CNC & Laser\nDòng 3: PikDraw 2026";
    auto layout = pikdraw::typography::TextLayout::layout_text(multiline, font);
    assert(layout.line_count() == 3);
    assert(layout.bounds().width > 0.0);

    std::string grapheme_test = "Bi\u1EC3n";
    size_t c0 = 0;
    size_t c1 = pikdraw::typography::next_grapheme_cluster(grapheme_test, c0);
    assert(c1 == 1);
    size_t c2 = pikdraw::typography::next_grapheme_cluster(grapheme_test, c1);
    assert(c2 == 2);
    size_t c3 = pikdraw::typography::next_grapheme_cluster(grapheme_test, c2);
    assert(c3 == 5);

    auto outlines = pikdraw::typography::text_to_path_outlines(layout);
    assert(!outlines.empty());
    assert(outlines.bounds().width > 0.0);

    // ----------------------------------------------------
    // Item 3: Vector & Solid Tools Interaction
    // ----------------------------------------------------
    sel.clear();
    sel.select(text_id);
    bool split_text_ok = workflow.split_objects_by_line({50.0, 30.0}, {50.0, 90.0}, 20.0);
    assert(split_text_ok);
    assert(sel.count() == 2);
    assert(workflow.undo());

    // ----------------------------------------------------
    // Item 4: On-Canvas Inline Editing, Typography Ribbon & Corel Table
    // ----------------------------------------------------
    sel.clear();
    sel.select(text_id);
    win.cycle_font_family();
    win.adjust_font_size(4.0);
    win.toggle_font_bold();
    win.toggle_font_italic();
    win.toggle_font_underline();
    win.set_font_alignment(pikdraw::typography::TextAlignment::Center);

    auto snap = win.root().document().current_snapshot();
    auto cur_text_obj = std::get<pikdraw::document::TextObject>(*snap->objects().get(text_id));
    assert(cur_text_obj.font.weight == 700);
    assert(cur_text_obj.font.italic == true);
    assert(cur_text_obj.underline == true);
    assert(cur_text_obj.alignment == pikdraw::typography::TextAlignment::Center);

    // 4b. On-Canvas Inline Text Editing
    win.start_inline_text_editing({50.0, 70.0});
    assert(win.is_inline_editing());
    // Simulate real typing onto canvas
    capture_shell_screenshot(win, "m1_pikdraw_text_inline_editing.bmp");
    win.commit_inline_text_editing();
    assert(!win.is_inline_editing());

    // 4c. CorelDRAW-Style Vector Table Tool
    auto table_id = workflow.create_table(30.0, 35.0, 180.0, 80.0, 3, 4, "#0ea5e9", "#f8fafc", 0.8);
    assert(table_id != pikdraw::document::kInvalidObjectId);
    assert(workflow.is_group(table_id));
    sel.clear();
    sel.select(table_id);
    capture_shell_screenshot(win, "m1_pikdraw_corel_table.bmp");

    // 4d. Convert Text to Curves (CorelDRAW Ctrl+Q)
    const auto text_curves_id = workflow.add_vietnamese_text(35.0, 130.0, "PIKDRAW CURVES CNC 2026", "Segoe UI", 32.0, "#f59e0b");
    sel.clear();
    sel.select(text_curves_id);
    bool conv_ok = workflow.convert_text_to_curves(text_curves_id);
    assert(conv_ok);
    const auto new_curve_id = sel.selected_ids().front();
    auto curve_obj = win.root().document().current_snapshot()->objects().get(new_curve_id);
    assert(curve_obj.has_value());
    assert(std::holds_alternative<pikdraw::document::PathObject>(*curve_obj));
    capture_shell_screenshot(win, "m1_pikdraw_text_curves.bmp");

    std::cout << "[PASS] test_typography_inline_editing_ribbon_and_table\n";
}

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

    // 2. Hover over Left Tool Strip: Tool 0 (Pick Tool), Tool 4 (Text Tool), Tool 5 (Knife Tool)
    win.set_language(UILanguage::Vietnamese);
    win.update_hover_tooltip(20, 115); // Tool 0 (content_top + 10 = 112)
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"chọn") != std::wstring::npos);
    assert(win.tooltip().text.find(L"Phím V") != std::wstring::npos);

    win.update_hover_tooltip(20, 115 + 4 * 42); // Tool 4 (Text Tool)
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"văn bản") != std::wstring::npos);
    assert(win.tooltip().text.find(L"F8") != std::wstring::npos);

    win.update_hover_tooltip(20, 115 + 5 * 42); // Tool 5 (Knife Tool)
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

    // 9. Verify Del, Dup, and Page buttons are completely absent from the ribbon
    win.update_hover_tooltip(490, 80); // Former Del position
    assert(!win.tooltip().visible);
    win.update_hover_tooltip(545, 80); // Former Dup position
    assert(!win.tooltip().visible);

    // Single-selection verification
    win.root().vector_controller().selection().clear();
    win.root().vector_controller().selection().select(r1, pikdraw::vector::SelectionMode::Add);
    win.update_hover_tooltip(490, 80); // Former Del position
    assert(!win.tooltip().visible);
    win.update_hover_tooltip(545, 80); // Former Dup position
    assert(!win.tooltip().visible);
    win.update_hover_tooltip(590, 80); // 580..625 is now FlipH (replacing Page)
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"Lật ngang") != std::wstring::npos);

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

    std::cout << "DEBUG: active_tool=" << win.active_tool() << " sel=" << sel.count()
              << " status=" << win.status_message() << " snap_objs="
              << win.root().document().current_snapshot()->object_count() << std::endl;
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

