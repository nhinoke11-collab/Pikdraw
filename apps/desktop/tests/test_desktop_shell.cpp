#include "pikdraw/desktop/shell/shell_geometry.hpp"
#include "pikdraw/desktop/shell/workspace_state.hpp"
#include "pikdraw/desktop/shell/status_bar_model.hpp"
#include "pikdraw/desktop/shell/desktop_window.hpp"
#include "pikdraw/typography/typography.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace pikdraw::desktop;

void test_shell_geometry_resolutions_and_dpi() {
    // 1920x1080 @ 100% DPI
    auto area1080 = ShellGeometry::compute_canvas_area(1920.0, 1080.0, 1.0, true, 300.0, 52.0);
    assert(area1080.x == 52.0);
    assert(area1080.y == 72.0); // 38 + 34
    assert(area1080.width == 1920.0 - 52.0 - 300.0);
    assert(area1080.height == 1080.0 - 72.0 - 26.0);

    // 1366x768 @ 100% DPI
    auto area768 = ShellGeometry::compute_canvas_area(1366.0, 768.0, 1.0, true, 280.0, 48.0);
    assert(area768.x == 48.0);
    assert(area768.y == 72.0);
    assert(area768.width == 1366.0 - 48.0 - 280.0);
    assert(area768.height == 768.0 - 72.0 - 26.0);

    // DPI 150% scaling
    auto areaDpi150 = ShellGeometry::compute_canvas_area(1920.0, 1080.0, 1.5, true, 300.0, 52.0);
    assert(areaDpi150.x == 52.0 * 1.5);
    assert(areaDpi150.y == (38.0 + 34.0) * 1.5);
    assert(areaDpi150.width == 1920.0 - (52.0 + 300.0) * 1.5);

    // DPI 200% scaling
    auto areaDpi200 = ShellGeometry::compute_canvas_area(3840.0, 2160.0, 2.0, true, 300.0, 52.0);
    assert(areaDpi200.x == 104.0);
    assert(areaDpi200.y == 144.0);

    std::cout << "[PASS] test_shell_geometry_resolutions_and_dpi\n";
}

void test_workspace_state_and_capabilities() {
    WorkspaceStateManager mgr;
    assert(mgr.active_workspace() == WorkspaceType::Vector);
    assert(mgr.active_info().capability == WorkspaceCapability::Operational);

    // Verify 4 workspaces exist
    const auto& all = mgr.all_workspaces();
    assert(all.size() == 4);

    // Verify non-vector workspaces report M2 placeholder capability
    bool found_inan = false;
    bool found_thicong = false;
    bool found_3d = false;

    for (const auto& ws : all) {
        if (ws.type == WorkspaceType::InAn) {
            found_inan = true;
            assert(ws.capability == WorkspaceCapability::PlaceholderM2);
        } else if (ws.type == WorkspaceType::ThiCong) {
            found_thicong = true;
            assert(ws.capability == WorkspaceCapability::PlaceholderM2);
        } else if (ws.type == WorkspaceType::Scene3D) {
            found_3d = true;
            assert(ws.capability == WorkspaceCapability::PlaceholderM2);
        }
    }

    assert(found_inan && found_thicong && found_3d);

    // Switch workspace
    assert(mgr.switch_workspace(WorkspaceType::InAn));
    assert(mgr.active_workspace() == WorkspaceType::InAn);
    assert(mgr.active_info().title_vi == "In ấn");

    std::cout << "[PASS] test_workspace_state_and_capabilities\n";
}

void test_status_bar_metrics_and_vietnamese_labels() {
    StatusBarModel status;
    status.set_cursor_position(20000.0, 1000.0);
    status.set_zoom_ratio(1.25);
    status.set_selection_count(3);
    status.set_document_state(42, true);

    std::string coords = status.format_coordinates_vi();
    assert(coords.find("20000.00 mm") != std::string::npos);
    assert(coords.find("1000.00 mm") != std::string::npos);

    std::string zoom = status.format_zoom_vi();
    assert(zoom == "125%");

    std::string sel = status.format_selection_vi();
    assert(sel == "Đã chọn 3 đối tượng");

    std::string rev = status.format_document_revision_vi();
    assert(rev.find("Rev 42") != std::string::npos);
    assert(rev.find("Chưa lưu") != std::string::npos);

    std::cout << "[PASS] test_status_bar_metrics_and_vietnamese_labels\n";
}

#include "pikdraw/desktop/shell/native_window.hpp"

void test_native_window_theme_and_settings_state() {
    NativeWindow win(1440, 900);
    assert(win.theme() == ThemeMode::Light);
    assert(win.language() == UILanguage::English);
    assert(!win.is_settings_modal_open());

    win.set_theme(ThemeMode::CyberDark);
    assert(win.theme() == ThemeMode::CyberDark);
    assert(win.settings_theme_choice() == 0);

    win.toggle_theme();
    assert(win.theme() == ThemeMode::Light);

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

    std::cout << "[PASS] test_native_window_theme_and_settings_state\n";
}

void test_multi_document_tabs() {
    NativeWindow win(1440, 900);
    assert(win.tabs().size() == 1);
    assert(win.active_tab_index() == 0);
    assert(win.active_tab().title == "Document 1");
    assert(std::abs(win.active_tab().page_width_mm - 297.0) < 0.1);
    assert(std::abs(win.active_tab().page_height_mm - 210.0) < 0.1);

    // Create a new tab
    win.create_new_tab("Pano Đèn 2x1m", 2000.0, 1000.0, "CMYK", 300);
    assert(win.tabs().size() == 2);
    assert(win.active_tab_index() == 1);
    assert(win.active_tab().title == "Pano Đèn 2x1m");
    assert(std::abs(win.active_tab().page_width_mm - 2000.0) < 0.1);
    assert(std::abs(win.active_tab().page_height_mm - 1000.0) < 0.1);

    // Add rectangle to tab 1
    const auto id1 = win.workflow().add_rectangle(100.0, 100.0, 500.0, 300.0, "#00f2c3", "#0ea5e9", 2.0);
    assert(id1 != pikdraw::document::kInvalidObjectId);
    assert(win.root().document().current_snapshot()->object_count() == 1);

    // Switch back to tab 0
    win.switch_to_tab(0);
    assert(win.active_tab_index() == 0);
    assert(win.root().document().current_snapshot()->object_count() == 0); // Isolated document!

    // Switch to tab 1 again
    win.switch_to_tab(1);
    assert(win.root().document().current_snapshot()->object_count() == 1);

    // Close tab 1
    win.close_tab(1);
    assert(win.tabs().size() == 1);
    assert(win.active_tab_index() == 0);

    std::cout << "[PASS] test_multi_document_tabs\n";
}

void test_page_sizing_and_orientation() {
    NativeWindow win(1440, 900);
    win.set_active_page_size(297.0, 420.0); // A3 portrait
    assert(std::abs(win.active_tab().page_width_mm - 297.0) < 0.1);
    assert(std::abs(win.active_tab().page_height_mm - 420.0) < 0.1);
    assert(std::abs(win.root().document().current_snapshot()->page().width_mm - 297.0) < 0.1);
    assert(std::abs(win.root().document().current_snapshot()->page().height_mm - 420.0) < 0.1);

    // Toggle orientation to landscape
    win.toggle_page_orientation();
    assert(std::abs(win.active_tab().page_width_mm - 420.0) < 0.1);
    assert(std::abs(win.active_tab().page_height_mm - 297.0) < 0.1);
    assert(std::abs(win.root().document().current_snapshot()->page().width_mm - 420.0) < 0.1);
    assert(std::abs(win.root().document().current_snapshot()->page().height_mm - 297.0) < 0.1);

    std::cout << "[PASS] test_page_sizing_and_orientation\n";
}

void test_object_sizing_and_aspect_lock() {
    NativeWindow win(1440, 900);
    const auto id = win.workflow().add_rectangle(10.0, 10.0, 100.0, 50.0, "#00f2c3", "#0ea5e9", 2.0);
    assert(id != pikdraw::document::kInvalidObjectId);
    win.root().vector_controller().selection().select(id);

    // Default: aspect ratio is locked
    assert(win.is_aspect_ratio_locked());

    // Scale W to 200 (delta +100). Since aspect ratio is 2:1 (100:50), H must scale to 100!
    win.resize_selected_object(100.0, 0.0, true);
    auto snapshot = win.root().document().current_snapshot();
    auto obj = snapshot->objects().get(id);
    assert(obj.has_value());
    auto bounds = std::visit([](const auto& val) -> pikdraw::geometry::Rect2D {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, pikdraw::document::PathObject>) return val.bounds();
        return {};
    }, *obj);
    assert(std::abs(bounds.width - 200.0) < 0.5);
    assert(std::abs(bounds.height - 100.0) < 0.5);

    // Unlock aspect ratio
    win.toggle_aspect_ratio_lock();
    assert(!win.is_aspect_ratio_locked());

    // Scale W to 250 (delta +50). H must remain 100!
    win.resize_selected_object(50.0, 0.0, true);
    snapshot = win.root().document().current_snapshot();
    obj = snapshot->objects().get(id);
    bounds = std::visit([](const auto& val) -> pikdraw::geometry::Rect2D {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, pikdraw::document::PathObject>) return val.bounds();
        return {};
    }, *obj);
    assert(std::abs(bounds.width - 250.0) < 0.5);
    assert(std::abs(bounds.height - 100.0) < 0.5);

    // Test Duplicate with offset
    win.workflow().duplicate_selected(15.0);
    snapshot = win.root().document().current_snapshot();
    assert(snapshot->object_count() == 2);

    // Test Delete selected
    win.workflow().delete_selected();
    snapshot = win.root().document().current_snapshot();
    assert(snapshot->object_count() == 1);

    std::cout << "[PASS] test_object_sizing_and_aspect_lock\n";
}

void test_delete_key_shortcut_and_undo_redo() {
    NativeWindow win(1440, 900);
    // Add 3 shapes
    const auto id1 = win.workflow().add_rectangle(10.0, 10.0, 50.0, 50.0, "#00f2c3", "#0ea5e9", 2.0);
    const auto id2 = win.workflow().add_rectangle(70.0, 10.0, 50.0, 50.0, "#00f2c3", "#0ea5e9", 2.0);
    const auto id3 = win.workflow().add_ellipse(150.0, 30.0, 25.0, 25.0, "#00f2c3", "#0ea5e9", 2.0);
    assert(win.root().document().current_snapshot()->object_count() == 3);

    // Multi-select id1 and id2
    auto& sel = win.root().vector_controller().selection();
    sel.select(id1, pikdraw::vector::SelectionMode::Replace);
    sel.select(id2, pikdraw::vector::SelectionMode::Add);
    assert(sel.count() == 2);

    // Press VK_DELETE key
    bool handled = win.handle_key_down(VK_DELETE);
    assert(handled);
    assert(win.root().document().current_snapshot()->object_count() == 1);
    assert(sel.empty());
    assert(win.root().document().current_snapshot()->objects().get(id3).has_value());

    // Undo restores both id1 and id2 atomically in a single step
    win.workflow().undo();
    assert(win.root().document().current_snapshot()->object_count() == 3);
    assert(win.root().document().current_snapshot()->objects().get(id1).has_value());
    assert(win.root().document().current_snapshot()->objects().get(id2).has_value());
    assert(win.root().document().current_snapshot()->objects().get(id3).has_value());

    // Redo re-deletes both id1 and id2
    win.workflow().redo();
    assert(win.root().document().current_snapshot()->object_count() == 1);

    // Test VK_BACK (Backspace) on the remaining object
    sel.select(id3, pikdraw::vector::SelectionMode::Replace);
    assert(sel.count() == 1);
    handled = win.handle_key_down(VK_BACK);
    assert(handled);
    assert(win.root().document().current_snapshot()->object_count() == 0);

    // Pressing Delete when selection is empty is a safe no-op
    handled = win.handle_key_down(VK_DELETE);
    assert(handled);
    assert(win.root().document().current_snapshot()->object_count() == 0);

    std::cout << "[PASS] test_delete_key_shortcut_and_undo_redo\n";
}

void test_group_ungroup_and_isolation_mode() {
    pikdraw::desktop::NativeWindow win(1440, 900);
    auto& workflow = win.workflow();
    auto& controller = win.root().vector_controller();
    auto& sel = controller.selection();

    // 1. Create two shapes
    const auto id1 = workflow.add_rectangle(20.0, 30.0, 100.0, 50.0);
    const auto id2 = workflow.add_ellipse(160.0, 55.0, 40.0, 40.0);
    assert(id1 != pikdraw::document::kInvalidObjectId);
    assert(id2 != pikdraw::document::kInvalidObjectId);

    // 2. Select both shapes and group them
    sel.clear();
    sel.select(id1, pikdraw::vector::SelectionMode::Add);
    sel.select(id2, pikdraw::vector::SelectionMode::Add);
    assert(sel.count() == 2);

    const auto gid = workflow.group_selected();
    assert(gid != pikdraw::document::kInvalidObjectId);
    assert(workflow.is_group(gid));
    assert(!workflow.is_group(id1));
    assert(!workflow.is_group(id2));

    // Selection now replaced with the group
    assert(sel.count() == 1);
    assert(sel.is_selected(gid));

    // Verify parent group lookup
    assert(workflow.find_parent_group(id1) == gid);
    assert(workflow.find_parent_group(id2) == gid);

    // Verify composite bounds of the group
    const auto b1 = workflow.get_object_bounds(id1);
    const auto b2 = workflow.get_object_bounds(id2);
    const auto gb = workflow.get_object_bounds(gid);
    assert(b1.has_value() && b2.has_value() && gb.has_value());
    assert(gb->x <= b1->x && gb->x <= b2->x);
    assert(gb->width >= b1->width);

    // 3. Test Group Isolation Mode
    assert(!win.is_in_isolation_mode());
    win.enter_group_isolation(gid);
    assert(win.is_in_isolation_mode());
    assert(win.current_isolated_group_id() == gid);
    assert(win.is_object_in_isolated_group(id1));
    assert(win.is_object_in_isolated_group(id2));

    // Create an object outside the group
    const auto id3 = workflow.add_rectangle(300.0, 20.0, 50.0, 50.0);
    assert(!win.is_object_in_isolated_group(id3));

    // In isolation mode, child of group is selected for direct editing
    sel.select(id1, pikdraw::vector::SelectionMode::Replace);
    assert(sel.is_selected(id1));

    // 4. Test Exit Isolation Mode via Esc key
    bool esc_handled = win.handle_key_down(VK_ESCAPE);
    assert(esc_handled);
    assert(!win.is_in_isolation_mode());
    assert(sel.is_selected(gid));

    // 5. Test Ungrouping
    bool ungrouped = workflow.ungroup_selected();
    assert(ungrouped);
    assert(!workflow.is_group(gid));
    assert(sel.is_selected(id1));
    assert(sel.is_selected(id2));

    // 6. Test Undo restores group
    bool undo_ok = workflow.undo();
    assert(undo_ok);
    assert(workflow.is_group(gid));

    // 7. Test Redo ungroups again
    bool redo_ok = workflow.redo();
    assert(redo_ok);
    assert(!workflow.is_group(gid));

    std::cout << "[PASS] test_group_ungroup_and_isolation_mode\n";
}

void test_solid_tools_boolean_knife_and_shaping_docker() {
    NativeWindow win(1440, 900);
    auto& workflow = win.workflow();
    auto& sel = win.root().vector_controller().selection();

    // 1. Stage 1: Boolean Weld (Union)
    const auto r1 = workflow.add_rectangle(40.0, 40.0, 80.0, 60.0);
    const auto r2 = workflow.add_rectangle(80.0, 60.0, 70.0, 60.0);
    sel.clear();
    sel.select(r1, pikdraw::vector::SelectionMode::Add);
    sel.select(r2, pikdraw::vector::SelectionMode::Add);
    assert(sel.count() == 2);

    bool weld_ok = win.trigger_boolean_operation(pikdraw::geometry::BooleanOp::Union);
    assert(weld_ok);
    assert(sel.count() == 1);
    const auto welded_id = sel.selected_ids().front();
    auto welded_obj = win.root().document().current_snapshot()->objects().get(welded_id);
    assert(welded_obj.has_value());

    // Test Undo restores original shapes
    assert(workflow.undo());
    assert(win.root().document().current_snapshot()->objects().get(r1).has_value());
    assert(win.root().document().current_snapshot()->objects().get(r2).has_value());

    // 2. Stage 1: Boolean Trim (Difference)
    sel.clear();
    sel.select(r1, pikdraw::vector::SelectionMode::Add);
    sel.select(r2, pikdraw::vector::SelectionMode::Add);
    bool trim_ok = win.trigger_boolean_operation(pikdraw::geometry::BooleanOp::Difference);
    assert(trim_ok);
    assert(sel.count() == 1);
    const auto trimmed_id = sel.selected_ids().front();
    auto trimmed_obj = win.root().document().current_snapshot()->objects().get(trimmed_id);
    assert(trimmed_obj.has_value());

    // 3. Stage 2: Interactive Knife / Split Tool
    workflow.undo(); // back to r1 and r2
    sel.clear();
    sel.select(r1);
    // Cut r1 (40, 40, 80, 60) with a vertical line at x = 70 (from (70, 20) to (70, 120))
    bool cut_ok = workflow.split_objects_by_line({70.0, 20.0}, {70.0, 120.0});
    assert(cut_ok);
    // r1 is now split into 2 pieces: one width 30 (40..70), one width 50 (70..120)
    assert(sel.count() == 2);
    auto b0 = workflow.get_object_bounds(sel.selected_ids()[0]);
    auto b1 = workflow.get_object_bounds(sel.selected_ids()[1]);
    assert(b0.has_value() && b1.has_value());
    double min_w = std::min(b0->width, b1->width);
    double max_w = std::max(b0->width, b1->width);
    assert(std::abs(min_w - 30.0) < 1e-4);
    assert(std::abs(max_w - 50.0) < 1e-4);

    // Test Undo restores unsplit r1
    assert(workflow.undo());
    assert(win.root().document().current_snapshot()->objects().get(r1).has_value());

    // 4. Stage 3: Shaping Docker Options (Keep Source & Keep Target)
    win.set_keep_source_object(true);
    win.set_keep_target_object(true);
    assert(win.keep_source_object());
    assert(win.keep_target_object());

    sel.clear();
    sel.select(r1, pikdraw::vector::SelectionMode::Add);
    sel.select(r2, pikdraw::vector::SelectionMode::Add);
    bool inter_ok = win.trigger_boolean_operation(pikdraw::geometry::BooleanOp::Intersection);
    assert(inter_ok);
    // Because keep_source and keep_target are true, r1, r2, and intersection all remain!
    assert(win.root().document().current_snapshot()->objects().get(r1).has_value());
    assert(win.root().document().current_snapshot()->objects().get(r2).has_value());
    assert(win.root().document().current_snapshot()->object_count() == 3);

    // 5. Stage 3: CNC Auto-Weld Helper
    bool auto_weld_ok = win.trigger_auto_weld();
    assert(auto_weld_ok);

    std::cout << "[PASS] test_solid_tools_boolean_knife_and_shaping_docker\n";
}

void test_vector_and_bitmap_interactions() {
    NativeWindow win(1440, 900);
    auto& workflow = win.workflow();
    auto& sel = win.root().vector_controller().selection();

    // 1. Add Procedural Bitmap Photo Object
    const auto bmp_id = workflow.add_bitmap_image(30.0, 40.0, 100.0, 80.0, "Test Landscape");
    assert(bmp_id != pikdraw::document::kInvalidObjectId);
    auto bmp_obj = win.root().document().current_snapshot()->objects().get(bmp_id);
    assert(bmp_obj.has_value());
    assert(std::holds_alternative<pikdraw::document::ImageObject>(*bmp_obj));
    const auto& img = std::get<pikdraw::document::ImageObject>(*bmp_obj);
    assert(img.width_px == 160 && img.height_px == 120);
    assert(img.pixels_rgba.size() == 160 * 120);

    // 2. Knife Split on Bitmap with 30mm Separation
    sel.clear();
    sel.select(bmp_id);
    bool split_ok = workflow.split_objects_by_line({70.0, 20.0}, {70.0, 140.0}, 30.0);
    assert(split_ok);
    assert(sel.count() == 2);

    auto piece1_b = workflow.get_object_bounds(sel.selected_ids()[0]);
    auto piece2_b = workflow.get_object_bounds(sel.selected_ids()[1]);
    assert(piece1_b.has_value() && piece2_b.has_value());
    // Verify physical gap between the two separated bitmap pieces
    double left_right_edge = std::min(piece1_b->right(), piece2_b->right());
    double right_left_edge = std::max(piece1_b->x, piece2_b->x);
    double gap = right_left_edge - left_right_edge;
    assert(gap >= 29.0); // Exact 30mm separation gap

    // 3. Undo restores original bitmap
    assert(workflow.undo());
    assert(win.root().document().current_snapshot()->objects().get(bmp_id).has_value());

    // 4. Vector on Bitmap Cookie-Cutter / Trim
    const auto cutter_id = workflow.add_ellipse(60.0, 60.0, 20.0, 20.0, "#00f2c3", "#0ea5e9", 2.0);
    assert(cutter_id != pikdraw::document::kInvalidObjectId);
    bool trim_ok = workflow.trim_bitmap_with_vector(bmp_id, cutter_id, 25.0);
    assert(trim_ok);

    std::cout << "[PASS] test_vector_and_bitmap_interactions\n";
}

void test_typography_inline_editing_ribbon_and_table() {
    NativeWindow win(1440, 900);
    auto& workflow = win.workflow();
    auto& sel = win.root().vector_controller().selection();

    // ----------------------------------------------------
    // Item 1: UI & Vector Text Object Interaction
    // ----------------------------------------------------
    win.set_active_tool(6);
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
    bool found_combining = false;
    for (const auto& g : run_nfd.glyphs) {
        if (g.glyph_id == 0x0309) {
            found_combining = true;
            assert(std::abs(g.x_advance_mm) < 1e-9);
        }
    }
    assert(found_combining);

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
    win.capture_screenshot_bmp("docs/architecture/ui-reference/m1_pikdraw_text_inline_editing.bmp");
    win.commit_inline_text_editing();
    assert(!win.is_inline_editing());

    // 4c. CorelDRAW-Style Vector Table Tool
    auto table_id = workflow.create_table(30.0, 35.0, 180.0, 80.0, 3, 4, "#0ea5e9", "#f8fafc", 0.8);
    assert(table_id != pikdraw::document::kInvalidObjectId);
    assert(workflow.is_group(table_id));
    sel.clear();
    sel.select(table_id);
    win.capture_screenshot_bmp("docs/architecture/ui-reference/m1_pikdraw_corel_table.bmp");

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
    win.capture_screenshot_bmp("docs/architecture/ui-reference/m1_pikdraw_text_curves.bmp");

    std::cout << "[PASS] test_typography_inline_editing_ribbon_and_table\n";
}

void test_align_and_distribute_tools() {
    NativeWindow win(1440, 900);
    auto& workflow = win.workflow();
    auto& sel = win.root().vector_controller().selection();

    // 1. Create 3 distinct objects for alignment & distribution testing
    const auto r1 = workflow.add_rectangle(20.0, 30.0, 40.0, 20.0, "#00f2c3", "#0ea5e9", 1.0);
    const auto r2 = workflow.add_rectangle(70.0, 60.0, 30.0, 30.0, "#eab308", "#f59e0b", 1.0);
    const auto r3 = workflow.add_rectangle(120.0, 40.0, 50.0, 25.0, "#ef4444", "#dc2626", 1.0);
    assert(r1 != pikdraw::document::kInvalidObjectId);
    assert(r2 != pikdraw::document::kInvalidObjectId);
    assert(r3 != pikdraw::document::kInvalidObjectId);

    sel.clear();
    sel.select(r1, pikdraw::vector::SelectionMode::Add);
    sel.select(r2, pikdraw::vector::SelectionMode::Add);
    sel.select(r3, pikdraw::vector::SelectionMode::Add);
    assert(sel.count() == 3);

    // Initial screenshot with selection showing Align Ribbon and Inspector Align Docker
    win.capture_screenshot_bmp("docs/architecture/ui-reference/m1_pikdraw_align_ribbon_and_docker.bmp");

    // 2. Test Align Left (L)
    win.align_selected_objects(pikdraw::geometry::AlignMode::Left);
    auto b1 = workflow.get_object_bounds(r1);
    auto b2 = workflow.get_object_bounds(r2);
    auto b3 = workflow.get_object_bounds(r3);
    assert(b1 && b2 && b3);
    assert(std::abs(b1->x - 20.0) < 1e-4);
    assert(std::abs(b2->x - 20.0) < 1e-4);
    assert(std::abs(b3->x - 20.0) < 1e-4);

    // Test 1-step Undo
    assert(workflow.undo());
    b1 = workflow.get_object_bounds(r1);
    b2 = workflow.get_object_bounds(r2);
    b3 = workflow.get_object_bounds(r3);
    assert(std::abs(b1->x - 20.0) < 1e-4);
    assert(std::abs(b2->x - 70.0) < 1e-4);
    assert(std::abs(b3->x - 120.0) < 1e-4);

    // 3. Test Align Top (T)
    win.align_selected_objects(pikdraw::geometry::AlignMode::Top);
    b1 = workflow.get_object_bounds(r1);
    b2 = workflow.get_object_bounds(r2);
    b3 = workflow.get_object_bounds(r3);
    assert(std::abs(b1->y - 30.0) < 1e-4);
    assert(std::abs(b2->y - 30.0) < 1e-4);
    assert(std::abs(b3->y - 30.0) < 1e-4);
    assert(workflow.undo());

    // 4. Test Align Centers Horizontally (C)
    // Union X range: [20, 120+50=170], center = 95.0
    win.align_selected_objects(pikdraw::geometry::AlignMode::CenterHorizontal);
    b1 = workflow.get_object_bounds(r1);
    b2 = workflow.get_object_bounds(r2);
    b3 = workflow.get_object_bounds(r3);
    assert(std::abs(b1->center().x - 95.0) < 1e-4);
    assert(std::abs(b2->center().x - 95.0) < 1e-4);
    assert(std::abs(b3->center().x - 95.0) < 1e-4);
    assert(workflow.undo());

    // 5. Test Align to Key Object
    win.set_align_reference(pikdraw::geometry::AlignReference::KeyObject);
    win.set_key_object(r2);
    win.align_selected_objects(pikdraw::geometry::AlignMode::Left);
    b1 = workflow.get_object_bounds(r1);
    b2 = workflow.get_object_bounds(r2);
    b3 = workflow.get_object_bounds(r3);
    // Key object R2 stayed at 70.0, R1 and R3 aligned to 70.0
    assert(std::abs(b2->x - 70.0) < 1e-4);
    assert(std::abs(b1->x - 70.0) < 1e-4);
    assert(std::abs(b3->x - 70.0) < 1e-4);
    assert(workflow.undo());
    win.set_align_reference(pikdraw::geometry::AlignReference::Selection);
    win.set_key_object(std::nullopt);

    // 6. Test Center to Page (P)
    // Single object center to page
    sel.clear();
    sel.select(r1);
    win.align_selected_objects(pikdraw::geometry::AlignMode::CenterPage);
    b1 = workflow.get_object_bounds(r1);
    // Page is 297 x 210 -> center is (148.5, 105.0)
    assert(std::abs(b1->center().x - 148.5) < 1e-4);
    assert(std::abs(b1->center().y - 105.0) < 1e-4);
    assert(workflow.undo());

    // Multiple objects collective Center to Page
    sel.clear();
    sel.select(r1, pikdraw::vector::SelectionMode::Add);
    sel.select(r2, pikdraw::vector::SelectionMode::Add);
    sel.select(r3, pikdraw::vector::SelectionMode::Add);
    win.align_selected_objects(pikdraw::geometry::AlignMode::CenterPage);
    b1 = workflow.get_object_bounds(r1);
    b2 = workflow.get_object_bounds(r2);
    b3 = workflow.get_object_bounds(r3);
    auto union_box = b1->united(*b2).united(*b3);
    assert(std::abs(union_box.center().x - 148.5) < 1e-4);
    assert(std::abs(union_box.center().y - 105.0) < 1e-4);
    win.capture_screenshot_bmp("docs/architecture/ui-reference/m1_pikdraw_align_left_and_page.bmp");
    assert(workflow.undo());

    // 7. Test Distribute Spacing with Specified Gap (10.0 mm)
    win.set_distribute_spacing_mm(10.0);
    win.apply_specified_spacing(true);
    b1 = workflow.get_object_bounds(r1);
    b2 = workflow.get_object_bounds(r2);
    b3 = workflow.get_object_bounds(r3);
    // Sorted left-to-right: R1 (x: 20, w: 40 -> right: 60)
    // R2 target left: 60 + 10 = 70 (w: 30 -> right: 100)
    // R3 target left: 100 + 10 = 110 (w: 50 -> right: 160)
    assert(std::abs(b2->x - b1->right() - 10.0) < 1e-4);
    assert(std::abs(b3->x - b2->right() - 10.0) < 1e-4);
    win.capture_screenshot_bmp("docs/architecture/ui-reference/m1_pikdraw_distribute_spacing.bmp");
    assert(workflow.undo());

    // 8. Test Ctrl+Shift+A Shortcut to activate Inspector & Align Properties panel
    win.set_active_dock_tab(2); // Switch away to History tab
    assert(win.active_dock_tab() == 2);
    win.open_inspector_properties_panel(); // Simulates Ctrl+Shift+A
    assert(win.active_dock_tab() == 0); // Immediately focused to Inspector tab!
    assert(win.status_message().find("Inspector") != std::string::npos ||
           win.status_message().find("Thuộc tính") != std::string::npos);

    std::cout << "[PASS] test_align_and_distribute_tools\n";
}

void test_compact_icons_and_hover_tooltips() {
    NativeWindow win(1440, 900);

    // 1. Hover over Top Bar: New Document button (x: 170, y: 15)
    win.set_language(UILanguage::Vietnamese);
    win.update_hover_tooltip(170, 15);
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"Tạo tài liệu mới") != std::wstring::npos);
    assert(win.tooltip().text.find(L"Ctrl+N") != std::wstring::npos);

    // Switch to English: Tooltip changes language immediately
    win.set_language(UILanguage::English);
    win.update_hover_tooltip(170, 15);
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"Create new vector document") != std::wstring::npos);
    assert(win.tooltip().text.find(L"Ctrl+N") != std::wstring::npos);

    // 2. Hover over Left Tool Strip: Tool 0 (Pick Tool), Tool 4 (Text Tool), Tool 5 (Knife Tool)
    win.set_language(UILanguage::Vietnamese);
    win.update_hover_tooltip(20, 115); // Tool 0 (content_top + 10 = 112)
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"chọn") != std::wstring::npos);
    assert(win.tooltip().text.find(L"Phím V") != std::wstring::npos);

    win.update_hover_tooltip(20, 115 + 4 * 42); // Tool 4 (Line Tool)
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"đoạn thẳng") != std::wstring::npos);

    win.update_hover_tooltip(20, 115 + 5 * 42); // Tool 5 (Polyline Tool)
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"gấp khúc") != std::wstring::npos);

    win.update_hover_tooltip(20, 115 + 6 * 42); // Tool 6 (Text Tool)
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"văn bản") != std::wstring::npos);
    assert(win.tooltip().text.find(L"F8") != std::wstring::npos);

    win.update_hover_tooltip(20, 115 + 7 * 42); // Tool 7 (Knife Tool)
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"Dao cắt") != std::wstring::npos);
    assert(win.tooltip().text.find(L"Phím K") != std::wstring::npos);

    // 3. Hover over Ribbon: Text Mode (Convert to Curves `⤹ Q`, Table `▦ Table`)
    win.set_active_tool(6); // Text Tool active
    win.update_hover_tooltip(580, 80); // Curves button (556 to 616)
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"đường cong") != std::wstring::npos);
    assert(win.tooltip().text.find(L"Ctrl+Q") != std::wstring::npos);

    win.update_hover_tooltip(650, 80); // Table button (622 to 692)
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"bảng") != std::wstring::npos);

    // 4. Hover over Inspector Docker: Align Left (L), Center to Page (P)
    int insp_x = 1440 - 280;
    int content_y = 38 + 28 + 36 + 36;
    int by3 = content_y + 248 + 30 + 32;
    int by_align_hdr = by3 + 36;
    int by_a1 = by_align_hdr + 24;
    int by_a3 = by_a1 + 60;

    win.update_hover_tooltip(insp_x + 30, by_a1 + 10); // Align Left button
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"Căn lề trái") != std::wstring::npos);
    assert(win.tooltip().text.find(L"L") != std::wstring::npos);

    win.update_hover_tooltip(insp_x + 30, by_a3 + 10); // Center to Page button
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"Căn giữa") != std::wstring::npos);
    assert(win.tooltip().text.find(L"P") != std::wstring::npos);

    // 5. English Mode for Inspector Align buttons
    win.set_language(UILanguage::English);
    win.update_hover_tooltip(insp_x + 30, by_a1 + 10);
    assert(win.tooltip().visible);
    assert(win.tooltip().text.find(L"Left") != std::wstring::npos);

    // 6. Hovering empty canvas clears tooltip
    win.update_hover_tooltip(500, 500);
    assert(!win.tooltip().visible);
    assert(win.tooltip().text.empty());

    // 7. Capture visual proof of compact buttons and hover tooltip popover
    win.set_language(UILanguage::Vietnamese);
    win.update_hover_tooltip(insp_x + 40, by_a1 + 10); // Hover over Align Left
    win.capture_screenshot_bmp("docs/architecture/ui-reference/m1_pikdraw_compact_icons_hover_tooltip.bmp");

    // Also capture Text Ribbon with hover tooltip over ⤹ Q
    win.set_active_tool(6);
    win.update_hover_tooltip(585, 80);
    win.capture_screenshot_bmp("docs/architecture/ui-reference/m1_pikdraw_compact_text_ribbon_hover.bmp");

    // 8. Capture visual proof of multiple selected objects with compact ribbon buttons
    win.set_active_tool(0); // Pick tool
    auto r1 = win.workflow().add_rectangle(50.0, 50.0, 80.0, 60.0, "#00f2c3", "#0ea5e9", 1.0);
    auto r2 = win.workflow().add_rectangle(150.0, 70.0, 100.0, 80.0, "#eab308", "#f59e0b", 1.0);
    win.root().vector_controller().selection().clear();
    win.root().vector_controller().selection().select(r1, pikdraw::vector::SelectionMode::Add);
    win.root().vector_controller().selection().select(r2, pikdraw::vector::SelectionMode::Add);
    win.set_language(UILanguage::Vietnamese);
    win.update_hover_tooltip(642, 80); // Hover over |◀ L on object ribbon
    win.capture_screenshot_bmp("docs/architecture/ui-reference/m1_pikdraw_compact_object_ribbon_hover.bmp");

    std::cout << "[PASS] test_compact_icons_and_hover_tooltips\n";
}

void test_canvas_rulers_and_guidelines() {
    NativeWindow win(1440, 900);
    const auto area = win.canvas_rect(1440, 900);

    // Initial state
    assert(win.guideline_count() == 0);
    assert(win.show_guidelines());

    // 1. Drag Horizontal Guideline from Top Ruler (x: 300, y: 110 inside top ruler)
    win.handle_mouse_down(300, 110);
    assert(win.is_dragging_guideline());
    assert(win.drag_guideline_orientation() == NativeWindow::GuidelineOrientation::Horizontal);
    assert(win.show_guidelines());

    // Move to Y=220 on canvas
    win.handle_mouse_move(300, 220);
    assert(win.is_dragging_guideline());
    // Capture visual proof of live dragging guideline with floating coordinate badge
    win.capture_screenshot_bmp("docs/architecture/ui-reference/m1_pikdraw_guidelines_drag_active.bmp");

    // Release to drop guideline onto canvas
    win.handle_mouse_up(300, 220);
    assert(!win.is_dragging_guideline());
    assert(win.guideline_count() == 1);
    assert(win.guidelines()[0].orientation == NativeWindow::GuidelineOrientation::Horizontal);

    // 2. Drag Vertical Guideline from Left Ruler (x: 60, y: 300 inside left ruler)
    win.handle_mouse_down(60, 300);
    assert(win.is_dragging_guideline());
    assert(win.drag_guideline_orientation() == NativeWindow::GuidelineOrientation::Vertical);

    // Move to X=350 on canvas and drop
    win.handle_mouse_move(350, 300);
    win.handle_mouse_up(350, 300);
    assert(!win.is_dragging_guideline());
    assert(win.guideline_count() == 2);
    assert(win.guidelines()[1].orientation == NativeWindow::GuidelineOrientation::Vertical);

    // 3. Add more guidelines to test multiple guidelines on canvas
    win.add_guideline(NativeWindow::GuidelineOrientation::Horizontal, 80.0);
    win.add_guideline(NativeWindow::GuidelineOrientation::Horizontal, 140.0);
    win.add_guideline(NativeWindow::GuidelineOrientation::Vertical, 180.0);
    assert(win.guideline_count() == 5);

    // Add some vector objects so we see the guidelines aligned with design elements
    win.workflow().add_rectangle(50.0, 50.0, 100.0, 70.0, "#00f2c3", "#0ea5e9", 1.5);
    win.workflow().add_ellipse(200.0, 90.0, 40.0, 40.0, "#f59e0b", "#d97706", 1.5);

    // Capture visual proof of multiple guidelines (cyan dashed lines, coordinate tags, ruler markers)
    win.capture_screenshot_bmp("docs/architecture/ui-reference/m1_pikdraw_guidelines_multiple.bmp");

    // 4. Test File Menu Toggle and Clear
    win.open_file_menu();
    assert(win.is_file_menu_open());
    // Capture visual proof of File Menu with Toggle Guidelines & Clear All Guidelines (English)
    win.capture_screenshot_bmp("docs/architecture/ui-reference/m1_pikdraw_guidelines_menu_toggle.bmp");
    win.close_file_menu();

    // Vietnamese language mode
    win.set_language(UILanguage::Vietnamese);
    win.open_file_menu();
    win.capture_screenshot_bmp("docs/architecture/ui-reference/m1_pikdraw_guidelines_menu_toggle_vi.bmp");

    // Click Toggle Guidelines in File Menu (x=450, y = 38 + 160)
    win.handle_mouse_down(450, 38 + 160);
    assert(!win.is_file_menu_open());
    assert(!win.show_guidelines()); // Hidden!

    // Toggle again via method / shortcut
    win.toggle_guidelines();
    assert(win.show_guidelines()); // Visible again!

    // Click Clear Guidelines in File Menu (x=450, y = 38 + 190)
    win.open_file_menu();
    win.handle_mouse_down(450, 38 + 190);
    assert(!win.is_file_menu_open());
    assert(win.guideline_count() == 0); // All guidelines cleared!

    std::cout << "[PASS] test_canvas_rulers_and_guidelines\n";
}

void test_canvas_workspace_and_object_size_limits() {
    NativeWindow win(1440, 900);
    win.workflow().new_document(297.0, 210.0); // A4 page
    win.fit_page_to_canvas();

    // 1. Add object inside page
    auto id_in = win.workflow().add_rectangle(40.0, 40.0, 80.0, 60.0, "#00f2c3", "#0ea5e9", 1.5);
    assert(id_in != pikdraw::document::kInvalidObjectId);

    // 2. Add object OUTSIDE page to the right on the white canvas workspace (Pasteboard)
    auto id_out_right = win.workflow().add_rectangle(350.0, 50.0, 90.0, 70.0, "#f59e0b", "#d97706", 1.5);
    assert(id_out_right != pikdraw::document::kInvalidObjectId);

    // 3. Add object OUTSIDE page below on the white canvas workspace
    auto id_out_bottom = win.workflow().add_ellipse(150.0, 300.0, 40.0, 30.0, "#ef4444", "#dc2626", 1.5);
    assert(id_out_bottom != pikdraw::document::kInvalidObjectId);

    // Verify all 3 objects exist and can be selected
    auto snap = win.root().document().current_snapshot();
    assert(snap->object_count() == 3);
    win.root().vector_controller().selection().select(id_out_right);
    assert(win.root().vector_controller().selection().is_selected(id_out_right));

    // Capture visual proof of white canvas workspace outside page with objects placed on it
    win.capture_screenshot_bmp("docs/architecture/ui-reference/m1_pikdraw_canvas_white_workspace.bmp");

    // 4. Test 40-Meter (40,000 mm) Object Size Limit
    // Attempt to resize an object beyond 40 meters
    if (win.is_aspect_ratio_locked()) {
        win.toggle_aspect_ratio_lock(); // unlock aspect ratio
    }
    win.resize_selected_object(60000.0, 60000.0, false);
    auto b_res = win.workflow().get_object_bounds(id_out_right);
    assert(b_res.has_value());
    assert(b_res->width <= pikdraw::geometry::kMaxObjectDimensionMm + 0.1);
    assert(b_res->height <= pikdraw::geometry::kMaxObjectDimensionMm + 0.1);

    // Attempt to create an oversized rectangle beyond 40m
    auto id_huge = win.workflow().add_rectangle(0.0, 0.0, 80000.0, 75000.0, "#00f2c3", "#0ea5e9", 2.0);
    auto b_huge = win.workflow().get_object_bounds(id_huge);
    assert(b_huge.has_value());
    assert(b_huge->width <= pikdraw::geometry::kMaxObjectDimensionMm + 0.1);
    assert(b_huge->height <= pikdraw::geometry::kMaxObjectDimensionMm + 0.1);

    // 5. Test 40-Meter Canvas Workspace Page Limit
    // Attempt to set page size beyond 40 meters (40,000 mm)
    win.set_active_page_size(50000.0, 60000.0);
    assert(win.active_tab().page_width_mm <= pikdraw::geometry::kMaxCanvasDimensionMm);
    assert(win.active_tab().page_height_mm <= pikdraw::geometry::kMaxCanvasDimensionMm);
    assert(win.root().document().current_snapshot()->page().width_mm <= pikdraw::geometry::kMaxCanvasDimensionMm);

    // Set page to maximum 40m billboard (40,000 × 5,000 mm pano)
    win.set_active_page_size(40000.0, 5000.0);
    win.fit_page_to_canvas();
    win.capture_screenshot_bmp("docs/architecture/ui-reference/m1_pikdraw_canvas_40m_billboard.bmp");

    std::cout << "[PASS] test_canvas_workspace_and_object_size_limits\n";
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
    win.capture_screenshot_bmp("docs/architecture/ui-reference/m1_pikdraw_numeric_input_page_ribbon.bmp");

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
    win.capture_screenshot_bmp("docs/architecture/ui-reference/m1_pikdraw_numeric_input_object_ribbon.bmp");

    // Capture Focused State screenshot with cyan highlight and cursor |
    win.start_numeric_input(NativeWindow::NumericFieldId::ObjectWidth);
    win.capture_screenshot_bmp("docs/architecture/ui-reference/m1_pikdraw_numeric_input_focused.bmp");
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
    win.capture_screenshot_bmp("docs/architecture/ui-reference/m1_pikdraw_new_doc_clean_inputs.bmp");
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
    win.capture_screenshot_bmp("docs/architecture/ui-reference/m1_pikdraw_page_preset_dropdown_open.bmp");

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
    win.capture_screenshot_bmp("docs/architecture/ui-reference/m1_pikdraw_page_preset_dropdown_vi.bmp");
    win.close_page_preset_dropdown();

    // Capture visual proof of Compact Ribbon with Preset Combobox (Collapsed)
    win.capture_screenshot_bmp("docs/architecture/ui-reference/m1_pikdraw_page_ribbon_compact_combobox.bmp");

    std::cout << "[PASS] test_page_preset_dropdown_and_modal_row_clicks\n";
}

int main() {
    std::cout << "=== Running Desktop Shell Tests ===\n";
    test_shell_geometry_resolutions_and_dpi();
    test_workspace_state_and_capabilities();
    test_status_bar_metrics_and_vietnamese_labels();
    test_native_window_theme_and_settings_state();
    test_multi_document_tabs();
    test_page_sizing_and_orientation();
    test_object_sizing_and_aspect_lock();
    test_delete_key_shortcut_and_undo_redo();
    test_group_ungroup_and_isolation_mode();
    test_solid_tools_boolean_knife_and_shaping_docker();
    test_vector_and_bitmap_interactions();
    test_typography_inline_editing_ribbon_and_table();
    test_align_and_distribute_tools();
    test_compact_icons_and_hover_tooltips();
    test_canvas_rulers_and_guidelines();
    test_canvas_workspace_and_object_size_limits();
    test_numeric_keyboard_input_property_bar();
    test_page_preset_dropdown_and_modal_row_clicks();
    std::cout << "All Desktop Shell Tests PASSED.\n";
    return 0;
}

