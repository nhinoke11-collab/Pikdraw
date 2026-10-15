cmake_minimum_required(VERSION 3.25)
project(pikdraw_desktop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Widgets QUIET)
if(NOT Qt6Widgets_FOUND)
    message(FATAL_ERROR "apps/desktop: Qt6::Widgets is required to build pikdraw_spike_a_viewport.")
endif()

if(NOT TARGET pikdraw_rendering_skia)
    message(FATAL_ERROR "apps/desktop: Target pikdraw_rendering_skia is required to build pikdraw_spike_a_viewport. Enable PIKDRAW_ENABLE_SKIA and provide Skia.")
endif()

add_executable(pikdraw_spike_a_viewport
    include/pikdraw/desktop/viewport_widget.hpp
    src/main_viewport.cpp
    src/viewport_widget.cpp
)

target_include_directories(pikdraw_spike_a_viewport PRIVATE
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/rendering/include
)

target_link_libraries(pikdraw_spike_a_viewport PRIVATE
    Qt6::Widgets
    pikdraw_rendering_skia
    pikdraw_rendering_core
)
