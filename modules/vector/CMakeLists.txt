cmake_minimum_required(VERSION 3.25)
project(pikdraw_vector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pikdraw_vector STATIC
    src/vector_controller.cpp
    src/selection/hit_test.cpp
    src/selection/hit_geometry.cpp
    src/selection/region_selection.cpp
    src/gestures/style_tool.cpp
    src/gestures/selection_resize.cpp
    src/gestures/text_tool.cpp
    src/gestures/text_operations.cpp
    src/interaction/tool_session.cpp
    src/interaction/draft_history.cpp
    src/interaction/draft_recovery.cpp
    src/interaction/input_router.cpp
    src/precision/numeric/numeric_input.cpp
    src/precision/snap/snap_config.cpp
    src/precision/snap/snap_index.cpp
    src/precision/snap/layout_snap.cpp
    src/precision/snap/snap_math.cpp
    src/precision/snap/direction_snap.cpp
    src/tools/line/path_drawing_tool.cpp
    src/tools/line/path_drawing_tool_commit.cpp
    src/tools/line/path_object_builder.cpp
    src/tools/pen/pen_drawing_tool.cpp
    src/tools/node/node_tool.cpp
    src/tools/cut_tool_controller.cpp
    src/tools/contour/contour_tool.cpp
    src/tools/gradient_tool.cpp
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
    add_vector_test(test_pikdraw_editor_selection tests/selection/test_editor_selection.cpp)
    add_vector_test(test_pikdraw_vector_tool_session tests/interaction/test_tool_session.cpp)
    add_vector_test(test_pikdraw_vector_input_router tests/interaction/test_input_router.cpp)
    add_vector_test(test_pikdraw_vector_numeric tests/precision/numeric/test_numeric_input.cpp)
    add_vector_test(test_pikdraw_vector_snap tests/precision/snap/test_snap.cpp)
    add_vector_test(test_pikdraw_vector_direction_snap tests/precision/snap/test_direction_snap.cpp)
    add_vector_test(test_pikdraw_vector_line_polyline tests/tools/line/test_path_drawing_tool.cpp)
    add_vector_test(test_pikdraw_vector_node tests/tools/node/test_node_tool.cpp)
    add_vector_test(test_pikdraw_vector_contour tests/tools/contour/test_contour_tool.cpp)
    add_vector_test(test_pikdraw_vector_cut_tool tests/tools/test_cut_tool_controller.cpp)
    add_vector_test(test_pikdraw_vector_gradient tests/tools/test_gradient_tool.cpp)
endif()
