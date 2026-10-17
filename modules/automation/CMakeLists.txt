cmake_minimum_required(VERSION 3.25)
project(pikdraw_automation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pikdraw_automation STATIC
    src/automation_service.cpp
    src/automation_rpc.cpp
    src/json_value.cpp
)
target_include_directories(pikdraw_automation PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(pikdraw_automation PUBLIC
    pikdraw_document
    pikdraw_vector
    pikdraw_typography
)

if(PIKDRAW_BUILD_TESTS)
    add_executable(test_pikdraw_automation_service
        tests/test_automation_service.cpp
    )
    target_link_libraries(test_pikdraw_automation_service PRIVATE pikdraw_automation)
    add_test(NAME test_pikdraw_automation_service COMMAND test_pikdraw_automation_service)

    add_executable(test_pikdraw_automation_rpc tests/test_automation_rpc.cpp)
    target_link_libraries(test_pikdraw_automation_rpc PRIVATE pikdraw_automation)
    add_test(NAME test_pikdraw_automation_rpc COMMAND test_pikdraw_automation_rpc)
endif()
