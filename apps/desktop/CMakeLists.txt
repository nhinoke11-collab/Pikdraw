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
    src/themes/theme_tokens.cpp
    src/development/ui_reload_controller.cpp
    src/runtime/composition_root.cpp
    src/workflows/document_workflow.cpp
    src/shell/native_window.cpp
    src/shell/native_window_events.cpp
    src/shell/native_window_canvas.cpp
    src/shell/native_window_selection.cpp
    src/shell/native_window_selection_paint.cpp
    src/shell/native_window_style.cpp
    src/shell/native_window_style_events.cpp
    src/shell/native_window_style_paint.cpp
    src/shell/native_window_color_profiles.cpp
    src/shell/native_style_dialog.cpp
    src/shell/native_window_files.cpp
    src/shell/native_window_paint.cpp
    src/shell/native_window_paint_menu.cpp
    src/shell/native_window_paint_home.cpp
    src/shell/native_window_paint_modal.cpp
)

target_include_directories(pikdraw_desktop PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(pikdraw_desktop PUBLIC
    pikdraw_geometry
    pikdraw_color
    pikdraw_typography
    pikdraw_document
    pikdraw_rendering
    pikdraw_vector
    pikdraw_interop
)

if(WIN32)
    target_link_libraries(pikdraw_desktop PUBLIC user32 gdi32 msimg32 comdlg32)
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
endif()
