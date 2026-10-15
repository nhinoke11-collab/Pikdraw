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

# Optional dependencies for later milestones (Qt6, Skia)
# These will be required starting from M1 ps05/ps09.
find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
    message(STATUS "PikDraw: Found Qt6 Widgets (${Qt6_VERSION})")
else()
    message(STATUS "PikDraw: Qt6 Widgets not yet required in Milestone M0.")
endif()
