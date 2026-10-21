add_executable(raw_capture main.cpp raw_capture.cpp)
target_include_directories(raw_capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Row #171 (Phase D seed 3): the device-free coverage of the raw capture path.
# The capture sink and its bounds, the mock source, the capture-file reader,
# and the bounded/fail-closed laser + exposure command path — no device, no
# vendor SDK, no network. The live "IR+depth on a known scene with no vendor
# process" gate is the operator's; this test is what runs here.
add_executable(raw_capture_test raw_capture_test.cpp raw_capture.cpp)
target_include_directories(raw_capture_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME raw_capture_test COMMAND raw_capture_test)
