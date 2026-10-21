# The Phase D RPC shim (row #172).
#
# The library is the wire codec, the frame sources, the property bank and the
# server; the executable is the CLI, and the test drives the same library with
# a real raptor_client subprocess. capture.cpp is the recorder's own reader:
# compiling it here is what makes the offline bundle source use one parser
# for the bundle format rather than a second one.

add_library(raptor_shim_lib STATIC
    msgpack.cpp
    frame_source.cpp
    bundle_source.cpp
    properties.cpp
    shim_server.cpp
    discovery.cpp
    ${CMAKE_SOURCE_DIR}/tools/raptor_client/capture.cpp
)
target_include_directories(raptor_shim_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/tools/raptor_client
)
target_compile_features(raptor_shim_lib PUBLIC cxx_std_17)

find_package(Threads REQUIRED)
target_link_libraries(raptor_shim_lib PUBLIC Threads::Threads)

add_executable(raptor_shim main.cpp)
target_link_libraries(raptor_shim PRIVATE raptor_shim_lib)

# Device-free test (row #172): the array layout, the discovery command, the
# property framing, then a real `raptor_client --live` against the shim on the
# IR interface port, served from a synthetic bundle. The A/B against the
# vendor daemon on the same scene is the hardware card.
add_executable(raptor_shim_test shim_test.cpp)
target_link_libraries(raptor_shim_test PRIVATE raptor_shim_lib)
add_test(NAME raptor_shim_test
         COMMAND raptor_shim_test $<TARGET_FILE:raptor_client>)
