#include "desktop_shell_test_helpers.hpp"
#include "../src/shell/native_window_selection_geometry.hpp"

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

    // Canvas authoring changes only its preview snapshot until the user commits.
    NativeWindow editor(1440, 900);
    editor.set_active_tool(4);
    const auto start_revision = editor.root().document().revision();
    editor.start_inline_text_editing({42.0, 54.0});
    assert(editor.is_inline_editing());
    assert(editor.handle_inline_text_character('B'));
    assert(editor.handle_inline_text_character('i'));
    assert(editor.handle_inline_text_character('e'));
    assert(editor.handle_inline_text_character(0x0309));
    assert(editor.inline_text_buffer() == "Bie\u0309");
    assert(editor.root().document().revision() == start_revision);
    auto preview = editor.workflow().snapshot_for_canvas();
    assert(preview->revision() == start_revision);
    auto preview_id = pikdraw::document::kInvalidObjectId;
    for (const auto& [id, object] : preview->objects().all())
        if (const auto* text = std::get_if<pikdraw::document::TextObject>(&object);
            text && text->text == "Bie\u0309") preview_id = id;
    assert(preview_id != pikdraw::document::kInvalidObjectId);
    assert(editor.handle_inline_text_key(VK_RETURN, false, false));
    assert(editor.handle_inline_text_character(VK_RETURN)); // matching translated WM_CHAR is consumed
    assert(editor.inline_text_buffer() == "Bie\u0309\n");
    assert(editor.handle_inline_text_character('N'));
    assert(editor.handle_inline_text_key('B', true, false));
    assert(editor.is_inline_editing());
    assert(editor.handle_inline_text_key('Z', true, false)); // editor-local undo
    assert(editor.inline_text_buffer() == "Bie\u0309\nN");
    assert(editor.root().document().revision() == start_revision);
    auto pixel_preview = editor.workflow().render_snapshot(160, 90);
    editor.commit_inline_text_editing();
    assert(!editor.is_inline_editing());
    assert(editor.root().document().revision() == start_revision + 1);
    auto committed_text = editor.root().document().current_snapshot()->objects().get(preview_id);
    assert(committed_text && std::get<pikdraw::document::TextObject>(*committed_text).text ==
           "Bie\u0309\nN");
    assert(editor.workflow().render_snapshot(160, 90) == pixel_preview);
    assert(editor.workflow().undo());
    assert(!editor.root().document().current_snapshot()->objects().get(preview_id));
    assert(editor.workflow().redo());

    const auto cancel_revision = editor.root().document().revision();
    const auto cancel_object_count = editor.root().document().current_snapshot()->object_count();
    editor.start_inline_text_editing({120, 40});
    assert(editor.handle_inline_text_character('d'));
    assert(editor.handle_inline_text_key(VK_ESCAPE, false, false));
    assert(!editor.is_inline_editing());
    assert(editor.root().document().revision() == cancel_revision);
    assert(editor.root().document().current_snapshot()->object_count() == cancel_object_count);

    pikdraw::document::TextObject source_text;
    source_text.id = pikdraw::document::generate_object_id();
    source_text.name = "Text";
    source_text.text = "Bi e\u0309n";
    source_text.font.family_name = "Arial";
    source_text.font.postscript_name = "Keep-Imported-Face-Identity";
    source_text.font.size_pt = 22;
    source_text.position = {46, 72};
    source_text.transform = pikdraw::geometry::Transform2D::translation(7, 9);
    source_text.frame_width_mm = 34;
    source_text.line_spacing_ratio = 1.4;
    const auto nfd_id = source_text.id;
    assert(editor.root().document().execute_command(
        std::make_unique<pikdraw::document::AddObjectCommand>(source_text)).success);
    sel.clear();
    sel.select(nfd_id);
    const auto object_to_edit = std::get<pikdraw::document::TextObject>(
        *editor.root().document().current_snapshot()->objects().get(nfd_id));
    editor.start_inline_text_editing({object_to_edit.position.x + 34, object_to_edit.position.y}, nfd_id);
    assert(editor.inline_text_buffer() == object_to_edit.text);
    assert(editor.handle_inline_text_key('A', true, false));
    assert(editor.inline_text_buffer() == object_to_edit.text);
    assert(editor.handle_inline_text_character('X'));
    editor.commit_inline_text_editing();
    auto updated = std::get<pikdraw::document::TextObject>(
        *editor.root().document().current_snapshot()->objects().get(nfd_id));
    assert(updated.text == "X");
    assert(updated.font.postscript_name == "Keep-Imported-Face-Identity");
    assert(updated.frame_width_mm == 34 && updated.line_spacing_ratio == 1.4);
    assert(updated.transform.tx == 7 && updated.transform.ty == 9);
    const auto native_path = shell_screenshot_path("canvas-text-serialization.pikdraw");
    assert(editor.workflow().save_native(native_path.string()));
    assert(editor.workflow().open_native(native_path.string()));
    auto reopened = std::get<pikdraw::document::TextObject>(
        *editor.root().document().current_snapshot()->objects().get(nfd_id));
    assert(reopened.text == updated.text && reopened.font.postscript_name == updated.font.postscript_name);
    assert(reopened.frame_width_mm == 34 && reopened.line_spacing_ratio == 1.4);
    assert(reopened.transform.tx == 7 && reopened.transform.ty == 9);
    editor.start_inline_text_editing(reopened.position, nfd_id);
    assert(editor.handle_inline_text_key('A', true, false));
    assert(editor.handle_inline_text_character('Q'));
    assert(editor.handle_inline_text_key(VK_ESCAPE, false, false));
    const auto after_cancel = std::get<pikdraw::document::TextObject>(
        *editor.root().document().current_snapshot()->objects().get(nfd_id));
    assert(after_cancel.text == reopened.text);
    std::filesystem::remove(native_path);

    const auto stale_id = editor.workflow().add_vietnamese_text(
        80, 90, "Keep source", "Arial", 22, "#123456");
    const auto stale_obj = std::get<pikdraw::document::TextObject>(
        *editor.root().document().current_snapshot()->objects().get(stale_id));
    editor.start_inline_text_editing(stale_obj.position, stale_id);
    editor.handle_inline_text_key('A', true, false);
    assert(editor.handle_inline_text_character('X'));
    assert(editor.workflow().transform_object(stale_id, 1, 0));
    editor.commit_inline_text_editing();
    assert(std::get<pikdraw::document::TextObject>(
        *editor.root().document().current_snapshot()->objects().get(stale_id)).text == "Keep source");

    // A selected text object can be scaled directly from its handles while Text stays active.
    NativeWindow text_scale(1440, 900);
    text_scale.set_active_tool(4);
    auto& scale_camera = text_scale.root().renderer().camera();
    scale_camera.set_viewport_size(1440 - 352, 900 - 148);
    text_scale.start_inline_text_editing({50, 80});
    for (char value : std::string("Scale directly"))
        text_scale.handle_inline_text_character(static_cast<unsigned char>(value));
    text_scale.commit_inline_text_editing();
    const auto scale_id = text_scale.root().vector_controller().selection().selected_ids().front();
    const auto before_scale = text_scale.root().vector_controller().selection().bounds(
        *text_scale.root().document().current_snapshot());
    assert(before_scale && before_scale->width > 0 && before_scale->height > 0);
    const auto body_screen = scale_camera.world_to_screen(before_scale->center());
    const int body_x = 72 + static_cast<int>(std::lround(body_screen.x));
    const int body_y = 122 + static_cast<int>(std::lround(body_screen.y));
    text_scale.handle_mouse_down(body_x, body_y);
    assert(!text_scale.is_inline_editing());
    text_scale.handle_mouse_move(body_x + 24, body_y + 12);
    text_scale.handle_mouse_up(body_x + 24, body_y + 12);
    const auto after_move = text_scale.root().vector_controller().selection().bounds(
        *text_scale.root().document().current_snapshot());
    assert(after_move && std::hypot(after_move->center().x - before_scale->center().x,
                                   after_move->center().y - before_scale->center().y) > 0.1);
    assert(!text_scale.is_inline_editing());
    const auto selection_rect = selection_ui::screen_bounds(scale_camera, *after_move,
        text_scale.canvas_rect(1440, 900));
    const auto scale_handle = selection_ui::handles(selection_rect).front();
    assert(text_scale.selection_handle_at(scale_handle.x, scale_handle.y) == 0);
    text_scale.handle_mouse_down(scale_handle.x, scale_handle.y);
    text_scale.handle_mouse_move(scale_handle.x - 24, scale_handle.y - 18);
    text_scale.handle_mouse_up(scale_handle.x - 24, scale_handle.y - 18);
    const auto after_scale = text_scale.root().vector_controller().selection().bounds(
        *text_scale.root().document().current_snapshot());
    assert(after_scale && after_scale->width > before_scale->width &&
           after_scale->height > before_scale->height);
    const auto selected_side_rect = selection_ui::screen_bounds(scale_camera, *after_scale,
        text_scale.canvas_rect(1440, 900));
    const auto selected_side = selection_ui::handles(selected_side_rect)[1];
    assert(text_scale.selection_handle_at(selected_side.x, selected_side.y) == 1);
    text_scale.handle_mouse_down(selected_side.x, selected_side.y);
    text_scale.handle_mouse_move(selected_side.x, selected_side.y - 24);
    text_scale.handle_mouse_up(selected_side.x, selected_side.y - 24);
    const auto after_side_scale = text_scale.root().vector_controller().selection().bounds(
        *text_scale.root().document().current_snapshot());
    assert(after_side_scale && after_side_scale->height > after_scale->height);
    assert(std::abs(after_side_scale->width / after_scale->width -
                    after_side_scale->height / after_scale->height) < 1e-4);
    assert(std::holds_alternative<pikdraw::document::TextObject>(
        *text_scale.root().document().current_snapshot()->objects().get(scale_id)));

    // Live corner scaling updates the draft's font size proportionally and keeps editing active.
    NativeWindow live_scale(1440, 900);
    live_scale.set_active_tool(4);
    auto& live_camera = live_scale.root().renderer().camera();
    live_camera.set_viewport_size(1440 - 352, 900 - 148);
    live_scale.start_inline_text_editing({60, 80});
    for (char value : std::string("Keep proportions"))
        live_scale.handle_inline_text_character(static_cast<unsigned char>(value));
    const auto live_revision = live_scale.root().document().revision();
    auto live_preview = live_scale.workflow().snapshot_for_canvas();
    std::optional<pikdraw::document::TextObject> draft_before;
    for (const auto& [id, object] : live_preview->objects().all()) {
        (void)id;
        if (const auto* text = std::get_if<pikdraw::document::TextObject>(&object)) draft_before = *text;
    }
    assert(draft_before);
    const auto live_bounds_before = draft_before->bounds();
    const auto live_size_before = draft_before->font.size_pt;
    const auto live_rect = selection_ui::screen_bounds(live_camera, live_bounds_before,
        live_scale.canvas_rect(1440, 900));
    const auto live_handle = selection_ui::handles(live_rect).front();
    assert(live_scale.selection_handle_at(live_handle.x, live_handle.y) == 0);
    live_scale.handle_mouse_down(live_handle.x, live_handle.y);
    assert(live_scale.is_inline_editing());
    live_scale.handle_mouse_move(live_handle.x - 30, live_handle.y - 24);
    live_scale.handle_mouse_up(live_handle.x - 30, live_handle.y - 24);
    assert(live_scale.is_inline_editing());
    assert(live_scale.root().document().revision() == live_revision);
    live_preview = live_scale.workflow().snapshot_for_canvas();
    std::optional<pikdraw::document::TextObject> draft_after;
    for (const auto& [id, object] : live_preview->objects().all()) {
        (void)id;
        if (const auto* text = std::get_if<pikdraw::document::TextObject>(&object)) draft_after = *text;
    }
    assert(draft_after && draft_after->font.size_pt > live_size_before);
    const auto live_bounds_after = draft_after->bounds();
    assert(std::abs(live_bounds_after.right() - live_bounds_before.right()) < 1e-4);
    assert(std::abs(live_bounds_after.y - live_bounds_before.y) < 1e-4);
    const double live_factor = draft_after->font.size_pt / live_size_before;
    assert(std::abs(live_bounds_after.width / live_bounds_before.width - live_factor) < 1e-4);
    assert(std::abs(live_bounds_after.height / live_bounds_before.height - live_factor) < 1e-4);
    assert(live_scale.handle_inline_text_character('!'));
    assert(live_scale.inline_text_buffer().ends_with("!"));

    // Side handles scale both live text dimensions uniformly, keep the opposite edge anchored,
    // and leave the caret/editor active so typing can continue without switching tools.
    live_preview = live_scale.workflow().snapshot_for_canvas();
    std::optional<pikdraw::document::TextObject> draft_before_side;
    for (const auto& [id, object] : live_preview->objects().all()) {
        (void)id;
        if (const auto* text = std::get_if<pikdraw::document::TextObject>(&object))
            draft_before_side = *text;
    }
    assert(draft_before_side);
    const auto side_bounds_before = draft_before_side->bounds();
    const auto side_rect = selection_ui::screen_bounds(live_camera, side_bounds_before,
        live_scale.canvas_rect(1440, 900));
    const auto side_handle = selection_ui::handles(side_rect)[1];
    assert(live_scale.selection_handle_at(side_handle.x, side_handle.y) == 1);
    live_scale.handle_mouse_down(side_handle.x, side_handle.y);
    live_scale.handle_mouse_move(side_handle.x, side_handle.y - 24);
    live_scale.handle_mouse_up(side_handle.x, side_handle.y - 24);
    assert(live_scale.is_inline_editing());
    assert(live_scale.root().document().revision() == live_revision);
    live_preview = live_scale.workflow().snapshot_for_canvas();
    std::optional<pikdraw::document::TextObject> draft_after_side;
    for (const auto& [id, object] : live_preview->objects().all()) {
        (void)id;
        if (const auto* text = std::get_if<pikdraw::document::TextObject>(&object))
            draft_after_side = *text;
    }
    assert(draft_after_side && draft_after_side->font.size_pt > draft_before_side->font.size_pt);
    const auto side_bounds_after = draft_after_side->bounds();
    const double side_factor = draft_after_side->font.size_pt / draft_before_side->font.size_pt;
    assert(std::abs(side_bounds_after.width / side_bounds_before.width - side_factor) < 1e-4);
    assert(std::abs(side_bounds_after.height / side_bounds_before.height - side_factor) < 1e-4);
    assert(std::abs(side_bounds_after.center().x - side_bounds_before.center().x) < 1e-4);
    assert(std::abs(side_bounds_after.y - side_bounds_before.y) < 1e-4);
    assert(live_scale.handle_inline_text_character('?'));
    assert(live_scale.inline_text_buffer().ends_with("!?"));

    // Text Properties exposes a real point-size field and preserves unrelated paragraph/style data.
    NativeWindow text_properties(1440, 900);
    const auto property_text_id = text_properties.workflow().add_vietnamese_text(
        40, 50, "Property font size", "Be Vietnam Pro", 24, "#102030");
    auto& property_selection = text_properties.root().vector_controller().selection();
    property_selection.select(property_text_id);
    assert(text_properties.workflow().style_selected_text("", 24, true, true, true,
        pikdraw::typography::TextAlignment::Center));
    text_properties.start_numeric_input(NativeWindow::NumericFieldId::TextFontSize);
    text_properties.handle_numeric_char_input('3');
    text_properties.handle_numeric_char_input('6');
    text_properties.commit_numeric_field();
    const auto property_text = std::get<pikdraw::document::TextObject>(
        *text_properties.root().document().current_snapshot()->objects().get(property_text_id));
    assert(property_text.font.size_pt == 36);
    assert(property_text.font.weight >= 700 && property_text.font.italic);
    assert(property_text.underline);
    assert(property_text.alignment == pikdraw::typography::TextAlignment::Center);
    assert(text_properties.workflow().undo());
    const auto property_text_undone = std::get<pikdraw::document::TextObject>(
        *text_properties.root().document().current_snapshot()->objects().get(property_text_id));
    assert(property_text_undone.font.size_pt == 24);
    assert(property_text_undone.underline &&
           property_text_undone.alignment == pikdraw::typography::TextAlignment::Center);

    // Real pointer path: dragging with Text selected creates a wrapping text frame.
    NativeWindow frame(1440, 900);
    frame.set_active_tool(4);
    auto& camera = frame.root().renderer().camera();
    camera.set_viewport_size(1440 - 352, 900 - 148);
    const auto canvas_point = [&](pikdraw::geometry::Point2D world) {
        const auto screen = camera.world_to_screen(world);
        return std::pair<int, int>{72 + static_cast<int>(screen.x), 122 + static_cast<int>(screen.y)};
    };
    const auto [start_x, start_y] = canvas_point({50, 100});
    const auto [end_x, end_y] = canvas_point({105, 65});
    frame.handle_mouse_down(start_x, start_y);
    frame.handle_mouse_move(end_x, end_y);
    frame.handle_mouse_up(end_x, end_y);
    for (char value : std::string("PikDraw text wraps on the canvas"))
        frame.handle_inline_text_character(static_cast<unsigned char>(value));
    auto frame_preview = frame.workflow().snapshot_for_canvas();
    std::optional<pikdraw::document::TextObject> paragraph;
    for (const auto& [id, object] : frame_preview->objects().all()) {
        (void)id;
        if (const auto* text = std::get_if<pikdraw::document::TextObject>(&object)) paragraph = *text;
    }
    assert(paragraph && paragraph->frame_width_mm > 0);
    assert(paragraph->layout().line_count() > 1);
    const auto paragraph_origin = camera.world_to_screen({paragraph->position.x,
        paragraph->position.y + paragraph->layout().lines().front().run.ascent_mm});
    assert(std::abs(72 + paragraph_origin.x - start_x) <= 1.0);
    assert(std::abs(122 + paragraph_origin.y - start_y) <= 1.0);
    frame.commit_inline_text_editing();
    auto saved_paragraph = std::get<pikdraw::document::TextObject>(
        *frame.root().document().current_snapshot()->objects().get(paragraph->id));
    assert(saved_paragraph.frame_width_mm == paragraph->frame_width_mm);

    std::cout << "[PASS] test_typography_inline_editing_ribbon_and_table\n";
}

