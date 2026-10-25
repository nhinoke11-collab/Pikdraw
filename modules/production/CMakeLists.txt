cmake_minimum_required(VERSION 3.25)
project(pikdraw_production LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pikdraw_production STATIC
    src/measurement/ciede2000.cpp
    src/measurement/measurement_import.cpp
    src/measurement/measurement_report.cpp
    src/measurement/measurement_run_store.cpp
    src/print/test_sheet.cpp
    src/print/test_sheet_render.cpp
    src/print/tac_preflight.cpp
)

target_include_directories(pikdraw_production PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(pikdraw_production PUBLIC
    pikdraw_document
    pikdraw_geometry
    pikdraw_color
    pikdraw_raster
)

if(PIKDRAW_BUILD_TESTS)
    add_executable(test_pikdraw_production_print tests/print/test_test_sheet.cpp)
    target_link_libraries(test_pikdraw_production_print PRIVATE pikdraw_production)
    add_test(NAME test_pikdraw_production_print COMMAND test_pikdraw_production_print)

    add_executable(test_pikdraw_production_print_render
        tests/print/test_test_sheet_render.cpp)
    target_link_libraries(test_pikdraw_production_print_render PRIVATE pikdraw_production)
    add_test(NAME test_pikdraw_production_print_render
        COMMAND test_pikdraw_production_print_render)

    add_executable(test_pikdraw_production_measurement
        tests/measurement/test_measurement_import.cpp)
    target_link_libraries(test_pikdraw_production_measurement PRIVATE pikdraw_production)
    add_test(NAME test_pikdraw_production_measurement
        COMMAND test_pikdraw_production_measurement)

    add_executable(test_pikdraw_production_tac
        tests/print/test_tac_preflight.cpp)
    target_link_libraries(test_pikdraw_production_tac PRIVATE pikdraw_production)
    add_test(NAME test_pikdraw_production_tac COMMAND test_pikdraw_production_tac)
endif()
