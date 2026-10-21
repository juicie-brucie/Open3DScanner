# The Phase D RPC shim (row #172) and its live-source seam (row #179).
#
# The library is the wire codec, the frame sources (offline bundle and the
# live adapter), the property bank and the server; the executable is the CLI,
# and the test drives the same library with a real raptor_client subprocess.
# capture.cpp is the recorder's own reader, and raw_capture.cpp is #171's
# transport seam: compiling them here is what makes each seam use the one
# existing implementation rather than a second one.

find_package(Threads REQUIRED)

add_library(raptor_shim_lib STATIC
    msgpack.cpp
    frame_source.cpp
    bundle_source.cpp
    live_source.cpp
    properties.cpp
    shim_server.cpp
    discovery.cpp
    ${CMAKE_SOURCE_DIR}/tools/raptor_client/capture.cpp
    ${CMAKE_SOURCE_DIR}/tools/raw_capture/raw_capture.cpp
)
target_include_directories(raptor_shim_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/tools/raptor_client
    ${CMAKE_SOURCE_DIR}/tools/raw_capture
    ${CMAKE_SOURCE_DIR}/engine/include
)
target_compile_features(raptor_shim_lib PUBLIC cxx_std_17)
target_link_libraries(raptor_shim_lib PUBLIC Threads::Threads)

# The claim channel: the helper's XPC status on macOS (row #170's client), and
# a named refusal where no helper can exist.
if(APPLE)
    target_sources(raptor_shim_lib PRIVATE helper_claim.mm)
    target_link_libraries(raptor_shim_lib PUBLIC openscanner_helper_xpc)
    target_compile_definitions(raptor_shim_lib PUBLIC OS_OBJECT_USE_OBJC=0)

    # Row #181: the shim's own claim channel against a fake helper over a real
    # XPC round trip. The helper is an anonymous listener (the real
    # HelperService), so no launchd, entitlement, device or root is involved.
    add_executable(helper_claim_test helper_claim_test.mm)
    target_link_libraries(helper_claim_test PRIVATE raptor_shim_lib openscanner_helper_xpc)
    target_compile_definitions(helper_claim_test PRIVATE OS_OBJECT_USE_OBJC=0)
    add_test(NAME helper_claim_test COMMAND helper_claim_test)
else()
    target_sources(raptor_shim_lib PRIVATE helper_claim_unwired.cpp)
endif()

add_executable(raptor_shim main.cpp)
target_link_libraries(raptor_shim PRIVATE raptor_shim_lib)

# Device-free test (rows #172/#179): the array layout, the discovery command,
# the property framing, the daemon switch and the live seam's refusals, then a
# real `raptor_client --live` against the shim — offline on the vendor port
# and, with the switch on, on the shim's own port. The A/B against the vendor
# daemon on the same scene is the hardware card.
add_executable(raptor_shim_test shim_test.cpp)
target_link_libraries(raptor_shim_test PRIVATE raptor_shim_lib)
add_test(NAME raptor_shim_test
         COMMAND raptor_shim_test $<TARGET_FILE:raptor_client> $<TARGET_FILE:raptor_shim>)
