cmake_minimum_required(VERSION 3.20)
project(pikdraw_geometry LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pikdraw_geometry STATIC
    src/transform.cpp
    src/rect.cpp
    src/curve.cpp
    src/boolean_offset.cpp
)

target_include_directories(pikdraw_geometry PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
