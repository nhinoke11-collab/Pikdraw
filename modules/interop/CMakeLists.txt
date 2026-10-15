cmake_minimum_required(VERSION 3.20)
project(pikdraw_interop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pikdraw_interop STATIC
    src/interop_types.cpp
    src/ipc_dto.cpp
    src/payload_validator.cpp
    src/format_capability_matrix.cpp
    src/fixture_manifest.cpp
)

target_include_directories(pikdraw_interop PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(MSVC)
    target_compile_options(pikdraw_interop PRIVATE /W4 /WX- /permissive-)
else()
    target_compile_options(pikdraw_interop PRIVATE -Wall -Wextra -Wpedantic)
endif()
