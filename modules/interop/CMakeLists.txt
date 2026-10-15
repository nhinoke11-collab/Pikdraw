cmake_minimum_required(VERSION 3.25)
project(pikdraw_interop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pikdraw_interop STATIC
    src/adapters/svg_importer.cpp
    src/writers/svg_exporter.cpp
    src/ipc_dto.cpp
    src/fixture_manifest.cpp
    src/format_capability_matrix.cpp
    src/path_adapter/svg_path_adapter.cpp
    src/interop_types.cpp
    src/payload_validator.cpp
)

target_include_directories(pikdraw_interop PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(pikdraw_interop PUBLIC
    pikdraw_document
    pikdraw_geometry
    pikdraw_color
    pikdraw_typography
)

if(PIKDRAW_BUILD_TESTS)
    macro(add_interop_test target_name source_file)
        add_executable(${target_name} ${source_file})
        target_include_directories(${target_name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        target_link_libraries(${target_name} PRIVATE
            pikdraw_interop
            pikdraw_document
            pikdraw_geometry
            pikdraw_color
            pikdraw_typography
        )
        add_test(NAME ${target_name} COMMAND ${target_name})
    endmacro()

    add_interop_test(test_pikdraw_interop_capabilities tests/capabilities/test_capabilities.cpp)
    add_interop_test(test_pikdraw_interop_svg_adapter tests/adapters/test_svg_adapter.cpp)
    add_interop_test(test_pikdraw_interop_svg_exporter tests/writers/test_svg_exporter.cpp)
endif()
