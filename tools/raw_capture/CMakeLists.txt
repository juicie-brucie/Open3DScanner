add_library(raw_capture_core STATIC
    raw_capture.cpp
    correlation.cpp
    descriptor_probe.cpp
    uvc_frames.cpp
    hid_report.cpp
    hid_report_descriptor.cpp
    uvc_selection.cpp
    live_source.cpp
)
target_include_directories(raw_capture_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(raw_capture_core PUBLIC cxx_std_17)

add_executable(raw_capture main.cpp)
target_link_libraries(raw_capture PRIVATE raw_capture_core)

if(APPLE)
    # The live backends and the descriptor probe (row #178): IOUSBHost only.
    target_sources(raw_capture PRIVATE iokit_probe.mm iokit_source.mm iokit_registry.mm)
    target_link_libraries(raw_capture PRIVATE
        "-framework CoreFoundation"
        "-framework Foundation"
        "-framework IOKit"
        "-framework IOUSBHost"
    )
endif()

# Row #171's coverage: the capture sink and its bounds, the mock source, the
# capture-file reader, and the bounded/fail-closed command path.
add_executable(raw_capture_test raw_capture_test.cpp)
target_link_libraries(raw_capture_test PRIVATE raw_capture_core)
add_test(NAME raw_capture_test COMMAND raw_capture_test)

# Row #178's coverage: the descriptor parser (a synthetic USB tree), the UVC
# payload assembler, and the HID output-report builder. Device-free.
add_executable(raw_capture_probe_test probe_test.cpp)
target_link_libraries(raw_capture_probe_test PRIVATE raw_capture_core)
add_test(NAME raw_capture_probe_test COMMAND raw_capture_probe_test)

# Row #182's coverage: the IR/depth/RGB correlation from interface names and
# alternate settings, and the watch tracker. Device-free.
add_executable(raw_capture_correlation_test correlation_test.cpp)
target_link_libraries(raw_capture_correlation_test PRIVATE raw_capture_core)
add_test(NAME raw_capture_correlation_test COMMAND raw_capture_correlation_test)

# Row #189's coverage: the HID report-descriptor parser (fields, report ids,
# the output-report verdict) and the named fetch/file failure paths.
add_executable(raw_capture_hid_report_test hid_report_descriptor_test.cpp)
target_link_libraries(raw_capture_hid_report_test PRIVATE raw_capture_core)
add_test(NAME raw_capture_hid_report_test COMMAND raw_capture_hid_report_test)

# Row #197's coverage: the UVC streaming alternate-setting choice (the table's
# alt vs the alt carrying the endpoint, and the no-redundant-SET_INTERFACE
# rule).
add_executable(raw_capture_selection_test uvc_selection_test.cpp)
target_link_libraries(raw_capture_selection_test PRIVATE raw_capture_core)
add_test(NAME raw_capture_selection_test COMMAND raw_capture_selection_test)
