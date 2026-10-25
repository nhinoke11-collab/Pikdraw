# Installable Windows ZIP containing the native editor and both local agent adapters.
if(NOT WIN32)
    return()
endif()

set(PIKDRAW_PACKAGE_TARGETS pikdraw pikdraw-mcp-server pikdraw-com-server)
if(TARGET spike_worker)
    list(APPEND PIKDRAW_PACKAGE_TARGETS spike_worker)
endif()
foreach(target IN LISTS PIKDRAW_PACKAGE_TARGETS)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "PikDraw Windows package is incomplete: missing target '${target}'.")
    endif()
endforeach()

install(TARGETS pikdraw
    RUNTIME_DEPENDENCY_SET PikDrawDesktopRuntime
    RUNTIME DESTINATION .
    COMPONENT Runtime
)
if(TARGET spike_worker)
    install(TARGETS spike_worker
        RUNTIME DESTINATION workers
        COMPONENT Runtime
    )
endif()
install(TARGETS pikdraw-mcp-server
    RUNTIME_DEPENDENCY_SET PikDrawMcpRuntime
    RUNTIME DESTINATION apps/mcp-server
    COMPONENT Runtime
)
install(TARGETS pikdraw-com-server
    RUNTIME_DEPENDENCY_SET PikDrawComRuntime
    RUNTIME DESTINATION apps/com-server
    COMPONENT Runtime
)

install(RUNTIME_DEPENDENCY_SET PikDrawDesktopRuntime
    DIRECTORIES
        "${CMAKE_BINARY_DIR}/apps/desktop"
    PRE_EXCLUDE_REGEXES
        "api-ms-.*"
        "ext-ms-.*"
    POST_EXCLUDE_REGEXES
        ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Tt][Ee][Mm]32[\\\\/].*"
        ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Ww][Oo][Ww]64[\\\\/].*"
    DESTINATION .
    COMPONENT Runtime
)
install(RUNTIME_DEPENDENCY_SET PikDrawMcpRuntime
    DIRECTORIES "${CMAKE_BINARY_DIR}/apps/mcp-server"
    PRE_EXCLUDE_REGEXES
        "api-ms-.*"
        "ext-ms-.*"
    POST_EXCLUDE_REGEXES
        ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Tt][Ee][Mm]32[\\\\/].*"
        ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Ww][Oo][Ww]64[\\\\/].*"
    DESTINATION apps/mcp-server
    COMPONENT Runtime
)
install(RUNTIME_DEPENDENCY_SET PikDrawComRuntime
    DIRECTORIES "${CMAKE_BINARY_DIR}/apps/com-server"
    PRE_EXCLUDE_REGEXES
        "api-ms-.*"
        "ext-ms-.*"
    POST_EXCLUDE_REGEXES
        ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Tt][Ee][Mm]32[\\\\/].*"
        ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Ww][Oo][Ww]64[\\\\/].*"
    DESTINATION apps/com-server
    COMPONENT Runtime
)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/apps/desktop/resources/"
    DESTINATION resources
    COMPONENT Runtime
)
install(FILES
    "${CMAKE_SOURCE_DIR}/README.md"
    "${CMAKE_SOURCE_DIR}/LICENSE"
    DESTINATION .
    COMPONENT Documentation
)
install(FILES
    "${CMAKE_SOURCE_DIR}/LICENSES/MIT-foundation.txt"
    "${CMAKE_SOURCE_DIR}/third_party/licenses/clipper2-2.0.1/LICENSE.txt"
    DESTINATION licenses
    COMPONENT Documentation
)
install(FILES
    "${CMAKE_SOURCE_DIR}/apps/mcp-server/README.md"
    DESTINATION docs
    RENAME AI-Automation-MCP.md
    COMPONENT Documentation
)
install(FILES
    "${CMAKE_SOURCE_DIR}/apps/com-server/README.md"
    DESTINATION docs
    RENAME AI-Automation-COM.md
    COMPONENT Documentation
)
install(FILES
    "${CMAKE_SOURCE_DIR}/docs/development/windows-package.md"
    DESTINATION docs
    RENAME Windows-Package.md
    COMPONENT Documentation
)

math(EXPR PIKDRAW_PACKAGE_BITS "${CMAKE_SIZEOF_VOID_P} * 8")
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "PikDraw")
set(CPACK_PACKAGE_VENDOR "PikDraw")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "PikDraw-${PROJECT_VERSION}-win${PIKDRAW_PACKAGE_BITS}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "PikDraw")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "PikDraw vector design editor with local AI automation")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_MONOLITHIC_INSTALL ON)
include(CPack)

# Keep packaging ordered after every executable is rebuilt when called through the
# shared native build runner; CPack's built-in `package` utility has no target deps.
add_custom_target(pikdraw-package
    COMMAND "${CMAKE_CPACK_COMMAND}" --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
    DEPENDS ${PIKDRAW_PACKAGE_TARGETS}
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Packaging the rebuilt PikDraw desktop and agent adapters"
    VERBATIM
)
