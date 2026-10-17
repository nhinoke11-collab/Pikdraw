# cmake/PikDrawDependencies.cmake
# Dependency discovery and diagnostic reporting.

# Discover suitable Python 3 (matching pikdraw.ps1 strategy)
set(PIKDRAW_PYTHON_CANDIDATES)
if(DEFINED ENV{PIKDRAW_PYTHON})
    list(APPEND PIKDRAW_PYTHON_CANDIDATES "$ENV{PIKDRAW_PYTHON}")
endif()
if(DEFINED ENV{USERPROFILE})
    list(APPEND PIKDRAW_PYTHON_CANDIDATES "$ENV{USERPROFILE}/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe")
endif()

find_package(Python3 3.10 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    list(APPEND PIKDRAW_PYTHON_CANDIDATES "${Python3_EXECUTABLE}")
endif()

find_program(PIKDRAW_SYS_PYTHON NAMES python3 python)
if(PIKDRAW_SYS_PYTHON AND NOT PIKDRAW_SYS_PYTHON MATCHES "WindowsApps")
    list(APPEND PIKDRAW_PYTHON_CANDIDATES "${PIKDRAW_SYS_PYTHON}")
endif()

set(PIKDRAW_RESOLVED_PYTHON "")
foreach(cand IN LISTS PIKDRAW_PYTHON_CANDIDATES)
    if(EXISTS "${cand}")
        set(PIKDRAW_RESOLVED_PYTHON "${cand}")
        break()
    endif()
endforeach()

if(NOT PIKDRAW_RESOLVED_PYTHON)
    set(PIKDRAW_RESOLVED_PYTHON "python")
endif()

set(PIKDRAW_PYTHON_EXECUTABLE "${PIKDRAW_RESOLVED_PYTHON}" CACHE FILEPATH "Python 3 interpreter path" FORCE)
message(STATUS "PikDraw: Using Python interpreter: ${PIKDRAW_PYTHON_EXECUTABLE}")

# Optional dependencies (Qt6, Skia)
find_package(Qt6 COMPONENTS Core Gui Widgets Svg QUIET)
if(Qt6_FOUND)
    message(STATUS "PikDraw: Found Qt6 (${Qt6_VERSION})")
else()
    message(STATUS "PikDraw: Qt6 not found; desktop uses native Win32/Direct2D runtime.")
endif()

option(PIKDRAW_ENABLE_SKIA "Enable Skia GPU rendering backend" OFF)
if(PIKDRAW_ENABLE_SKIA)
    find_path(SKIA_INCLUDE_DIR "include/core/SkCanvas.h"
        PATHS "${CMAKE_SOURCE_DIR}/.agent/toolchains/skia"
              "$ENV{SKIA_DIR}"
    )
    if(SKIA_INCLUDE_DIR)
        message(STATUS "PikDraw: Found Skia headers at ${SKIA_INCLUDE_DIR}")
        set(SKIA_FOUND TRUE)
    else()
        message(STATUS "PikDraw: Skia headers not found, using architectural adapter layer.")
    endif()
endif()

