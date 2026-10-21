add_executable(scan_server main.cpp)
# probe
target_link_libraries(scan_server PRIVATE openscanner_engine)

# Hardware-free smoke test for the tracking-verdict name rendering (row #91).
add_executable(tracking_status_test tracking_status_test.cpp)
target_link_libraries(tracking_status_test PRIVATE openscanner_engine)
add_test(NAME tracking_status_test COMMAND tracking_status_test)

# Hardware-free test for the per-tick timing fields (rows #111/#139).
add_executable(perf_window_test perf_window_test.cpp)
target_link_libraries(perf_window_test PRIVATE openscanner_engine)
add_test(NAME perf_window_test COMMAND perf_window_test)

# Hardware-free test for the exposure/laser apply decision (row #153).
add_executable(exposure_apply_test exposure_apply_test.cpp)
target_link_libraries(exposure_apply_test PRIVATE openscanner_engine)
add_test(NAME exposure_apply_test COMMAND exposure_apply_test)

# Hardware-free smoke test for the per-call bound protocol (row #89).
add_executable(bounded_watchdog_test bounded_watchdog_test.cpp)
target_link_libraries(bounded_watchdog_test PRIVATE openscanner_engine)
add_test(NAME bounded_watchdog_test COMMAND bounded_watchdog_test)

# Hardware-free test for the live start's bounded device-enumeration wait
# (row #167): the transient count=0 recovery, the backoff and the budget.
add_executable(device_probe_test device_probe_test.cpp)
target_link_libraries(device_probe_test PRIVATE openscanner_engine)
add_test(NAME device_probe_test COMMAND device_probe_test)

# Hardware-free test for the vendor model.ply reader and the session-folder
# resolver (row #149): small synthetic fixtures, the named failure paths.
add_executable(model_ply_test model_ply_test.cpp)
target_link_libraries(model_ply_test PRIVATE openscanner_engine)
add_test(NAME model_ply_test COMMAND model_ply_test)

# Hardware-free test for the per-client broadcast counters (row #163): a real
# loopback server, fast clients, the 1-vs-8 fan-out, and a stalled client that
# exercises the drop and the send timeout.
add_executable(ws_broadcast_counters_test ws_broadcast_counters_test.cpp)
target_link_libraries(ws_broadcast_counters_test PRIVATE openscanner_engine)
add_test(NAME ws_broadcast_counters_test COMMAND ws_broadcast_counters_test)

# Hardware-free smoke test for the HUD's frame-age arithmetic (row #142): it
# pins the arithmetic and, when the built server is available, takes a real age
# off the synthetic server. The UI is plain JS with no build step, so node is
# the only requirement (skip the test when it is absent).
find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    add_test(NAME ui_frame_age_test
             COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/ui/frame_age_test.js
                     $<TARGET_FILE:scan_server>)
    # The same check against the row #151 load harness: the rate and the frame
    # size the publish path sustains, with the frame-id continuity check that
    # gives the drop rate (the V1 envelope's `seq`).
    add_test(NAME ui_frame_age_load_test
             COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/ui/frame_age_test.js
                     $<TARGET_FILE:scan_server> --synthetic-load 50 20000 6.5)
endif()
# The measurement behind the performance contract (row #152): the encoder
# and the broadcast, measured with the product's own code, device-free.
# scripts/perf-budget.py drives it and checks the budgets.
add_executable(perf_budget perf_budget.cpp)
target_link_libraries(perf_budget PRIVATE openscanner_engine)

# Hardware-free test for the allocation-free cloud-snapshot encoder (row #164):
# byte-identical to the shared header's encoder, one reusable buffer.
add_executable(fast_encode_test fast_encode_test.cpp)
target_link_libraries(fast_encode_test PRIVATE openscanner_engine)
add_test(NAME fast_encode_test COMMAND fast_encode_test)


# Hardware-free selftest for the runtime plane-removal command (row #158):
# the command, the transition log and the WebSocket round trip, with a mock
# vendor in place of the SDK.
add_executable(plane_toggle_test plane_toggle_test.cpp)
target_link_libraries(plane_toggle_test PRIVATE openscanner_engine)
add_test(NAME plane_toggle_test COMMAND plane_toggle_test)

if(APPLE)
    find_library(CORE_FOUNDATION_LIBRARY CoreFoundation REQUIRED)
    target_link_libraries(scan_server PRIVATE ${CORE_FOUNDATION_LIBRARY})

    # --live mode runs a real OBScan session; same crash/hang isolation
    # story as live_capture_test (see vulkan_disable_interpose.c /
    # worker_process.cpp).
    add_dependencies(scan_server vulkan_disable_interpose)
    target_compile_definitions(scan_server PRIVATE
        VULKAN_DISABLE_INTERPOSE_DYLIB="$<TARGET_FILE:vulkan_disable_interpose>"
    )

    # Row #107: refresh the app bundle's copy of this worker whenever it
    # rebuilds. Attached here (add_custom_command(TARGET ...) must live in the
    # target's own directory) rather than on OpenScanner, whose link step CMake
    # skips when only the worker changed — which left the bundled copy stale.
    # The script no-ops when no bundle exists yet; OpenScanner's own POST_BUILD
    # populates it on a clean/app build.
    add_custom_command(TARGET scan_server POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                "-DAPP_CONTENTS=$<TARGET_BUNDLE_CONTENT_DIR:OpenScanner>"
                "-DWORKER=$<TARGET_FILE:scan_server>"
                "-DUI_DIR=${CMAKE_SOURCE_DIR}/ui"
                -P "${CMAKE_SOURCE_DIR}/tools/macos_app/install_bundle.cmake"
        COMMENT "Refreshing the app bundle's scan_server (worker rebuild)"
    )
endif()
