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

if(WIN32)
    if(NOT PDFium_DIR)
        set(PDFium_DIR "${CMAKE_SOURCE_DIR}/.agent/toolchains/pdfium-8100" CACHE PATH
            "Pinned PDFium 157.0.8100.0 dependency directory")
    endif()
    find_package(PDFium CONFIG REQUIRED PATHS "${PDFium_DIR}" NO_DEFAULT_PATH)
    if(NOT PDFium_VERSION STREQUAL "157.0.8100.0")
        message(FATAL_ERROR
            "PikDraw PDF import is pinned to PDFium 157.0.8100.0; found ${PDFium_VERSION}")
    endif()
    file(SHA256 "${PDFium_LIBRARY}" PIKDRAW_PDFIUM_RUNTIME_SHA256)
    if(NOT PIKDRAW_PDFIUM_RUNTIME_SHA256 STREQUAL
       "ae913f407e8d3f5cdbf08a9869564d647b13e132e01eb376c0a5a33b7a227a45")
        message(FATAL_ERROR "Pinned PDFium runtime DLL SHA-256 mismatch")
    endif()
    function(pikdraw_copy_pdfium_runtime target)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PDFium_LIBRARY}" "$<TARGET_FILE_DIR:${target}>/pdfium.dll"
            VERBATIM)
    endfunction()

    if(NOT PIKDRAW_LIBCDR_ROOT)
        set(PIKDRAW_LIBCDR_ROOT "${CMAKE_SOURCE_DIR}/.agent/toolchains/libcdr-msys2/root/ucrt64" CACHE PATH
            "Pinned MSYS2 UCRT64 libcdr runtime and headers")
    endif()
    set(PIKDRAW_CDR_BRIDGE_DLL "${CMAKE_BINARY_DIR}/third_party/libcdr/pikdraw_cdr_bridge.dll"
        CACHE INTERNAL "Generated pinned libcdr C ABI bridge")
    foreach(required_path
            "${PIKDRAW_LIBCDR_ROOT}/include/libcdr-0.1/libcdr/libcdr.h"
            "${PIKDRAW_LIBCDR_ROOT}/include/librevenge-0.0/librevenge/librevenge.h"
            "${PIKDRAW_LIBCDR_ROOT}/lib/libcdr-0.1.dll.a"
            "${PIKDRAW_LIBCDR_ROOT}/bin/libcdr-0.1.dll")
        if(NOT EXISTS "${required_path}")
            message(FATAL_ERROR
                "Pinned libcdr development/runtime package is missing. Run tooling/native/setup-libcdr.ps1 first.")
        endif()
    endforeach()

    get_filename_component(PIKDRAW_COMPILER_RUNTIME_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(PIKDRAW_LIBCDR_RUNTIME_DLLS
        "${PIKDRAW_LIBCDR_ROOT}/bin/libcdr-0.1.dll"
        "${PIKDRAW_LIBCDR_ROOT}/bin/librevenge-0.0.dll"
        "${PIKDRAW_LIBCDR_ROOT}/bin/libstdc++-6.dll"
        "${PIKDRAW_LIBCDR_ROOT}/bin/libgcc_s_seh-1.dll"
        "${PIKDRAW_COMPILER_RUNTIME_DIR}/libunwind.dll"
        "${PIKDRAW_LIBCDR_ROOT}/bin/libwinpthread-1.dll"
        "${PIKDRAW_LIBCDR_ROOT}/bin/libicuuc78.dll"
        "${PIKDRAW_LIBCDR_ROOT}/bin/libicuin78.dll"
        "${PIKDRAW_LIBCDR_ROOT}/bin/libicudt78.dll"
        "${PIKDRAW_LIBCDR_ROOT}/bin/liblcms2-2.dll"
        "${PIKDRAW_LIBCDR_ROOT}/bin/zlib1.dll")
    foreach(runtime_dll IN LISTS PIKDRAW_LIBCDR_RUNTIME_DLLS)
        if(NOT EXISTS "${runtime_dll}")
            message(FATAL_ERROR "Pinned libcdr runtime dependency is missing: ${runtime_dll}")
        endif()
    endforeach()

    function(pikdraw_copy_libcdr_runtime target)
        add_dependencies(${target} pikdraw_cdr_bridge)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PIKDRAW_CDR_BRIDGE_DLL}" "$<TARGET_FILE_DIR:${target}>/pikdraw_cdr_bridge.dll"
            VERBATIM)
        foreach(runtime_dll IN LISTS PIKDRAW_LIBCDR_RUNTIME_DLLS)
            get_filename_component(runtime_name "${runtime_dll}" NAME)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${runtime_dll}" "$<TARGET_FILE_DIR:${target}>/${runtime_name}"
                VERBATIM)
        endforeach()
    endfunction()
endif()

# Optional dependencies (Qt6, Skia)
find_package(Qt6 COMPONENTS Core Gui Widgets Svg QUIET)
if(Qt6_FOUND)
    message(STATUS "PikDraw: Found Qt6 (${Qt6_VERSION})")
else()
    message(STATUS "PikDraw: Qt6 not found; desktop uses native Win32/Direct2D runtime.")
endif()



