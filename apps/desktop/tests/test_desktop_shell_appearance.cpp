#include "desktop_shell_test_helpers.hpp"
#include "pikdraw/document/commands/contour_commands.hpp"
#include "../src/tools/contour_tool_controller.hpp"
#include "../src/shell/native_window_text_properties.hpp"
void test_object_opacity_property_bar_and_appearance_panel() {
    NativeWindow win(1440, 900);
    const auto path_id = win.workflow().add_ellipse(20.0, 30.0, 80.0, 55.0,
                                                    "#80c0e0", "#204060", 1.0);
    const auto text_id = win.workflow().add_vietnamese_text(120.0, 40.0, "PikDraw opacity");
    auto& selection = win.root().vector_controller().selection();

    const auto object_opacity = [&](pikdraw::document::ObjectId id) {
        const auto object = win.root().document().current_snapshot()->objects().get(id);
        assert(object.has_value());
        if (const auto* path = std::get_if<pikdraw::document::PathObject>(&*object))
            return path->appearance.object_opacity;
        assert(std::holds_alternative<pikdraw::document::TextObject>(*object));
        return std::get<pikdraw::document::TextObject>(*object).appearance.object_opacity;
    };
    const auto type = [&](std::wstring_view value) {
        for (const wchar_t ch : value)
            win.handle_numeric_char_input(ch);
        win.handle_numeric_char_input(13);
    };

    // AC-UI-OP-01 — the quick property-bar field edits Path and Text without changing source color alpha.
    selection.select(path_id);
    win.handle_mouse_down(830, 82);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::ObjectOpacity);
    type(L"35.5");
    assert(std::abs(object_opacity(path_id) - 0.355) < 1e-9);
    auto path_obj = std::get<pikdraw::document::PathObject>(
        *win.root().document().current_snapshot()->objects().get(path_id));
    const double fill_a = std::visit([](const auto& c) -> double {
        if constexpr (std::is_same_v<std::decay_t<decltype(c)>, pikdraw::color::SpotColor>) return c.alternate_rgb.a;
        else return c.a;
    }, path_obj.fill.payload());
    assert(std::abs(fill_a - 1.0) < 1e-9);

    selection.select(text_id);
    win.handle_mouse_down(930, 82);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::ObjectOpacity);
    type(L"88,8");
    assert(std::abs(object_opacity(text_id) - 0.888) < 1e-9);
    const auto text_layout = [&](pikdraw::document::ObjectId id) { return std::get<pikdraw::document::TextObject>(
        *win.root().document().current_snapshot()->objects().get(id)); };
    const auto layout_revision = win.root().document().current_snapshot()->revision();
    win.handle_mouse_down(1350, 138 + TextPropertiesLayout::frame_field_top + 14);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::TextFrameWidth);
    type(L"52,5");
    assert(text_layout(text_id).frame_width_mm == 52.5);
    assert(win.root().document().current_snapshot()->revision() == layout_revision + 1);
    assert(win.workflow().undo());
    assert(text_layout(text_id).frame_width_mm == 0.0);
    win.handle_mouse_down(1350, 138 + TextPropertiesLayout::spacing_field_top + 14);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::TextLineSpacing && win.numeric_input_buffer().empty());
    type(L"145");
    assert(std::abs(text_layout(text_id).line_spacing_ratio - 1.45) < 1e-9);
    assert(win.workflow().undo());
    const auto draft_revision = win.root().document().current_snapshot()->revision();
    win.start_inline_text_editing({120.0, 40.0}, text_id);
    win.handle_mouse_down(1350, 138 + TextPropertiesLayout::frame_field_top + 14);
    type(L"63");
    assert(win.root().document().current_snapshot()->revision() == draft_revision);
    assert(std::get<pikdraw::document::TextObject>(*win.workflow().snapshot_for_canvas()->objects().get(text_id)).frame_width_mm == 63.0);
    win.cancel_inline_text_editing();
    assert(text_layout(text_id).frame_width_mm == 0.0);
    pikdraw::document::TextObject second_text;
    second_text.id = pikdraw::document::generate_object_id(); second_text.name = "Second paragraph";
    second_text.text = "Second";
    second_text.frame_width_mm = 44.0;
    second_text.line_spacing_ratio = 1.8;
    const auto second_text_id = second_text.id;
    assert(win.root().document().execute_command(std::make_unique<pikdraw::document::AddObjectCommand>(second_text)).success);
    selection.clear(); selection.select(text_id);
    selection.select(second_text_id, pikdraw::vector::SelectionMode::Add);
    win.handle_mouse_down(1350, 138 + TextPropertiesLayout::frame_field_top + 14);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::TextFrameWidth);
    assert(win.numeric_input_buffer().empty());
    type(L"72.5");
    assert(text_layout(text_id).frame_width_mm == 72.5);
    assert(text_layout(second_text_id).frame_width_mm == 72.5);
    assert(win.workflow().undo());
    assert(text_layout(text_id).frame_width_mm == 0.0);
    assert(text_layout(second_text_id).frame_width_mm == 44.0);
    // AC-UI-OP-02 — Object > Properties focuses Appearance without a document/history write.
    const auto before_open = win.root().document().current_snapshot()->revision();
    const bool had_undo = win.root().document().can_undo();
    win.open_object_properties_panel();
    assert(win.active_dock_tab() == 0);
    assert(win.style_inspector_visible());
    assert(win.root().document().current_snapshot()->revision() == before_open);
    assert(win.root().document().can_undo() == had_undo);
    assert(pikdraw::desktop::actions::ActionId::is_valid_action_id(
        pikdraw::desktop::actions::ActionId::kObjectProperties));
    assert(std::any_of(kObjectMenuItems.begin(), kObjectMenuItems.end(), [](const auto& item) {
        return item.action == MenuActionId::ObjectProperties &&
               std::wstring(item.label_en) == L"Properties…";
    }));

    // Create differing values, then edit a mixed Path/Text selection through Appearance.
    selection.select(path_id);
    assert(win.workflow().set_selected_object_opacity(0.2));
    selection.select(text_id);
    assert(win.workflow().set_selected_object_opacity(0.8));
    selection.clear();
    selection.select(path_id);
    selection.select(text_id, pikdraw::vector::SelectionMode::Add);
    const auto appearance = pikdraw::desktop::selection_appearance(
        *win.root().document().current_snapshot(), selection.selected_ids());
    assert(appearance.can_edit_object_opacity());
    assert(appearance.mixed_object_opacity);

    win.handle_mouse_down(1440 - 280 + 20, 162 + 310);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::ObjectOpacity);
    assert(win.numeric_input_buffer().empty()); // Mixed does not silently select the first object's value.
    type(L"55,5");
    assert(std::abs(object_opacity(path_id) - 0.555) < 1e-9);
    assert(std::abs(object_opacity(text_id) - 0.555) < 1e-9);
    assert(win.workflow().undo());
    assert(std::abs(object_opacity(path_id) - 0.2) < 1e-9);
    assert(std::abs(object_opacity(text_id) - 0.8) < 1e-9);

    // AC-UI-OP-03 — invalid input, unsupported targets and locked targets leave all values untouched.
    const auto before_invalid = win.root().document().current_snapshot()->revision();
    win.start_numeric_input(NativeWindow::NumericFieldId::ObjectOpacity);
    type(L"101");
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::ObjectOpacity);
    assert(win.root().document().current_snapshot()->revision() == before_invalid);
    win.cancel_numeric_field();

    const auto image_id = win.workflow().add_bitmap_image(240.0, 40.0, 50.0, 35.0);
    selection.clear();
    selection.select(path_id);
    selection.select(image_id, pikdraw::vector::SelectionMode::Add);
    const auto before_unsupported = win.root().document().current_snapshot()->revision();
    assert(!win.workflow().can_set_selected_object_opacity());
    assert(!win.workflow().set_selected_object_opacity(0.4));
    assert(win.root().document().current_snapshot()->revision() == before_unsupported);
    assert(std::abs(object_opacity(path_id) - 0.2) < 1e-9);

    pikdraw::document::TextObject locked_text;
    locked_text.id = pikdraw::document::generate_object_id();
    locked_text.name = "Locked opacity target";
    locked_text.text = "locked";
    locked_text.locked = true;
    const auto locked_text_id = locked_text.id;
    const auto add_locked = win.root().document().execute_command(
        std::make_unique<pikdraw::document::AddObjectCommand>(locked_text));
    assert(add_locked.success);
    selection.clear();
    selection.select(path_id);
    selection.select(locked_text_id, pikdraw::vector::SelectionMode::Add);
    const auto before_locked = win.root().document().current_snapshot()->revision();
    const auto locked_appearance = pikdraw::desktop::selection_appearance(
        *win.root().document().current_snapshot(), selection.selected_ids());
    assert(locked_appearance.contains_locked);
    assert(!locked_appearance.can_edit_object_opacity());
    assert(!win.workflow().can_set_selected_object_opacity());
    assert(!win.workflow().set_selected_object_opacity(0.4));
    assert(win.root().document().current_snapshot()->revision() == before_locked);
    assert(std::abs(object_opacity(path_id) - 0.2) < 1e-9);
    assert(std::abs(object_opacity(locked_text_id) - 1.0) < 1e-9);

    std::cout << "[PASS] test_object_opacity_property_bar_and_appearance_panel\n";
}

void test_fill_and_stroke_opacity_properties() {
    NativeWindow win(1440, 900);
    using namespace pikdraw;

    auto spot = color::ColorValue::make_spot(
        "PikDraw Fill Opacity Spot", 0.42, color::RGBColor{0.3, 0.2, 0.8, 0.37});
    spot.set_overprint({true, false});
    auto path_one = vector::ShapeFactory::create_rectangle(
        geometry::Rect2D{20.0, 24.0, 48.0, 36.0}, spot,
        color::ColorValue::make_rgb(0.1, 0.2, 0.3, 0.61), 0.8);
    path_one.appearance.fill_opacity = 0.2;
    path_one.appearance.stroke_opacity = 0.15;
    const auto path_one_id = path_one.id;
    assert(win.root().document().execute_command(
        std::make_unique<document::AddObjectCommand>(document::ObjectVariant{path_one})).success);

    auto path_two = vector::ShapeFactory::create_rectangle(
        geometry::Rect2D{84.0, 24.0, 48.0, 36.0},
        color::ColorValue::make_rgb(0.4, 0.5, 0.6, 0.63),
        color::ColorValue::make_rgb(0.6, 0.4, 0.2, 0.82), 1.2);
    path_two.appearance.fill_opacity = 0.4;
    path_two.appearance.stroke_opacity = 0.85;
    const auto path_two_id = path_two.id;
    assert(win.root().document().execute_command(
        std::make_unique<document::AddObjectCommand>(document::ObjectVariant{path_two})).success);

    document::TextObject text;
    text.id = document::generate_object_id();
    text.name = "Fill opacity text";
    text.text = "Opacity";
    text.position = {144.0, 30.0};
    text.fill = color::ColorValue::make_rgb(0.7, 0.2, 0.4, 0.52);
    text.appearance.fill_opacity = 0.8;
    text.appearance.stroke_opacity = 0.9;
    const auto text_id = text.id;
    assert(win.root().document().execute_command(
        std::make_unique<document::AddObjectCommand>(document::ObjectVariant{text})).success);

    auto locked_path = vector::ShapeFactory::create_rectangle(
        geometry::Rect2D{204.0, 24.0, 48.0, 36.0});
    locked_path.locked = true;
    locked_path.appearance.fill_opacity = 0.7;
    locked_path.appearance.stroke_opacity = 0.6;
    const auto locked_path_id = locked_path.id;
    assert(win.root().document().execute_command(
        std::make_unique<document::AddObjectCommand>(document::ObjectVariant{locked_path})).success);

    const auto image_id = win.workflow().add_bitmap_image(264.0, 24.0, 40.0, 30.0);
    const auto appearance_of = [&](document::ObjectId id) {
        const auto object = win.root().document().current_snapshot()->objects().get(id);
        assert(object.has_value());
        if (const auto *path = std::get_if<document::PathObject>(&*object))
            return path->appearance;
        assert(std::holds_alternative<document::TextObject>(*object));
        return std::get<document::TextObject>(*object).appearance;
    };
    const auto type_and_commit = [&](std::wstring_view value) {
        for (const wchar_t ch : value) win.handle_numeric_char_input(ch);
        win.handle_numeric_char_input(13);
    };
    const int panel_left = 1440 - 280;
    const int fill_opacity_y = 162 + 270 + 12;

    win.open_object_properties_panel();
    auto &selection = win.root().vector_controller().selection();

    // AC-FS-OP-01 — mixed Path/Text fill opacity starts blank and updates in one undoable command.
    selection.select(path_one_id);
    selection.select(text_id, vector::SelectionMode::Add);
    auto summary = selection_appearance(
        *win.root().document().current_snapshot(), selection.selected_ids());
    assert(summary.can_edit_fill_opacity());
    assert(summary.mixed_fill_opacity);
    assert(summary.all_path_text && !summary.all_paths);
    assert(!summary.can_edit_stroke_opacity()); // Text stroke is not rendered by this editor.

    const auto before_fill = win.root().document().current_snapshot()->revision();
    win.handle_mouse_down(panel_left + 72, fill_opacity_y);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::FillOpacity);
    assert(win.numeric_input_buffer().empty()); // Never borrow the first selected object's value.
    type_and_commit(L"55,5");
    assert(win.root().document().current_snapshot()->revision() == before_fill + 1);
    assert(std::abs(appearance_of(path_one_id).fill_opacity - 0.555) < 1e-9);
    assert(std::abs(appearance_of(text_id).fill_opacity - 0.555) < 1e-9);
    const auto path_after_fill = std::get<document::PathObject>(
        *win.root().document().current_snapshot()->objects().get(path_one_id));
    const auto text_after_fill = std::get<document::TextObject>(
        *win.root().document().current_snapshot()->objects().get(text_id));
    const auto &spot_after = std::get<color::SpotColor>(path_after_fill.fill.payload());
    assert(spot_after.spot_name == "PikDraw Fill Opacity Spot");
    assert(std::abs(spot_after.tint - 0.42) < 1e-9);
    assert(std::abs(spot_after.alternate_rgb.a - 0.37) < 1e-9);
    const auto payload_alpha = [](const color::ColorValue& cv) {
        return std::visit([](const auto& c) -> double {
            if constexpr (std::is_same_v<std::decay_t<decltype(c)>, color::SpotColor>) return c.alternate_rgb.a;
            else return c.a;
        }, cv.payload());
    };
    assert(std::abs(payload_alpha(text_after_fill.fill) - 0.52) < 1e-9);
    assert(win.workflow().undo());
    assert(std::abs(appearance_of(path_one_id).fill_opacity - 0.2) < 1e-9);
    assert(std::abs(appearance_of(text_id).fill_opacity - 0.8) < 1e-9);

    // AC-FS-OP-02 — mixed Path stroke opacity applies atomically; source alpha remains independent.
    selection.clear();
    selection.select(path_one_id);
    selection.select(path_two_id, vector::SelectionMode::Add);
    summary = selection_appearance(
        *win.root().document().current_snapshot(), selection.selected_ids());
    assert(summary.can_edit_stroke_opacity());
    assert(summary.mixed_stroke_opacity);
    const auto before_stroke = win.root().document().current_snapshot()->revision();
    win.handle_mouse_down(panel_left + 204, fill_opacity_y);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::StrokeOpacity);
    assert(win.numeric_input_buffer().empty());
    type_and_commit(L"33,3");
    assert(win.root().document().current_snapshot()->revision() == before_stroke + 1);
    assert(std::abs(appearance_of(path_one_id).stroke_opacity - 0.333) < 1e-9);
    assert(std::abs(appearance_of(path_two_id).stroke_opacity - 0.333) < 1e-9);
    auto path_one_after_stroke = std::get<document::PathObject>(
        *win.root().document().current_snapshot()->objects().get(path_one_id));
    auto path_two_after_stroke = std::get<document::PathObject>(
        *win.root().document().current_snapshot()->objects().get(path_two_id));
    assert(std::abs(payload_alpha(path_one_after_stroke.stroke) - 0.61) < 1e-9);
    assert(std::abs(payload_alpha(path_two_after_stroke.stroke) - 0.82) < 1e-9);
    assert(std::abs(appearance_of(path_one_id).fill_opacity - 0.2) < 1e-9);
    assert(std::abs(appearance_of(path_two_id).fill_opacity - 0.4) < 1e-9);
    assert(win.workflow().undo());
    assert(std::abs(appearance_of(path_one_id).stroke_opacity - 0.15) < 1e-9);
    assert(std::abs(appearance_of(path_two_id).stroke_opacity - 0.85) < 1e-9);

    // AC-FS-OP-03 — Text presents Stroke as Path-only and the edit is rejected without a write.
    selection.clear();
    selection.select(text_id);
    const auto before_text_stroke = win.root().document().current_snapshot()->revision();
    assert(!win.workflow().can_set_selected_stroke_opacity());
    win.handle_mouse_down(panel_left + 204, fill_opacity_y);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::None);
    assert(win.root().document().current_snapshot()->revision() == before_text_stroke);

    // AC-FS-OP-04 — locked and unsupported targets reject the whole selection.
    selection.clear();
    selection.select(path_two_id);
    selection.select(locked_path_id, vector::SelectionMode::Add);
    const auto before_locked = win.root().document().current_snapshot()->revision();
    assert(!win.workflow().can_set_selected_fill_opacity());
    assert(!win.workflow().set_selected_fill_opacity(0.25));
    assert(!win.workflow().can_set_selected_stroke_opacity());
    assert(!win.workflow().set_selected_stroke_opacity(0.25));
    assert(win.root().document().current_snapshot()->revision() == before_locked);
    assert(std::abs(appearance_of(path_two_id).fill_opacity - 0.4) < 1e-9);
    assert(std::abs(appearance_of(path_two_id).stroke_opacity - 0.85) < 1e-9);

    selection.clear();
    selection.select(path_two_id);
    selection.select(image_id, vector::SelectionMode::Add);
    const auto before_unsupported = win.root().document().current_snapshot()->revision();
    assert(!win.workflow().can_set_selected_fill_opacity());
    assert(!win.workflow().set_selected_fill_opacity(0.25));
    assert(win.root().document().current_snapshot()->revision() == before_unsupported);
    assert(std::abs(appearance_of(path_two_id).fill_opacity - 0.4) < 1e-9);

    // AC-FS-OP-05 — range errors and a stale target set cannot commit a partial edit.
    selection.clear();
    selection.select(path_one_id);
    const auto before_range = win.root().document().current_snapshot()->revision();
    win.handle_mouse_down(panel_left + 72, fill_opacity_y);
    type_and_commit(L"100.1");
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::FillOpacity);
    assert(win.root().document().current_snapshot()->revision() == before_range);
    win.cancel_numeric_field();

    selection.clear();
    selection.select(path_one_id);
    selection.select(path_two_id, vector::SelectionMode::Add);
    const auto before_stale = win.root().document().current_snapshot()->revision();
    win.handle_mouse_down(panel_left + 72, fill_opacity_y);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::FillOpacity);
    selection.clear();
    selection.select(path_one_id);
    type_and_commit(L"60");
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::None);
    assert(win.root().document().current_snapshot()->revision() == before_stale);
    assert(std::abs(appearance_of(path_one_id).fill_opacity - 0.2) < 1e-9);
    assert(std::abs(appearance_of(path_two_id).fill_opacity - 0.4) < 1e-9);

    std::cout << "[PASS] test_fill_and_stroke_opacity_properties\n";
}

void test_blend_and_effect_properties_panel() {
    NativeWindow win(1440, 900);
    const auto path_id = win.workflow().add_rectangle(24.0, 32.0, 76.0, 48.0,
                                                       "#80c0e0", "#204060", 1.0);
    const auto text_id = win.workflow().add_vietnamese_text(120.0, 42.0, "Appearance stack");
    auto &selection = win.root().vector_controller().selection();
    const auto appearance_of = [&](pikdraw::document::ObjectId id) {
        const auto object = win.root().document().current_snapshot()->objects().get(id);
        assert(object.has_value());
        if (const auto *path = std::get_if<pikdraw::document::PathObject>(&*object))
            return path->appearance;
        assert(std::holds_alternative<pikdraw::document::TextObject>(*object));
        return std::get<pikdraw::document::TextObject>(*object).appearance;
    };
    const int panel_left = 1440 - 280;
    const auto enter_value = [&](std::wstring_view value) {
        for (const wchar_t ch : value)
            win.handle_numeric_char_input(ch);
        win.handle_numeric_char_input(13);
    };
    const auto replace_value = [&](std::wstring_view value) {
        while (!win.numeric_input_buffer().empty())
            win.handle_numeric_char_input(8);
        enter_value(value);
    };

    win.open_object_properties_panel();
    selection.select(path_id);
    assert(win.workflow().set_selected_blend_mode(pikdraw::document::BlendMode::Normal));
    selection.select(text_id);
    assert(win.workflow().set_selected_blend_mode(pikdraw::document::BlendMode::Multiply));
    selection.clear();
    selection.select(path_id);
    selection.select(text_id, pikdraw::vector::SelectionMode::Add);
    auto summary = pikdraw::desktop::selection_appearance(
        *win.root().document().current_snapshot(), selection.selected_ids());
    assert(summary.mixed_blend_mode);
    const auto before_blend = win.root().document().current_snapshot()->revision();
    // AC-BF08-01 — the labeled Screen control applies one blend mode to the full mixed selection.
    win.handle_mouse_down(panel_left + 228, 162 + 616 + 26 + 12);
    assert(appearance_of(path_id).blend_mode == pikdraw::document::BlendMode::Screen);
    assert(appearance_of(text_id).blend_mode == pikdraw::document::BlendMode::Screen);
    assert(win.root().document().current_snapshot()->revision() == before_blend + 1);
    assert(win.workflow().undo());
    assert(appearance_of(path_id).blend_mode == pikdraw::document::BlendMode::Normal);
    assert(appearance_of(text_id).blend_mode == pikdraw::document::BlendMode::Multiply);

    // The dock wheel exposes the add controls while the object-opacity property-bar field stays compact.
    win.scroll_style_inspector(-240);
    const int add_button_y = 162 + 708 + 50 + 14 - win.style_inspector_scroll_offset();
    win.handle_mouse_down(panel_left + 70, add_button_y);
    assert(appearance_of(path_id).effects.size() == 1);
    assert(appearance_of(text_id).effects.size() == 1);

    // AC-BF08-02 — node IDs are unique per object; mixed numeric values never borrow one target silently.
    selection.clear();
    selection.select(path_id);
    assert(win.workflow().set_selected_effect_sigma(0, true, 2.0));
    selection.select(text_id);
    assert(win.workflow().set_selected_effect_sigma(0, true, 4.0));
    selection.clear();
    selection.select(path_id);
    selection.select(text_id, pikdraw::vector::SelectionMode::Add);
    summary = pikdraw::desktop::selection_appearance(
        *win.root().document().current_snapshot(), selection.selected_ids());
    assert(summary.effect_stack_compatible);
    assert(summary.effects.size() == 1 && summary.effects[0].mixed_sigma_x);
    win.scroll_style_inspector(-240);
    const int blur_sigma_x_y = 162 + 794 + 48 + 12 - win.style_inspector_scroll_offset();
    win.handle_mouse_down(panel_left + 82, blur_sigma_x_y);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::BlurSigmaX);
    assert(win.numeric_input_buffer().empty());
    const auto before_sigma = win.root().document().current_snapshot()->revision();
    enter_value(L"3,5");
    assert(std::abs(std::get<pikdraw::document::GaussianBlurEffect>(
        appearance_of(path_id).effects[0].parameters).sigma_x_mm - 3.5) < 1e-9);
    assert(std::abs(std::get<pikdraw::document::GaussianBlurEffect>(
        appearance_of(text_id).effects[0].parameters).sigma_x_mm - 3.5) < 1e-9);
    assert(win.root().document().current_snapshot()->revision() == before_sigma + 1);
    assert(win.workflow().undo());
    assert(std::abs(std::get<pikdraw::document::GaussianBlurEffect>(
        appearance_of(path_id).effects[0].parameters).sigma_x_mm - 2.0) < 1e-9);
    assert(std::abs(std::get<pikdraw::document::GaussianBlurEffect>(
        appearance_of(text_id).effects[0].parameters).sigma_x_mm - 4.0) < 1e-9);
    assert(win.workflow().redo());

    // Add a second ordered node to both selected objects using the same Properties surface.
    const int add_shadow_y = 162 + 708 + 50 + 14 - win.style_inspector_scroll_offset();
    win.handle_mouse_down(panel_left + 205, add_shadow_y);
    assert(appearance_of(path_id).effects.size() == 2);
    assert(appearance_of(text_id).effects.size() == 2);
    auto path_appearance = appearance_of(path_id);
    auto text_appearance = appearance_of(text_id);
    assert(path_appearance.effects[0].id != text_appearance.effects[0].id);
    assert(path_appearance.effects[1].id != text_appearance.effects[1].id);
    assert(std::holds_alternative<pikdraw::document::GaussianBlurEffect>(
        path_appearance.effects[0].parameters));
    assert(std::holds_alternative<pikdraw::document::DropShadowEffect>(
        path_appearance.effects[1].parameters));

    // Editing a numeric field must preserve the shadow's spot-color and overprint source metadata.
    auto spot = pikdraw::color::ColorValue::make_spot(
        "PikDraw Test Spot", 0.42, pikdraw::color::RGBColor{0.3, 0.2, 0.8, 1.0});
    spot.set_overprint({true, false});
    assert(win.workflow().set_selected_effect_color(1, spot));
    win.scroll_style_inspector(-480);
    const int shadow_offset_x_y = 162 + 794 + 128 + 48 + 12 -
                                  win.style_inspector_scroll_offset();
    win.handle_mouse_down(panel_left + 82, shadow_offset_x_y);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::ShadowOffsetX);
    replace_value(L"-7,5");
    path_appearance = appearance_of(path_id);
    text_appearance = appearance_of(text_id);
    const auto &path_shadow = std::get<pikdraw::document::DropShadowEffect>(
        path_appearance.effects[1].parameters);
    const auto &text_shadow = std::get<pikdraw::document::DropShadowEffect>(
        text_appearance.effects[1].parameters);
    assert(std::abs(path_shadow.dx_mm + 7.5) < 1e-9);
    assert(std::abs(text_shadow.dx_mm + 7.5) < 1e-9);
    assert(std::get<pikdraw::color::SpotColor>(path_shadow.color.payload()).spot_name ==
           "PikDraw Test Spot");
    assert(std::get<pikdraw::color::SpotColor>(text_shadow.color.payload()).tint == 0.42);
    assert(path_shadow.color.overprint().fill && !path_shadow.color.overprint().stroke);
    assert(text_shadow.color.overprint().fill && !text_shadow.color.overprint().stroke);

    // AC-BF08-03 — opacity is a percent input; range rejection leaves the source unchanged.
    const int shadow_opacity_y = 162 + 794 + 128 + 48 + 92 + 12 -
                                 win.style_inspector_scroll_offset();
    win.handle_mouse_down(panel_left + 82, shadow_opacity_y);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::ShadowOpacity);
    replace_value(L"35");
    assert(std::abs(std::get<pikdraw::document::DropShadowEffect>(
        appearance_of(path_id).effects[1].parameters).opacity - 0.35) < 1e-9);
    const auto before_invalid = win.root().document().current_snapshot()->revision();
    const int blur_sigma_y = 162 + 794 + 48 + 12 - win.style_inspector_scroll_offset();
    win.handle_mouse_down(panel_left + 82, blur_sigma_y);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::BlurSigmaX);
    replace_value(L"1000001");
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::BlurSigmaX);
    assert(win.root().document().current_snapshot()->revision() == before_invalid);
    win.cancel_numeric_field();

    // AC-BF08-04 — order and enable state are explicit stack edits and Undo restores the old graph.
    const int shadow_up_y = 162 + 794 + 128 + 22 + 11 -
                            win.style_inspector_scroll_offset();
    win.handle_mouse_down(panel_left + 124, shadow_up_y);
    assert(std::holds_alternative<pikdraw::document::DropShadowEffect>(
        appearance_of(path_id).effects[0].parameters));
    assert(std::holds_alternative<pikdraw::document::DropShadowEffect>(
        appearance_of(text_id).effects[0].parameters));
    assert(win.workflow().undo());
    assert(std::holds_alternative<pikdraw::document::GaussianBlurEffect>(
        appearance_of(path_id).effects[0].parameters));
    const int blur_toggle_y = 162 + 794 + 22 + 11 - win.style_inspector_scroll_offset();
    win.handle_mouse_down(panel_left + 52, blur_toggle_y);
    assert(!appearance_of(path_id).effects[0].enabled);
    assert(!appearance_of(text_id).effects[0].enabled);
    assert(win.workflow().undo());
    assert(appearance_of(path_id).effects[0].enabled);

    // AC-BF08-07 — Capture the real NativeWindow/GDI Opacity controls in both supported UI languages.
    // Return the dock to its first page and use one editable Path so Fill and Stroke values are visible.
    selection.clear();
    selection.select(path_id);
    win.scroll_style_inspector(12000);
    win.handle_mouse_move(0, 0); // Clear the prior test interaction's hover tooltip before capture.
    win.set_language(UILanguage::English);
    capture_shell_screenshot(win, "basic-effects-properties-english.bmp");
    win.set_language(UILanguage::Vietnamese);
    capture_shell_screenshot(win, "basic-effects-properties-vietnamese.bmp");
    win.set_language(UILanguage::English);

    // Different node count blocks node editing but still allows object Blend and appending per target.
    selection.clear();
    selection.select(path_id);
    assert(win.workflow().add_selected_effect(pikdraw::document::GaussianBlurEffect{0.5, 0.5}));
    selection.clear();
    selection.select(path_id);
    selection.select(text_id, pikdraw::vector::SelectionMode::Add);
    summary = pikdraw::desktop::selection_appearance(
        *win.root().document().current_snapshot(), selection.selected_ids());
    assert(!summary.effect_stack_compatible);
    assert(win.workflow().can_edit_selected_appearance());
    assert(!win.workflow().can_edit_selected_effect(0));
    const auto before_mismatched_node = win.root().document().current_snapshot()->revision();
    assert(!win.workflow().set_selected_effect_sigma(0, true, 8.0));
    assert(win.root().document().current_snapshot()->revision() == before_mismatched_node);
    assert(win.workflow().can_add_selected_effect());
    assert(win.workflow().set_selected_blend_mode(pikdraw::document::BlendMode::Lighten));

    // A locked member prevents every Appearance edit for the whole selection.
    pikdraw::document::TextObject locked;
    locked.id = pikdraw::document::generate_object_id();
    locked.text = "locked appearance";
    locked.locked = true;
    const auto locked_id = locked.id;
    assert(win.root().document().execute_command(
        std::make_unique<pikdraw::document::AddObjectCommand>(locked)).success);
    selection.clear();
    selection.select(path_id);
    selection.select(locked_id, pikdraw::vector::SelectionMode::Add);
    const auto before_locked = win.root().document().current_snapshot()->revision();
    assert(!win.workflow().can_edit_selected_appearance());
    assert(!win.workflow().can_add_selected_effect());
    assert(!win.workflow().set_selected_blend_mode(pikdraw::document::BlendMode::Overlay));
    assert(!win.workflow().add_selected_effect(pikdraw::document::GaussianBlurEffect{}));
    assert(win.root().document().current_snapshot()->revision() == before_locked);

    const auto image_id = win.workflow().add_bitmap_image(220.0, 80.0, 32.0, 24.0);
    selection.clear();
    selection.select(path_id);
    selection.select(image_id, pikdraw::vector::SelectionMode::Add);
    const auto before_unsupported = win.root().document().current_snapshot()->revision();
    assert(!win.workflow().can_edit_selected_appearance());
    assert(!win.workflow().can_add_selected_effect());
    assert(!win.workflow().set_selected_blend_mode(pikdraw::document::BlendMode::Overlay));
    assert(win.root().document().current_snapshot()->revision() == before_unsupported);

    std::cout << "[PASS] test_blend_and_effect_properties_panel\n";
}

void test_effects_toolbox_flyout() {
    NativeWindow win(1440, 900);
    const auto path_id = win.workflow().add_rectangle(30.0, 36.0, 72.0, 44.0,
                                                       "#80b8dc", "#28445e", 1.0);
    auto &selection = win.root().vector_controller().selection();
    selection.select(path_id);
    const auto path_appearance = [&]() {
        const auto object = win.root().document().current_snapshot()->objects().get(path_id);
        assert(object && std::holds_alternative<pikdraw::document::PathObject>(*object));
        return std::get<pikdraw::document::PathObject>(*object).appearance;
    };

#ifdef _WIN32
    // AC-BF10-04 — keyboard-only path: Tab focuses Appearance, Alt+Down opens it,
    // arrows choose Blur, Enter applies once, and Escape closes/returns focus.
    while (win.status_message().find("Appearance") == std::string::npos)
        assert(win.handle_toolbox_key(VK_TAB));
    assert(win.status_message().find("Appearance") != std::string::npos);
    assert(!win.handle_toolbox_key(VK_TAB, true, false)); // Preserve the system Alt+Tab switcher.
    assert(win.handle_toolbox_key(VK_DOWN, true, false));
    assert(win.status_message().find("Appearance Properties") != std::string::npos);
    assert(win.handle_toolbox_key(VK_DOWN));
    assert(win.status_message().find("Gaussian Blur") != std::string::npos);
    auto keyboard_revision = win.root().document().current_snapshot()->revision();
    assert(win.handle_toolbox_key(VK_RETURN));
    assert(win.root().document().current_snapshot()->revision() == keyboard_revision + 1);
    assert(path_appearance().effects.size() == 1);
    assert(std::holds_alternative<pikdraw::document::GaussianBlurEffect>(
        path_appearance().effects[0].parameters));
    assert(win.workflow().undo());
    assert(path_appearance().effects.empty());
    assert(win.handle_toolbox_key(VK_DOWN, true, false));
    assert(win.handle_toolbox_key(VK_ESCAPE));
    assert(win.root().document().current_snapshot()->revision() == keyboard_revision + 2);
    assert(win.handle_toolbox_key(VK_ESCAPE));
    assert(win.status_message().find("Canvas focus") != std::string::npos);
    while (win.status_message().find("Appearance") == std::string::npos)
        assert(win.handle_toolbox_key(VK_TAB));
    assert(win.handle_toolbox_key(VK_DOWN, true, false));
    capture_shell_screenshot(win, "effects-toolbox-keyboard-focus-english.bmp");
    win.set_language(UILanguage::Vietnamese);
    capture_shell_screenshot(win, "effects-toolbox-keyboard-focus-vietnamese.bmp");
    win.set_language(UILanguage::English);
    assert(win.handle_toolbox_key(VK_ESCAPE));
    assert(win.handle_toolbox_key(VK_ESCAPE));
#endif

    // Direct toolbox buttons for Gaussian Blur (row 7, y = 424) and Drop Shadow (row 8, y = 466).
    auto before_effect = win.root().document().current_snapshot()->revision();
    win.handle_mouse_down(26, 424); // Row 7: Gaussian Blur button.
    assert(win.style_inspector_visible());
    assert(win.root().document().current_snapshot()->revision() == before_effect + 1);
    assert(path_appearance().effects.size() == 1);
    assert(std::holds_alternative<pikdraw::document::GaussianBlurEffect>(
        path_appearance().effects[0].parameters));

    // Capture the native paint path in both supported UI languages.
    capture_shell_screenshot(win, "effects-toolbox-english.bmp");
    win.set_language(UILanguage::Vietnamese);
    capture_shell_screenshot(win, "effects-toolbox-vietnamese.bmp");
    win.set_language(UILanguage::English);

    before_effect = win.root().document().current_snapshot()->revision();
    win.handle_mouse_down(26, 466); // Row 8: Drop Shadow button.
    assert(win.root().document().current_snapshot()->revision() == before_effect + 1);
    assert(path_appearance().effects.size() == 2);
    assert(std::holds_alternative<pikdraw::document::DropShadowEffect>(
        path_appearance().effects[1].parameters));

    // AC-BF11-01 — Clear Effects removes the whole stack in one command and preserves other Appearance fields.
    auto appearance_before_clear = path_appearance();
    appearance_before_clear.object_opacity = 0.42;
    appearance_before_clear.fill_opacity = 0.68;
    appearance_before_clear.stroke_opacity = 0.31;
    appearance_before_clear.blend_mode = pikdraw::document::BlendMode::Multiply;
    assert(win.root().document().execute_command(
        std::make_unique<pikdraw::document::SetAppearanceCommand>(
            std::vector<pikdraw::document::AppearanceReplacement>{{path_id, appearance_before_clear}})).success);
    assert(win.workflow().can_clear_selected_effects());
    const auto before_clear = win.root().document().current_snapshot()->revision();
    const int clear_y = 30 + pikdraw::desktop::get_menu_item_top(pikdraw::desktop::MenuId::Effects, pikdraw::desktop::MenuActionId::EffectsClear) + 10;
    win.handle_mouse_down(400, 20); // Effects menu on the 1440px English window.
    win.handle_mouse_down(400, clear_y); // Clear Effects row
    assert(win.root().document().current_snapshot()->revision() == before_clear + 1);
    auto cleared_appearance = path_appearance();
    assert(cleared_appearance.effects.empty());
    assert(std::abs(cleared_appearance.object_opacity - 0.42) < 1e-9);
    assert(std::abs(cleared_appearance.fill_opacity - 0.68) < 1e-9);
    assert(std::abs(cleared_appearance.stroke_opacity - 0.31) < 1e-9);
    assert(cleared_appearance.blend_mode == pikdraw::document::BlendMode::Multiply);
    assert(!win.workflow().can_clear_selected_effects());
    assert(!win.workflow().clear_selected_effects());
    assert(win.root().document().current_snapshot()->revision() == before_clear + 1);
    const auto before_disabled_clear = win.root().document().current_snapshot()->revision();
    win.handle_mouse_down(400, 20);
    win.handle_mouse_down(400, clear_y);
    assert(win.root().document().current_snapshot()->revision() == before_disabled_clear);
    assert(win.workflow().undo());
    assert(path_appearance().effects.size() == 2);
    assert(std::holds_alternative<pikdraw::document::GaussianBlurEffect>(
        path_appearance().effects[0].parameters));
    assert(std::holds_alternative<pikdraw::document::DropShadowEffect>(
        path_appearance().effects[1].parameters));
    assert(std::abs(path_appearance().object_opacity - 0.42) < 1e-9);
    assert(std::abs(path_appearance().fill_opacity - 0.68) < 1e-9);
    assert(std::abs(path_appearance().stroke_opacity - 0.31) < 1e-9);
    assert(path_appearance().blend_mode == pikdraw::document::BlendMode::Multiply);
    assert(win.workflow().undo()); // Undo the setup command, keeping both effect nodes.
    assert(win.workflow().undo());
    assert(path_appearance().effects.size() == 1);

    // Contour tool activation via direct toolbox button (row 9, y = 508)
    selection.select(path_id);
    win.handle_mouse_down(26, 508); // Row 9: Contour tool
    assert(win.active_tool() == 12);
    assert(win.active_dock_tab() == 0);
    capture_shell_screenshot(win, "contour-properties-1440-en.bmp");
    win.set_language(UILanguage::Vietnamese);
    capture_shell_screenshot(win, "contour-properties-1440-vi.bmp");
    win.set_language(UILanguage::English);
    NativeWindow narrow_contour(1366, 768);
    const auto narrow_path = narrow_contour.workflow().add_rectangle(20, 20, 70, 45, "#80b8dc", "#28445e", 1);
    narrow_contour.root().vector_controller().selection().select(narrow_path);
    narrow_contour.set_active_tool(12);
    capture_shell_screenshot(narrow_contour, "contour-properties-1366-en.bmp");
    NativeWindow wide_contour(1920, 1080);
    const auto wide_path = wide_contour.workflow().add_rectangle(20, 20, 70, 45, "#80b8dc", "#28445e", 1);
    wide_contour.root().vector_controller().selection().select(wide_path);
    wide_contour.set_active_tool(12);
    capture_shell_screenshot(wide_contour, "contour-properties-1920-en.bmp");
    auto* contour = win.contour_tool();
    assert(contour && contour->active());
    const auto contour_revision = win.root().document().current_snapshot()->revision();
    const auto set_contour_field = [&](int y, std::wstring_view value,
                                       NativeWindow::NumericFieldId expected) {
        win.handle_mouse_down(1300, y);
        assert(win.active_numeric_field() == expected);
        while (!win.numeric_input_buffer().empty())
            win.handle_numeric_char_input(8);
        for (const wchar_t ch : value) win.handle_numeric_char_input(ch);
        win.handle_numeric_char_input(13);
        assert(win.active_numeric_field() == NativeWindow::NumericFieldId::None);
    };
    // Properties edits preview parameters without writing document history; Tab follows field order.
    win.handle_mouse_down(1300, 254);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::ContourSpacing);
    while (!win.numeric_input_buffer().empty()) win.handle_numeric_char_input(8);
    for (const wchar_t ch : std::wstring_view(L"2,75")) win.handle_numeric_char_input(ch);
    win.handle_numeric_char_input(9);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::ContourSteps);
    while (!win.numeric_input_buffer().empty()) win.handle_numeric_char_input(8);
    for (const wchar_t ch : std::wstring_view(L"5")) win.handle_numeric_char_input(ch);
    win.handle_numeric_char_input(9);
    if (win.active_numeric_field() == NativeWindow::NumericFieldId::ContourTolerance)
        win.handle_numeric_char_input(9);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::ContourMiterLimit);
    while (!win.numeric_input_buffer().empty()) win.handle_numeric_char_input(8);
    for (const wchar_t ch : std::wstring_view(L"7.5")) win.handle_numeric_char_input(ch);
    win.handle_numeric_char_input(13);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::None);
    assert(std::abs(contour->spacing_mm() - 2.75) < 1e-9);
    assert(contour->steps() == 5);
    assert(std::abs(contour->miter_limit() - 7.5) < 1e-9);
    assert(contour->has_preview());
    assert(win.root().document().current_snapshot()->revision() == contour_revision);
    win.handle_mouse_down(1300, 254);
    while (!win.numeric_input_buffer().empty()) win.handle_numeric_char_input(8);
    for (const wchar_t ch : std::wstring_view(L"0.05")) win.handle_numeric_char_input(ch);
    win.handle_numeric_char_input(13);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::ContourSpacing);
    assert(std::abs(contour->spacing_mm() - 2.75) < 1e-9);
    win.handle_numeric_char_input(27);

    // Apply writes one undoable feature; Cancel restores that saved draft without a write.
    win.handle_mouse_down(1184, 505); // Properties > Apply.
    assert(win.root().document().current_snapshot()->revision() == contour_revision + 1);
    auto contour_feature = pikdraw::document::get_object_contour(
        win.root().document(), path_id);
    assert(contour_feature);
    assert(std::abs(contour_feature->spacing_mm - 2.75) < 1e-9);
    assert(contour_feature->steps == 5);
    assert(std::abs(contour_feature->miter_limit - 7.5) < 1e-9);
    set_contour_field(254, L"3.5", NativeWindow::NumericFieldId::ContourSpacing);
    const auto before_cancel = win.root().document().current_snapshot()->revision();
    win.handle_mouse_down(1380, 505); // Properties > Cancel.
    assert(std::abs(contour->spacing_mm() - 2.75) < 1e-9);
    assert(win.root().document().current_snapshot()->revision() == before_cancel);

    // Remove calls the dedicated Contour operation and Undo restores its saved feature.
    win.handle_mouse_down(1248, 505); // Properties > Remove.
    assert(!pikdraw::document::get_object_contour(win.root().document(), path_id));
    assert(win.root().document().current_snapshot()->revision() == before_cancel + 1);
    assert(win.workflow().undo());
    assert(pikdraw::document::get_object_contour(win.root().document(), path_id));

    // Contour tool activation via Effects menu
    win.set_active_tool(0);
    assert(win.active_tool() == 0);
    const int contour_y = 30 + pikdraw::desktop::get_menu_item_top(pikdraw::desktop::MenuId::Effects, pikdraw::desktop::MenuActionId::EffectsContour) + 10;
    win.handle_mouse_down(400, 20); // Open Effects menu
    win.handle_mouse_down(400, contour_y); // Contour row
    assert(win.active_tool() == 12);

    // Contour tool activation via Ctrl+F9 shortcut
    win.set_active_tool(0);
    assert(win.active_tool() == 0);
    assert(win.root().window().action_dispatcher().dispatch_shortcut("Ctrl+F9"));
    assert(win.active_tool() == 12);

    // Break Contour Apart via Effects menu
    const int break_contour_y = 30 + pikdraw::desktop::get_menu_item_top(pikdraw::desktop::MenuId::Effects, pikdraw::desktop::MenuActionId::EffectsBreakContourApart) + 10;
    win.handle_mouse_down(400, 20);
    win.handle_mouse_down(400, break_contour_y); // Break Contour Apart row

    // Test Contour Ribbon Property Bar Clicks and Controls:
    win.root().vector_controller().selection().select(path_id);
    win.set_active_tool(12);
    assert(win.contour_tool() && win.contour_tool()->active());

    // 1. Direction buttons on ribbon (y in [66, 102], say y = 80)
    win.handle_mouse_down(100, 80); // To Center: x in [75, 135]
    assert(win.contour_tool()->direction() == pikdraw::geometry::ContourDirection::ToCenter);
    win.handle_mouse_down(160, 80); // Inside: x in [138, 198]
    assert(win.contour_tool()->direction() == pikdraw::geometry::ContourDirection::Inside);
    win.handle_mouse_down(230, 80); // Outside: x in [201, 261]
    assert(win.contour_tool()->direction() == pikdraw::geometry::ContourDirection::Outside);

    // 2. Steps input box: x in [270, 330]
    win.handle_mouse_down(300, 80);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::ContourSteps);
    win.commit_numeric_field();

    // 3. Steps spinner: x in [332, 352]
    const int initial_steps = win.contour_tool()->steps();
    win.handle_mouse_down(342, 75); // Top half (▲)
    assert(win.contour_tool()->steps() == initial_steps + 1);
    win.handle_mouse_down(342, 90); // Bottom half (▼)
    assert(win.contour_tool()->steps() == initial_steps);

    // 4. Spacing input box: x in [360, 440]
    win.handle_mouse_down(400, 80);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::ContourSpacing);
    win.commit_numeric_field();

    // 5. Spacing spinner: x in [442, 462]
    const double initial_spacing = win.contour_tool()->spacing_mm();
    win.handle_mouse_down(452, 75); // Top half (▲)
    assert(std::abs(win.contour_tool()->spacing_mm() - (initial_spacing + 0.5)) < 1e-6);
    win.handle_mouse_down(452, 90); // Bottom half (▼)
    assert(std::abs(win.contour_tool()->spacing_mm() - initial_spacing) < 1e-6);

    // 6. Corners buttons: Miter [470..530], Round [533..593], Bevel [596..656]
    win.handle_mouse_down(560, 80); // Round
    assert(win.contour_tool()->join_type() == pikdraw::geometry::JoinType::Round);
    win.handle_mouse_down(620, 80); // Bevel
    assert(win.contour_tool()->join_type() == pikdraw::geometry::JoinType::Bevel);
    win.handle_mouse_down(500, 80); // Miter
    assert(win.contour_tool()->join_type() == pikdraw::geometry::JoinType::Miter);

    // 7. Interactive Canvas Dragging:
    const auto canvas_area = win.canvas_rect(win.width(), win.height());
    auto& camera = win.root().renderer().camera();
    camera.set_viewport_size(canvas_area.right - canvas_area.left, canvas_area.bottom - canvas_area.top);
    const auto center_screen = camera.world_to_screen({66.0f, 58.0f});
    const int cx = canvas_area.left + static_cast<int>(std::lround(center_screen.x));
    const int cy = canvas_area.top + static_cast<int>(std::lround(center_screen.y));

    // Drag from center outward
    win.handle_mouse_down(cx, cy);
    win.handle_mouse_move(cx + 80, cy);
    assert(win.contour_tool()->direction() == pikdraw::geometry::ContourDirection::Outside);
    win.handle_mouse_up(cx + 80, cy);

    // Drag from outer point inward
    win.handle_mouse_down(cx + 80, cy);
    win.handle_mouse_move(cx + 10, cy);
    assert(win.contour_tool()->direction() == pikdraw::geometry::ContourDirection::Inside);
    win.handle_mouse_up(cx + 10, cy);

    // 7b. Outline and Fill buttons:
    win.handle_mouse_down(680, 80); // Toggle outline
    assert(!win.contour_tool()->has_outline());
    win.handle_mouse_down(680, 80); // Toggle outline back on
    assert(win.contour_tool()->has_outline());

    // 8. Apply button: x in [775..850]
    win.handle_mouse_down(800, 80); // Apply
    assert(pikdraw::document::get_object_contour(win.root().document(), path_id));

    // 9. Remove button: x in [990..1055]
    win.handle_mouse_down(1010, 80); // Remove
    assert(!pikdraw::document::get_object_contour(win.root().document(), path_id));

    // AC-BF10-03 — no selection disables effect insertion and does not write the document.
    selection.clear();
    const auto before_disabled = win.root().document().current_snapshot()->revision();
    win.handle_mouse_down(26, 424); // Row 7: Gaussian Blur button with no selection.
    assert(win.root().document().current_snapshot()->revision() == before_disabled);
    assert(win.status_message().find("Select unlocked Path/Text") != std::string::npos);
    win.set_language(UILanguage::Vietnamese);
    win.handle_mouse_down(26, 424);
    assert(win.root().document().current_snapshot()->revision() == before_disabled);
    assert(win.status_message().find("Chọn Path/Text") != std::string::npos);

    std::cout << "[PASS] test_effects_toolbox_flyout\n";
}

