#include "desktop_shell_test_helpers.hpp"

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

void test_solid_tools_boolean_knife_and_context_actions() {
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
    assert(workflow.can_boolean_operation_selected(pikdraw::geometry::BooleanOp::Union));

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

    // 4. Keep Source / Keep Target are Object > Shaping menu preferences.
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

    std::cout << "[PASS] test_solid_tools_boolean_knife_and_context_actions\n";
}

void test_contextual_object_action_capabilities() {
    NativeWindow win(1440, 900);
    auto& workflow = win.workflow();
    auto& selection = win.root().vector_controller().selection();
    const auto first = workflow.add_rectangle(20.0, 20.0, 40.0, 30.0);
    const auto second = workflow.add_rectangle(40.0, 20.0, 40.0, 30.0);
    const auto third = workflow.add_rectangle(70.0, 20.0, 40.0, 30.0);
    const auto ellipse = workflow.add_ellipse(60.0, 50.0, 20.0, 15.0);

    // AC-CTX-01 — contextual commands follow selection count and supported geometry.
    // Given: no selection; When: capabilities are queried; Then: object actions are absent.
    assert(!workflow.can_align_selected(pikdraw::geometry::AlignMode::Left));
    assert(!workflow.can_boolean_operation_selected(pikdraw::geometry::BooleanOp::Union));
    assert(!workflow.can_group_selected());
    auto empty_buttons = get_multi_object_context_buttons(workflow, false, 1160);
    assert(empty_buttons.empty());

    // A single object can center on the page, but cannot use selection-to-selection alignment.
    selection.select(first);
    assert(workflow.can_align_selected(pikdraw::geometry::AlignMode::CenterPage));
    assert(!workflow.can_align_selected(pikdraw::geometry::AlignMode::Left));

    // Two editable rectangles enable Boolean and align actions, but equal-gap distribution
    // stays hidden because two objects do not form a gap sequence.
    selection.select(second, pikdraw::vector::SelectionMode::Add);
    assert(workflow.can_align_selected(pikdraw::geometry::AlignMode::Left));
    assert(workflow.can_boolean_operation_selected(pikdraw::geometry::BooleanOp::Union));
    assert(workflow.can_group_selected());
    assert(!workflow.can_distribute_selected(pikdraw::geometry::DistributeMode::SpacingHorizontal));
    assert(workflow.can_distribute_selected(pikdraw::geometry::DistributeMode::LeftEdges));
    assert(workflow.can_distribute_selected(pikdraw::geometry::DistributeMode::CentersHorizontal));
    assert(workflow.can_distribute_selected(pikdraw::geometry::DistributeMode::RightEdges));
    assert(workflow.can_distribute_selected(pikdraw::geometry::DistributeMode::TopEdges));
    assert(workflow.can_distribute_selected(pikdraw::geometry::DistributeMode::CentersVertical));
    assert(workflow.can_distribute_selected(pikdraw::geometry::DistributeMode::BottomEdges));
    assert(workflow.can_distribute_selected(pikdraw::geometry::DistributeMode::SpacingHorizontal, 0.0));
    const auto two_buttons = get_multi_object_context_buttons(workflow, workflow.can_group_selected(), 1160);
    assert(std::any_of(two_buttons.begin(), two_buttons.end(), [](const auto& button) {
        return button.action == ObjectContextAction::BooleanUnion;
    }));
    assert(std::none_of(two_buttons.begin(), two_buttons.end(), [](const auto& button) {
        return button.action == ObjectContextAction::DistributeHorizontal;
    }));

    // Three objects enable equal spacing but suppress pairwise shaping.
    selection.select(third, pikdraw::vector::SelectionMode::Add);
    assert(workflow.can_distribute_selected(pikdraw::geometry::DistributeMode::SpacingHorizontal));
    assert(!workflow.can_boolean_operation_selected(pikdraw::geometry::BooleanOp::Union));
    const auto three_buttons = get_multi_object_context_buttons(workflow, workflow.can_group_selected(), 1160);
    assert(std::any_of(three_buttons.begin(), three_buttons.end(), [](const auto& button) {
        return button.action == ObjectContextAction::DistributeHorizontal;
    }));
    assert(std::none_of(three_buttons.begin(), three_buttons.end(), [](const auto& button) {
        return button.action == ObjectContextAction::BooleanUnion;
    }));
    const auto count_before_overselect = win.root().document().current_snapshot()->object_count();
    const auto revision_before_overselect = win.root().document().current_snapshot()->revision();
    assert(!workflow.boolean_operation_selected(pikdraw::geometry::BooleanOp::Union));
    assert(win.root().document().current_snapshot()->object_count() == count_before_overselect);
    assert(win.root().document().current_snapshot()->revision() == revision_before_overselect);
    assert(selection.count() == 3);

    // Curved paths are rejected instead of silently replacing their shape with bounds.
    selection.clear();
    selection.select(first, pikdraw::vector::SelectionMode::Add);
    selection.select(ellipse, pikdraw::vector::SelectionMode::Add);
    assert(!workflow.can_boolean_operation_selected(pikdraw::geometry::BooleanOp::Intersection));
    assert(!workflow.boolean_operation_selected(pikdraw::geometry::BooleanOp::Intersection));
    assert(win.root().document().current_snapshot()->objects().get(first).has_value());
    assert(win.root().document().current_snapshot()->objects().get(ellipse).has_value());

    // Transforms are applied to source paths before a supported Boolean operation.
    const auto moved_target = workflow.add_rectangle(150.0, 20.0, 20.0, 20.0);
    const auto clip = workflow.add_rectangle(170.0, 20.0, 20.0, 20.0);
    assert(workflow.transform_object(moved_target, 5.0, 0.0));
    selection.clear();
    selection.select(moved_target, pikdraw::vector::SelectionMode::Add);
    selection.select(clip, pikdraw::vector::SelectionMode::Add);
    assert(workflow.can_boolean_operation_selected(pikdraw::geometry::BooleanOp::Intersection));
    assert(workflow.boolean_operation_selected(pikdraw::geometry::BooleanOp::Intersection, true, true));
    const auto result_id = selection.selected_ids().front();
    const auto result_bounds = workflow.get_object_bounds(result_id);
    assert(result_bounds && std::abs(result_bounds->x - 170.0) < 1e-6);
    assert(std::abs(result_bounds->width - 5.0) < 1e-6);

    // No operation is applied if the command preconditions say it would produce no geometry.
    const auto disjoint = workflow.add_rectangle(250.0, 20.0, 20.0, 20.0);
    selection.clear();
    selection.select(moved_target, pikdraw::vector::SelectionMode::Add);
    selection.select(disjoint, pikdraw::vector::SelectionMode::Add);
    assert(!workflow.can_boolean_operation_selected(pikdraw::geometry::BooleanOp::Intersection));

    // Properties remain an inspector; action help is no longer attached to the old docker rows.
    win.update_hover_tooltip(1440 - 280 + 30, 420);
    assert(!win.tooltip().visible);
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

    // AC-ALIGN-01 — three selected objects expose align and equal-spacing controls on the context bar.
    assert(workflow.can_align_selected(pikdraw::geometry::AlignMode::Left));
    assert(workflow.can_distribute_selected(pikdraw::geometry::DistributeMode::SpacingHorizontal));
    capture_shell_screenshot(win, "object-context-actions.bmp");

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
    assert(workflow.can_align_selected(pikdraw::geometry::AlignMode::CenterPage));
    assert(!workflow.can_align_selected(pikdraw::geometry::AlignMode::Left));
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
    capture_shell_screenshot(win, "m1_pikdraw_align_left_and_page.bmp");
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
    capture_shell_screenshot(win, "m1_pikdraw_distribute_spacing.bmp");
    assert(workflow.undo());

    // AC-ALIGN-02 — Ctrl+Shift+A opens the on-demand Align & Distribute panel inside the existing dock.
    const auto& window_menu = kWindowMenuItems;
    const auto align_panel_item = std::find_if(window_menu.begin(), window_menu.end(), [](const auto& item) {
        return item.action == MenuActionId::WindowAlignDistributePanel;
    });
    assert(align_panel_item != window_menu.end());
    assert(std::wstring(align_panel_item->shortcut) == L"Ctrl+Shift+A");
    for (const auto action : {
             MenuActionId::ObjectDistributeHorizontal,
             MenuActionId::ObjectDistributeVertical,
             MenuActionId::ObjectDistributeLeftEdges,
             MenuActionId::ObjectDistributeCentersHorizontal,
             MenuActionId::ObjectDistributeRightEdges,
             MenuActionId::ObjectDistributeTopEdges,
             MenuActionId::ObjectDistributeCentersVertical,
             MenuActionId::ObjectDistributeBottomEdges}) {
        assert(std::any_of(kObjectMenuItems.begin(), kObjectMenuItems.end(), [action](const auto& item) {
            return item.action == action;
        }));
    }

    // 8. Panel buttons expose automatic spacing while Properties stay compact by default.
    win.open_align_distribute_panel();
    assert(win.active_dock_tab() == 0);
    assert(win.align_distribute_panel_visible());
    capture_shell_screenshot(win, "align-distribute-panel-english.bmp");
    win.set_language(UILanguage::Vietnamese);
    capture_shell_screenshot(win, "align-distribute-panel-vietnamese.bmp");
    win.set_language(UILanguage::English);
    const int panel_left = 1440 - 280 + AlignPanelMetrics::content_left;
    const int reference_button_width = (AlignPanelMetrics::content_width - 12) / 3;
    const int two_button_width = (AlignPanelMetrics::content_width - 6) / 2;
    win.handle_mouse_down(panel_left + reference_button_width + 18,
                          AlignPanelMetrics::reference_top + 8); // Key Object = last selected (R3).
    assert(win.align_reference() == pikdraw::geometry::AlignReference::KeyObject);
    win.handle_mouse_down(panel_left + 20, AlignPanelMetrics::align_top + 8); // Align Left to R3.
    b1 = workflow.get_object_bounds(r1);
    b2 = workflow.get_object_bounds(r2);
    b3 = workflow.get_object_bounds(r3);
    assert(std::abs(b1->x - b3->x) < 1e-4);
    assert(std::abs(b2->x - b3->x) < 1e-4);
    assert(workflow.undo());
    win.handle_mouse_down(panel_left + 18, AlignPanelMetrics::reference_top + 8);
    assert(win.align_reference() == pikdraw::geometry::AlignReference::Selection);

    win.handle_mouse_down(panel_left + 24, AlignPanelMetrics::horizontal_top + 8);
    b1 = workflow.get_object_bounds(r1);
    b2 = workflow.get_object_bounds(r2);
    b3 = workflow.get_object_bounds(r3);
    assert(std::abs((b2->x - b1->right()) - 15.0) < 1e-4);
    assert(std::abs((b3->x - b2->right()) - 15.0) < 1e-4);
    assert(workflow.undo());

    win.handle_mouse_down(panel_left + 24, AlignPanelMetrics::vertical_top + 8);
    b1 = workflow.get_object_bounds(r1);
    b2 = workflow.get_object_bounds(r2);
    b3 = workflow.get_object_bounds(r3);
    assert(std::abs((b3->y - b1->bottom()) + 7.5) < 1e-4);
    assert(std::abs((b2->y - b3->bottom()) + 7.5) < 1e-4);
    assert(workflow.undo());

    // AC-ALIGN-03 — fixed spacing input accepts decimal comma, validates mm, and applies to two or more objects.
    win.set_distribute_spacing_mm(10.0);
    win.handle_mouse_down(panel_left + 20, AlignPanelMetrics::gap_field_top + 8);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::DistributeSpacing);
    for (int i = 0; i < 4; ++i) win.handle_numeric_char_input(8);
    for (const wchar_t ch : L"12,5") win.handle_numeric_char_input(ch);
    win.handle_numeric_char_input(13);
    assert(std::abs(win.distribute_spacing_mm() - 12.5) < 1e-9);
    win.start_numeric_input(NativeWindow::NumericFieldId::DistributeSpacing);
    for (int i = 0; i < 4; ++i) win.handle_numeric_char_input(8);
    win.handle_numeric_char_input(L'-');
    win.handle_numeric_char_input(L'1');
    win.handle_numeric_char_input(13);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::DistributeSpacing);
    assert(std::abs(win.distribute_spacing_mm() - 12.5) < 1e-9);
    const auto revision_before_invalid_apply = win.root().document().current_snapshot()->revision();
    win.handle_mouse_down(panel_left + two_button_width + 18, AlignPanelMetrics::apply_top + 8);
    assert(win.root().document().current_snapshot()->revision() == revision_before_invalid_apply);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::DistributeSpacing);
    win.cancel_numeric_field();
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::None);
    win.handle_mouse_down(panel_left + two_button_width + 18, AlignPanelMetrics::apply_top + 8);
    b1 = workflow.get_object_bounds(r1);
    b2 = workflow.get_object_bounds(r2);
    b3 = workflow.get_object_bounds(r3);
    assert(std::abs((b3->y - b1->bottom()) - 12.5) < 1e-4);
    assert(std::abs((b2->y - b3->bottom()) - 12.5) < 1e-4);
    assert(workflow.undo());

    // The regular Inspector remains available and does not retain the Align view.
    win.set_active_dock_tab(2); // Switch away to History tab
    assert(win.active_dock_tab() == 2);
    win.open_inspector_properties_panel();
    assert(win.active_dock_tab() == 0); // Immediately focused to Inspector tab!
    assert(!win.align_distribute_panel_visible());
    win.open_align_distribute_panel(); // shortcut target
    assert(win.align_distribute_panel_visible());

    std::cout << "[PASS] test_align_and_distribute_tools\n";
}

void test_selection_directional_scale_rotate_and_pivot() {
    NativeWindow win(1440, 900);
    const auto id = win.workflow().add_rectangle(50.0, 50.0, 100.0, 60.0, "#00f2c3", "#0ea5e9", 2.0);
    assert(id != pikdraw::document::kInvalidObjectId);
    win.root().vector_controller().selection().select(id);

    // 1. Verify directional handle scaling via selection_resize_transform
    const pikdraw::geometry::Rect2D bounds{50.0, 50.0, 100.0, 60.0};

    // Bottom handle (5): dragging down increases height, width MUST NOT change
    const auto resize_bottom = pikdraw::vector::selection_resize_transform(bounds, 5, {0.0, -20.0}, false);
    assert(resize_bottom.has_value());
    const auto rect_bottom = resize_bottom->map_rect(bounds);
    assert(std::abs(rect_bottom.width - 100.0) < 1e-6);
    assert(std::abs(rect_bottom.height - 80.0) < 1e-6);
    assert(std::abs(rect_bottom.y - 30.0) < 1e-6); // bottom expanded downward

    // Right handle (3): dragging right increases width, height MUST NOT change
    const auto resize_right = pikdraw::vector::selection_resize_transform(bounds, 3, {30.0, 0.0}, false);
    assert(resize_right.has_value());
    const auto rect_right = resize_right->map_rect(bounds);
    assert(std::abs(rect_right.width - 130.0) < 1e-6);
    assert(std::abs(rect_right.height - 60.0) < 1e-6);

    // Corner handle (2): dragging top-right freely scales both width and height independently
    const auto resize_corner_free = pikdraw::vector::selection_resize_transform(bounds, 2, {40.0, 10.0}, false);
    assert(resize_corner_free.has_value());
    const auto rect_corner_free = resize_corner_free->map_rect(bounds);
    assert(std::abs(rect_corner_free.width - 140.0) < 1e-6);
    assert(std::abs(rect_corner_free.height - 70.0) < 1e-6);

    // Corner handle (2) with preserve_aspect = true scales proportionally
    const auto resize_corner_lock = pikdraw::vector::selection_resize_transform(bounds, 2, {40.0, 10.0}, true);
    assert(resize_corner_lock.has_value());
    const auto rect_corner_lock = resize_corner_lock->map_rect(bounds);
    assert(std::abs(rect_corner_lock.width / rect_corner_lock.height - 100.0 / 60.0) < 1e-6);

    // 2. Rotate Mode & Pivot Point mechanics
    assert(!win.is_rotate_mode());
    assert(!win.custom_pivot_world().has_value());

    // Toggle rotate mode
    win.toggle_rotate_mode();
    assert(win.is_rotate_mode());

    // Default pivot falls back to bounds center
    const auto pivot_default = win.custom_pivot_world().value_or(bounds.center());
    assert(std::abs(pivot_default.x - 100.0) < 1e-6);
    assert(std::abs(pivot_default.y - 80.0) < 1e-6);

    // Set custom pivot point
    const pikdraw::geometry::Point2D custom_pivot{50.0, 50.0}; // bottom-left corner as pivot
    win.set_custom_pivot_world(custom_pivot);
    assert(win.custom_pivot_world().has_value());
    assert(win.custom_pivot_world()->x == 50.0 && win.custom_pivot_world()->y == 50.0);

    // Rotate 90 degrees around custom pivot {50, 50}
    // Point {150, 50} rotated 90 degrees around {50, 50} moves to {50, 150}
    const auto rot_transform = pikdraw::geometry::Transform2D::translation(custom_pivot.x, custom_pivot.y) *
                               pikdraw::geometry::Transform2D::rotation_degrees(90.0) *
                               pikdraw::geometry::Transform2D::translation(-custom_pivot.x, -custom_pivot.y);
    const auto rotated_pt = rot_transform.map_point({150.0, 50.0});
    assert(std::abs(rotated_pt.x - 50.0) < 1e-4);
    assert(std::abs(rotated_pt.y - 150.0) < 1e-4);

    // Capture visual proof of Rotate Mode with Pivot and Thin Handles
    capture_shell_screenshot(win, "selection_rotate_mode_pivot.bmp");

    // Toggle back to scale mode
    win.toggle_rotate_mode();
    assert(!win.is_rotate_mode());
    capture_shell_screenshot(win, "selection_scale_mode_thin_handles.bmp");

    std::cout << "[PASS] test_selection_directional_scale_rotate_and_pivot\n";
}

void test_double_click_rotate_and_flip_transform_suite() {
    NativeWindow win(1440, 900);
    auto& workflow = win.workflow();
    workflow.new_document(297.0, 210.0);
    auto& sel = win.root().vector_controller().selection();

    // Create an object: rectangle [100, 100] to [160, 140]
    // Width = 60, height = 40, center = (130, 120)
    const auto obj_id = workflow.add_rectangle(100.0, 100.0, 60.0, 40.0);
    assert(obj_id != pikdraw::document::kInvalidObjectId);

    sel.select(obj_id);
    assert(sel.count() == 1);
    const auto orig_bounds = *workflow.get_object_bounds(obj_id);
    assert(std::abs(orig_bounds.width - 60.0) < 1e-4);
    assert(std::abs(orig_bounds.height - 40.0) < 1e-4);
    assert(std::abs(orig_bounds.center().x - 130.0) < 1e-4);
    assert(std::abs(orig_bounds.center().y - 120.0) < 1e-4);

    // 1. Double-click activation of Rotate Mode
    assert(!win.is_rotate_mode());

    // Single click (mouse down + mouse up without drag) must NOT enter rotate mode!
    const auto screen_center = win.root().renderer().camera().world_to_screen({130.0, 120.0});
    const auto area = win.canvas_rect(1440, 900);
    const int cx = area.left + static_cast<int>(screen_center.x);
    const int cy = area.top + static_cast<int>(screen_center.y);

    win.handle_mouse_down(cx, cy, 0);
    win.handle_mouse_up(cx, cy, 0);
    assert(!win.is_rotate_mode()); // Must remain false on single click!

    // Rapid double-click on selected object activates rotate mode
    win.handle_double_click(cx, cy);
    assert(win.is_rotate_mode()); // Must be true after double click!

    // Top-left corner point in world is (100.0, 140.0)
    const auto screen_corner = win.root().renderer().camera().world_to_screen({100.0, 140.0});
    const int hx = area.left + static_cast<int>(screen_corner.x);
    const int hy = area.top + static_cast<int>(screen_corner.y);

    // Corner handle cursor in rotate mode is IDC_HAND (32649)
    assert(win.canvas_cursor_at(hx, hy) == 32649);

    // Escape exits rotate mode back to normal transform mode
    assert(win.handle_key_down(VK_ESCAPE));
    assert(!win.is_rotate_mode()); // Rotate mode is cleared!
    assert(sel.count() == 1); // Selection remains!

    // Corner handle cursor in normal mode is diagonal scale (32642/32643), NOT rotate!
    const WORD corner_cursor = win.canvas_cursor_at(hx, hy);
    assert(corner_cursor == 32642 || corner_cursor == 32643);

    // 2. Flip Horizontal (mirror across center.x = 130.0)
    assert(workflow.flip_selected_horizontal());
    auto flipped_h_bounds = *workflow.get_object_bounds(obj_id);
    assert(std::abs(flipped_h_bounds.center().x - 130.0) < 1e-4);
    assert(std::abs(flipped_h_bounds.center().y - 120.0) < 1e-4);
    assert(workflow.undo());
    auto restored_bounds = *workflow.get_object_bounds(obj_id);
    assert(std::abs(restored_bounds.x - orig_bounds.x) < 1e-4);

    // 3. Flip Vertical (mirror across center.y = 120.0)
    assert(workflow.flip_selected_vertical());
    auto flipped_v_bounds = *workflow.get_object_bounds(obj_id);
    assert(std::abs(flipped_v_bounds.center().x - 130.0) < 1e-4);
    assert(std::abs(flipped_v_bounds.center().y - 120.0) < 1e-4);
    assert(workflow.undo());

    // 4. Rotate 90° CW
    assert(workflow.rotate_selected_90_cw());
    auto rot_cw_bounds = *workflow.get_object_bounds(obj_id);
    assert(std::abs(rot_cw_bounds.center().x - 130.0) < 1e-4);
    assert(std::abs(rot_cw_bounds.center().y - 120.0) < 1e-4);
    // Width and height swap: 60x40 becomes 40x60
    assert(std::abs(rot_cw_bounds.width - 40.0) < 1e-4);
    assert(std::abs(rot_cw_bounds.height - 60.0) < 1e-4);
    assert(workflow.undo());

    // 5. Rotate 90° CCW
    assert(workflow.rotate_selected_90_ccw());
    auto rot_ccw_bounds = *workflow.get_object_bounds(obj_id);
    assert(std::abs(rot_ccw_bounds.width - 40.0) < 1e-4);
    assert(std::abs(rot_ccw_bounds.height - 60.0) < 1e-4);
    assert(workflow.undo());

    // 6. Rotate 180°
    assert(workflow.rotate_selected_180());
    auto rot_180_bounds = *workflow.get_object_bounds(obj_id);
    assert(std::abs(rot_180_bounds.width - 60.0) < 1e-4);
    assert(std::abs(rot_180_bounds.height - 40.0) < 1e-4);
    assert(workflow.undo());

    // 7. Menu Action execution
    win.execute_menu_action(static_cast<int>(MenuActionId::ObjectFlipHorizontal));
    assert(workflow.undo());

    win.execute_menu_action(static_cast<int>(MenuActionId::ObjectRotate90CW));
    auto menu_rot_bounds = *workflow.get_object_bounds(obj_id);
    assert(std::abs(menu_rot_bounds.width - 40.0) < 1e-4);
    assert(workflow.undo());

    std::cout << "[PASS] test_double_click_rotate_and_flip_transform_suite\n";
}

void test_powerclip_and_clipping_mask_suite() {
    pikdraw::desktop::NativeWindow win(1440, 900);
    auto& workflow = win.workflow();
    auto& controller = win.root().vector_controller();
    auto& sel = controller.selection();

    // 1. Create content (ellipse) and container mask (rectangle on top)
    const auto content_id = workflow.add_ellipse(70.0, 70.0, 80.0, 80.0, "#f59e0b", "#d97706", 1.5);
    const auto mask_id = workflow.add_rectangle(50.0, 50.0, 100.0, 100.0, "#00f2c3", "#0ea5e9", 2.0);
    assert(content_id != pikdraw::document::kInvalidObjectId);
    assert(mask_id != pikdraw::document::kInvalidObjectId);

    // 2. Select both objects
    sel.clear();
    sel.select(content_id, pikdraw::vector::SelectionMode::Add);
    sel.select(mask_id, pikdraw::vector::SelectionMode::Add);
    assert(sel.count() == 2);
    assert(workflow.can_make_clipping_mask_selected());

    // 3. Make Clipping Mask / PowerClip (preserve frame appearance = true)
    const auto clip_id = workflow.make_clipping_mask_selected(true);
    assert(clip_id != pikdraw::document::kInvalidObjectId);
    assert(workflow.is_group(clip_id));
    assert(workflow.is_clipping_container(clip_id));
    assert(sel.count() == 1);
    assert(sel.is_selected(clip_id));

    // Bounds of clipping container must match the mask frame
    const auto clip_bounds = workflow.get_object_bounds(clip_id);
    const auto mask_bounds = workflow.get_object_bounds(mask_id);
    assert(clip_bounds.has_value() && mask_bounds.has_value());
    assert(std::abs(clip_bounds->width - mask_bounds->width) < 1e-4);
    assert(std::abs(clip_bounds->height - mask_bounds->height) < 1e-4);

    // Contents must be auto-centered inside the frame!
    const auto content_bounds = workflow.get_object_bounds(content_id);
    assert(content_bounds.has_value());
    assert(std::abs(content_bounds->center().x - mask_bounds->center().x) < 1e-4);
    assert(std::abs(content_bounds->center().y - mask_bounds->center().y) < 1e-4);

    // 4. Test Lock Contents and Fit Modes
    assert(workflow.set_powerclip_lock_contents(clip_id, true));
    assert(workflow.fit_powerclip_contents(clip_id, pikdraw::document::ClipContentFitMode::CenterContents));
    assert(workflow.fit_powerclip_contents(clip_id, pikdraw::document::ClipContentFitMode::FitProportionally));
    assert(workflow.fit_powerclip_contents(clip_id, pikdraw::document::ClipContentFitMode::FillProportionally));
    assert(workflow.fit_powerclip_contents(clip_id, pikdraw::document::ClipContentFitMode::StretchToFill));
    const auto stretched_bounds = workflow.get_object_bounds(content_id);
    assert(stretched_bounds.has_value());
    assert(std::abs(stretched_bounds->width - mask_bounds->width) < 1e-4);
    assert(std::abs(stretched_bounds->height - mask_bounds->height) < 1e-4);
    assert(workflow.fit_powerclip_contents(clip_id, pikdraw::document::ClipContentFitMode::CurrentPosition));

    // 5. Test Group Isolation Mode for PowerClip editing
    assert(!win.is_in_isolation_mode());
    win.enter_group_isolation(clip_id);
    assert(win.is_in_isolation_mode());
    assert(win.current_isolated_group_id() == clip_id);
    assert(win.is_object_in_isolated_group(content_id));

    // Exit isolation mode
    win.exit_group_isolation();
    assert(!win.is_in_isolation_mode());

    // 6. Place Inside PowerClip (Container Pick Target)
    const auto extra_content = workflow.add_ellipse(10.0, 10.0, 30.0, 30.0);
    const auto extra_frame = workflow.add_rectangle(0.0, 0.0, 50.0, 50.0);
    const auto new_clip = workflow.place_inside_powerclip(extra_content, extra_frame);
    assert(new_clip != pikdraw::document::kInvalidObjectId);
    assert(workflow.is_clipping_container(new_clip));

    // 7. Test Release Clipping Mask
    sel.clear();
    sel.select(clip_id, pikdraw::vector::SelectionMode::Replace);
    assert(workflow.release_clipping_mask_selected());
    assert(!workflow.is_clipping_container(clip_id));

    // 8. Test Undo & Redo fidelity
    assert(workflow.undo());
    assert(workflow.is_clipping_container(clip_id));
    assert(workflow.redo());
    assert(!workflow.is_clipping_container(clip_id));

    std::cout << "[PASS] test_powerclip_and_clipping_mask_suite\n";
}

void test_smart_context_menu_suite() {
    pikdraw::desktop::NativeWindow win(1440, 900);
    auto& workflow = win.workflow();
    auto& controller = win.root().vector_controller();
    auto& sel = controller.selection();

    // 1. Context menu on empty canvas (no selection)
    sel.clear();
    HMENU hEmptyMenu = win.build_context_menu({}, false);
    assert(hEmptyMenu != nullptr);
    const int emptyCount = GetMenuItemCount(hEmptyMenu);
    assert(emptyCount > 5); // Paste, Select All, Zoom, Guidelines, Snap, Page Setup
    DestroyMenu(hEmptyMenu);

    // 2. Context menu for 1 regular object
    const auto rect_id = workflow.add_rectangle(10.0, 10.0, 50.0, 30.0);
    assert(rect_id != pikdraw::document::kInvalidObjectId);
    sel.clear();
    sel.select(rect_id, pikdraw::vector::SelectionMode::Replace);

    HMENU hSingleMenu = win.build_context_menu({rect_id}, true);
    assert(hSingleMenu != nullptr);
    const int singleCount = GetMenuItemCount(hSingleMenu);
    assert(singleCount > 5);
    DestroyMenu(hSingleMenu);

    // Test actions from context menu: Lock & Unlock
    win.execute_menu_action(static_cast<int>(pikdraw::desktop::MenuActionId::ObjectLock));
    auto snap = win.root().document().current_snapshot();
    auto obj = snap->objects().get(rect_id);
    assert(obj.has_value());
    std::visit([](const auto& o) { assert(o.locked); }, *obj);

    win.execute_menu_action(static_cast<int>(pikdraw::desktop::MenuActionId::ObjectUnlock));
    snap = win.root().document().current_snapshot();
    obj = snap->objects().get(rect_id);
    assert(obj.has_value());
    std::visit([](const auto& o) { assert(!o.locked); }, *obj);

    // 3. Context menu for PowerClip container
    const auto circle_id = workflow.add_ellipse(15.0, 15.0, 20.0, 20.0);
    const auto clip_id = workflow.place_inside_powerclip(circle_id, rect_id);
    assert(clip_id != pikdraw::document::kInvalidObjectId);
    assert(workflow.is_clipping_container(clip_id));
    sel.clear();
    sel.select(clip_id, pikdraw::vector::SelectionMode::Replace);

    HMENU hClipMenu = win.build_context_menu({clip_id}, true);
    assert(hClipMenu != nullptr);
    DestroyMenu(hClipMenu);

    // Test toggle lock contents via context menu action
    win.execute_menu_action(static_cast<int>(pikdraw::desktop::MenuActionId::ObjectPowerClipToggleLock));
    snap = win.root().document().current_snapshot();
    auto clip_obj = snap->objects().get(clip_id);
    assert(clip_obj.has_value());
    if (const auto* grp = std::get_if<pikdraw::document::GroupObject>(&(*clip_obj))) {
        assert(grp->clip.has_value());
        assert(!grp->clip->lock_contents_to_frame);
    }

    // 4. Context menu for Multi-Selection (2+ objects)
    const auto shape2 = workflow.add_rectangle(100.0, 100.0, 40.0, 40.0);
    const auto shape3 = workflow.add_rectangle(200.0, 100.0, 40.0, 40.0);
    sel.clear();
    sel.select(shape2, pikdraw::vector::SelectionMode::Add);
    sel.select(shape3, pikdraw::vector::SelectionMode::Add);
    assert(sel.count() == 2);

    HMENU hMultiMenu = win.build_context_menu({shape2, shape3}, true);
    assert(hMultiMenu != nullptr);
    const int multiCount = GetMenuItemCount(hMultiMenu);
    assert(multiCount >= 6); // Group, PowerClip, Shaping submenu, Align submenu, Order submenu, etc.
    DestroyMenu(hMultiMenu);

    // Test reverse order
    win.execute_menu_action(static_cast<int>(pikdraw::desktop::MenuActionId::ObjectOrderReverse));

    // Test multi-object group via context menu
    win.execute_menu_action(static_cast<int>(pikdraw::desktop::MenuActionId::ObjectGroup));
    assert(sel.count() == 1);
    assert(workflow.is_group(sel.selected_ids().front()));

    std::cout << "[PASS] test_smart_context_menu_suite\n";
}


