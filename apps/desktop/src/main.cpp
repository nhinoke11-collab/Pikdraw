#include "pikdraw/desktop/shell/native_window.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::cout << "PikDraw Professional Vector Editor 2026 (Milestone M1 Native Shell)\n";

    bool screenshot_mode = false;
    std::string screenshot_path;
    bool headless = false;
    bool mcp_enabled = false;
    bool mcp_edit_access = false;
    int timeout_ms = 0;
    pikdraw::desktop::UILanguage initial_lang = pikdraw::desktop::UILanguage::English;
    pikdraw::desktop::ThemeMode initial_theme = pikdraw::desktop::ThemeMode::Light;
    bool open_settings = false;
    int settings_tab = 0;
    bool open_home = false;
    int home_tab = 0;
    bool open_file_menu = false;
    bool open_new_doc_modal = false;
    bool create_tabs = false;
    bool demo_objects = false;
    bool demo_group = false;
    bool demo_isolation = false;
    bool demo_solid_selected = false;
    bool demo_solid_weld = false;
    bool demo_solid_trim = false;
    bool demo_knife = false;
    bool demo_vector_bitmap = false;
    int tool_id = -1;
    int flyout_group = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--screenshot" && i + 1 < argc) {
            screenshot_mode = true;
            screenshot_path = argv[++i];
        } else if (arg == "--headless") {
            headless = true;
        } else if (arg == "--enable-mcp") {
            mcp_enabled = true;
        } else if (arg == "--enable-mcp-edit") {
            mcp_enabled = true;
            mcp_edit_access = true;
        } else if (arg == "--timeout" && i + 1 < argc) {
            timeout_ms = std::stoi(argv[++i]) * 1000;
        } else if (arg == "--lang" && i + 1 < argc) {
            std::string l = argv[++i];
            initial_lang = (l == "en" || l == "EN") ?
                pikdraw::desktop::UILanguage::English :
                pikdraw::desktop::UILanguage::Vietnamese;
        } else if (arg == "--theme" && i + 1 < argc) {
            std::string t = argv[++i];
            initial_theme = (t == "light" || t == "LIGHT" || t == "Light") ?
                pikdraw::desktop::ThemeMode::Light :
                pikdraw::desktop::ThemeMode::CyberDark;
        } else if (arg == "--settings" || arg == "--settings-modal") {
            open_settings = true;
        } else if (arg == "--settings-tab" && i + 1 < argc) {
            open_settings = true;
            settings_tab = std::stoi(argv[++i]);
        } else if (arg == "--home" || arg == "--welcome") {
            open_home = true;
        } else if (arg == "--home-tab" && i + 1 < argc) {
            open_home = true;
            home_tab = std::stoi(argv[++i]);
        } else if (arg == "--file-menu") {
            open_file_menu = true;
        } else if (arg == "--new-doc-modal") {
            open_new_doc_modal = true;
        } else if (arg == "--create-tabs") {
            create_tabs = true;
        } else if (arg == "--demo-objects") {
            demo_objects = true;
        } else if (arg == "--demo-group") {
            demo_group = true;
        } else if (arg == "--demo-isolation") {
            demo_isolation = true;
        } else if (arg == "--demo-solid-selected") {
            demo_solid_selected = true;
        } else if (arg == "--demo-solid-weld") {
            demo_solid_weld = true;
        } else if (arg == "--demo-solid-trim") {
            demo_solid_trim = true;
        } else if (arg == "--demo-knife") {
            demo_knife = true;
        } else if (arg == "--demo-vector-bitmap") {
            demo_vector_bitmap = true;
        } else if (arg == "--tool" && i + 1 < argc) {
            tool_id = std::stoi(argv[++i]);
        } else if (arg == "--flyout" && i + 1 < argc) {
            flyout_group = std::stoi(argv[++i]);
        }
    }

    pikdraw::desktop::NativeWindow window(1440, 900);
    if (mcp_enabled) window.enable_mcp(mcp_edit_access);
    window.set_language(initial_lang);
    window.set_theme(initial_theme);
    if (tool_id >= 0) {
        window.set_active_tool(tool_id);
    }
    if (flyout_group > 0) {
        const int fy = (flyout_group == 1) ? 154 : (flyout_group == 2) ? 196 : 364;
        window.handle_mouse_down(42, fy);
        window.handle_mouse_up(42, fy);
    }
    if (create_tabs) {
        window.create_new_tab("Pano Đô Thị (CMYK)", 2000.0, 1000.0, "CMYK", 150);
        window.create_new_tab("Logo Nhãn Hàng (RGB)", 100.0, 100.0, "RGB", 300);
        window.switch_to_tab(0);
    }
    if (demo_objects) {
        const auto id1 = window.workflow().add_rectangle(30.0, 30.0, 120.0, 70.0, "#00f2c3", "#0ea5e9", 2.0);
        window.workflow().add_ellipse(180.0, 40.0, 80.0, 80.0, "#f59e0b", "#d97706", 2.0);
        if (id1 != pikdraw::document::kInvalidObjectId) {
            window.root().vector_controller().selection().select(id1);
        }
    }
    if (demo_solid_selected) {
        const auto id1 = window.workflow().add_rectangle(40.0, 40.0, 100.0, 70.0, "#00f2c3", "#0ea5e9", 2.0);
        const auto id2 = window.workflow().add_rectangle(100.0, 70.0, 90.0, 80.0, "#f59e0b", "#d97706", 2.0);
        window.root().vector_controller().selection().clear();
        window.root().vector_controller().selection().select(id1, pikdraw::vector::SelectionMode::Add);
        window.root().vector_controller().selection().select(id2, pikdraw::vector::SelectionMode::Add);
    }
    if (demo_solid_weld) {
        const auto id1 = window.workflow().add_rectangle(40.0, 40.0, 100.0, 70.0, "#00f2c3", "#0ea5e9", 2.0);
        const auto id2 = window.workflow().add_rectangle(100.0, 70.0, 90.0, 80.0, "#f59e0b", "#d97706", 2.0);
        window.root().vector_controller().selection().clear();
        window.root().vector_controller().selection().select(id1, pikdraw::vector::SelectionMode::Add);
        window.root().vector_controller().selection().select(id2, pikdraw::vector::SelectionMode::Add);
        window.trigger_boolean_operation(pikdraw::geometry::BooleanOp::Union);
    }
    if (demo_solid_trim) {
        const auto id1 = window.workflow().add_rectangle(40.0, 40.0, 120.0, 90.0, "#00f2c3", "#0ea5e9", 2.0);
        const auto id2 = window.workflow().add_rectangle(110.0, 70.0, 80.0, 80.0, "#f59e0b", "#d97706", 2.0);
        window.root().vector_controller().selection().clear();
        window.root().vector_controller().selection().select(id1, pikdraw::vector::SelectionMode::Add);
        window.root().vector_controller().selection().select(id2, pikdraw::vector::SelectionMode::Add);
        window.workflow().boolean_operation_selected(pikdraw::geometry::BooleanOp::Difference, true);
        window.workflow().transform_object(id2, 35.0, 15.0);
        window.root().vector_controller().selection().clear();
        window.root().vector_controller().selection().select(id2);
    }
    if (demo_knife) {
        window.set_active_tool(5); // Knife Tool
        const auto id1 = window.workflow().add_rectangle(40.0, 40.0, 140.0, 90.0, "#00f2c3", "#0ea5e9", 2.0);
        window.root().vector_controller().selection().clear();
        window.root().vector_controller().selection().select(id1);
        // Execute knife cut across (105, 30) -> (105, 140) with 30mm separation offset!
        window.workflow().split_objects_by_line({105.0, 30.0}, {105.0, 140.0}, 30.0);
    }
    if (demo_vector_bitmap) {
        window.set_active_tool(5); // Knife Tool
        // 1. Photographic bitmap image cut into two separated halves
        const auto bmp_id = window.workflow().add_bitmap_image(20.0, 110.0, 100.0, 75.0, "Sunset Bitmap Asset");
        window.root().vector_controller().selection().clear();
        window.root().vector_controller().selection().select(bmp_id);
        // Knife cut across x = 65 mm with 35mm separation offset
        window.workflow().split_objects_by_line({65.0, 95.0}, {65.0, 200.0}, 35.0);

        // 2. Second photographic bitmap trimmed by a Vector Circle (Cookie-Cutter)
        const auto bmp2_id = window.workflow().add_bitmap_image(20.0, 25.0, 65.0, 65.0, "Photo Patch Sample");
        const auto vec_cutter = window.workflow().add_ellipse(52.5, 57.5, 18.0, 18.0, "#00f2c3", "#0ea5e9", 2.0);
        window.workflow().trim_bitmap_with_vector(bmp2_id, vec_cutter, 45.0);

        // 3. Vector with Vector Knife Cut Demo beside it on the right
        const auto v_id = window.workflow().add_rectangle(140.0, 25.0, 90.0, 65.0, "#00f2c3", "#0ea5e9", 2.0);
        window.root().vector_controller().selection().clear();
        window.root().vector_controller().selection().select(v_id);
        window.workflow().split_objects_by_line({180.0, 15.0}, {180.0, 105.0}, 30.0);

        // Select the trimmed patch
        const auto snap = window.root().document().current_snapshot();
        for (const auto& [id, obj] : snap->objects().all()) {
            if (const auto* im = std::get_if<pikdraw::document::ImageObject>(&obj)) {
                if (im->name.find("Trimmed") != std::string::npos) {
                    window.root().vector_controller().selection().select(id, pikdraw::vector::SelectionMode::Replace);
                    break;
                }
            }
        }
    }
    if (demo_group) {
        const auto id1 = window.workflow().add_rectangle(35.0, 35.0, 100.0, 60.0, "#00f2c3", "#0ea5e9", 2.0);
        const auto id2 = window.workflow().add_ellipse(170.0, 65.0, 35.0, 35.0, "#f59e0b", "#d97706", 2.0);
        window.root().vector_controller().selection().clear();
        window.root().vector_controller().selection().select(id1, pikdraw::vector::SelectionMode::Add);
        window.root().vector_controller().selection().select(id2, pikdraw::vector::SelectionMode::Add);
        const auto gid = window.workflow().group_selected();
        if (gid != pikdraw::document::kInvalidObjectId) {
            window.root().vector_controller().selection().select(gid, pikdraw::vector::SelectionMode::Replace);
        }
    }
    if (demo_isolation) {
        window.workflow().add_rectangle(230.0, 40.0, 50.0, 80.0, "#8b5cf6", "#7c3aed", 2.0);
        const auto id1 = window.workflow().add_rectangle(35.0, 35.0, 100.0, 60.0, "#00f2c3", "#0ea5e9", 2.0);
        const auto id2 = window.workflow().add_ellipse(170.0, 65.0, 35.0, 35.0, "#f59e0b", "#d97706", 2.0);
        window.root().vector_controller().selection().clear();
        window.root().vector_controller().selection().select(id1, pikdraw::vector::SelectionMode::Add);
        window.root().vector_controller().selection().select(id2, pikdraw::vector::SelectionMode::Add);
        const auto gid = window.workflow().group_selected();
        if (gid != pikdraw::document::kInvalidObjectId) {
            window.enter_group_isolation(gid);
            window.root().vector_controller().selection().select(id1, pikdraw::vector::SelectionMode::Replace);
        }
    }
    if (open_settings) {
        window.open_settings_modal();
        window.set_settings_tab(settings_tab);
    }
    if (open_home) {
        window.open_home_screen();
        window.set_active_home_tab(home_tab);
    }
    if (open_file_menu) {
        window.open_file_menu();
    }
    if (open_new_doc_modal) {
        window.open_new_doc_modal();
    }

    const auto area = window.shell().canvas_area();
    std::cout << "Shell Initialized:\n"
              << "  Canvas Viewport Area: " << area.width << " x " << area.height
              << " (Offset X=" << area.x << ", Y=" << area.y << ")\n"
              << "  Registered Actions: " << window.shell().action_dispatcher().action_count() << "\n"
              << "  Active Workspace: " << window.shell().workspace_state().active_info().title_vi << "\n"
              << "  Status: " << window.shell().status_bar().format_coordinates_vi() << "\n";

    if (screenshot_mode) {
        window.capture_screenshot_bmp(screenshot_path);
        std::cout << "Screenshot captured to: " << screenshot_path << "\n";
        return 0;
    }

    if (headless) {
        return 0;
    }

    const std::string win_title = (initial_lang == pikdraw::desktop::UILanguage::Vietnamese) ?
        "PikDraw 2026 - Bản vẽ không tên [Vector Workspace]" :
        "PikDraw 2026 - Untitled Document [Vector Workspace]";
    if (!window.initialize(win_title)) {
        std::cerr << "Failed to create native desktop window.\n";
        return 1;
    }

#ifdef _WIN32
    HWND console_hwnd = GetConsoleWindow();
    if (console_hwnd) {
        ShowWindow(console_hwnd, SW_HIDE);
    }

    if (timeout_ms > 0 && window.hwnd()) {
        SetTimer(window.hwnd(), 1001, timeout_ms, [](HWND hwnd, UINT, UINT_PTR, DWORD) {
            DestroyWindow(hwnd);
        });
    }
#endif

    return window.run();
}
