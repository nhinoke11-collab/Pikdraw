cmake_minimum_required(VERSION 3.25)
project(pikdraw_color LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pikdraw_color STATIC
    src/values/color_value.cpp
    src/profiles/profile_resource.cpp
    src/profiles/profile_registry.cpp
    src/preview/color_transform.cpp
    src/preview/preview_cache.cpp
    src/preview/icc_preview.cpp
    src/preview/profile_color_transform.cpp
)
if(WIN32)
    target_link_libraries(pikdraw_color PRIVATE mscms bcrypt)
endif()

target_include_directories(pikdraw_color PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(PIKDRAW_BUILD_TESTS)
    add_executable(test_pikdraw_color
        tests/test_main.cpp
        tests/values/test_color_values.cpp
        tests/profiles/test_profiles.cpp
        tests/preview/test_preview.cpp
    )

    target_include_directories(test_pikdraw_color PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(test_pikdraw_color PRIVATE
        pikdraw_color
    )

    add_test(NAME pikdraw_color_tests COMMAND test_pikdraw_color)
    add_executable(test_pikdraw_icc_preview tests/preview/test_icc_preview.cpp)
    target_link_libraries(test_pikdraw_icc_preview PRIVATE pikdraw_color)
    add_test(NAME test_pikdraw_icc_preview COMMAND test_pikdraw_icc_preview)
endif()
