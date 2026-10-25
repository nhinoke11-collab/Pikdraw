cmake_minimum_required(VERSION 3.25)
project(pikdraw_interop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    set(_cdr_cxx_root "${PIKDRAW_LIBCDR_ROOT}/include/c++/16.2.0")
    set(_cdr_target_cxx_root "${_cdr_cxx_root}/x86_64-w64-mingw32")
    set(_cdr_lib_root "${PIKDRAW_LIBCDR_ROOT}/lib")
    add_custom_command(OUTPUT "${PIKDRAW_CDR_BRIDGE_DLL}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/third_party/libcdr"
        COMMAND "${CMAKE_CXX_COMPILER}" -std=c++17 -O2 -shared -stdlib=libstdc++ -nostdinc++
            -isystem "${_cdr_cxx_root}"
            -isystem "${_cdr_target_cxx_root}"
            -isystem "${_cdr_cxx_root}/backward"
            -I "${PIKDRAW_LIBCDR_ROOT}/include"
            -I "${PIKDRAW_LIBCDR_ROOT}/include/libcdr-0.1"
            -I "${PIKDRAW_LIBCDR_ROOT}/include/librevenge-0.0"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/adapters/cdr_libcdr_bridge.cpp"
            -L "${_cdr_lib_root}"
            -L "${_cdr_lib_root}/gcc/x86_64-w64-mingw32/16.2.0"
            -lcdr-0.1 -lrevenge-generators-0.0 -lrevenge-stream-0.0 -lrevenge-0.0 -lz
            -o "${PIKDRAW_CDR_BRIDGE_DLL}"
        DEPENDS src/adapters/cdr_libcdr_bridge.cpp
        VERBATIM)
    add_custom_target(pikdraw_cdr_bridge DEPENDS "${PIKDRAW_CDR_BRIDGE_DLL}")
endif()

add_library(pikdraw_interop STATIC
    src/adapters/dxf_importer.cpp
    src/adapters/dxf_source_reader.cpp
    src/adapters/dxf_entity_parser.cpp
    src/adapters/dxf_worker_adapter.cpp
    src/adapters/raster_image_decoder.cpp
    src/adapters/raster_png_decoder.cpp
    src/adapters/raster_worker_adapter.cpp
    src/adapters/svg_importer.cpp
    src/adapters/svg_path_parser.cpp
    src/adapters/svg_xml_cursor.cpp
    src/adapters/svg_worker_adapter.cpp
    src/adapters/svg_import_record_codec.cpp
    src/adapters/cdr_worker_adapter.cpp
    src/adapters/pdf_worker_adapter.cpp
    src/adapters/pdf_page_set_builder.cpp
    src/adapters/eps_worker_adapter.cpp
    src/writers/raster_image_writer.cpp
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
if(WIN32)
    target_include_directories(pikdraw_interop PRIVATE "${PDFium_INCLUDE_DIR}")
endif()

target_link_libraries(pikdraw_interop PUBLIC
    pikdraw_document
    pikdraw_geometry
    pikdraw_color
    pikdraw_raster
    pikdraw_typography
)

if(WIN32)
    target_link_libraries(pikdraw_interop PUBLIC bcrypt windowscodecs ole32 oleaut32 uuid)
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
            pikdraw_raster
            pikdraw_typography
        )
        add_test(NAME ${target_name} COMMAND ${target_name})
    endmacro()

    add_interop_test(test_pikdraw_interop_capabilities tests/capabilities/test_capabilities.cpp)
    add_interop_test(test_pikdraw_interop_dxf_adapter tests/adapters/test_dxf_adapter.cpp)
    add_interop_test(test_pikdraw_interop_dxf_worker_adapter tests/adapters/test_dxf_worker_adapter.cpp)
    add_interop_test(test_pikdraw_interop_raster_image_decoder tests/adapters/test_raster_image_decoder.cpp)
    add_interop_test(test_pikdraw_interop_raster_asset_byte_source tests/adapters/test_raster_asset_byte_source.cpp)
    add_interop_test(test_pikdraw_interop_raster_worker_adapter tests/adapters/test_raster_worker_adapter.cpp)
    add_interop_test(test_pikdraw_interop_svg_adapter tests/adapters/test_svg_adapter.cpp)
    add_interop_test(test_pikdraw_interop_svg_worker_adapter tests/adapters/test_svg_worker_adapter.cpp)
    add_interop_test(test_pikdraw_interop_svg_exporter tests/writers/test_svg_exporter.cpp)
    add_interop_test(test_pikdraw_interop_raster_image_writer tests/writers/test_raster_image_writer.cpp)
    add_interop_test(test_pikdraw_interop_export_job tests/writers/test_export_job.cpp)
    add_interop_test(test_pikdraw_interop_export_report tests/writers/test_export_report.cpp)
    add_interop_test(test_pikdraw_interop_export_options tests/writers/test_export_options.cpp)
    add_interop_test(test_pikdraw_interop_atomic_export tests/writers/test_atomic_export.cpp)
    add_interop_test(test_pikdraw_interop_pdf_ai_extractor tests/adapters/test_pdf_ai_extractor.cpp)
    add_interop_test(test_pikdraw_interop_pdf_worker_adapter tests/adapters/test_pdf_worker_adapter.cpp)
    add_interop_test(test_pikdraw_interop_eps_worker_adapter tests/adapters/test_eps_worker_adapter.cpp)
    if(WIN32)
        pikdraw_copy_pdfium_runtime(test_pikdraw_interop_pdf_worker_adapter)
        add_interop_test(test_pikdraw_interop_cdr_worker_adapter tests/adapters/test_cdr_worker_adapter.cpp)
        pikdraw_copy_libcdr_runtime(test_pikdraw_interop_cdr_worker_adapter)
    endif()
endif()
