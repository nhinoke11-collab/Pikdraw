cmake_minimum_required(VERSION 3.25)
project(pikdraw_desktop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Desktop core shell and presenters library
add_library(pikdraw_desktop STATIC
    src/shell/shell_geometry.cpp
    src/shell/workspace_state.cpp
    src/shell/status_bar_model.cpp
    src/shell/desktop_window.cpp
    src/actions/action_dispatcher.cpp
    src/presenters/tool_strip_presenter.cpp
    src/presenters/ribbon_presenter.cpp
    src/presenters/inspector_presenter.cpp
    src/presenters/object_style.cpp
    src/presenters/gradient_properties.cpp
    src/themes/theme_tokens.cpp
    src/development/ui_reload_controller.cpp
    src/runtime/composition_root.cpp
    src/workflows/document_workflow.cpp
    src/workflows/document_workflow_clipboard_transform.cpp
    src/workflows/document_workflow_appearance.cpp
    src/workflows/document_workflow_io_shaping.cpp
    src/clipboard/native_clipboard.cpp
    src/tools/drawing_preferences.cpp
    src/tools/drawing_tool_controller.cpp
    src/tools/pen_tool_controller.cpp
    src/tools/node_tool_controller.cpp
    src/tools/contour_tool_controller.cpp
    src/tools/gradient_tool_controller.cpp
    src/tools/native_drawing_input.cpp
    src/tools/snap_index_worker.cpp
    src/tools/snap_status_controller.cpp
    src/tools/dimension_tool_controller.cpp
    src/shell/native_window.cpp
    src/shell/native_window_settings.cpp
    src/shell/native_window_text_align.cpp
    src/shell/native_window_tooltips_guides.cpp
    src/shell/native_window_numeric_input.cpp
    src/automation/automation_pipe_host.cpp
    src/automation/native_window_automation.cpp
    src/shell/native_window_events.cpp
    src/shell/native_window_tool_events.cpp
    src/shell/native_window_modal_clicks.cpp
    src/shell/native_window_chrome_clicks.cpp
    src/shell/native_window_shortcuts.cpp
    src/shell/native_window_canvas.cpp
    src/shell/native_window_canvas_overlays.cpp
    src/shell/native_window_selection.cpp
    src/shell/native_window_selection_paint.cpp
    src/shell/native_window_style.cpp
    src/shell/native_window_style_events.cpp
    src/shell/native_window_style_paint.cpp
    src/shell/native_window_color_profiles.cpp
    src/shell/native_style_dialog.cpp
    src/shell/native_window_files.cpp
    src/shell/native_window_paint_icons.cpp
    src/shell/native_window_paint_docks.cpp
    src/shell/native_window_paint.cpp
    src/shell/native_window_paint_menu.cpp
    src/shell/native_window_paint_home.cpp
    src/shell/native_window_paint_modal.cpp
    src/shell/native_window_paint_settings_modal.cpp
)

target_include_directories(pikdraw_desktop PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

target_link_libraries(pikdraw_desktop PUBLIC
    pikdraw_geometry
    pikdraw_color
    pikdraw_typography
    pikdraw_document
    pikdraw_rendering
    pikdraw_vector
    pikdraw_interop
    pikdraw_automation
)

if(WIN32)
    target_link_libraries(pikdraw_desktop PUBLIC user32 gdi32 msimg32 comdlg32 comctl32)
    if(MSVC)
        target_compile_options(pikdraw_desktop PRIVATE /utf-8)
    endif()
endif()

# Native shell application entry point
add_executable(pikdraw
    src/main.cpp
)

target_link_libraries(pikdraw PRIVATE
    pikdraw_desktop
)

# Native unit and lifecycle tests
if(PIKDRAW_BUILD_TESTS)
    add_executable(test_pikdraw_desktop_shell
        tests/test_desktop_shell.cpp
    )
    target_link_libraries(test_pikdraw_desktop_shell PRIVATE
        pikdraw_desktop
    )
    add_test(NAME test_pikdraw_desktop_shell COMMAND test_pikdraw_desktop_shell)

    add_executable(test_pikdraw_effect_draft
        tests/test_effect_draft.cpp
    )
    target_link_libraries(test_pikdraw_effect_draft PRIVATE
        pikdraw_desktop
    )
    add_test(NAME test_pikdraw_effect_draft COMMAND test_pikdraw_effect_draft)

    add_executable(test_pikdraw_effect_render_warning
        tests/test_effect_render_warning.cpp
    )
    target_link_libraries(test_pikdraw_effect_render_warning PRIVATE
        pikdraw_desktop
    )
    add_test(NAME test_pikdraw_effect_render_warning COMMAND test_pikdraw_effect_render_warning)

    add_executable(test_pikdraw_desktop_actions
        tests/test_desktop_actions.cpp
    )
    target_link_libraries(test_pikdraw_desktop_actions PRIVATE
        pikdraw_desktop
    )
    add_test(NAME test_pikdraw_desktop_actions COMMAND test_pikdraw_desktop_actions)

    add_executable(test_pikdraw_desktop_reload
        tests/test_desktop_reload.cpp
    )
    target_link_libraries(test_pikdraw_desktop_reload PRIVATE
        pikdraw_desktop
    )
    add_test(NAME test_pikdraw_desktop_reload COMMAND test_pikdraw_desktop_reload)

    add_executable(test_pikdraw_desktop_composition
        tests/test_desktop_composition.cpp
    )
    target_link_libraries(test_pikdraw_desktop_composition PRIVATE
        pikdraw_desktop
    )
    add_test(NAME test_pikdraw_desktop_composition COMMAND test_pikdraw_desktop_composition)

    add_executable(test_pikdraw_vertical_slice_e2e
        tests/test_vertical_slice_e2e.cpp
    )
    target_link_libraries(test_pikdraw_vertical_slice_e2e PRIVATE
        pikdraw_desktop
    )
    add_test(NAME test_pikdraw_vertical_slice_e2e COMMAND test_pikdraw_vertical_slice_e2e)

    add_executable(test_pikdraw_canvas_view
        tests/test_canvas_view.cpp
    )
    target_link_libraries(test_pikdraw_canvas_view PRIVATE
        pikdraw_desktop
    )
    add_test(NAME test_pikdraw_canvas_view COMMAND test_pikdraw_canvas_view)
    add_executable(test_pikdraw_editor_tools tests/test_editor_tools.cpp)
    target_link_libraries(test_pikdraw_editor_tools PRIVATE pikdraw_desktop)
    add_test(NAME test_pikdraw_editor_tools COMMAND test_pikdraw_editor_tools)
    add_executable(benchmark_pikdraw_editor_tools tests/benchmark_editor_tools.cpp)
    target_link_libraries(benchmark_pikdraw_editor_tools PRIVATE pikdraw_desktop)

    add_executable(test_pikdraw_drawing_tool_controller tests/test_drawing_tool_controller.cpp)
    target_link_libraries(test_pikdraw_drawing_tool_controller PRIVATE pikdraw_desktop)
    add_test(NAME test_pikdraw_drawing_tool_controller COMMAND test_pikdraw_drawing_tool_controller)

    add_executable(test_pikdraw_drawing_tool_ui tests/test_drawing_tool_ui.cpp)
    target_link_libraries(test_pikdraw_drawing_tool_ui PRIVATE pikdraw_desktop)
    add_test(NAME test_pikdraw_drawing_tool_ui COMMAND test_pikdraw_drawing_tool_ui)

    add_executable(test_pikdraw_pen_native_shell tests/test_pen_native_shell.cpp)
    target_link_libraries(test_pikdraw_pen_native_shell PRIVATE pikdraw_desktop)
    add_test(NAME test_pikdraw_pen_native_shell COMMAND test_pikdraw_pen_native_shell)

    add_executable(test_pikdraw_node_tool_controller tests/test_node_tool_controller.cpp)
    target_link_libraries(test_pikdraw_node_tool_controller PRIVATE pikdraw_desktop)
    add_test(NAME test_pikdraw_node_tool_controller COMMAND test_pikdraw_node_tool_controller)

    add_executable(test_pikdraw_contour_tool_controller
        tests/test_contour_tool_controller.cpp
        src/tools/contour_tool_controller.cpp
    )
    target_include_directories(test_pikdraw_contour_tool_controller PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(test_pikdraw_contour_tool_controller PRIVATE
        pikdraw_document
        pikdraw_vector
        pikdraw_geometry
    )
    add_test(NAME test_pikdraw_contour_tool_controller COMMAND test_pikdraw_contour_tool_controller)

    add_executable(test_pikdraw_snap_status_controller
        tests/test_snap_status_controller.cpp
        src/tools/snap_status_controller.cpp
    )
    target_include_directories(test_pikdraw_snap_status_controller PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(test_pikdraw_snap_status_controller PRIVATE
        pikdraw_document
        pikdraw_vector
        pikdraw_geometry
    )
    add_test(NAME test_pikdraw_snap_status_controller COMMAND test_pikdraw_snap_status_controller)

    add_executable(test_pikdraw_dimension_tool_controller
        tests/test_dimension_tool_controller.cpp
        src/tools/dimension_tool_controller.cpp
    )
    target_include_directories(test_pikdraw_dimension_tool_controller PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(test_pikdraw_dimension_tool_controller PRIVATE
        pikdraw_document
        pikdraw_vector
        pikdraw_geometry
    )
    add_test(NAME test_pikdraw_dimension_tool_controller COMMAND test_pikdraw_dimension_tool_controller)
 
    add_executable(test_pikdraw_gradient_tool_controller
        tests/test_gradient_tool_controller.cpp
        src/tools/gradient_tool_controller.cpp
        src/presenters/gradient_properties.cpp
    )
    target_include_directories(test_pikdraw_gradient_tool_controller PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(test_pikdraw_gradient_tool_controller PRIVATE
        pikdraw_document
        pikdraw_vector
        pikdraw_geometry
        pikdraw_color
    )
    add_test(NAME test_pikdraw_gradient_tool_controller COMMAND test_pikdraw_gradient_tool_controller)

    add_executable(test_pikdraw_gradient_native_shell
        tests/test_gradient_native_shell.cpp
    )
    target_link_libraries(test_pikdraw_gradient_native_shell PRIVATE
        pikdraw_desktop
    )
    add_test(NAME test_pikdraw_gradient_native_shell COMMAND test_pikdraw_gradient_native_shell)
endif()
