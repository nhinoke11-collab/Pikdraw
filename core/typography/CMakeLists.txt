cmake_minimum_required(VERSION 3.25)
project(pikdraw_typography LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pikdraw_typography STATIC
    src/shaping/font_identity.cpp
    src/shaping/font_resolver.cpp
    src/shaping/shaper.cpp
    src/layout/text_layout.cpp
    src/layout/caret_navigation.cpp
    src/layout/glyph_outline.cpp
)

target_include_directories(pikdraw_typography PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(pikdraw_typography PUBLIC
    pikdraw_geometry
)

if(PIKDRAW_BUILD_TESTS)
    add_executable(test_pikdraw_typography
        tests/test_main.cpp
        tests/shaping/test_shaping.cpp
        tests/layout/test_layout.cpp
    )

    target_include_directories(test_pikdraw_typography PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(test_pikdraw_typography PRIVATE
        pikdraw_typography
        pikdraw_geometry
    )

    add_test(NAME pikdraw_typography_tests COMMAND test_pikdraw_typography)
endif()
