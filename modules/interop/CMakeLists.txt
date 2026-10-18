cmake_minimum_required(VERSION 3.25)
project(pikdraw_interop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pikdraw_interop STATIC
    src/adapters/dxf_importer.cpp
    src/adapters/dxf_source_reader.cpp
    src/adapters/dxf_entity_parser.cpp
    src/adapters/dxf_worker_adapter.cpp
    src/adapters/raster_image_decoder.cpp
    src/adapters/raster_worker_adapter.cpp
    src/adapters/svg_importer.cpp
    src/adapters/svg_path_parser.cpp
    src/adapters/svg_xml_cursor.cpp
    src/adapters/svg_worker_adapter.cpp
    src/adapters/svg_import_record_codec.cpp
    src/writers/svg_exporter.cpp
    src/export/export_job.cpp
    src/export/export_options.cpp
    src/export/export_report.cpp
    src/export/atomic_export.cpp
    src/export/atomic_export_file.cpp
    src/ipc_dto.cpp
    src/ipc_v2_dto.cpp
    src/fixture_manifest.cpp
    src/format_capability_matrix.cpp
    src/path_adapter/svg_path_adapter.cpp
    src/interop_types.cpp
    src/adapters/tinflate.c
    src/adapters/cdr_extractor.cpp
    src/adapters/pdf_ai_extractor.cpp
    src/payload_validator.cpp
)

set_source_files_properties(src/adapters/tinflate.c PROPERTIES LANGUAGE CXX)

target_include_directories(pikdraw_interop PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(pikdraw_interop PUBLIC
    pikdraw_document
    pikdraw_geometry
    pikdraw_color
    pikdraw_typography
)

if(WIN32)
    target_link_libraries(pikdraw_interop PUBLIC bcrypt windowscodecs uuid)
endif()

if(PIKDRAW_BUILD_TESTS)
    macro(add_interop_test target_name source_file)
        add_executable(${target_name} ${source_file})
        target_include_directories(${target_name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        target_link_libraries(${target_name} PRIVATE
            pikdraw_interop
            pikdraw_document
            pikdraw_geometry
            pikdraw_color
            pikdraw_typography
        )
        add_test(NAME ${target_name} COMMAND ${target_name})
    endmacro()

    add_interop_test(test_pikdraw_interop_capabilities tests/capabilities/test_capabilities.cpp)
    add_interop_test(test_pikdraw_interop_dxf_adapter tests/adapters/test_dxf_adapter.cpp)
    add_interop_test(test_pikdraw_interop_dxf_worker_adapter tests/adapters/test_dxf_worker_adapter.cpp)
    add_interop_test(test_pikdraw_interop_raster_image_decoder tests/adapters/test_raster_image_decoder.cpp)
    add_interop_test(test_pikdraw_interop_raster_worker_adapter tests/adapters/test_raster_worker_adapter.cpp)
    add_interop_test(test_pikdraw_interop_svg_adapter tests/adapters/test_svg_adapter.cpp)
    add_interop_test(test_pikdraw_interop_svg_worker_adapter tests/adapters/test_svg_worker_adapter.cpp)
    add_interop_test(test_pikdraw_interop_svg_exporter tests/writers/test_svg_exporter.cpp)
    add_interop_test(test_pikdraw_interop_export_job tests/writers/test_export_job.cpp)
    add_interop_test(test_pikdraw_interop_export_report tests/writers/test_export_report.cpp)
    add_interop_test(test_pikdraw_interop_export_options tests/writers/test_export_options.cpp)
    add_interop_test(test_pikdraw_interop_atomic_export tests/writers/test_atomic_export.cpp)
    add_interop_test(test_pikdraw_interop_pdf_ai_extractor tests/adapters/test_pdf_ai_extractor.cpp)
endif()
