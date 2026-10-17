cmake_minimum_required(VERSION 3.25)
project(pikdraw_mcp_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pikdraw_mcp_server_lib STATIC src/mcp_server.cpp)
target_include_directories(pikdraw_mcp_server_lib PUBLIC include)
target_link_libraries(pikdraw_mcp_server_lib PUBLIC pikdraw_automation)

add_executable(pikdraw-mcp-server src/main.cpp)
target_link_libraries(pikdraw-mcp-server PRIVATE pikdraw_mcp_server_lib)

if(PIKDRAW_BUILD_TESTS)
    add_executable(test_pikdraw_mcp_server tests/test_mcp_server.cpp)
    target_link_libraries(test_pikdraw_mcp_server PRIVATE pikdraw_mcp_server_lib)
    add_test(NAME test_pikdraw_mcp_server COMMAND test_pikdraw_mcp_server)

    add_executable(test_pikdraw_mcp_pipe_integration tests/test_desktop_pipe_host.cpp)
    target_include_directories(test_pikdraw_mcp_pipe_integration PRIVATE
        "${CMAKE_SOURCE_DIR}/apps/desktop/src/automation")
    target_link_libraries(test_pikdraw_mcp_pipe_integration PRIVATE pikdraw_desktop)
    add_test(NAME test_pikdraw_mcp_pipe_integration COMMAND test_pikdraw_mcp_pipe_integration)
endif()
