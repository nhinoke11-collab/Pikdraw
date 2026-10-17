cmake_minimum_required(VERSION 3.25)
project(pikdraw_rendering LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pikdraw_rendering STATIC
    src/software_rasterizer.cpp
    src/effects/blend_operator.cpp
    src/effects/drop_shadow.cpp
    src/effects/gaussian_blur.cpp
    src/effects/gaussian_blur_avx.cpp
    src/effects/gaussian_blur_simd.cpp
    src/effects/object_compositor.cpp
    src/effects/object_compositor_surface.cpp
    src/renderer.cpp
    src/raster/path_flatten.cpp
    src/raster/path_paint.cpp
    src/raster/path_stroke.cpp
    src/raster/gradient_sampler.cpp
    src/path_adapter/authoring_path_adapter.cpp
    src/culling/spatial_index.cpp
    src/culling/damage_tracker.cpp
    src/gpu/d2d/d2d_device_manager.cpp
    src/gpu/d2d/d2d_geometry_cache.cpp
    src/gpu/d2d/d2d_effects_pipeline.cpp
    src/gpu/d2d/d2d_render_backend.cpp
    src/gpu/skia/skia_context_d3d12.cpp
    src/gpu/skia/skia_path_converter.cpp
    src/gpu/skia/skia_effects_compositor.cpp
    src/gpu/skia/skia_render_backend.cpp
)

target_include_directories(pikdraw_rendering PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(pikdraw_rendering PUBLIC
    pikdraw_document
    pikdraw_geometry
    pikdraw_color
    pikdraw_typography
)

if(WIN32)
    target_link_libraries(pikdraw_rendering PUBLIC
        d2d1
        d3d11
        dxgi
        dwrite
        ole32
    )
endif()

# Keep the dispatcher at the baseline ISA and isolate AVX instructions in one
# translation unit. Runtime dispatch checks both CPU support and OS XMM/YMM
# state before entering this function.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86)$" OR
   CMAKE_GENERATOR_PLATFORM MATCHES "^(Win32|x64)$")
    if(MSVC)
        set_source_files_properties(src/effects/gaussian_blur_avx.cpp
            PROPERTIES COMPILE_OPTIONS "/arch:AVX")
        target_compile_definitions(pikdraw_rendering PRIVATE
            PIKDRAW_RENDERING_HAS_AVX_TU=1)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        set_source_files_properties(src/effects/gaussian_blur_avx.cpp
            PROPERTIES COMPILE_OPTIONS "-mavx")
        target_compile_definitions(pikdraw_rendering PRIVATE
            PIKDRAW_RENDERING_HAS_AVX_TU=1)
    else()
        target_compile_definitions(pikdraw_rendering PRIVATE
            PIKDRAW_RENDERING_HAS_AVX_TU=0)
    endif()
else()
    target_compile_definitions(pikdraw_rendering PRIVATE
        PIKDRAW_RENDERING_HAS_AVX_TU=0)
endif()

if(PIKDRAW_BUILD_TESTS)
    macro(add_rendering_test target_name source_file)
        add_executable(${target_name} ${source_file})
        target_include_directories(${target_name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        target_link_libraries(${target_name} PRIVATE
            pikdraw_rendering
            pikdraw_document
            pikdraw_geometry
            pikdraw_color
            pikdraw_typography
        )
        add_test(NAME ${target_name} COMMAND ${target_name})
    endmacro()

    add_rendering_test(test_pikdraw_rendering_pixels tests/rasterizer/test_pixel_semantics.cpp)
    add_rendering_test(test_pikdraw_rendering_camera tests/camera/test_camera.cpp)
    add_rendering_test(test_pikdraw_rendering_culling tests/culling/test_culling.cpp)
    add_rendering_test(test_pikdraw_rendering_rasterizer tests/rasterizer/test_rasterizer.cpp)
    add_rendering_test(test_pikdraw_rendering_authoring_path_adapter tests/rasterizer/test_authoring_path_adapter.cpp)
    add_rendering_test(test_pikdraw_rendering_effect_compositor tests/effects/test_object_compositor.cpp)
    add_rendering_test(test_pikdraw_rendering_float_scratch_paint tests/rasterizer/test_float_scratch_paint.cpp)
    add_rendering_test(test_pikdraw_rendering_blend_operator tests/effects/test_blend_operator.cpp)
    add_rendering_test(test_pikdraw_rendering_drop_shadow tests/effects/test_drop_shadow.cpp)
    add_rendering_test(test_pikdraw_rendering_gaussian_blur tests/effects/test_gaussian_blur.cpp)
    add_rendering_test(test_pikdraw_rendering_gaussian_blur_simd tests/effects/test_gaussian_blur_simd.cpp)
    add_rendering_test(test_pikdraw_rendering_gradient tests/rasterizer/test_gradient_pixels.cpp)
    add_rendering_test(test_pikdraw_rendering_gradient_compositor tests/effects/test_gradient_compositor.cpp)

    # A header probe alone does not mean a Skia runtime is linked. Keep this
    # independent fallback contract enabled until CMake wires up that runtime.
    if(NOT PIKDRAW_SKIA_RUNTIME_ENABLED)
        add_rendering_test(test_pikdraw_skia_fallback_contract_independent
            tests/gpu_independent/test_skia_fallback_contract.cpp)
        set_tests_properties(test_pikdraw_skia_fallback_contract_independent
            PROPERTIES ENVIRONMENT "PIKDRAW_RENDER_BACKEND=auto")
    endif()

    # Explicitly built performance probe; not registered as a normal CTest test.
    add_executable(benchmark_pikdraw_rendering_effects
        tests/effects/benchmark_effect_pipeline.cpp)
    target_include_directories(benchmark_pikdraw_rendering_effects PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(benchmark_pikdraw_rendering_effects PRIVATE
        pikdraw_rendering
        pikdraw_document
        pikdraw_geometry
        pikdraw_color
        pikdraw_typography
        psapi)
endif()
