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
    src/themes/theme_tokens.cpp
    src/development/ui_reload_controller.cpp
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
endif()
