cmake_minimum_required(VERSION 3.25)
project(pikdraw_document LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pikdraw_document STATIC
    src/async/proposal_validator.cpp
    src/snapshots/structure_validator.cpp
    src/commands/command.cpp
    src/commands/appearance_commands.cpp
    src/commands/gradient_commands.cpp
    src/commands/pen_path_extension_command.cpp
    src/commands/node_commands.cpp
    src/commands/contour_commands.cpp
    src/commands/dimension_commands.cpp
    src/annotations/dimension_style.cpp
    src/annotations/dimension_layout.cpp
    src/commands/transaction_writer.cpp
    src/storage/native_format.cpp
    src/storage/crc32.cpp
    src/storage/color_codec.cpp
    src/storage/appearance_codec.cpp
    src/storage/gradient_codec.cpp
    src/storage/object_codec.cpp
    src/storage/editable_path_codec.cpp
    src/storage/recovery_draft.cpp
    src/storage/save_recovery.cpp
    src/storage/file_io.cpp
    src/model/clone_utils.cpp
    src/model/gradient_paint.cpp
    src/document.cpp
)

target_include_directories(pikdraw_document PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(pikdraw_document PUBLIC
    pikdraw_geometry
    pikdraw_color
    pikdraw_typography
)

if(PIKDRAW_BUILD_TESTS)
    macro(add_doc_test target_name source_file)
        add_executable(${target_name} ${source_file})
        target_include_directories(${target_name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        target_link_libraries(${target_name} PRIVATE
            pikdraw_document
            pikdraw_geometry
            pikdraw_color
            pikdraw_typography
        )
        add_test(NAME ${target_name} COMMAND ${target_name})
    endmacro()

    add_doc_test(test_pikdraw_document_commands tests/commands/test_commands.cpp)
    add_doc_test(test_pikdraw_document_node_commands tests/commands/test_node_commands.cpp)
    add_doc_test(test_pikdraw_document_contour tests/commands/test_contour_commands.cpp)
    add_doc_test(test_pikdraw_document_pen_path_extension tests/commands/test_pen_path_extension.cpp)
    add_doc_test(test_pikdraw_document_snapshots tests/snapshots/test_snapshots.cpp)
    add_doc_test(test_pikdraw_document_persistent_map tests/snapshots/test_persistent_map.cpp)
    add_doc_test(test_pikdraw_document_async tests/async/test_stale_proposals.cpp)
    add_doc_test(test_pikdraw_document_writer_safety tests/async/test_writer_safety.cpp)
    add_doc_test(test_pikdraw_document_storage tests/storage/test_storage.cpp)
    add_doc_test(test_pikdraw_document_storage_fidelity tests/storage/test_storage_fidelity.cpp)
    add_doc_test(test_pikdraw_document_clone_utils tests/model/test_clone_utils.cpp)
    add_doc_test(test_pikdraw_document_dimension_model tests/annotations/test_dimension_model.cpp)
    add_doc_test(test_pikdraw_document_dimension_commands tests/commands/test_dimension_commands.cpp)
    add_doc_test(test_pikdraw_document_gradient tests/model/test_gradient_paint.cpp)
    add_doc_test(test_pikdraw_document_gradient_commands tests/commands/test_gradient_commands.cpp)
    add_doc_test(test_pikdraw_document_gradient_storage tests/storage/test_gradient_storage.cpp)
endif()
