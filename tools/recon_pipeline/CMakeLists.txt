# WO-B2's end-to-end tool: a recorded IR session + calibration -> our fused
# mesh, A/B vs the vendor mesh. The math is in the engine; this target adds the
# RCRP front end (the shared capture::Reader) and the calibration/PLY I/O. The
# --selftest is device-free and runs under ctest.
add_executable(recon_pipeline
    main.cpp
    ${CMAKE_SOURCE_DIR}/tools/raptor_client/capture.cpp
)
target_link_libraries(recon_pipeline PRIVATE openscanner_engine)
target_include_directories(recon_pipeline PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/src
    ${CMAKE_SOURCE_DIR}/tools/raptor_client
)

add_test(NAME recon_pipeline_selftest COMMAND recon_pipeline --selftest)
