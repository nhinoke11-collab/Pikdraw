cmake_minimum_required(VERSION 3.25)
project(pikdraw_rendering LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pikdraw_rendering STATIC
    src/software_rasterizer.cpp
    src/renderer.cpp
    src/raster/path_flatten.cpp
    src/raster/path_paint.cpp
    src/raster/path_stroke.cpp
    src/path_adapter/authoring_path_adapter.cpp
)

target_include_directories(pikdraw_rendering PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(pikdraw_rendering PUBLIC
    pikdraw_document
    pikdraw_geometry
    pikdraw_color
    pikdraw_typography
)

if(PIKDRAW_BUILD_TESTS)
    macro(add_rendering_test target_name source_file)
        add_executable(${target_name} ${source_file})
        target_include_directories(${target_name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        target_link_libraries(${target_name} PRIVATE
            pikdraw_rendering
            pikdraw_document
            pikdraw_geometry
            pikdraw_color
            pikdraw_typography
        )
        add_test(NAME ${target_name} COMMAND ${target_name})
    endmacro()

    add_rendering_test(test_pikdraw_rendering_pixels tests/rasterizer/test_pixel_semantics.cpp)
    add_rendering_test(test_pikdraw_rendering_camera tests/camera/test_camera.cpp)
    add_rendering_test(test_pikdraw_rendering_culling tests/culling/test_culling.cpp)
    add_rendering_test(test_pikdraw_rendering_rasterizer tests/rasterizer/test_rasterizer.cpp)
    add_rendering_test(test_pikdraw_rendering_authoring_path_adapter tests/rasterizer/test_authoring_path_adapter.cpp)
endif()
