#include "desktop_shell_test_helpers.hpp"
#include "../src/shell/native_window_selection_geometry.hpp"
#include "pikdraw/desktop/shell/native_font_picker.hpp"
#include "pikdraw/typography/layout/case_conversion.hpp"
#include "pikdraw/vector/gestures/text_tool.hpp"
#include <string_view>

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
    const auto before_text_cut_revision = win.root().document().revision();
    bool split_text_ok = workflow.split_objects_by_line({50.0, 30.0}, {50.0, 90.0}, 20.0);
    assert(!split_text_ok);
    assert(win.root().document().revision() == before_text_cut_revision);
    assert(win.root().document().current_snapshot()->objects().get(text_id).has_value());

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
    const auto source_text_value = win.root().document().current_snapshot()->objects().get(text_curves_id);
    assert(source_text_value && std::holds_alternative<pikdraw::document::TextObject>(*source_text_value));
    const auto source_fill = std::get<pikdraw::document::TextObject>(*source_text_value).fill;
    sel.clear();
    sel.select(text_curves_id);
    bool conv_ok = workflow.convert_text_to_curves(text_curves_id);
    assert(conv_ok);
    const auto new_curve_id = sel.selected_ids().front();
    auto curve_obj = win.root().document().current_snapshot()->objects().get(new_curve_id);
    assert(curve_obj.has_value());
    assert(std::holds_alternative<pikdraw::document::PathObject>(*curve_obj));
    const auto& outlined_path = std::get<pikdraw::document::PathObject>(*curve_obj);
    assert(outlined_path.has_fill);
    assert(!outlined_path.has_stroke);
    assert(outlined_path.stroke_width_mm == 0.0);
    assert(outlined_path.visible);
    assert(outlined_path.fill.color_space() == source_fill.color_space());
    if (const auto* source_rgb = std::get_if<pikdraw::color::RGBColor>(&source_fill.payload())) {
        const auto* curve_rgb = std::get_if<pikdraw::color::RGBColor>(&outlined_path.fill.payload());
        assert(curve_rgb && curve_rgb->approx_equal(*source_rgb));
    } else if (const auto* source_cmyk = std::get_if<pikdraw::color::CMYKColor>(&source_fill.payload())) {
        const auto* curve_cmyk = std::get_if<pikdraw::color::CMYKColor>(&outlined_path.fill.payload());
        assert(curve_cmyk && curve_cmyk->approx_equal(*source_cmyk));
    }

    const auto empty_text_id = workflow.add_vietnamese_text(35.0, 170.0, "", "Segoe UI", 32.0);
    const auto before_empty_conversion = win.root().document().current_snapshot();
    const auto before_empty_revision = before_empty_conversion->revision();
    assert(!workflow.convert_text_to_curves(empty_text_id));
    const auto after_empty_conversion = win.root().document().current_snapshot();
    assert(after_empty_conversion->revision() == before_empty_revision);
    assert(std::holds_alternative<pikdraw::document::TextObject>(
        *after_empty_conversion->objects().get(empty_text_id)));
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
    const auto native_path = shell_screenshot_path("canvas-text-serialization.pr");
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
    const auto scale_canvas = text_scale.canvas_rect(1440, 900);
    scale_camera.set_viewport_size(scale_canvas.right - scale_canvas.left, scale_canvas.bottom - scale_canvas.top);
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
    const auto live_canvas = live_scale.canvas_rect(1440, 900);
    live_camera.set_viewport_size(live_canvas.right - live_canvas.left, live_canvas.bottom - live_canvas.top);
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
    pikdraw::vector::TextPropertiesDelta initial_text_style;
    initial_text_style.size_pt = 24;
    initial_text_style.weight = 700;
    initial_text_style.italic = true;
    initial_text_style.underline = true;
    initial_text_style.alignment = pikdraw::typography::TextAlignment::Center;
    assert(text_properties.workflow().style_selected_text(initial_text_style));
    text_properties.start_numeric_input(NativeWindow::NumericFieldId::TextFontSize);
    text_properties.handle_numeric_char_input('3');
    text_properties.handle_numeric_char_input('6');
    text_properties.commit_numeric_field();
    const auto property_text = std::get<pikdraw::document::TextObject>(
        *text_properties.root().document().current_snapshot()->objects().get(property_text_id));
    assert(property_text.font.size_pt == 36);
    assert(property_text.font.weight >= 700 && property_text.font.italic);
    assert(property_text.underline &&
           property_text.alignment == pikdraw::typography::TextAlignment::Center);
    assert(property_text.underline);
    assert(property_text.alignment == pikdraw::typography::TextAlignment::Center);
    assert(text_properties.workflow().undo());
    const auto property_text_undone = std::get<pikdraw::document::TextObject>(
        *text_properties.root().document().current_snapshot()->objects().get(property_text_id));
    assert(property_text_undone.font.size_pt == 24);
    assert(property_text_undone.underline &&
           property_text_undone.alignment == pikdraw::typography::TextAlignment::Center);

    text_properties.start_numeric_input(NativeWindow::NumericFieldId::TextFrameWidth);
    assert(text_properties.active_numeric_field() == NativeWindow::NumericFieldId::TextFrameWidth);
    for (const wchar_t ch : std::wstring_view(L"25.4mm")) text_properties.handle_numeric_char_input(ch);
    text_properties.commit_numeric_field();
    auto paragraph_fields = std::get<pikdraw::document::TextObject>(
        *text_properties.root().document().current_snapshot()->objects().get(property_text_id));
    assert(paragraph_fields.frame_width_mm == 25.4);
    assert(paragraph_fields.line_spacing_ratio == 1.2);
    assert(text_properties.workflow().undo());

    text_properties.start_numeric_input(NativeWindow::NumericFieldId::TextLineSpacing);
    assert(text_properties.active_numeric_field() == NativeWindow::NumericFieldId::TextLineSpacing);
    for (const wchar_t ch : std::wstring_view(L"150")) text_properties.handle_numeric_char_input(ch);
    text_properties.commit_numeric_field();
    paragraph_fields = std::get<pikdraw::document::TextObject>(
        *text_properties.root().document().current_snapshot()->objects().get(property_text_id));
    assert(paragraph_fields.line_spacing_ratio == 1.5);

    // Real pointer path: dragging with Text selected creates a wrapping text frame.
    NativeWindow frame(1440, 900);
    frame.set_active_tool(4);
    auto& camera = frame.root().renderer().camera();
    const auto frame_canvas = frame.canvas_rect(1440, 900);
    camera.set_viewport_size(frame_canvas.right - frame_canvas.left, frame_canvas.bottom - frame_canvas.top);
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

    // ----------------------------------------------------
    // Item 5: Live Text Frame Move & Paragraph Edge Scale (Corel-style)
    // ----------------------------------------------------
    NativeWindow live_edit(1440, 900);
    live_edit.set_active_tool(4);
    auto& live_cam = live_edit.root().renderer().camera();
    const auto live_edit_canvas = live_edit.canvas_rect(1440, 900);
    live_cam.set_viewport_size(live_edit_canvas.right - live_edit_canvas.left, live_edit_canvas.bottom - live_edit_canvas.top);
    live_edit.start_inline_text_editing({50, 70});
    for (char c : std::string("Corel live move"))
        live_edit.handle_inline_text_character(static_cast<unsigned char>(c));
    assert(live_edit.is_inline_editing());

    auto snap_before = live_edit.workflow().snapshot_for_canvas();
    std::optional<pikdraw::document::TextObject> txt_before;
    for (const auto& [id, obj] : snap_before->objects().all()) {
        (void)id;
        if (const auto* t = std::get_if<pikdraw::document::TextObject>(&obj)) txt_before = *t;
    }
    assert(txt_before);

    const auto screen_b = selection_ui::screen_bounds(live_cam, txt_before->bounds(), live_edit.canvas_rect(1440, 900));
    const int top_edge_x = screen_b.left + (screen_b.right - screen_b.left) / 4;
    const int top_edge_y = screen_b.top;
    assert(live_edit.selection_handle_at(top_edge_x, top_edge_y) == 10);
    assert(live_edit.canvas_cursor_at(top_edge_x, top_edge_y) == 32646); // IDC_SIZEALL

    live_edit.handle_mouse_down(top_edge_x, top_edge_y);
    assert(live_edit.is_inline_editing());
    live_edit.handle_mouse_move(top_edge_x + 25, top_edge_y + 15);
    live_edit.handle_mouse_up(top_edge_x + 25, top_edge_y + 15);
    assert(live_edit.is_inline_editing());

    auto snap_after_move = live_edit.workflow().snapshot_for_canvas();
    std::optional<pikdraw::document::TextObject> txt_moved;
    for (const auto& [id, obj] : snap_after_move->objects().all()) {
        (void)id;
        if (const auto* t = std::get_if<pikdraw::document::TextObject>(&obj)) txt_moved = *t;
    }
    assert(txt_moved);
    assert(txt_moved->position.x != txt_before->position.x || txt_moved->position.y != txt_before->position.y);

    assert(live_edit.handle_inline_text_character('!'));
    assert(live_edit.inline_text_buffer().ends_with("!"));

    assert(live_edit.handle_inline_text_key('Z', true, false)); // undo '!'
    assert(live_edit.handle_inline_text_key('Z', true, false)); // undo move
    auto snap_undone = live_edit.workflow().snapshot_for_canvas();
    std::optional<pikdraw::document::TextObject> txt_undone;
    for (const auto& [id, obj] : snap_undone->objects().all()) {
        (void)id;
        if (const auto* t = std::get_if<pikdraw::document::TextObject>(&obj)) txt_undone = *t;
    }
    assert(txt_undone);
    assert(std::abs(txt_undone->position.x - txt_before->position.x) < 1e-3);
    assert(std::abs(txt_undone->position.y - txt_before->position.y) < 1e-3);

    // Paragraph frame width edge handle scaling
    NativeWindow para_win(1440, 900);
    para_win.set_active_tool(4);
    auto& p_cam = para_win.root().renderer().camera();
    const auto para_canvas = para_win.canvas_rect(1440, 900);
    p_cam.set_viewport_size(para_canvas.right - para_canvas.left, para_canvas.bottom - para_canvas.top);
    const auto p_canvas_point = [&](pikdraw::geometry::Point2D world) {
        const auto screen = p_cam.world_to_screen(world);
        return std::pair<int, int>{72 + static_cast<int>(screen.x), 122 + static_cast<int>(screen.y)};
    };
    const auto [p_start_x, p_start_y] = p_canvas_point({30, 80});
    const auto [p_end_x, p_end_y] = p_canvas_point({90, 50});
    para_win.handle_mouse_down(p_start_x, p_start_y);
    para_win.handle_mouse_move(p_end_x, p_end_y);
    para_win.handle_mouse_up(p_end_x, p_end_y);
    for (char c : std::string("Paragraph frame width adjustment"))
        para_win.handle_inline_text_character(static_cast<unsigned char>(c));

    auto p_snap = para_win.workflow().snapshot_for_canvas();
    std::optional<pikdraw::document::TextObject> p_before;
    for (const auto& [id, obj] : p_snap->objects().all()) {
        (void)id;
        if (const auto* t = std::get_if<pikdraw::document::TextObject>(&obj)) p_before = *t;
    }
    assert(p_before && p_before->frame_width_mm > 0);
    const double initial_frame_w = p_before->frame_width_mm;
    const double initial_font_pt = p_before->font.size_pt;

    const auto p_screen = selection_ui::screen_bounds(p_cam, p_before->bounds(), para_win.canvas_rect(1440, 900));
    const auto h3 = selection_ui::handles(p_screen)[3];
    assert(para_win.selection_handle_at(h3.x, h3.y) == 3);
    para_win.handle_mouse_down(h3.x, h3.y);
    para_win.handle_mouse_move(h3.x + 35, h3.y);
    para_win.handle_mouse_up(h3.x + 35, h3.y);
    assert(para_win.is_inline_editing());

    auto p_snap_after = para_win.workflow().snapshot_for_canvas();
    std::optional<pikdraw::document::TextObject> p_after;
    for (const auto& [id, obj] : p_snap_after->objects().all()) {
        (void)id;
        if (const auto* t = std::get_if<pikdraw::document::TextObject>(&obj)) p_after = *t;
    }
    assert(p_after);
    assert(p_after->frame_width_mm > initial_frame_w);
    assert(std::abs(p_after->font.size_pt - initial_font_pt) < 1e-4);

    // Mouse click on empty canvas finishes text editing and returns to Select Tool
    assert(para_win.is_inline_editing());
    para_win.handle_mouse_down(500, 500);
    assert(!para_win.is_inline_editing());
    assert(para_win.active_tool() == 0);

    // Double-click on existing TextObject activates inline text editing without manual tool selection
    const auto [text_click_x, text_click_y] = p_canvas_point({35, 75});
    assert(para_win.active_tool() == 0);
    para_win.handle_double_click(text_click_x, text_click_y);
    assert(para_win.is_inline_editing());
    assert(para_win.active_tool() == 4);
    para_win.commit_inline_text_editing();

    // ----------------------------------------------------
    // Item 6: Native Font Picker Overlay Verification
    // ----------------------------------------------------
    {
        pikdraw::desktop::NativeFontPicker picker;
        assert(!picker.is_open());
        picker.open(100, 100, 1440, 900, "Arial", "PikDraw Tiếng Việt Ánh Sáng");
        assert(picker.is_open());
        assert(picker.baseline_font() == "Arial");
        assert(picker.bounds().left == 100);
        assert(picker.bounds().top == 104);
        assert(picker.filtered_count() > 0);

        // Filter tests
        picker.set_active_filter(pikdraw::desktop::FontCategoryFilter::SansSerif);
        assert(picker.active_filter() == pikdraw::desktop::FontCategoryFilter::SansSerif);
        picker.set_active_filter(pikdraw::desktop::FontCategoryFilter::All);
        assert(picker.active_filter() == pikdraw::desktop::FontCategoryFilter::All);

        // Search tests
        const size_t total_count = picker.filtered_count();
        picker.set_search_query("aria");
        assert(picker.search_query() == "aria");
        assert(picker.filtered_count() <= total_count);
        picker.set_search_query("");
        assert(picker.filtered_count() == total_count);

        // Key navigation
        if (picker.filtered_count() > 1) {
            picker.handle_key_down(VK_DOWN, false, false);
            assert(picker.selected_index() >= 0);
        }

        // Callbacks test
        std::string previewed;
        std::string selected;
        bool cancelled = false;
        picker.set_on_preview([&](const std::string& f) { previewed = f; });
        picker.set_on_select([&](const std::string& f) { selected = f; });
        picker.set_on_cancel([&]() { cancelled = true; });

        picker.select_index(0);
        assert(!previewed.empty());

        picker.handle_key_down(VK_RETURN, false, false);
        assert(!selected.empty());
        assert(!picker.is_open());

        // Reopen & cancel via ESC
        picker.open(200, 200, 1440, 900, "Segoe UI");
        assert(picker.is_open());
        picker.handle_key_down(VK_ESCAPE, false, false);
        assert(!picker.is_open());
        assert(cancelled);

        // Reopen & click outside to close
        picker.open(300, 300, 1440, 900, "Segoe UI");
        assert(picker.is_open());
        bool handled_outside = picker.handle_mouse_down(10, 10);
        assert(handled_outside);
        assert(!picker.is_open());

        // NativeWindow font picker integration
        NativeWindow picker_win(1440, 900);
        picker_win.set_active_tool(4);
        assert(!picker_win.is_font_picker_open());
        picker_win.open_font_picker(52, 96);
        assert(picker_win.is_font_picker_open());
        picker_win.close_font_picker();
        assert(!picker_win.is_font_picker_open());

        // Font picker during inline editing
        auto& p_sel = picker_win.root().vector_controller().selection();
        const auto p_text_id = picker_win.workflow().add_vietnamese_text(
            40.0, 50.0, "Font Picker Test", "Be Vietnam Pro", 24.0, "#000000"
        );
        p_sel.select(p_text_id);
        picker_win.open_font_picker(52, 96);
        assert(picker_win.is_font_picker_open());
        // Click outside closes and cancels picker
        picker_win.handle_mouse_down(10, 10);
        assert(!picker_win.is_font_picker_open());
    }

    // ----------------------------------------------------
    // Item 7: Contextual Text Property Bar Controls
    // ----------------------------------------------------
    {
        NativeWindow pbar_win(1440, 900);
        pbar_win.set_active_tool(4);
        auto& pb_sel = pbar_win.root().vector_controller().selection();
        const auto txt_id = pbar_win.workflow().add_vietnamese_text(
            50.0, 50.0, "Dong 1\nDong 2", "Arial", 18.0, "#000000"
        );
        pb_sel.select(txt_id);

        // Strikethrough toggle via click at 710
        pbar_win.handle_mouse_down(710, 80);
        assert(pbar_win.status_message().find("Gạch ngang: Bật") != std::string::npos ||
               pbar_win.status_message().find("Strikethrough: ON") != std::string::npos);

        // Bullets toggle via click at 745
        pbar_win.handle_mouse_down(745, 80);
        assert(pbar_win.status_message().find("Bullets") != std::string::npos);

        // Numbering toggle via click at 780
        pbar_win.handle_mouse_down(780, 80);
        assert(pbar_win.status_message().find("Numbering") != std::string::npos);

        // Non-printing characters toggle via click at 815
        pbar_win.handle_mouse_down(815, 80);
        assert(pbar_win.status_message().find("Non-Printing") != std::string::npos);

        // Artistic / Paragraph toggle via click at 845
        pbar_win.handle_mouse_down(845, 80);
        assert(pbar_win.status_message().find("Khung") != std::string::npos ||
               pbar_win.status_message().find("Paragraph") != std::string::npos);
        assert(pbar_win.workflow().undo());
    }

    // ----------------------------------------------------
    // Item 8: Text Menu Actions & Typography Docker
    // ----------------------------------------------------
    {
        NativeWindow menu_win(1440, 900);
        auto& mwf = menu_win.workflow();
        auto& msel = menu_win.root().vector_controller().selection();
        const auto txt = mwf.add_vietnamese_text(
            20.0, 20.0, "Menu Typography Test", "Segoe UI", 16.0, "#000000"
        );
        msel.select(txt);

        // Alignment Center via Text Menu
        menu_win.execute_menu_action(static_cast<int>(MenuActionId::TextAlignCenter));
        assert(menu_win.status_message().find("Căn giữa") != std::string::npos ||
               menu_win.status_message().find("Center") != std::string::npos);

        // Underline and Strikethrough via Text Menu
        menu_win.execute_menu_action(static_cast<int>(MenuActionId::TextUnderline));
        assert(menu_win.status_message().find("Gạch chân: Bật") != std::string::npos ||
               menu_win.status_message().find("Underline: ON") != std::string::npos);

        menu_win.execute_menu_action(static_cast<int>(MenuActionId::TextStrikethrough));
        assert(menu_win.status_message().find("Gạch ngang: Bật") != std::string::npos ||
               menu_win.status_message().find("Strikethrough: ON") != std::string::npos);

        // Non-printing characters toggle via Text Menu
        menu_win.execute_menu_action(static_cast<int>(MenuActionId::TextNonPrinting));
        assert(menu_win.status_message().find("Non-Printing") != std::string::npos ||
               menu_win.status_message().find("Hiện ký tự ẩn") != std::string::npos);

        // Text Docker (Inspector & Properties panel) via Text Menu
        menu_win.execute_menu_action(static_cast<int>(MenuActionId::TextDocker));
        assert(menu_win.status_message().find("thuộc tính") != std::string::npos ||
               menu_win.status_message().find("Properties") != std::string::npos);
    }

    // ----------------------------------------------------
    // Item 9: Case conversion, Justify, Strikethrough & Non-destructive property mutation
    // ----------------------------------------------------
    {
        using namespace pikdraw::typography;
        // 1. Unicode Vietnamese case conversion tests
        const std::string vi_lower = "cộng hòa xã hội chủ nghĩa việt nam";
        const std::string vi_upper = convert_case_utf8(vi_lower, ChangeCaseMode::Uppercase);
        assert(vi_upper == "CỘNG HÒA XÃ HỘI CHỦ NGHĨA VIỆT NAM");
        assert(convert_case_utf8(vi_upper, ChangeCaseMode::Lowercase) == vi_lower);

        const std::string vi_title = convert_case_utf8(vi_lower, ChangeCaseMode::TitleCase);
        assert(vi_title == "Cộng Hòa Xã Hội Chủ Nghĩa Việt Nam");

        const std::string vi_sent = convert_case_utf8(vi_lower, ChangeCaseMode::SentenceCase);
        assert(vi_sent.starts_with("Cộng"));

        const std::string vi_toggle = convert_case_utf8("Cộng Hòa", ChangeCaseMode::ToggleCase);
        assert(vi_toggle == "cỘNG hÒA");
        const std::string vi_nfd = std::string("Tie") + "\xCC\x82\xCC\x81" + "ng";
        const std::string vi_nfd_upper = std::string("TIE") + "\xCC\x82\xCC\x81" + "NG";
        assert(convert_case_utf8(vi_nfd, ChangeCaseMode::Uppercase) == vi_nfd_upper);
        assert(convert_case_utf8("hello, world", ChangeCaseMode::TitleCase) == "Hello, World");
        assert(convert_case_utf8("đường phố", ChangeCaseMode::TitleCase) == "Đường Phố");
        assert(convert_case_utf8("đường", ChangeCaseMode::SentenceCase) == "Đường");
        assert(convert_case_utf8("Đường", ChangeCaseMode::ToggleCase) == "đƯỜNG");
        assert(convert_case_utf8("đây là câu. đây là câu!", ChangeCaseMode::SentenceCase) ==
               "Đây là câu. Đây là câu!");

        // 2. Shell Text Object Non-Destructive Mutations & Sync
        NativeWindow text_win(1440, 900);
        auto& twf = text_win.workflow();
        auto& tsel = text_win.root().vector_controller().selection();

        const auto tid = twf.add_vietnamese_text(
            30.0, 40.0, "cộng hòa xã hội chủ nghĩa", "Arial", 20.0, "#000000"
        );
        assert(tid != pikdraw::document::kInvalidObjectId);
        tsel.select(tid);

        // Turn bold and underline ON
        text_win.toggle_font_bold();
        text_win.toggle_font_underline();

        // Check current text object properties
        auto snap = text_win.root().document().current_snapshot();
        auto txt_obj = std::get<pikdraw::document::TextObject>(*snap->objects().get(tid));
        assert(txt_obj.font.weight >= 700);
        assert(txt_obj.underline == true);
        assert(txt_obj.strikethrough == false);
        assert(txt_obj.alignment == TextAlignment::Left);
        assert(std::abs(txt_obj.font.size_pt - 20.0) < 1e-6);

        // Now adjust font size - bold and underline MUST remain unchanged (no clobbering!)
        text_win.adjust_font_size(6.0);
        snap = text_win.root().document().current_snapshot();
        txt_obj = std::get<pikdraw::document::TextObject>(*snap->objects().get(tid));
        assert(std::abs(txt_obj.font.size_pt - 26.0) < 1e-6);
        assert(txt_obj.font.weight >= 700);
        assert(txt_obj.underline == true);

        // Toggle strikethrough - font size and weight must remain unchanged
        text_win.toggle_font_strikethrough();
        snap = text_win.root().document().current_snapshot();
        txt_obj = std::get<pikdraw::document::TextObject>(*snap->objects().get(tid));
        assert(txt_obj.strikethrough == true);
        assert(std::abs(txt_obj.font.size_pt - 26.0) < 1e-6);
        assert(txt_obj.font.weight >= 700);

        pikdraw::vector::TextPropertiesDelta tracking_delta;
        tracking_delta.letter_spacing_mm = 0.375;
        assert(twf.update_selected_text_properties(tracking_delta));
        snap = text_win.root().document().current_snapshot();
        txt_obj = std::get<pikdraw::document::TextObject>(*snap->objects().get(tid));
        assert(txt_obj.letter_spacing_mm == 0.375);
        assert(twf.undo());

        // Justify Alignment (Ctrl+J / Menu / Action)
        text_win.set_font_alignment(TextAlignment::Justify);
        snap = text_win.root().document().current_snapshot();
        txt_obj = std::get<pikdraw::document::TextObject>(*snap->objects().get(tid));
        assert(txt_obj.alignment == TextAlignment::Justify);

        // Case Conversion on Selected Object
        text_win.apply_change_case(ChangeCaseMode::Uppercase);
        snap = text_win.root().document().current_snapshot();
        txt_obj = std::get<pikdraw::document::TextObject>(*snap->objects().get(tid));
        assert(txt_obj.text == "CỘNG HÒA XÃ HỘI CHỦ NGHĨA");

        // Cycle Change Case (Shift+F3 behavior)
        text_win.cycle_change_case();
        snap = text_win.root().document().current_snapshot();
        txt_obj = std::get<pikdraw::document::TextObject>(*snap->objects().get(tid));
        assert(txt_obj.text == "cộng hòa xã hội chủ nghĩa");

        // Mixed-size selection applies Bold to every text while preserving each object's other values.
        NativeWindow multi_text_win(1440, 900);
        auto& multi_workflow = multi_text_win.workflow();
        auto& multi_selection = multi_text_win.root().vector_controller().selection();
        const auto headline_id = multi_workflow.add_vietnamese_text(
            25.0, 30.0, "Headline", "Arial", 48.0, "#000000");
        const auto body_id = multi_workflow.add_vietnamese_text(
            25.0, 90.0, "Body copy", "Arial", 12.0, "#000000");
        multi_selection.select(headline_id);
        multi_text_win.set_font_alignment(TextAlignment::Center);
        multi_text_win.toggle_font_underline();
        multi_selection.select(body_id, pikdraw::vector::SelectionMode::Add);
        multi_text_win.toggle_font_bold();
        const auto multi_snapshot = multi_text_win.root().document().current_snapshot();
        const auto headline = std::get<pikdraw::document::TextObject>(*multi_snapshot->objects().get(headline_id));
        const auto body = std::get<pikdraw::document::TextObject>(*multi_snapshot->objects().get(body_id));
        assert(headline.font.weight >= 700 && body.font.weight >= 700);
        assert(headline.font.size_pt == 48.0 && body.font.size_pt == 12.0);
        assert(headline.alignment == TextAlignment::Center && body.alignment == TextAlignment::Left);
        assert(headline.underline && !body.underline);

        text_win.cycle_change_case();
        snap = text_win.root().document().current_snapshot();
        txt_obj = std::get<pikdraw::document::TextObject>(*snap->objects().get(tid));
        assert(txt_obj.text == "Cộng Hòa Xã Hội Chủ Nghĩa");

        // Verify Undo restores previous state correctly
        assert(twf.undo());
        snap = text_win.root().document().current_snapshot();
        txt_obj = std::get<pikdraw::document::TextObject>(*snap->objects().get(tid));
        assert(txt_obj.text == "cộng hòa xã hội chủ nghĩa");

        // Item 10: Linked Frames, Combine/Split Text, and Text on Path
        {
            // 10.1: Linked Text Frames (Threading)
            const auto f1_id = twf.add_vietnamese_text(
                10.0, 10.0,
                "Dòng 1: Tiêu đề thiết kế hồ sơ năng lực.\nDòng 2: Nội dung đoạn văn bản hồ sơ.\nDòng 3: Bảng thông số kỹ thuật chi tiết.\nDòng 4: Kết luận và địa chỉ công ty.",
                "Arial", 12.0, "#000000");
            const auto f2_id = twf.add_vietnamese_text(100.0, 10.0, "", "Arial", 12.0, "#000000");

            auto& sel = text_win.root().vector_controller().selection();
            sel.clear();
            sel.select(f1_id);

            pikdraw::vector::TextPropertiesDelta f1_delta;
            f1_delta.frame_width_mm = 80.0;
            f1_delta.frame_height_mm = 12.0; // Restrict height to cause overflow
            assert(twf.update_selected_text_properties(f1_delta));

            sel.select(f2_id, pikdraw::vector::SelectionMode::Add);
            text_win.link_selected_text_frames();
            auto l_snap = text_win.root().document().current_snapshot();
            auto l_f1 = std::get<pikdraw::document::TextObject>(*l_snap->objects().get(f1_id));
            auto l_f2 = std::get<pikdraw::document::TextObject>(*l_snap->objects().get(f2_id));

            assert(l_f1.next_frame_id == f2_id);
            assert(l_f2.prev_frame_id == f1_id);
            assert(l_f1.is_linked());
            assert(l_f2.is_linked());

            // Test Unlink
            sel.clear();
            sel.select(f1_id);
            text_win.unlink_selected_text_frames();
            l_snap = text_win.root().document().current_snapshot();
            l_f1 = std::get<pikdraw::document::TextObject>(*l_snap->objects().get(f1_id));
            l_f2 = std::get<pikdraw::document::TextObject>(*l_snap->objects().get(f2_id));
            assert(!l_f1.is_linked());
            assert(!l_f2.is_linked());

            // 10.2: Combine Text Objects (Ctrl+Shift+C)
            const auto t_a = twf.add_vietnamese_text(10.0, 50.0, "Họ và tên: Nguyễn Văn A", "Arial", 14.0, "#000000");
            const auto t_b = twf.add_vietnamese_text(10.0, 60.0, "Chức vụ: Kỹ sư trưởng", "Arial", 14.0, "#000000");
            const auto t_c = twf.add_vietnamese_text(10.0, 70.0, "Công ty: PikDraw Vietnam", "Arial", 14.0, "#000000");

            sel.clear();
            sel.select(t_a);
            sel.select(t_b, pikdraw::vector::SelectionMode::Add);
            sel.select(t_c, pikdraw::vector::SelectionMode::Add);

            text_win.combine_selected_text_objects();
            auto c_snap = text_win.root().document().current_snapshot();
            assert(!c_snap->objects().contains(t_a));
            assert(!c_snap->objects().contains(t_b));
            assert(!c_snap->objects().contains(t_c));
            assert(sel.count() == 1);
            auto combined_id = sel.selected_ids().front();
            auto combined_obj = std::get<pikdraw::document::TextObject>(*c_snap->objects().get(combined_id));
            assert(combined_obj.text.find("Họ và tên") != std::string::npos);
            assert(combined_obj.text.find("Chức vụ") != std::string::npos);
            assert(combined_obj.text.find("PikDraw Vietnam") != std::string::npos);

            // Undo Combine
            assert(twf.undo());
            c_snap = text_win.root().document().current_snapshot();
            assert(c_snap->objects().contains(t_a));
            assert(c_snap->objects().contains(t_b));
            assert(c_snap->objects().contains(t_c));

            // 10.3: Split Text Lines (Ctrl+Shift+S)
            const auto multiline_id = twf.add_vietnamese_text(
                10.0, 100.0,
                "Tem mã hàng: PK-901\nSố lượng: 500 cái\nXuất xứ: Việt Nam",
                "Arial", 12.0, "#000000");
            sel.clear();
            sel.select(multiline_id);

            text_win.split_selected_text_lines();
            auto s_snap = text_win.root().document().current_snapshot();
            assert(!s_snap->objects().contains(multiline_id));
            assert(sel.count() == 3);
            auto s1 = std::get<pikdraw::document::TextObject>(*s_snap->objects().get(sel.selected_ids()[0]));
            auto s2 = std::get<pikdraw::document::TextObject>(*s_snap->objects().get(sel.selected_ids()[1]));
            auto s3 = std::get<pikdraw::document::TextObject>(*s_snap->objects().get(sel.selected_ids()[2]));
            assert(s1.text == "Tem mã hàng: PK-901");
            assert(s2.text == "Số lượng: 500 cái");
            assert(s3.text == "Xuất xứ: Việt Nam");

            // Undo Split
            assert(twf.undo());
            s_snap = text_win.root().document().current_snapshot();
            assert(s_snap->objects().contains(multiline_id));

            // 10.4: Text on Path (Fit Text to Path & Detach)
            const auto curve_id = twf.add_ellipse(50.0, 150.0, 100.0, 100.0, "", "#0ea5e9", 1.0);
            const auto path_text_id = twf.add_vietnamese_text(
                0.0, 0.0, "CÔNG TY CỔ PHẦN CÔNG NGHỆ PIKDRAW VIỆT NAM", "Arial", 14.0, "#000000");

            sel.clear();
            sel.select(curve_id);
            sel.select(path_text_id, pikdraw::vector::SelectionMode::Add);

            text_win.fit_text_to_path();
            auto p_snap = text_win.root().document().current_snapshot();
            auto top_obj = std::get<pikdraw::document::TextObject>(*p_snap->objects().get(path_text_id));
            assert(top_obj.is_text_on_path());
            assert(top_obj.path_binding_id == curve_id);

            // Reverse path
            sel.clear();
            sel.select(path_text_id);
            text_win.reverse_text_on_path();
            p_snap = text_win.root().document().current_snapshot();
            top_obj = std::get<pikdraw::document::TextObject>(*p_snap->objects().get(path_text_id));
            assert(top_obj.path_reversed == true);

            // Convert Text on Path to Curves (Ctrl+Q)
            assert(twf.convert_text_to_curves(path_text_id));
            p_snap = text_win.root().document().current_snapshot();
            assert(!p_snap->objects().contains(path_text_id));
            assert(sel.count() == 1);
            auto curves_id = sel.selected_ids().front();
            assert(std::holds_alternative<pikdraw::document::PathObject>(*p_snap->objects().get(curves_id)));

            // Undo Convert to Curves
            assert(twf.undo());
            p_snap = text_win.root().document().current_snapshot();
            assert(p_snap->objects().contains(path_text_id));
            top_obj = std::get<pikdraw::document::TextObject>(*p_snap->objects().get(path_text_id));
            assert(top_obj.is_text_on_path());

            // Straighten text
            sel.clear();
            sel.select(path_text_id);
            text_win.straighten_text_from_path();
            p_snap = text_win.root().document().current_snapshot();
            top_obj = std::get<pikdraw::document::TextObject>(*p_snap->objects().get(path_text_id));
            assert(!top_obj.is_text_on_path());
            assert(top_obj.path_binding_id == pikdraw::document::kInvalidObjectId);
        }
    }

    std::cout << "[PASS] test_typography_inline_editing_ribbon_and_table\n";
}

