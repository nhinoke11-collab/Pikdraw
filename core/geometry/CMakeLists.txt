cmake_minimum_required(VERSION 3.25)
project(pikdraw_geometry LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pikdraw_geometry STATIC
    src/primitives/transform.cpp
    src/curves/curve.cpp
    src/curves/path.cpp
    src/flattening/flattening.cpp
    src/operations/boolean.cpp
    src/operations/offset.cpp
)

target_include_directories(pikdraw_geometry PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(PIKDRAW_BUILD_TESTS)
    add_executable(test_pikdraw_geometry
        tests/test_main.cpp
        tests/primitives/test_primitives.cpp
        tests/curves/test_curves.cpp
        tests/operations/test_operations.cpp
        tests/acceptance/test_20m_billboard.cpp
    )

    target_include_directories(test_pikdraw_geometry PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(test_pikdraw_geometry PRIVATE
        pikdraw_geometry
    )

    add_test(NAME pikdraw_geometry_tests COMMAND test_pikdraw_geometry)
endif()
