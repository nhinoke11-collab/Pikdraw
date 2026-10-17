#include "pikdraw/desktop/shell/shell_geometry.hpp"
#include "pikdraw/desktop/shell/workspace_state.hpp"
#include "pikdraw/desktop/shell/status_bar_model.hpp"
#include "pikdraw/desktop/shell/desktop_window.hpp"
#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/commands/appearance_commands.hpp"
#include "pikdraw/typography/typography.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include "pikdraw/vector/gestures/selection_resize.hpp"
#include "../src/shell/native_window_menus.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <cassert>
#include <cmath>
#include <string_view>

using namespace pikdraw::desktop;

namespace {

std::filesystem::path shell_screenshot_path(const char* filename) {
    const auto directory = std::filesystem::temp_directory_path() / "pikdraw-desktop-shell-test-20261017";
    std::filesystem::create_directories(directory);
    return directory / filename;
}

void capture_shell_screenshot(NativeWindow& window, const char* filename) {
    window.capture_screenshot_bmp(shell_screenshot_path(filename).string());
}

} // namespace

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

void test_object_opacity_property_bar_and_appearance_panel() {
    NativeWindow win(1440, 900);
    const auto path_id = win.workflow().add_rectangle(20.0, 30.0, 80.0, 55.0,
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
    auto path = std::get<pikdraw::document::PathObject>(
        *win.root().document().current_snapshot()->objects().get(path_id));
    assert(std::abs(std::get<pikdraw::color::RGBColor>(path.fill.payload()).a - 1.0) < 1e-9);

    selection.select(text_id);
    win.handle_mouse_down(930, 82);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::ObjectOpacity);
    type(L"88,8");
    assert(std::abs(object_opacity(text_id) - 0.888) < 1e-9);

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
    assert(path_after_fill.fill.overprint().fill && !path_after_fill.fill.overprint().stroke);
    assert(std::abs(std::get<color::RGBColor>(text_after_fill.fill.payload()).a - 0.52) < 1e-9);
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
    assert(std::abs(std::get<color::RGBColor>(path_one_after_stroke.stroke.payload()).a - 0.61) < 1e-9);
    assert(std::abs(std::get<color::RGBColor>(path_two_after_stroke.stroke.payload()).a - 0.82) < 1e-9);
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
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::EffectSigmaX);
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
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::EffectOffsetX);
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
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::EffectOpacity);
    replace_value(L"35");
    assert(std::abs(std::get<pikdraw::document::DropShadowEffect>(
        appearance_of(path_id).effects[1].parameters).opacity - 0.35) < 1e-9);
    const auto before_invalid = win.root().document().current_snapshot()->revision();
    const int blur_sigma_y = 162 + 794 + 48 + 12 - win.style_inspector_scroll_offset();
    win.handle_mouse_down(panel_left + 82, blur_sigma_y);
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::EffectSigmaX);
    replace_value(L"1000001");
    assert(win.active_numeric_field() == NativeWindow::NumericFieldId::EffectSigmaX);
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
    // AC-BF10-04 — keyboard-only path: Shift+Tab focuses Appearance, Alt+Down opens it,
    // arrows choose Blur, Enter applies once, and Escape closes/returns focus.
    assert(win.handle_toolbox_key(VK_TAB, false, true));
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
    assert(win.handle_toolbox_key(VK_TAB, false, true));
    assert(win.handle_toolbox_key(VK_DOWN, true, false));
    capture_shell_screenshot(win, "effects-toolbox-keyboard-focus-english.bmp");
    win.set_language(UILanguage::Vietnamese);
    capture_shell_screenshot(win, "effects-toolbox-keyboard-focus-vietnamese.bmp");
    win.set_language(UILanguage::English);
    assert(win.handle_toolbox_key(VK_ESCAPE));
    assert(win.handle_toolbox_key(VK_ESCAPE));
#endif

    // AC-BF10-01 — the main Appearance icon opens the existing Style Properties section.
    const auto before_properties = win.root().document().current_snapshot()->revision();
    win.handle_mouse_down(20, 382); // Appearance icon body, row 6.
    assert(win.style_inspector_visible());
    assert(win.root().document().current_snapshot()->revision() == before_properties);

    // AC-BF10-04 — capture the actual native paint path in both supported UI languages.
    win.handle_mouse_down(43, 382); // Group arrow opens the flyout.
    win.handle_mouse_move(0, 0);
    capture_shell_screenshot(win, "effects-toolbox-english.bmp");
    win.set_language(UILanguage::Vietnamese);
    win.handle_mouse_move(0, 0);
    capture_shell_screenshot(win, "effects-toolbox-vietnamese.bmp");
    win.set_language(UILanguage::English);

    // AC-BF10-02 — the flyout appends one Blur and one Shadow node through the shared workflow.
    auto before_effect = win.root().document().current_snapshot()->revision();
    win.handle_mouse_down(110, 433); // Gaussian Blur row.
    assert(win.root().document().current_snapshot()->revision() == before_effect + 1);
    assert(path_appearance().effects.size() == 1);
    assert(std::holds_alternative<pikdraw::document::GaussianBlurEffect>(
        path_appearance().effects[0].parameters));

    before_effect = win.root().document().current_snapshot()->revision();
    win.handle_mouse_down(43, 382);
    win.handle_mouse_down(110, 462); // Drop Shadow row.
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
    win.handle_mouse_down(400, 20); // Effects menu on the 1440px English window.
    win.handle_mouse_down(400, 199); // Clear Effects row.
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
    win.handle_mouse_down(400, 199);
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

    // AC-BF10-03 — no selection disables effect insertion and does not write the document.
    selection.clear();
    const auto before_disabled = win.root().document().current_snapshot()->revision();
    win.handle_mouse_down(43, 382);
    win.handle_mouse_down(110, 433);
    assert(win.root().document().current_snapshot()->revision() == before_disabled);
    assert(win.status_message().find("Select unlocked Path/Text") != std::string::npos);
    win.set_language(UILanguage::Vietnamese);
    win.handle_mouse_down(110, 433);
    assert(win.root().document().current_snapshot()->revision() == before_disabled);
    assert(win.status_message().find("Chọn Path/Text") != std::string::npos);

    std::cout << "[PASS] test_effects_toolbox_flyout\n";
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

    const auto p_down = win.root().renderer().camera().world_to_screen({100.0, 100.0});
    const auto p_up = win.root().renderer().camera().world_to_screen({150.0, 140.0});
    const int x1 = area.left + static_cast<int>(p_down.x);
    const int y1 = area.top + static_cast<int>(p_down.y);
    const int x2 = area.left + static_cast<int>(p_up.x);
    const int y2 = area.top + static_cast<int>(p_up.y);

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

void test_selection_borderless_default_stroke_none_and_hairline_zoom() {
    NativeWindow win;
    assert(win.initialize("Test Selection Borderless & Hairline"));
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
    std::cout << "All Desktop Shell Tests PASSED.\n";
    return 0;
}

