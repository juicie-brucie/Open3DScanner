# The privileged helper skeleton (row #170, Phase D seed 2).
#
# Architecture and the #97 decision: docs/recomp/FULL-INDEPENDENCE.md §5 L3
# and §6 Phase D. This target is the daemon's build; it is not installed by
# the build, is not added to the app bundle, and is never started by the app.

# The claim state machine and the request dispatcher are plain C++ so their
# tests run anywhere, with a fake backend and no device (row #170).
add_library(openscanner_helper_core STATIC
    helper_protocol.cpp
    device_claim.cpp
    helper_state.cpp
    helper_service.cpp
)
target_include_directories(openscanner_helper_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(openscanner_helper_core PUBLIC cxx_std_17)

add_executable(helper_state_test helper_state_test.cpp)
target_link_libraries(helper_state_test PRIVATE openscanner_helper_core)
add_test(NAME helper_state_test COMMAND helper_state_test)

add_executable(helper_service_test helper_service_test.cpp)
target_link_libraries(helper_service_test PRIVATE openscanner_helper_core)
add_test(NAME helper_service_test COMMAND helper_service_test)

# Everything below is the macOS daemon itself.
if(NOT APPLE)
    return()
endif()

# The XPC transport, shared by the daemon and the in-process handshake test.
add_library(openscanner_helper_xpc STATIC helper_xpc.mm)
target_link_libraries(openscanner_helper_xpc PUBLIC
    openscanner_helper_core
    "-framework CoreFoundation"
)
# dispatch objects are managed as C pointers here: no ARC, no ObjC ownership.
target_compile_definitions(openscanner_helper_xpc PUBLIC OS_OBJECT_USE_OBJC=0)

add_executable(openscanner-helper
    main.mm
    iokit_device_claim.mm
)
target_link_libraries(openscanner-helper PRIVATE
    openscanner_helper_xpc
    "-framework CoreFoundation"
    "-framework Foundation"
    "-framework IOKit"
    "-framework IOUSBHost"
)

add_executable(helper_xpc_test helper_xpc_test.mm)
target_link_libraries(helper_xpc_test PRIVATE
    openscanner_helper_xpc
    "-framework CoreFoundation"
)
add_test(NAME helper_xpc_test COMMAND helper_xpc_test)

# Row #181: the user LaunchAgent artifact — rendered and linted device-free
# (no signing, no launchctl, no ~/Library write).
add_test(NAME helper_agent_plist_test
         COMMAND ${CMAKE_SOURCE_DIR}/tools/macos_helper/helper_agent_plist_test.sh
                 $<TARGET_FILE:openscanner-helper>)
