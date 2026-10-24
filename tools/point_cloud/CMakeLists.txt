# WO-2's point-cloud tool: triangulation + the A/B cloud-compare harness on a
# recorded IR frame. The math is in the engine library; this target adds the
# I/O (the shared RCRP Reader from tools/raptor_client, a calibration text
# file, and ASCII PLY). The --selftest is device-free and runs under ctest.
add_executable(point_cloud
    main.cpp
    ${CMAKE_SOURCE_DIR}/tools/raptor_client/capture.cpp
)
target_link_libraries(point_cloud PRIVATE openscanner_engine)
target_include_directories(point_cloud PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/src
    ${CMAKE_SOURCE_DIR}/tools/raptor_client
)

add_test(NAME point_cloud_selftest COMMAND point_cloud --selftest)
