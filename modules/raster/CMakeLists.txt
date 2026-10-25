cmake_minimum_required(VERSION 3.25)
project(pikdraw_raster LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pikdraw_raster STATIC
    src/analysis/histogram.cpp
    src/analysis/histogram_service.cpp
    src/asset_store.cpp
    src/raster_source_restore.cpp
    src/edit_geometry.cpp
    src/edit_preview_service.cpp
    src/evaluator.cpp
    src/memory_budget.cpp
    src/profile_conversion.cpp
    src/sha256.cpp
    src/tile_cache.cpp
)

target_include_directories(pikdraw_raster PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(pikdraw_raster PUBLIC
    pikdraw_document
    pikdraw_color
)

if(PIKDRAW_BUILD_TESTS)
    add_executable(test_pikdraw_raster_assets tests/test_asset_store.cpp)
    target_include_directories(test_pikdraw_raster_assets PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(test_pikdraw_raster_assets PRIVATE pikdraw_raster)
    add_test(NAME test_pikdraw_raster_assets COMMAND test_pikdraw_raster_assets)

    add_executable(test_pikdraw_raster_cache tests/test_raster_cache.cpp)
    target_include_directories(test_pikdraw_raster_cache PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(test_pikdraw_raster_cache PRIVATE pikdraw_raster)
    add_test(NAME test_pikdraw_raster_cache COMMAND test_pikdraw_raster_cache)

    add_executable(test_pikdraw_raster_evaluator tests/test_raster_evaluator.cpp)
    target_include_directories(test_pikdraw_raster_evaluator PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(test_pikdraw_raster_evaluator PRIVATE pikdraw_raster)
    add_test(NAME test_pikdraw_raster_evaluator COMMAND test_pikdraw_raster_evaluator)

    add_executable(test_pikdraw_raster_histogram tests/test_raster_histogram.cpp)
    target_include_directories(test_pikdraw_raster_histogram PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(test_pikdraw_raster_histogram PRIVATE pikdraw_raster)
    add_test(NAME test_pikdraw_raster_histogram COMMAND test_pikdraw_raster_histogram)

    add_executable(test_pikdraw_raster_histogram_service
        tests/test_raster_histogram_service.cpp)
    target_include_directories(test_pikdraw_raster_histogram_service PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(test_pikdraw_raster_histogram_service PRIVATE pikdraw_raster)
    add_test(NAME test_pikdraw_raster_histogram_service
        COMMAND test_pikdraw_raster_histogram_service)

    add_executable(test_pikdraw_raster_vertical_slice tests/test_raster_vertical_slice.cpp)
    target_include_directories(test_pikdraw_raster_vertical_slice PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(test_pikdraw_raster_vertical_slice PRIVATE pikdraw_raster)
    add_test(NAME test_pikdraw_raster_vertical_slice
        COMMAND test_pikdraw_raster_vertical_slice)

    add_executable(test_pikdraw_raster_preview_service
        tests/test_raster_edit_preview.cpp)
    target_include_directories(test_pikdraw_raster_preview_service PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(test_pikdraw_raster_preview_service PRIVATE
        pikdraw_raster
        pikdraw_rendering
    )
    add_test(NAME test_pikdraw_raster_preview_service
        COMMAND test_pikdraw_raster_preview_service)

    add_executable(test_pikdraw_raster_profile_conversion
        tests/test_raster_profile_conversion.cpp)
    target_include_directories(test_pikdraw_raster_profile_conversion PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(test_pikdraw_raster_profile_conversion PRIVATE pikdraw_raster)
    add_test(NAME test_pikdraw_raster_profile_conversion
        COMMAND test_pikdraw_raster_profile_conversion)
endif()
