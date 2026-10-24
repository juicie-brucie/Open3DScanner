# The laser-line recorded-frame tool (row #205): the #132 IR frames through
# the laser-line module, using the shared RCRP Reader from tools/raptor_client
# (capture.h). The engine library and the module stay I/O-free; this target is
# the only laser-line consumer of a capture bundle.
add_executable(laser_line_report
    main.cpp
    ${CMAKE_SOURCE_DIR}/tools/raptor_client/capture.cpp
)
target_link_libraries(laser_line_report PRIVATE openscanner_engine)
target_include_directories(laser_line_report PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/src
    ${CMAKE_SOURCE_DIR}/tools/raptor_client
)
