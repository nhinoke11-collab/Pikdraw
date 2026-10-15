cmake_minimum_required(VERSION 3.25)
project(pikdraw_document LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pikdraw_document STATIC
    src/async/proposal_validator.cpp
    src/snapshots/structure_validator.cpp
    src/commands/command.cpp
    src/commands/transaction_writer.cpp
    src/storage/native_format.cpp
    src/storage/save_recovery.cpp
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
    add_doc_test(test_pikdraw_document_snapshots tests/snapshots/test_snapshots.cpp)
    add_doc_test(test_pikdraw_document_persistent_map tests/snapshots/test_persistent_map.cpp)
    add_doc_test(test_pikdraw_document_async tests/async/test_stale_proposals.cpp)
    add_doc_test(test_pikdraw_document_writer_safety tests/async/test_writer_safety.cpp)
    add_doc_test(test_pikdraw_document_storage tests/storage/test_storage.cpp)
endif()
