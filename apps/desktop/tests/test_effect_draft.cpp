#include "pikdraw/desktop/shell/native_window.hpp"
#include "../src/shell/native_window_menus.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include "pikdraw/document/model/object.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <variant>

using namespace pikdraw::desktop;

namespace {

pikdraw::document::Appearance appearance_of(const NativeWindow &window,
                                             pikdraw::document::ObjectId id) {
    const auto object = window.root().document().current_snapshot()->objects().get(id);
    assert(object);
    if (const auto *path = std::get_if<pikdraw::document::PathObject>(&*object))
        return path->appearance;
    if (const auto *text = std::get_if<pikdraw::document::TextObject>(&*object))
        return text->appearance;
    assert(false && "expected editable Path/Text");
    return {};
}

void test_effect_tool_reactivation_and_shadow_drag() {
    NativeWindow window(1440, 900);
    const auto path_id = window.workflow().add_rectangle(
        30.0, 36.0, 72.0, 44.0, "#80b8dc", "#28445e", 1.0);
    auto& selection = window.root().vector_controller().selection();
    selection.select(path_id);

    const auto shadow_action = static_cast<int>(MenuActionId::EffectsDropShadow);
    window.execute_menu_action(shadow_action);
    auto appearance = appearance_of(window, path_id);
    assert(appearance.effects.size() == 1);
    const auto shadow_id = appearance.effects.front().id;
    assert(window.style_inspector_visible());

    const pikdraw::document::DropShadowEffect customized{
        3.5, -1.5, 2.25, 1.75,
        pikdraw::color::RGBColor{0.2, 0.4, 0.8, 0.65}, 0.32};
    assert(window.workflow().set_selected_effect_parameters(0, customized));
    assert(window.workflow().can_set_selected_effect_blend_mode(
        0, pikdraw::document::BlendMode::Multiply));
    assert(window.workflow().set_selected_effect_blend_mode(
        0, pikdraw::document::BlendMode::Multiply));
    appearance = appearance_of(window, path_id);
    assert(appearance.effects.front().schema_minor ==
           pikdraw::document::kDropShadowBlendSchemaMinor);
    assert(std::get<pikdraw::document::DropShadowEffect>(
               appearance.effects.front().parameters).blend_mode ==
           pikdraw::document::BlendMode::Multiply);
    assert(!window.workflow().can_add_selected_effect(
        pikdraw::document::GaussianBlurEffect{1.0, 1.0}));
    const auto multiply_revision =
        window.root().document().current_snapshot()->revision();

    window.execute_menu_action(shadow_action);
    assert(window.root().document().current_snapshot()->revision() == multiply_revision);
    appearance = appearance_of(window, path_id);
    assert(appearance.effects.size() == 1);
    assert(appearance.effects.front().id == shadow_id);
    auto shadow = std::get<pikdraw::document::DropShadowEffect>(
        appearance.effects.front().parameters);
    assert(std::abs(shadow.dx_mm - customized.dx_mm) < 1e-9);
    assert(std::abs(shadow.dy_mm - customized.dy_mm) < 1e-9);
    assert(std::abs(shadow.sigma_x_mm - customized.sigma_x_mm) < 1e-9);
    assert(std::abs(shadow.sigma_y_mm - customized.sigma_y_mm) < 1e-9);
    assert(std::abs(shadow.opacity - customized.opacity) < 1e-9);
    if (const auto* rgb = std::get_if<pikdraw::color::RGBColor>(&shadow.color.payload())) {
        assert(std::abs(rgb->r - 0.2) < 1e-4);
        assert(std::abs(rgb->g - 0.4) < 1e-4);
        assert(std::abs(rgb->b - 0.8) < 1e-4);
        assert(std::abs(rgb->a - 0.65) < 1e-4);
    } else if (const auto* cmyk = std::get_if<pikdraw::color::CMYKColor>(&shadow.color.payload())) {
        assert(std::abs(cmyk->a - 0.65) < 1e-4);
    } else {
        assert(false && "unexpected color payload");
    }
    assert(!window.workflow().add_selected_effect(pikdraw::document::DropShadowEffect{}));
    assert(window.root().document().current_snapshot()->revision() == multiply_revision);
    assert(window.workflow().undo());
    appearance = appearance_of(window, path_id);
    assert(appearance.effects.front().schema_minor ==
           pikdraw::document::kBasicEffectNodeSchemaMinor);
    assert(std::get<pikdraw::document::DropShadowEffect>(
               appearance.effects.front().parameters).blend_mode ==
           pikdraw::document::BlendMode::Normal);
    const auto customized_revision =
        window.root().document().current_snapshot()->revision();

    const auto canvas = window.canvas_rect(1440, 900);
    auto& camera = window.root().renderer().camera();
    camera.set_center({66.0, 58.0});
    camera.set_zoom(2.0);
    camera.set_viewport_size(canvas.right - canvas.left, canvas.bottom - canvas.top);

    const auto reactivation_capture_dir = std::filesystem::temp_directory_path() /
        "pikdraw-effect-reactivation-20261017";
    std::filesystem::create_directories(reactivation_capture_dir);
    window.set_language(UILanguage::English);
    window.execute_menu_action(shadow_action);
    window.capture_screenshot_bmp(
        (reactivation_capture_dir / "existing-shadow-edit-english.bmp").string());
    window.set_language(UILanguage::Vietnamese);
    window.execute_menu_action(shadow_action);
    window.capture_screenshot_bmp(
        (reactivation_capture_dir / "existing-shadow-edit-vietnamese.bmp").string());
    window.set_language(UILanguage::English);
    assert(window.root().document().current_snapshot()->revision() == customized_revision);

    const auto bounds = selection.bounds(*window.root().document().current_snapshot());
    assert(bounds);
    const pikdraw::geometry::Point2D handle_world{
        bounds->center().x + shadow.dx_mm, bounds->center().y + shadow.dy_mm};
    const auto handle_screen = camera.world_to_screen(handle_world);
    const int handle_x = canvas.left + static_cast<int>(std::lround(handle_screen.x));
    const int handle_y = canvas.top + static_cast<int>(std::lround(handle_screen.y));

    window.handle_mouse_down(handle_x, handle_y, 1u);
    assert(window.workflow().has_effect_draft());
    window.handle_mouse_up(handle_x, handle_y, 0u);
    assert(!window.workflow().has_effect_draft());
    assert(window.root().document().current_snapshot()->revision() == customized_revision);

    window.handle_mouse_down(handle_x, handle_y, 1u); // MK_LBUTTON: begin edit preview.
    const auto draft = window.workflow().effect_draft();
    assert(draft && draft->editing_existing && draft->preview_enabled);
    assert(window.root().document().current_snapshot()->revision() == customized_revision);
    window.handle_mouse_move(handle_x + 20, handle_y - 12, 1u);
    const auto preview_draft = window.workflow().effect_draft();
    assert(preview_draft && preview_draft->editing_existing && preview_draft->preview_enabled);
    const auto preview_shadow = std::get<pikdraw::document::DropShadowEffect>(
        preview_draft->parameters);
    assert(std::abs(preview_shadow.dx_mm - 13.5) < 0.6);
    assert(std::abs(preview_shadow.dy_mm - 4.5) < 0.6);
    const auto candidate = window.workflow().snapshot_for_canvas();
    const auto candidate_object = candidate->objects().get(path_id);
    assert(candidate_object && std::holds_alternative<pikdraw::document::PathObject>(*candidate_object));
    const auto& candidate_nodes = std::get<pikdraw::document::PathObject>(
        *candidate_object).appearance.effects;
    assert(candidate_nodes.size() == 1 && candidate_nodes.front().id == shadow_id);
    const auto candidate_shadow = std::get<pikdraw::document::DropShadowEffect>(
        candidate_nodes.front().parameters);
    assert(std::abs(candidate_shadow.dx_mm - preview_shadow.dx_mm) < 1e-9);
    assert(std::abs(candidate_shadow.dy_mm - preview_shadow.dy_mm) < 1e-9);
    assert(window.root().document().current_snapshot()->revision() == customized_revision);

    window.handle_mouse_up(handle_x + 20, handle_y - 12, 0u);
    assert(!window.workflow().has_effect_draft());
    assert(window.root().document().current_snapshot()->revision() == customized_revision + 1);
    appearance = appearance_of(window, path_id);
    assert(appearance.effects.size() == 1);
    assert(appearance.effects.front().id == shadow_id);
    shadow = std::get<pikdraw::document::DropShadowEffect>(appearance.effects.front().parameters);
    assert(std::abs(shadow.dx_mm - 13.5) < 0.6);
    assert(std::abs(shadow.dy_mm - 4.5) < 0.6);
    assert(std::abs(shadow.sigma_x_mm - customized.sigma_x_mm) < 1e-9);
    assert(std::abs(shadow.sigma_y_mm - customized.sigma_y_mm) < 1e-9);
    assert(std::abs(shadow.opacity - customized.opacity) < 1e-9);
    assert(window.workflow().undo());
    appearance = appearance_of(window, path_id);
    shadow = std::get<pikdraw::document::DropShadowEffect>(appearance.effects.front().parameters);
    assert(std::abs(shadow.dx_mm - customized.dx_mm) < 1e-9);
    assert(std::abs(shadow.dy_mm - customized.dy_mm) < 1e-9);

    const auto blur_action = static_cast<int>(MenuActionId::EffectsGaussianBlur);
    window.execute_menu_action(blur_action);
    appearance = appearance_of(window, path_id);
    assert(appearance.effects.size() == 2);
    const auto blur_id = appearance.effects.back().id;
    assert(window.workflow().set_selected_effect_parameters(
        1, pikdraw::document::GaussianBlurEffect{4.25, 6.5}));
    const auto blur_revision = window.root().document().current_snapshot()->revision();
    window.execute_menu_action(blur_action);
    assert(window.root().document().current_snapshot()->revision() == blur_revision);
    appearance = appearance_of(window, path_id);
    assert(appearance.effects.size() == 2);
    assert(appearance.effects.back().id == blur_id);
    const auto blur = std::get<pikdraw::document::GaussianBlurEffect>(
        appearance.effects.back().parameters);
    assert(std::abs(blur.sigma_x_mm - 4.25) < 1e-9);
    assert(std::abs(blur.sigma_y_mm - 6.5) < 1e-9);

    const auto second_id = window.workflow().add_rectangle(
        130.0, 36.0, 44.0, 28.0, "#80b8dc", "#28445e", 1.0);
    selection.select(second_id, pikdraw::vector::SelectionMode::Add);
    const auto mixed_revision = window.root().document().current_snapshot()->revision();
    window.execute_menu_action(shadow_action);
    assert(window.root().document().current_snapshot()->revision() == mixed_revision);
    assert(appearance_of(window, path_id).effects.size() == 2);
    assert(appearance_of(window, second_id).effects.empty());
}

void test_non_normal_shadow_must_be_last_enabled_effect() {
    NativeWindow window(1440, 900);
    const auto path_id = window.workflow().add_rectangle(
        20.0, 24.0, 64.0, 48.0, "#c08040", "#302010", 0.5);
    window.root().vector_controller().selection().select(path_id);

    assert(window.workflow().add_selected_effect(
        pikdraw::document::DropShadowEffect{}));
    assert(window.workflow().add_selected_effect(
        pikdraw::document::GaussianBlurEffect{0.5, 0.5}));
    assert(!window.workflow().can_set_selected_effect_blend_mode(
        0, pikdraw::document::BlendMode::Multiply));
    assert(window.workflow().move_selected_effect(0, 1));
    assert(window.workflow().can_set_selected_effect_blend_mode(
        1, pikdraw::document::BlendMode::Multiply));
    assert(window.workflow().set_selected_effect_blend_mode(
        1, pikdraw::document::BlendMode::Multiply));
    assert(!window.workflow().can_move_selected_effect(1, -1));
    assert(!window.workflow().can_add_selected_effect(
        pikdraw::document::GaussianBlurEffect{1.0, 1.0}));

    const auto appearance = appearance_of(window, path_id);
    assert(appearance.effects.size() == 2);
    assert(std::holds_alternative<pikdraw::document::GaussianBlurEffect>(
        appearance.effects.front().parameters));
    assert(appearance.effects.back().schema_minor ==
           pikdraw::document::kDropShadowBlendSchemaMinor);
    assert(std::get<pikdraw::document::DropShadowEffect>(
        appearance.effects.back().parameters).blend_mode ==
           pikdraw::document::BlendMode::Multiply);
}

void test_shadow_blend_property_and_reactivation_edit_existing_node() {
    NativeWindow window(1440, 900);
    const auto path_id = window.workflow().add_rectangle(
        30.0, 36.0, 72.0, 44.0, "#80b8dc", "#28445e", 1.0);
    window.root().vector_controller().selection().select(path_id);

    const auto shadow_action = static_cast<int>(MenuActionId::EffectsDropShadow);
    window.execute_menu_action(shadow_action);
    const auto initial = appearance_of(window, path_id);
    assert(initial.effects.size() == 1);
    const auto shadow_id = initial.effects.front().id;
    const auto initial_revision = window.root().document().current_snapshot()->revision();

    window.scroll_style_inspector(-1200);
    const int panel_left = 1440 - 280;
    const int shadow_multiply_x = panel_left + 140;
    const int shadow_multiply_y = 162 + 794 + 48 + 144 + 12 -
                                  window.style_inspector_scroll_offset();
    window.handle_mouse_down(shadow_multiply_x, shadow_multiply_y);

    auto edited = appearance_of(window, path_id);
    assert(edited.effects.size() == 1);
    assert(edited.effects.front().id == shadow_id);
    assert(edited.effects.front().schema_minor ==
           pikdraw::document::kDropShadowBlendSchemaMinor);
    assert(std::get<pikdraw::document::DropShadowEffect>(
               edited.effects.front().parameters).blend_mode ==
           pikdraw::document::BlendMode::Multiply);
    assert(window.root().document().current_snapshot()->revision() ==
           initial_revision + 1);

    const auto edited_revision =
        window.root().document().current_snapshot()->revision();
    window.execute_menu_action(shadow_action);
    edited = appearance_of(window, path_id);
    assert(window.root().document().current_snapshot()->revision() == edited_revision);
    assert(edited.effects.size() == 1);
    assert(edited.effects.front().id == shadow_id);
    assert(std::get<pikdraw::document::DropShadowEffect>(
               edited.effects.front().parameters).blend_mode ==
           pikdraw::document::BlendMode::Multiply);
}

void test_preview_cancel_apply_and_stale_guards() {
    NativeWindow window(1440, 900);
    const auto path_id = window.workflow().add_rectangle(
        30.0, 36.0, 72.0, 44.0, "#80b8dc", "#28445e", 1.0);
    const auto text_id = window.workflow().add_vietnamese_text(
        42.0, 50.0, "PikDraw draft", "Arial", 14.0, "#19354c");
    window.root().renderer().camera().set_center({66.0, 58.0});
    window.root().renderer().camera().set_zoom(2.0);
    window.root().renderer().camera().set_viewport_size(256, 192);
    auto &selection = window.root().vector_controller().selection();
    selection.select(path_id);
    const auto initial = window.root().document().current_snapshot();
    const auto initial_revision = initial->revision();
    const auto initial_pixels = window.workflow().render_snapshot(256, 192);

    assert(window.workflow().begin_selected_effect_draft(
        pikdraw::document::GaussianBlurEffect{1.0, 1.0}));
    assert(window.workflow().has_effect_draft());
    assert(window.root().document().current_snapshot() == initial);
    assert(window.root().document().current_snapshot()->revision() == initial_revision);
    assert(window.workflow().snapshot_for_canvas() == initial);

    assert(window.workflow().start_effect_draft_numeric_input(
        DocumentWorkflow::EffectDraftField::SigmaX));
    for (const char ch : std::string("12,5"))
        assert(window.workflow().input_effect_draft_character(ch));
    assert(window.workflow().commit_effect_draft_numeric_input());
    assert(window.root().document().current_snapshot() == initial);
    assert(window.workflow().set_effect_draft_preview(true));
    const auto preview = window.workflow().snapshot_for_canvas();
    assert(preview && preview != initial);
    assert(preview->revision() == initial_revision);
    assert(appearance_of(window, path_id).effects.empty());
    const auto preview_object = preview->objects().get(path_id);
    assert(preview_object && std::holds_alternative<pikdraw::document::PathObject>(*preview_object));
    const auto &preview_effects = std::get<pikdraw::document::PathObject>(*preview_object).appearance.effects;
    assert(preview_effects.size() == 1);
    assert(std::abs(std::get<pikdraw::document::GaussianBlurEffect>(
        preview_effects.front().parameters).sigma_x_mm - 12.5) < 1e-9);
    const auto preview_pixels = window.workflow().render_snapshot(256, 192);
    assert(preview_pixels.size() == initial_pixels.size());
    assert(preview_pixels != initial_pixels);

    window.workflow().cancel_effect_draft();
    assert(!window.workflow().has_effect_draft());
    assert(window.workflow().snapshot_for_canvas() == initial);
    assert(window.root().document().current_snapshot()->revision() == initial_revision);
    assert(appearance_of(window, path_id).effects.empty());

    // A single atomic Apply creates a unique ID for each selected target and one Undo entry.
    selection.select(text_id, pikdraw::vector::SelectionMode::Add);
    const auto before_apply = window.root().document().current_snapshot()->revision();
    assert(window.workflow().begin_selected_effect_draft(pikdraw::document::DropShadowEffect{}));
    assert(window.workflow().start_effect_draft_numeric_input(
        DocumentWorkflow::EffectDraftField::OffsetX));
    for (const char ch : std::string("-3,5"))
        assert(window.workflow().input_effect_draft_character(ch));
    assert(window.workflow().commit_effect_draft_numeric_input());
    assert(window.workflow().start_effect_draft_numeric_input(
        DocumentWorkflow::EffectDraftField::Opacity));
    for (const char ch : std::string("35"))
        assert(window.workflow().input_effect_draft_character(ch));
    assert(window.workflow().commit_effect_draft_numeric_input());
    assert(window.workflow().set_effect_draft_preview(true));
    assert(window.root().document().current_snapshot()->revision() == before_apply);
    assert(window.workflow().apply_effect_draft());
    assert(!window.workflow().has_effect_draft());
    assert(window.root().document().current_snapshot()->revision() == before_apply + 1);
    const auto path_shadow = appearance_of(window, path_id).effects;
    const auto text_shadow = appearance_of(window, text_id).effects;
    assert(path_shadow.size() == 1 && text_shadow.size() == 1);
    assert(path_shadow.front().id != text_shadow.front().id);
    assert(std::abs(std::get<pikdraw::document::DropShadowEffect>(
        path_shadow.front().parameters).dx_mm + 3.5) < 1e-9);
    assert(std::abs(std::get<pikdraw::document::DropShadowEffect>(
        text_shadow.front().parameters).opacity - 0.35) < 1e-9);
    assert(window.workflow().undo());
    assert(window.root().document().current_snapshot()->revision() == before_apply + 2);
    assert(appearance_of(window, path_id).effects.empty());
    assert(appearance_of(window, text_id).effects.empty());

    selection.clear();
    selection.select(path_id);
    assert(window.workflow().begin_selected_effect_draft(pikdraw::document::GaussianBlurEffect{1.0, 1.0}));
    selection.clear();
    selection.select(text_id);
    assert(!window.workflow().effect_draft_is_current());
    assert(window.workflow().cancel_stale_effect_draft());
    assert(!window.workflow().has_effect_draft());

    assert(window.workflow().begin_selected_effect_draft(pikdraw::document::GaussianBlurEffect{1.0, 1.0}));
    assert(window.workflow().set_selected_blend_mode(pikdraw::document::BlendMode::Lighten));
    assert(!window.workflow().effect_draft_is_current());
    assert(window.workflow().cancel_stale_effect_draft());
    assert(appearance_of(window, text_id).effects.empty());
}

void test_properties_draft_preview_buttons_and_localized_captures() {
    NativeWindow window(1440, 900);
    const auto path_id = window.workflow().add_rectangle(
        30.0, 36.0, 72.0, 44.0, "#80b8dc", "#28445e", 1.0);
    window.root().vector_controller().selection().select(path_id);
    const auto before = window.root().document().current_snapshot();
    const int panel_left = 1440 - 280;
    window.handle_mouse_down(20, 382); // Open Appearance Properties from the toolbox.
    assert(window.style_inspector_visible());

    window.scroll_style_inspector(-360);
    int scroll = window.style_inspector_scroll_offset();
    const int start_draft_y = 162 + 794 + 24 + 13 - scroll;
    window.handle_mouse_down(panel_left + 70, start_draft_y);
    assert(window.workflow().has_effect_draft());
    assert(window.root().document().current_snapshot() == before);

    window.scroll_style_inspector(-360);
    scroll = window.style_inspector_scroll_offset();
    const int preview_y = 162 + 794 + 72 + 13 - scroll;
    window.handle_mouse_down(panel_left + 60, preview_y);
    assert(window.workflow().effect_draft()->preview_enabled);
    assert(window.root().document().current_snapshot() == before);

    const auto capture_dir = std::filesystem::temp_directory_path() /
                             "pikdraw-effect-draft-test-20261017";
    std::filesystem::create_directories(capture_dir);
    window.set_language(UILanguage::English);
    window.capture_screenshot_bmp((capture_dir / "effect-draft-preview-english.bmp").string());
    window.set_language(UILanguage::Vietnamese);
    window.capture_screenshot_bmp((capture_dir / "effect-draft-preview-vietnamese.bmp").string());
    window.set_language(UILanguage::English);

    scroll = window.style_inspector_scroll_offset();
    const int cancel_y = 162 + 794 + 72 + 13 - scroll;
    window.handle_mouse_down(panel_left + 225, cancel_y);
    assert(!window.workflow().has_effect_draft());
    assert(window.root().document().current_snapshot() == before);
}

void test_drop_shadow_property_bar_ribbon() {
    NativeWindow window(1440, 900);
    const auto path_id = window.workflow().add_rectangle(
        20.0, 20.0, 60.0, 40.0, "#336699", "#112233", 1.0);
    window.root().vector_controller().selection().select(path_id);

    // 1. Activate drop shadow
    window.execute_menu_action(static_cast<int>(MenuActionId::EffectsDropShadow));
    auto appearance = appearance_of(window, path_id);
    assert(appearance.effects.size() == 1);
    assert(std::holds_alternative<pikdraw::document::DropShadowEffect>(appearance.effects.front().parameters));

    // 2. Click Preset button at x = 100, y = 80
    const auto rev_before_preset = window.root().document().current_snapshot()->revision();
    window.handle_mouse_down(100, 80);
    assert(window.root().document().current_snapshot()->revision() > rev_before_preset);
    appearance = appearance_of(window, path_id);
    auto shadow = std::get<pikdraw::document::DropShadowEffect>(appearance.effects.front().parameters);
    assert(std::abs(shadow.dx_mm) > 0.0 || std::abs(shadow.dy_mm) > 0.0);

    // 3. Click Blend Mode button at x = 650, y = 80
    const auto rev_before_blend = window.root().document().current_snapshot()->revision();
    window.handle_mouse_down(650, 80);
    assert(window.root().document().current_snapshot()->revision() > rev_before_blend);
    appearance = appearance_of(window, path_id);
    shadow = std::get<pikdraw::document::DropShadowEffect>(appearance.effects.front().parameters);
    assert(shadow.blend_mode != pikdraw::document::BlendMode::Normal);

    // 4. Click Toggle Object button at x = 820, y = 80
    window.handle_mouse_down(820, 80);

    // 5. Toggle back to shadow ribbon via "◐ Bóng" button
    window.handle_mouse_down(700, 80);

    // 6. Click Clear Shadow button at x = 750, y = 80
    const auto rev_before_clear = window.root().document().current_snapshot()->revision();
    window.handle_mouse_down(750, 80);
    assert(window.root().document().current_snapshot()->revision() > rev_before_clear);
    appearance = appearance_of(window, path_id);
    assert(appearance.effects.empty());

    // 7. Undo restores the shadow
    assert(window.workflow().undo());
    appearance = appearance_of(window, path_id);
    assert(appearance.effects.size() == 1);
}

} // namespace

int main() {
    std::cerr << "Running test 1: test_effect_tool_reactivation_and_shadow_drag" << std::endl;
    test_effect_tool_reactivation_and_shadow_drag();
    std::cerr << "Running test 2: test_non_normal_shadow_must_be_last_enabled_effect" << std::endl;
    test_non_normal_shadow_must_be_last_enabled_effect();
    std::cerr << "Running test 3: test_shadow_blend_property_and_reactivation_edit_existing_node" << std::endl;
    test_shadow_blend_property_and_reactivation_edit_existing_node();
    std::cerr << "Running test 4: test_preview_cancel_apply_and_stale_guards" << std::endl;
    test_preview_cancel_apply_and_stale_guards();
    std::cerr << "Running test 5: test_properties_draft_preview_buttons_and_localized_captures" << std::endl;
    test_properties_draft_preview_buttons_and_localized_captures();
    std::cerr << "Running test 6: test_drop_shadow_property_bar_ribbon" << std::endl;
    test_drop_shadow_property_bar_ribbon();
    std::cout << "[PASS] test_pikdraw_effect_draft\n";
    return 0;
}
