# cmake/PikDrawOptions.cmake
# Global configuration options for PikDraw build system.

option(PIKDRAW_ENABLE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(PIKDRAW_BUILD_TESTS "Build native test targets and ctest runner" OFF)
option(PIKDRAW_ENABLE_SANITIZERS "Enable address and undefined sanitizers if available" OFF)
option(PIKDRAW_REQUIRE_M1_TARGETS "Require Milestone M1 module targets to be present" OFF)
option(PIKDRAW_FORCE_SOFTWARE "Enforce pure software 2D rendering path for testing" OFF)

# Export compile commands for IDEs (clangd, vscode)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands" FORCE)

message(STATUS "PikDraw: Warnings as errors = ${PIKDRAW_ENABLE_WARNINGS_AS_ERRORS}")
message(STATUS "PikDraw: Build tests = ${PIKDRAW_BUILD_TESTS}")
message(STATUS "PikDraw: Require M1 targets = ${PIKDRAW_REQUIRE_M1_TARGETS}")
