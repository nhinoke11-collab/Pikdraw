cmake_minimum_required(VERSION 3.25)
project(pikdraw_com_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(STATUS "PikDraw COM adapter is available only on Windows; skipping apps/com-server.")
    return()
endif()

add_library(pikdraw_com_dispatch STATIC
    src/automation_dispatch.cpp
)
target_include_directories(pikdraw_com_dispatch PUBLIC include)
target_link_libraries(pikdraw_com_dispatch PUBLIC pikdraw_mcp_server_lib ole32 oleaut32)

add_executable(pikdraw-com-server WIN32
    src/server_main.cpp
    src/registration.cpp
)
target_link_libraries(pikdraw-com-server PRIVATE pikdraw_com_dispatch ole32 oleaut32 advapi32 shell32)
if(MSVC)
    target_compile_options(pikdraw-com-server PRIVATE /utf-8)
endif()

if(PIKDRAW_BUILD_TESTS)
    add_executable(test_pikdraw_com_dispatch tests/test_com_dispatch.cpp)
    target_link_libraries(test_pikdraw_com_dispatch PRIVATE pikdraw_com_dispatch ole32 oleaut32)
    add_test(NAME test_pikdraw_com_dispatch COMMAND test_pikdraw_com_dispatch)

    add_executable(test_pikdraw_com_runtime_e2e tests/test_com_runtime_e2e.cpp)
    target_include_directories(test_pikdraw_com_runtime_e2e PRIVATE
        "${CMAKE_SOURCE_DIR}/apps/desktop/src/automation")
    target_link_libraries(test_pikdraw_com_runtime_e2e PRIVATE
        pikdraw_com_dispatch pikdraw_desktop ole32 oleaut32)
    add_test(NAME test_pikdraw_com_runtime_e2e COMMAND test_pikdraw_com_runtime_e2e)

    # This executable is run only by the acceptance script while the matching
    # per-user COM registration is temporarily active.
    add_executable(test_pikdraw_com_client tests/com_client_smoke.cpp)
    target_include_directories(test_pikdraw_com_client PRIVATE include)
    target_link_libraries(test_pikdraw_com_client PRIVATE ole32 oleaut32)
endif()
