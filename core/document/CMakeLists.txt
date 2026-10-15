cmake_minimum_required(VERSION 3.20)
project(pikdraw_document LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT TARGET pikdraw_geometry)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../geometry/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../geometry" "${CMAKE_CURRENT_BINARY_DIR}/geometry")
    endif()
endif()

add_library(pikdraw_document STATIC
    src/snapshot.cpp
    src/document.cpp
    src/persistence.cpp
)

target_include_directories(pikdraw_document PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(pikdraw_document PUBLIC pikdraw_geometry)

# Document tests target pikdraw_spike_b_tests registered with CTest
add_executable(pikdraw_spike_b_tests
    tests/test_main.cpp
    tests/test_geometry.cpp
    tests/test_document.cpp
    tests/test_persistence.cpp
    tests/test_trace_10k.cpp
    tests/test_review_regressions.cpp
)

target_include_directories(pikdraw_spike_b_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

target_link_libraries(pikdraw_spike_b_tests PRIVATE
    pikdraw_document
    pikdraw_geometry
)

enable_testing()
add_test(NAME pikdraw_spike_b_tests COMMAND pikdraw_spike_b_tests)
