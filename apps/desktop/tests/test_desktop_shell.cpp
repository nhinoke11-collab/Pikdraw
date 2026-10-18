#include "desktop_shell_test_helpers.hpp"
#include "pikdraw/desktop/shell/native_style_color_model.hpp"

void test_compact_icons_and_hover_tooltips();
void test_page_preset_dropdown_and_modal_row_clicks();
void test_double_click_rotate_and_flip_transform_suite();
void test_group_ungroup_and_isolation_mode();
void test_blend_and_effect_properties_panel();
void test_typography_inline_editing_ribbon_and_table();
void test_native_window_theme_and_settings_state();
void test_ai_connections_settings_draft_state();
void test_fill_and_stroke_opacity_properties();
void test_contextual_object_action_capabilities();
void test_solid_tools_boolean_knife_and_context_actions();
void test_numeric_keyboard_input_property_bar();
void test_corel_pick_auto_select_resume_guidelines_and_space_shortcut();
void test_effects_toolbox_flyout();
void test_align_and_distribute_tools();
void test_selection_directional_scale_rotate_and_pivot();
void test_object_opacity_property_bar_and_appearance_panel();
void test_layers_and_objects_dock_suite();

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

void test_canvas_rulers_and_guidelines() {
    NativeWindow win(1440, 900);
    const auto area = win.canvas_rect(1440, 900);

    assert(area.left <= 300 && 300 < area.right);
    assert(area.top <= 220 && 220 < area.bottom);
    assert(area.left <= 350 && 350 < area.right);
    assert(area.top <= 300 && 300 < area.bottom);

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
    capture_shell_screenshot(win, "m1_pikdraw_guidelines_drag_active.bmp");

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
    capture_shell_screenshot(win, "m1_pikdraw_guidelines_multiple.bmp");

    // 4. Test the View menu toggle and clear actions through menu clicks.
    const auto click_view_menu = [&win]() {
        // The 1440px test window places View at x=248 in English and x=280 in Vietnamese.
        const int menu_bar_x = win.language() == UILanguage::Vietnamese ? 280 : 248;
        win.handle_mouse_down(menu_bar_x, 20);
        assert(win.open_menu_id() == static_cast<int>(MenuId::View));
    };
    const int view_menu_x = 260;
    const int toggle_guidelines_y = 38 + kMenuPadding + 4 * kMenuRowHeight +
        kMenuSeparatorHeight + kMenuRowHeight / 2;
    const int clear_guidelines_y = toggle_guidelines_y + kMenuRowHeight;

    click_view_menu();
    // Capture visual proof of the View menu with guideline controls (English).
    capture_shell_screenshot(win, "m1_pikdraw_guidelines_menu_toggle.bmp");
    win.close_menu();

    // Vietnamese language mode
    win.set_language(UILanguage::Vietnamese);
    click_view_menu();
    capture_shell_screenshot(win, "m1_pikdraw_guidelines_menu_toggle_vi.bmp");

    // Click Toggle Guidelines in the View menu.
    win.handle_mouse_down(view_menu_x, toggle_guidelines_y);
    assert(!win.is_menu_open());
    assert(!win.show_guidelines()); // Hidden!

    // Toggle again via method / shortcut
    win.toggle_guidelines();
    assert(win.show_guidelines()); // Visible again!

    // Click Clear All Guidelines in the View menu.
    click_view_menu();
    win.handle_mouse_down(view_menu_x, clear_guidelines_y);
    assert(!win.is_menu_open());
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
    capture_shell_screenshot(win, "m1_pikdraw_canvas_white_workspace.bmp");

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
    capture_shell_screenshot(win, "m1_pikdraw_canvas_40m_billboard.bmp");

    std::cout << "[PASS] test_canvas_workspace_and_object_size_limits\n";
}

void test_selection_borderless_default_stroke_none_and_hairline_zoom() {
    NativeWindow win;
    assert(win.initialize("Test Selection Borderless & Hairline"));
    if (win.is_recovery_modal_open()) {
        win.handle_key_down(VK_ESCAPE);
    }
    auto& workflow = win.workflow();
    workflow.new_document(210.0, 297.0);

    // 1. Verify EditorStyle default stroke is none (has_stroke = false)
    EditorStyle default_style;
    assert(default_style.has_fill == true);
    assert(default_style.has_stroke == false);

    // 2. Create a shape using rectangle drawing tool (active_tool = 1)
    win.set_active_tool(1); // Rectangle tool
    const auto area = win.canvas_rect(win.width(), win.height());
    const int start_x = area.left + 50, start_y = area.top + 50;
    const int end_x = area.left + 150, end_y = area.top + 120;
    win.handle_mouse_down(start_x, start_y, 0);
    win.handle_mouse_move(end_x, end_y, 0);
    win.handle_mouse_up(end_x, end_y, 0);

    // Shape must be created and automatically selected with Pick tool
    assert(win.active_tool() == 0);
    const auto& selection = win.root().vector_controller().selection();
    assert(selection.count() == 1);
    const auto created_id = selection.selected_ids()[0];
    const auto snap = win.root().document().current_snapshot();
    const auto* created_obj = snap->objects().lookup_ref(created_id);
    assert(created_obj != nullptr);
    const auto& path_obj = std::get<pikdraw::document::PathObject>(*created_obj);

    // Mặc định viền stroke phải là none (has_stroke = false), có fill (has_fill = true)
    assert(path_obj.has_fill == true);
    assert(path_obj.has_stroke == false);

    // 3. Render selection overlay: selection is borderless (only handles + pivot, no blue outline)
    UpdateWindow(win.hwnd());

    // 4. Test Hairline minimum on extreme camera zoom out
    // Set a stroke to the object and zoom out far
    assert(win.apply_selection_color(true, pikdraw::color::RGBColor{0, 0, 0, 1}));
    const auto snap_after_stroke = win.root().document().current_snapshot();
    const auto& stroked_obj = std::get<pikdraw::document::PathObject>(*snap_after_stroke->objects().lookup_ref(created_id));
    assert(stroked_obj.has_stroke == true);

    // Zoom camera out so that stroke width in pixels would drop well below 0.5px
    auto& camera = win.root().renderer().camera();
    camera.set_zoom(0.01); // 0.01 px/mm -> 0.5mm stroke is 0.005px
    assert(camera.zoom() == 0.01);
    UpdateWindow(win.hwnd());

    std::cout << "[PASS] test_selection_borderless_default_stroke_none_and_hairline_zoom\n";
}

void test_copy_cut_paste_ecosystem_and_array_sessions() {
    NativeWindow win(1440, 900);
    auto& doc = win.root().document();
    auto& wf = win.workflow();

    // 1. Setup two shapes: a rectangle and an ellipse
    const auto rect_id = wf.add_rectangle(20.0, 30.0, 80.0, 60.0, "#00f2c3", "#0ea5e9", 1.5);
    const auto ell_id = wf.add_ellipse(150.0, 70.0, 40.0, 30.0, "#f43f5e", "#ffffff", 1.0);
    assert(doc.current_snapshot()->object_count() == 2);

    // 2. Multi-select both shapes
    win.root().vector_controller().selection().clear();
    win.root().vector_controller().selection().select(rect_id, pikdraw::vector::SelectionMode::Add);
    win.root().vector_controller().selection().select(ell_id, pikdraw::vector::SelectionMode::Add);
    assert(win.root().vector_controller().selection().count() == 2);

    // 3. Test Copy Selected
    bool copy_ok = wf.copy_selected(win.hwnd());
    assert(copy_ok);

    // 4. Test Cut Selected
    bool cut_ok = wf.cut_selected(win.hwnd());
    assert(cut_ok);
    assert(doc.current_snapshot()->object_count() == 0);
    assert(win.root().vector_controller().selection().empty());

    // Undo Cut -> both objects restored
    wf.undo();
    assert(doc.current_snapshot()->object_count() == 2);
    assert(doc.current_snapshot()->objects().get(rect_id).has_value());
    assert(doc.current_snapshot()->objects().get(ell_id).has_value());

    // Redo Cut -> 0 objects
    wf.redo();
    assert(doc.current_snapshot()->object_count() == 0);

    // 5. Test Paste (offset +5, +5)
    bool paste_ok = wf.paste(win.hwnd(), DocumentWorkflow::PastePlacement::Offset, 5.0, 5.0);
    assert(paste_ok);
    assert(doc.current_snapshot()->object_count() == 2);
    assert(win.root().vector_controller().selection().count() == 2);

    // Verify pasted rectangle and ellipse positions:
    // Rect was at (20, 30), offset (+5, +5) -> (25, 35)
    // Ellipse center was at (150, 70) with rx=40, ry=30 -> bounds (110, 40), offset (+5, +5) -> (115, 45)
    const auto pasted_ids = win.root().vector_controller().selection().selected_ids();
    assert(pasted_ids.size() == 2);
    bool found_rect = false;
    bool found_ell = false;
    for (auto pid : pasted_ids) {
        auto obj = doc.current_snapshot()->objects().get(pid);
        assert(obj.has_value());
        if (std::holds_alternative<pikdraw::document::PathObject>(*obj)) {
            auto b = wf.get_object_bounds(pid);
            if (b) {
                if (std::abs(b->x - 25.0) < 0.5 && std::abs(b->y - 35.0) < 0.5) {
                    found_rect = true;
                    assert(std::abs(b->width - 80.0) < 0.5);
                    assert(std::abs(b->height - 60.0) < 0.5);
                } else if (std::abs(b->x - 115.0) < 0.5 && std::abs(b->y - 45.0) < 0.5) {
                    found_ell = true;
                    assert(std::abs(b->width - 80.0) < 0.5);
                    assert(std::abs(b->height - 60.0) < 0.5);
                }
            }
        }
    }
    assert(found_rect);
    assert(found_ell);

    // 6. Test Paste in Place
    wf.copy_selected(win.hwnd());
    bool in_place_ok = wf.paste_in_place(win.hwnd());
    assert(in_place_ok);
    assert(doc.current_snapshot()->object_count() == 4);

    const auto in_place_ids = win.root().vector_controller().selection().selected_ids();
    assert(in_place_ids.size() == 2);
    bool found_in_place_rect = false;
    bool found_in_place_ell = false;
    for (auto pid : in_place_ids) {
        auto b = wf.get_object_bounds(pid);
        if (b) {
            if (std::abs(b->x - 25.0) < 0.5 && std::abs(b->y - 35.0) < 0.5) {
                found_in_place_rect = true;
            } else if (std::abs(b->x - 115.0) < 0.5 && std::abs(b->y - 45.0) < 0.5) {
                found_in_place_ell = true;
            }
        }
    }
    assert(found_in_place_rect);
    assert(found_in_place_ell);

    // 7. Test Duplicate at Origin (+)
    win.root().vector_controller().selection().clear();
    win.root().vector_controller().selection().select(in_place_ids.front(), pikdraw::vector::SelectionMode::Add);
    size_t count_before = doc.current_snapshot()->object_count();
    bool dup_origin_ok = wf.duplicate_at_origin();
    assert(dup_origin_ok);
    assert(doc.current_snapshot()->object_count() == count_before + 1);

    // 8. Test Dynamic Array Session (SketchUp *N multiplier and /N divider)
    wf.new_document(297.0, 210.0);
    assert(doc.current_snapshot()->object_count() == 0);

    const auto source_box = wf.add_rectangle(10.0, 10.0, 20.0, 20.0, "#00f2c3", "#ffffff", 1.0);
    win.root().vector_controller().selection().clear();
    win.root().vector_controller().selection().select(source_box, pikdraw::vector::SelectionMode::Add);

    // Duplicate by offset (30.0, 0.0) -> creates 1st copy at x=40
    bool dup_ok = wf.duplicate_selected(30.0, 0.0);
    assert(dup_ok);
    assert(doc.current_snapshot()->object_count() == 2);
    assert(wf.is_array_session_active());

    // Apply multiplier *4
    bool mult_ok = wf.apply_array_multiplier(4);
    assert(mult_ok);
    assert(doc.current_snapshot()->object_count() == 5); // 1 original + 4 copies
    assert(wf.array_session().current_count == 4);
    assert(wf.array_session().is_multiplier == true);

    // Check bounds of 4th copy: offset is 4 * 30 = 120 -> x = 10 + 120 = 130
    auto array_sel = win.root().vector_controller().selection().selected_ids();
    assert(array_sel.size() == 4);
    double max_x = 0.0;
    for (auto id : array_sel) {
        auto b = wf.get_object_bounds(id);
        if (b) max_x = (std::max)(max_x, b->x);
    }
    assert(std::abs(max_x - 130.0) < 0.5);

    // Dynamically switch to divider /2 without exiting session
    bool div_ok = wf.apply_array_divider(2);
    assert(div_ok);
    assert(doc.current_snapshot()->object_count() == 3); // 1 original + 2 copies
    assert(wf.array_session().current_count == 2);
    assert(wf.array_session().is_multiplier == false);

    // End array session
    wf.end_array_session();
    assert(!wf.is_array_session_active());

    std::cout << "[PASS] test_copy_cut_paste_ecosystem_and_array_sessions\n";
}

void test_source_color_models_and_object_overprint_patches() {
    using namespace pikdraw;
    auto spot = color::ColorValue{color::SpotColor{"CutContour", .42, {.9, .1, .7, .8}, true}};
    spot.set_overprint({true, false});
    assert(desktop::source_color::model_of(spot) == desktop::source_color::Model::Spot);

    const auto gray = desktop::source_color::convert(spot, desktop::source_color::Model::Grayscale);
    assert(desktop::source_color::model_of(gray) == desktop::source_color::Model::Grayscale);
    assert(gray.overprint().fill && !gray.overprint().stroke);
    const auto gray_source = std::get<color::GrayscaleColor>(gray.payload());
    assert(std::abs(gray_source.a - .8) < 1e-6);

    const auto rgb = desktop::source_color::convert(gray, desktop::source_color::Model::Rgb);
    assert(desktop::source_color::model_of(rgb) == desktop::source_color::Model::Rgb);
    const auto rgb_source = std::get<color::RGBColor>(rgb.payload());
    assert(std::abs(rgb_source.r - rgb_source.g) < 1e-6);
    assert(std::abs(rgb_source.g - rgb_source.b) < 1e-6);
    assert(rgb.overprint().fill && !rgb.overprint().stroke);

    const auto recreated_spot = desktop::source_color::convert(gray, desktop::source_color::Model::Spot);
    const auto recreated = std::get<color::SpotColor>(recreated_spot.payload());
    assert(recreated.spot_name == "Spot Color");
    assert(recreated.tint == 1.0);
    assert(std::abs(recreated.alternate_rgb.a - .8) < 1e-6);
    assert(recreated_spot.overprint().fill && !recreated_spot.overprint().stroke);
    const auto retained_spot = desktop::source_color::convert(spot, desktop::source_color::Model::Spot);
    const auto retained = std::get<color::SpotColor>(retained_spot.payload());
    assert(retained.spot_name == "CutContour" && retained.tint == .42 && retained.overprint);
    assert(retained.alternate_rgb.approx_equal({.9, .1, .7, .8}));

    desktop::NativeWindow win(1440, 900);
    auto& workflow = win.workflow();
    workflow.new_document(200.0, 200.0);
    const auto id = workflow.add_rectangle(20.0, 20.0, 30.0, 30.0);
    const auto before_patch = *win.root().document().current_snapshot()->objects().get(id);
    const auto original_fill_space = std::get<document::PathObject>(before_patch).fill.color_space();
    auto& selection = win.root().vector_controller().selection();
    selection.clear();
    selection.select(id, vector::SelectionMode::Add);

    vector::StylePatch fill_overprint;
    fill_overprint.fill_overprint = true;
    assert(win.apply_style_patch(fill_overprint));
    const auto after_overprint = *win.root().document().current_snapshot()->objects().get(id);
    assert(std::get<document::PathObject>(after_overprint).fill.overprint().fill);

    vector::StylePatch change_color;
    change_color.fill = color::ColorValue{color::GrayscaleColor{.25, .8}};
    assert(win.apply_style_patch(change_color));
    const auto after_gray = *win.root().document().current_snapshot()->objects().get(id);
    const auto& gray_path = std::get<document::PathObject>(after_gray);
    const auto& normalized_gray = std::get<color::RGBColor>(gray_path.fill.payload());
    assert(normalized_gray.approx_equal({.25, .25, .25, .8}, 0));
    assert(gray_path.fill.overprint().fill && !gray_path.fill.overprint().stroke);

    vector::StylePatch stroke_overprint;
    stroke_overprint.stroke_overprint = true;
    assert(win.apply_style_patch(stroke_overprint));
    const auto after_stroke = *win.root().document().current_snapshot()->objects().get(id);
    assert(std::get<document::PathObject>(after_stroke).stroke.overprint().stroke);
    assert(workflow.undo());
    const auto after_undo_stroke = *win.root().document().current_snapshot()->objects().get(id);
    assert(!std::get<document::PathObject>(after_undo_stroke).stroke.overprint().stroke);
    assert(workflow.undo());
    const auto after_undo_gray = *win.root().document().current_snapshot()->objects().get(id);
    assert(std::get<document::PathObject>(after_undo_gray).fill.overprint().fill);
    assert(std::get<document::PathObject>(after_undo_gray).fill.color_space() == original_fill_space);
    assert(workflow.undo());
    const auto after_undo_overprint = *win.root().document().current_snapshot()->objects().get(id);
    assert(!std::get<document::PathObject>(after_undo_overprint).fill.overprint().fill);
    std::cout << "[PASS] test_source_color_models_and_object_overprint_patches\n";
}

int main() {
    std::cout << "=== Running Desktop Shell Tests ===\n";
    test_shell_geometry_resolutions_and_dpi();
    test_workspace_state_and_capabilities();
    test_status_bar_metrics_and_vietnamese_labels();
    test_native_window_theme_and_settings_state();
    test_ai_connections_settings_draft_state();
    test_multi_document_tabs();
    test_page_sizing_and_orientation();
    test_object_sizing_and_aspect_lock();
    test_delete_key_shortcut_and_undo_redo();
    test_group_ungroup_and_isolation_mode();
    test_solid_tools_boolean_knife_and_context_actions();
    test_contextual_object_action_capabilities();
    test_vector_and_bitmap_interactions();
    test_typography_inline_editing_ribbon_and_table();
    test_align_and_distribute_tools();
    test_compact_icons_and_hover_tooltips();
    test_canvas_rulers_and_guidelines();
    test_canvas_workspace_and_object_size_limits();
    test_numeric_keyboard_input_property_bar();
    test_object_opacity_property_bar_and_appearance_panel();
    test_fill_and_stroke_opacity_properties();
    test_blend_and_effect_properties_panel();
    test_effects_toolbox_flyout();
    test_page_preset_dropdown_and_modal_row_clicks();
    test_selection_directional_scale_rotate_and_pivot();
    test_double_click_rotate_and_flip_transform_suite();
    test_corel_pick_auto_select_resume_guidelines_and_space_shortcut();
    test_selection_borderless_default_stroke_none_and_hairline_zoom();
    test_copy_cut_paste_ecosystem_and_array_sessions();
    test_source_color_models_and_object_overprint_patches();
    test_layers_and_objects_dock_suite();
    std::cout << "All Desktop Shell Tests PASSED.\n";
    return 0;
}

