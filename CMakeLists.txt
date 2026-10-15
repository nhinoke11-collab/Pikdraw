cmake_minimum_required(VERSION 3.25)

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
