#include "pikdraw/desktop/shell/native_window.hpp"
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <shellapi.h>
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // The narrow CRT argv uses the active ANSI codepage. Read Windows' Unicode
    // arguments and normalize to UTF-8 once, including Vietnamese file paths.
    int wide_argc = 0;
    auto* wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
    if (!wide_argv) return 1;
    std::vector<std::string> utf8_arguments;
    utf8_arguments.reserve(wide_argc);
    for (int index = 0; index < wide_argc; ++index)
        utf8_arguments.push_back(pikdraw::desktop::NativeWindow::wide_to_utf8(wide_argv[index]));
    LocalFree(wide_argv);
    std::vector<char*> normalized_argv;
    normalized_argv.reserve(utf8_arguments.size());
    for (auto& argument : utf8_arguments) normalized_argv.push_back(argument.data());
    argc = wide_argc;
    argv = normalized_argv.data();
#endif
    std::cout << "PikDraw Professional Vector Editor 2026 (Milestone M1 Native Shell)\n";

    bool screenshot_mode = false;
    std::string screenshot_path;
    std::string import_file_path;
    bool open_as_import = false;
    bool headless = false;
    bool auto_accept_import_review = false;
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
    bool demo_powerclip = false;
    bool demo_powerclip_edit = false;
    bool demo_rect_scale = false;
    bool demo_pivot = false;
    int tool_id = -1;
    int flyout_group = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--open" || arg == "--open-file" || arg == "--import") && i + 1 < argc) {
            import_file_path = argv[++i];
            open_as_import = (arg == "--import");
        } else if (arg == "--screenshot" && i + 1 < argc) {
            screenshot_mode = true;
            screenshot_path = argv[++i];
        } else if (arg == "--headless") {
            headless = true;
        } else if (arg == "--accept-import-loss") {
            auto_accept_import_review = true;
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
        } else if (arg == "--demo-powerclip") {
            demo_powerclip = true;
        } else if (arg == "--demo-powerclip-edit") {
            demo_powerclip_edit = true;
        } else if (arg == "--demo-rect-scale") {
            demo_rect_scale = true;
        } else if (arg == "--demo-pivot") {
            demo_pivot = true;
        } else if (arg == "--tool" && i + 1 < argc) {
            tool_id = std::stoi(argv[++i]);
        } else if (arg == "--flyout" && i + 1 < argc) {
            flyout_group = std::stoi(argv[++i]);
        }
    }

    pikdraw::desktop::NativeWindow window(1440, 900);
    if (headless && auto_accept_import_review)
        window.set_auto_accept_import_review(true);
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
        window.workflow().create_bitmap_crop_from_vector_bounds(bmp2_id, vec_cutter, 45.0);

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
    if (demo_pivot) {
        const auto rect_id = window.workflow().add_rectangle(55.0, 50.0, 110.0, 65.0, "#00f2c3", "#0ea5e9", 2.0);
        window.root().vector_controller().selection().clear();
        window.root().vector_controller().selection().select(rect_id, pikdraw::vector::SelectionMode::Replace);
        window.set_custom_pivot_world(pikdraw::geometry::Point2D{55.0, 50.0});
    }
    if (demo_rect_scale) {
        const auto rect_id = window.workflow().add_rectangle(45.0, 45.0, 100.0, 55.0, "#00f2c3", "#0ea5e9", 2.0);
        window.root().vector_controller().selection().clear();
        window.root().vector_controller().selection().select(rect_id, pikdraw::vector::SelectionMode::Replace);
        window.sync_rect_corner_controls_from_selection();
        pikdraw::vector::RectCornerSettings s;
        s.type = pikdraw::vector::RectCornerType::Round;
        s.tl = 15.0; s.tr = 15.0; s.br = 15.0; s.bl = 15.0;
        s.locked = true;
        s.scale_relative = false;
        window.apply_rect_corner_settings(s);
        window.workflow().resize_object(rect_id, 210.0, 55.0);
        window.sync_rect_corner_controls_from_selection();
        window.root().vector_controller().selection().select(rect_id, pikdraw::vector::SelectionMode::Replace);
    }
    if (demo_powerclip || demo_powerclip_edit) {
        // Create 4 distinct objects inside the clipping container
        const auto c1 = window.workflow().add_rectangle(50.0, 40.0, 50.0, 38.0, "#ef4444", "#b91c1c", 1.5);
        const auto c2 = window.workflow().add_ellipse(105.0, 40.0, 42.0, 42.0, "#f59e0b", "#d97706", 1.5);
        const auto c3 = window.workflow().add_rectangle(65.0, 72.0, 52.0, 36.0, "#8b5cf6", "#7c3aed", 1.5);
        const auto c4 = window.workflow().add_ellipse(115.0, 68.0, 48.0, 48.0, "#3b82f6", "#1d4ed8", 1.5);

        // Create cyan outer frame container (on top)
        const auto frame_id = window.workflow().add_rectangle(40.0, 30.0, 140.0, 95.0, "#00f2c3", "#0ea5e9", 2.0);

        window.root().vector_controller().selection().clear();
        window.root().vector_controller().selection().select(c1, pikdraw::vector::SelectionMode::Add);
        window.root().vector_controller().selection().select(c2, pikdraw::vector::SelectionMode::Add);
        window.root().vector_controller().selection().select(c3, pikdraw::vector::SelectionMode::Add);
        window.root().vector_controller().selection().select(c4, pikdraw::vector::SelectionMode::Add);
        window.root().vector_controller().selection().select(frame_id, pikdraw::vector::SelectionMode::Add);

        const auto clip_id = window.workflow().make_clipping_mask_selected(true);
        if (clip_id != pikdraw::document::kInvalidObjectId) {
            // Shift c4 to partially stick out of the right edge of frame (at x = 180) to show clipping
            window.workflow().move_object(c4, 155.0, 60.0);

            if (demo_powerclip_edit) {
                // Enter isolation mode and select c4 to show editing inside
                window.enter_group_isolation(clip_id);
                window.root().vector_controller().selection().select(c4, pikdraw::vector::SelectionMode::Replace);
            } else {
                window.root().vector_controller().selection().select(clip_id, pikdraw::vector::SelectionMode::Replace);
            }
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

    const std::string win_title = (initial_lang == pikdraw::desktop::UILanguage::Vietnamese) ?
        "PikDraw 2026 - Bản vẽ không tên [Vector Workspace]" :
        "PikDraw 2026 - Untitled Document [Vector Workspace]";
    if (!window.initialize(win_title)) {
        std::cerr << "Failed to create native desktop window.\n";
        return 1;
    }
    window.close_recovery_modal();

    if (screenshot_mode || headless) {
#ifdef _WIN32
        if (window.hwnd()) ShowWindow(window.hwnd(), SW_HIDE);
#endif
    }

    if (!import_file_path.empty()) {
        const std::wstring wide_path = pikdraw::desktop::NativeWindow::utf8_to_wide(import_file_path);
        std::cout << "Importing file: " << import_file_path << "...\n";
        const bool started = window.import_file_to_tab(wide_path, !open_as_import);
        const auto timeout_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (window.import_job_active() && std::chrono::steady_clock::now() < timeout_deadline) {
#ifdef _WIN32
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
#endif
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
#ifdef _WIN32
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
#endif
        if ((headless || screenshot_mode) &&
            (!started || window.import_job_active() || !window.last_import_succeeded())) {
            std::cerr << "Import did not complete successfully. Status: " << window.status_message() << "\n";
            return 2;
        }
        std::cout << "Import completed. Status: " << window.status_message() << "\n";
        window.close_recovery_modal();
        window.fit_page_to_canvas();
#ifdef _WIN32
        if (window.hwnd()) {
            InvalidateRect(window.hwnd(), nullptr, TRUE);
            UpdateWindow(window.hwnd());
            for (int frame = 0; frame < 5; ++frame) {
                MSG msg;
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
#endif
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
