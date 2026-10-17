if(MINGW)
    # Statically link runtime libraries so native executables run standalone
    add_link_options(-static)
endif()

function(pikdraw_apply_compiler_flags target_name)
    get_target_property(already_applied ${target_name} PIKDRAW_FLAGS_APPLIED)
    if(already_applied)
        return()
    endif()
    set_property(TARGET ${target_name} PROPERTY PIKDRAW_FLAGS_APPLIED TRUE)
    target_compile_features(${target_name} PUBLIC cxx_std_20)

    get_target_property(target_sources ${target_name} SOURCES)
    set(test_target FALSE)
    foreach(source IN LISTS target_sources)
        string(REPLACE "\\" "/" normalized_source "${source}")
        if(normalized_source MATCHES "(^|/)tests/")
            set(test_target TRUE)
        endif()
    endforeach()

    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            /W4
            /utf-8
            /permissive-
            /Zc:__cplusplus
            /EHsc
            $<$<CONFIG:Debug>:/Od>
            $<$<CONFIG:Debug>:/Zi>
            $<$<CONFIG:Release>:/O2>
            $<$<CONFIG:Release>:/DNDEBUG>
        )
        if(PIKDRAW_ENABLE_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()
        if(test_target)
            target_compile_options(${target_name} PRIVATE
                "/FI${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tooling/native/quality/test_assertions.hpp")
        endif()
        if(PIKDRAW_ENABLE_SANITIZERS)
            message(FATAL_ERROR "Requested ASan+UBSan combination is not supported by this MSVC policy; use a supported Clang toolchain and verify runtime availability.")
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target_name} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -fexceptions
            $<$<CONFIG:Debug>:-O0>
            $<$<CONFIG:Debug>:-g>
            $<$<CONFIG:Release>:-O3>
            $<$<CONFIG:Release>:-DNDEBUG>
        )
        if(PIKDRAW_ENABLE_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()

        if(test_target)
            # Keep assert-based checks live in optimized configurations.
            target_compile_options(${target_name} PRIVATE -UNDEBUG)
        endif()

        if(PIKDRAW_ENABLE_SANITIZERS)
            target_compile_options(${target_name} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target_name} PRIVATE -fsanitize=address,undefined)
        endif()
    else()
        message(FATAL_ERROR "No verified strict compiler policy for ${CMAKE_CXX_COMPILER_ID}")
    endif()
endfunction()

# Apply policy to every repository target, including targets nested in modules.
function(pikdraw_apply_directory_flags directory_path)
    get_property(repo_targets DIRECTORY "${directory_path}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(repo_target IN LISTS repo_targets)
        get_target_property(target_kind ${repo_target} TYPE)
        if(target_kind MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
            pikdraw_apply_compiler_flags(${repo_target})
        endif()
    endforeach()
    get_property(child_directories DIRECTORY "${directory_path}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS child_directories)
        pikdraw_apply_directory_flags("${child}")
    endforeach()
endfunction()
