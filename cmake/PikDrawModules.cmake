# cmake/PikDrawModules.cmake
# Registers modules according to registry/modules.json when implementations exist.

set(PIKDRAW_MODULE_PATHS
    "core/geometry"
    "core/color"
    "core/typography"
    "core/document"
    "core/rendering"
    "modules/vector"
    "modules/automation"
    "modules/raster"
    "modules/production"
    "modules/scene3d"
    "modules/interop"
    "packages/plugin-sdk"
    "platform/workers"
    "apps/desktop"
    "apps/mcp-server"
    "apps/com-server"
)

foreach(mod_path IN LISTS PIKDRAW_MODULE_PATHS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${mod_path}/CMakeLists.txt")
        message(STATUS "PikDraw: Registering active module: ${mod_path}")
        add_subdirectory("${mod_path}")
    else()
        message(STATUS "PikDraw: Module '${mod_path}' not yet implemented (M0 foundation stage).")
    endif()
endforeach()

# In Milestone M1+, require implemented targets; missing targets fail build configuration
if(PIKDRAW_REQUIRE_M1_TARGETS)
    set(PIKDRAW_M1_REQUIRED_TARGETS
        pikdraw_geometry
        pikdraw_color
        pikdraw_document
        pikdraw_rendering
        pikdraw_desktop
    )
    foreach(target IN LISTS PIKDRAW_M1_REQUIRED_TARGETS)
        if(NOT TARGET ${target})
            message(FATAL_ERROR "Milestone M1 gate failure: Required target '${target}' is not registered!")
        endif()
    endforeach()
endif()

# Run the M1 regression cases against the already-registered root targets.
# This avoids recursively configuring another copy of the product.
if(PIKDRAW_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/tests/integration/m1-audit/CMakeLists.txt")
    add_subdirectory("${CMAKE_SOURCE_DIR}/tests/integration/m1-audit" "tests/integration/m1-audit")
endif()

# Compiler policy must be applied after all modules and test targets exist.
pikdraw_apply_directory_flags("${CMAKE_CURRENT_SOURCE_DIR}")

# Register Foundation Architecture & Contract Gates in CTest
if(PIKDRAW_BUILD_TESTS)
    add_test(
        NAME architecture_gates
        COMMAND "${PIKDRAW_PYTHON_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/tests/architecture" -v
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_test(
        NAME contract_and_oracle_gates
        COMMAND "${PIKDRAW_PYTHON_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/architecture/verify_contracts.py"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_test(
        NAME harness_gates
        COMMAND "${PIKDRAW_PYTHON_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/tests/harness" -v
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()
