cmake_minimum_required(VERSION 3.25)

# One source checkout and one local build tree for every session.
if(EXISTS "${CMAKE_SOURCE_DIR}/.git" AND NOT IS_DIRECTORY "${CMAKE_SOURCE_DIR}/.git")
    message(FATAL_ERROR "Build from the primary PikDraw source, not a linked worktree.")
endif()
file(REAL_PATH "${CMAKE_SOURCE_DIR}/build/win-dev" PIKDRAW_CANONICAL_BUILD)
file(REAL_PATH "${CMAKE_BINARY_DIR}" PIKDRAW_REQUESTED_BUILD)
if(NOT PIKDRAW_REQUESTED_BUILD STREQUAL PIKDRAW_CANONICAL_BUILD)
    message(FATAL_ERROR "Single build policy: use build/win-dev (tooling/native/build.ps1).")
endif()

project(PikDraw
    VERSION 0.1.0
    DESCRIPTION "Professional vector editor and manufacturing graphics system"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable CTest
include(CTest)

# Modular build scripts
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(PikDrawOptions)
include(PikDrawCompilerFlags)
include(PikDrawDependencies)
include(PikDrawModules)

# The desktop owns the primary MCP Streamable HTTP endpoint. Keep the stdio
# executable as a compatibility adapter while sharing its MCP protocol library.
if(TARGET pikdraw_desktop AND TARGET pikdraw_mcp_server_lib)
    target_sources(pikdraw_desktop PRIVATE
        "${CMAKE_SOURCE_DIR}/apps/desktop/src/automation/embedded_mcp_http_host.cpp")
    target_link_libraries(pikdraw_desktop PUBLIC pikdraw_mcp_server_lib)
    if(WIN32)
        target_link_libraries(pikdraw_desktop PUBLIC ws2_32 bcrypt)
    endif()
endif()

if(PIKDRAW_BUILD_TESTS AND WIN32 AND TARGET pikdraw_desktop AND TARGET pikdraw_mcp_server_lib)
    add_executable(test_pikdraw_embedded_mcp_http
        apps/desktop/tests/test_embedded_mcp_http.cpp
        apps/desktop/src/automation/automation_pipe_host.cpp
        apps/desktop/src/automation/embedded_mcp_http_host.cpp)
    target_include_directories(test_pikdraw_embedded_mcp_http PRIVATE
        "${CMAKE_SOURCE_DIR}/apps/desktop/src/automation")
    target_link_libraries(test_pikdraw_embedded_mcp_http PRIVATE
        pikdraw_automation
        pikdraw_mcp_server_lib
        user32
        advapi32
        ws2_32
        bcrypt)
    pikdraw_apply_compiler_flags(test_pikdraw_embedded_mcp_http)
    add_test(NAME test_pikdraw_embedded_mcp_http COMMAND test_pikdraw_embedded_mcp_http)

    add_executable(test_pikdraw_in_process_mcp_e2e
        apps/desktop/tests/test_in_process_mcp_e2e.cpp)
    target_include_directories(test_pikdraw_in_process_mcp_e2e PRIVATE
        "${CMAKE_SOURCE_DIR}/apps/desktop/src/automation")
    target_link_libraries(test_pikdraw_in_process_mcp_e2e PRIVATE
        pikdraw_desktop
        ws2_32)
    pikdraw_apply_compiler_flags(test_pikdraw_in_process_mcp_e2e)
    add_test(NAME test_pikdraw_in_process_mcp_e2e COMMAND test_pikdraw_in_process_mcp_e2e)
endif()

if(TARGET pikdraw)
    set_target_properties(pikdraw PROPERTIES
        OUTPUT_NAME pikdraw
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/apps/desktop")
    foreach(PIKDRAW_CONFIG DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(pikdraw PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${PIKDRAW_CONFIG} "${CMAKE_BINARY_DIR}/apps/desktop")
    endforeach()
endif()

if(TARGET pikdraw-mcp-server)
    set_target_properties(pikdraw-mcp-server PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/apps/mcp-server")
    foreach(PIKDRAW_CONFIG DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(pikdraw-mcp-server PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${PIKDRAW_CONFIG} "${CMAKE_BINARY_DIR}/apps/mcp-server")
    endforeach()
endif()

if(TARGET pikdraw-com-server)
    set_target_properties(pikdraw-com-server PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/apps/com-server")
    foreach(PIKDRAW_CONFIG DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(pikdraw-com-server PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${PIKDRAW_CONFIG} "${CMAKE_BINARY_DIR}/apps/com-server")
    endforeach()
endif()

include(PikDrawPackaging)
