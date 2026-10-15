cmake_minimum_required(VERSION 3.20)
project(pikdraw_workers LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pikdraw_worker_common STATIC
    src/win32_common.cpp
    src/job_object.cpp
    src/appcontainer.cpp
    src/process_launcher.cpp
)

target_include_directories(pikdraw_worker_common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../modules/interop/include>
)

target_link_libraries(pikdraw_worker_common PUBLIC
    pikdraw_interop
    advapi32
    userenv
    ws2_32
)

if(MSVC)
    target_compile_options(pikdraw_worker_common PRIVATE /W4 /permissive-)
else()
    target_compile_options(pikdraw_worker_common PRIVATE -Wall -Wextra)
endif()

# Worker standalone executable
add_executable(pikdraw_spike_c_worker
    src/worker_main.cpp
)
target_link_libraries(pikdraw_spike_c_worker PRIVATE
    pikdraw_worker_common
)

# Test suite runner
add_executable(pikdraw_spike_c_tests
    tests/spike_c_tests_main.cpp
)
target_link_libraries(pikdraw_spike_c_tests PRIVATE
    pikdraw_worker_common
)

enable_testing()
add_test(NAME pikdraw_spike_c_tests COMMAND pikdraw_spike_c_tests)
add_test(NAME pikdraw_spike_c_protocol_tests COMMAND pikdraw_spike_c_tests --protocol-only)
# Full test intentionally fails while live containment evidence is missing.
