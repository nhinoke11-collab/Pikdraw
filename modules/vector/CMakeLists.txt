cmake_minimum_required(VERSION 3.25)
project(pikdraw_vector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pikdraw_vector STATIC
    src/vector_controller.cpp
    src/selection/hit_test.cpp
    src/selection/hit_geometry.cpp
    src/gestures/style_tool.cpp
    src/gestures/text_tool.cpp
)

target_include_directories(pikdraw_vector PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(pikdraw_vector PUBLIC
    pikdraw_document
    pikdraw_geometry
    pikdraw_color
    pikdraw_typography
)

if(PIKDRAW_BUILD_TESTS)
    macro(add_vector_test target_name source_file)
        add_executable(${target_name} ${source_file})
        target_include_directories(${target_name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        target_link_libraries(${target_name} PRIVATE
            pikdraw_vector
            pikdraw_document
            pikdraw_geometry
            pikdraw_color
            pikdraw_typography
        )
        add_test(NAME ${target_name} COMMAND ${target_name})
    endmacro()

    add_vector_test(test_pikdraw_vector_selection tests/selection/test_selection.cpp)
    add_vector_test(test_pikdraw_vector_shapes tests/tools/test_shape_tools.cpp)
    add_vector_test(test_pikdraw_vector_gestures tests/gestures/test_gestures.cpp)
endif()
