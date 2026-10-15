cmake_minimum_required(VERSION 3.25)
project(pikdraw_workers LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pikdraw_worker_common STATIC
    src/win32_common.cpp
    src/job_object.cpp
    src/appcontainer.cpp
    src/process_launcher.cpp
    src/source_snapshot.cpp
)

target_include_directories(pikdraw_worker_common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(pikdraw_worker_common PUBLIC
    pikdraw_interop
    advapi32
    userenv
    ws2_32
    bcrypt
)

# Worker standalone executable (must be compiled so host process can launch it)
add_executable(spike_worker
    src/worker_main.cpp
)
target_link_libraries(spike_worker PRIVATE
    pikdraw_worker_common
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(spike_worker PRIVATE -fms-extensions)
endif()

if(PIKDRAW_BUILD_TESTS)
    add_executable(test_pikdraw_workers
        tests/spike_c_tests_main.cpp
        tests/live_containment.cpp
    )
    target_link_libraries(test_pikdraw_workers PRIVATE
        pikdraw_worker_common
    )
    add_test(NAME test_pikdraw_workers COMMAND test_pikdraw_workers --protocol-only)
endif()
