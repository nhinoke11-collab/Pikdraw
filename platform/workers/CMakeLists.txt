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
if(WIN32)
    pikdraw_copy_pdfium_runtime(spike_worker)
    pikdraw_copy_libcdr_runtime(spike_worker)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(spike_worker PRIVATE -fms-extensions)
endif()
if(WIN32)
    target_link_options(spike_worker PRIVATE -static)
endif()

if(PIKDRAW_BUILD_TESTS)
    # Test hooks and sandbox bypasses are compiled into repository-owned fixtures only.
    add_library(pikdraw_worker_test_support STATIC
        src/win32_common.cpp
        src/job_object.cpp
        src/appcontainer.cpp
        src/process_launcher.cpp
        src/source_snapshot.cpp
        src/v2_proposal_validator.cpp
        src/v2_proposal_semantics.cpp
    )
    target_include_directories(pikdraw_worker_test_support PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
    target_link_libraries(pikdraw_worker_test_support PUBLIC
        pikdraw_interop advapi32 userenv ws2_32 bcrypt
    )
    target_compile_definitions(pikdraw_worker_test_support PRIVATE PIKDRAW_WORKER_TEST_HOOKS=1)

    add_executable(test_pikdraw_worker_sandbox tests/test_worker_sandbox.cpp)
    target_link_libraries(test_pikdraw_worker_sandbox PRIVATE pikdraw_worker_test_support)
    add_test(NAME test_pikdraw_worker_sandbox COMMAND test_pikdraw_worker_sandbox)
    set_tests_properties(test_pikdraw_worker_sandbox PROPERTIES TIMEOUT 30)

    add_executable(spike_worker_test
        src/worker_main.cpp
        src/v2_worker_protocol.cpp
    )
    target_link_libraries(spike_worker_test PRIVATE pikdraw_worker_test_support)
    target_compile_definitions(spike_worker_test PRIVATE PIKDRAW_WORKER_TEST_HOOKS=1)
    if(WIN32)
        pikdraw_copy_pdfium_runtime(spike_worker_test)
        pikdraw_copy_libcdr_runtime(spike_worker_test)
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(spike_worker_test PRIVATE -fms-extensions)
    endif()

    add_executable(test_pikdraw_workers
        tests/spike_c_tests_main.cpp
        tests/live_containment.cpp
    )
    target_link_libraries(test_pikdraw_workers PRIVATE
        pikdraw_worker_test_support
    )
    target_include_directories(test_pikdraw_workers PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_dependencies(test_pikdraw_workers spike_worker_test)
    add_test(NAME test_pikdraw_workers COMMAND test_pikdraw_workers --protocol-only)
    add_test(NAME test_pikdraw_workers_live_containment COMMAND test_pikdraw_workers --live-containment)
    set_tests_properties(test_pikdraw_workers_live_containment PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE LABELS "windows;live-containment")

    add_executable(test_pikdraw_worker_v2_integrity
        tests/v2_integrity_tests.cpp
    )
    target_link_libraries(test_pikdraw_worker_v2_integrity PRIVATE
        pikdraw_worker_test_support
    )
    add_dependencies(test_pikdraw_worker_v2_integrity spike_worker_test)
    add_test(NAME test_pikdraw_worker_v2_integrity COMMAND test_pikdraw_worker_v2_integrity)

    add_executable(test_pikdraw_worker_svg_import
        tests/svg_worker_import_tests.cpp
    )
    target_link_libraries(test_pikdraw_worker_svg_import PRIVATE
        pikdraw_worker_test_support
    )
    add_dependencies(test_pikdraw_worker_svg_import spike_worker_test)
    add_test(NAME test_pikdraw_worker_svg_import COMMAND test_pikdraw_worker_svg_import)

    add_executable(test_pikdraw_worker_raster_import
        tests/raster_worker_import_tests.cpp
    )
    target_link_libraries(test_pikdraw_worker_raster_import PRIVATE
        pikdraw_worker_test_support
    )
    add_dependencies(test_pikdraw_worker_raster_import spike_worker_test)
    add_test(NAME test_pikdraw_worker_raster_import COMMAND test_pikdraw_worker_raster_import)

    add_executable(test_pikdraw_worker_dxf_import
        tests/dxf_worker_import_tests.cpp
    )
    target_link_libraries(test_pikdraw_worker_dxf_import PRIVATE
        pikdraw_worker_test_support
    )
    add_dependencies(test_pikdraw_worker_dxf_import spike_worker_test)
    add_test(NAME test_pikdraw_worker_dxf_import COMMAND test_pikdraw_worker_dxf_import)

    add_executable(test_pikdraw_workers_policy tests/production_policy_tests.cpp)
    target_include_directories(test_pikdraw_workers_policy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(test_pikdraw_workers_policy PRIVATE pikdraw_worker_common)
    add_dependencies(test_pikdraw_workers_policy spike_worker)
    add_test(NAME test_pikdraw_workers_policy COMMAND test_pikdraw_workers_policy)
    set_tests_properties(test_pikdraw_workers_policy PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE LABELS "windows;live-policy")
endif()
