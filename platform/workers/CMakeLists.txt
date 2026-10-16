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
    src/v2_proposal_validator.cpp
    src/v2_proposal_semantics.cpp
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
    src/v2_worker_protocol.cpp
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

    add_executable(test_pikdraw_worker_v2_integrity
        tests/v2_integrity_tests.cpp
    )
    target_link_libraries(test_pikdraw_worker_v2_integrity PRIVATE
        pikdraw_worker_common
    )
    add_dependencies(test_pikdraw_worker_v2_integrity spike_worker)
    add_test(NAME test_pikdraw_worker_v2_integrity COMMAND test_pikdraw_worker_v2_integrity)

    add_executable(test_pikdraw_worker_svg_import
        tests/svg_worker_import_tests.cpp
    )
    target_link_libraries(test_pikdraw_worker_svg_import PRIVATE
        pikdraw_worker_common
    )
    add_dependencies(test_pikdraw_worker_svg_import spike_worker)
    add_test(NAME test_pikdraw_worker_svg_import COMMAND test_pikdraw_worker_svg_import)
endif()
