cmake_minimum_required(VERSION 3.25)
project(pikdraw_rendering LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pikdraw_rendering_core STATIC
    src/types.cpp
    src/camera.cpp
    src/culling.cpp
    src/fixtures.cpp
    src/trace.cpp
)

target_include_directories(pikdraw_rendering_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

include(CTest)
if(BUILD_TESTING)
    enable_testing()
    add_executable(pikdraw_rendering_tests
        tests/test_main.cpp
    )
    target_link_libraries(pikdraw_rendering_tests PRIVATE
        pikdraw_rendering_core
    )
    add_test(NAME pikdraw_rendering_math_tests COMMAND pikdraw_rendering_tests)
endif()

option(PIKDRAW_ENABLE_SKIA "Enable Skia raster backend" OFF)
if(PIKDRAW_ENABLE_SKIA)
    find_path(SKIA_INCLUDE_DIR include/core/SkSurface.h
        HINTS ${SKIA_DIR} ${PIKDRAW_SKIA_DIR} $ENV{SKIA_DIR}
        PATH_SUFFIXES include
    )
    find_library(SKIA_LIBRARY
        NAMES skia
        HINTS ${SKIA_DIR} ${PIKDRAW_SKIA_DIR} $ENV{SKIA_DIR}
        PATH_SUFFIXES out/Release out/Debug lib
    )

    if(SKIA_INCLUDE_DIR AND SKIA_LIBRARY)
        message(STATUS "Skia found: ${SKIA_LIBRARY}")
        add_library(pikdraw_rendering_skia STATIC
            src/skia_backend.cpp
        )
        target_include_directories(pikdraw_rendering_skia PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            ${SKIA_INCLUDE_DIR}
            ${SKIA_INCLUDE_DIR}/..
        )
        target_link_libraries(pikdraw_rendering_skia PUBLIC
            pikdraw_rendering_core
            ${SKIA_LIBRARY}
        )

        if(BUILD_TESTING)
            add_executable(pikdraw_rendering_skia_tests
                tests/test_skia_backend.cpp
            )
            target_link_libraries(pikdraw_rendering_skia_tests PRIVATE
                pikdraw_rendering_skia
            )
            add_test(NAME pikdraw_rendering_skia_integration_tests
                COMMAND pikdraw_rendering_skia_tests
            )
        endif()
    else()
        message(FATAL_ERROR "PIKDRAW_ENABLE_SKIA is ON but Skia headers or library were not found.")
    endif()
endif()
