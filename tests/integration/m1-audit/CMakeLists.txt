add_executable(m1_independent_regressions
    document_regressions.cpp
    fidelity_regressions.cpp)
target_link_libraries(m1_independent_regressions PRIVATE
    pikdraw_document pikdraw_geometry pikdraw_rendering pikdraw_interop)
foreach(case redo atomic_failure atomic_exception duplicate_id deletion_order foreign_document
             lifecycle_epoch source_color source_font boolean_curve offset_curve svg_units render_fill)
    add_test(NAME audit_${case} COMMAND m1_independent_regressions ${case})
endforeach()
